#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include "ext2.h"
#include "ext2_helpers.h"

extern unsigned char *disk;

//initialize the file system
unsigned char *ext2_init(char *path){
    int fd = open(path, O_RDWR);

    disk = mmap(NULL, 128 * 1024, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if(disk == MAP_FAILED) {
        perror("mmap");
        exit(1);
    }

    return disk;
}

//checks if path is absolute
int is_absolute_path(char* path) {
    if(path[0] == '/') {
        return 0;
    }
    return 1;
}

//returns the superblock
struct ext2_super_block *get_sb() {
    return (struct ext2_super_block*)(disk + EXT2_BLOCK_SIZE);
}

//returns the block group
struct ext2_group_desc *get_gd() {
    return (struct ext2_group_desc*)(disk + (EXT2_BLOCK_SIZE * 2));
}

//returns the inodetable
struct ext2_inode *get_inodetable() {
    struct ext2_group_desc *gd = get_gd();
    return (struct ext2_inode*)(disk + (EXT2_BLOCK_SIZE * gd->bg_inode_table));
}

//returns the block bitmap
unsigned char *get_blockbitmap() {
    struct ext2_group_desc *gd = get_gd();
    return (disk + (EXT2_BLOCK_SIZE * gd->bg_block_bitmap));
}

//returns the inode bitmap
unsigned char *get_inodebitmap() {
    struct ext2_group_desc *gd = get_gd();
    return (disk + (EXT2_BLOCK_SIZE * gd->bg_inode_bitmap));
}

//sets the bit of the inode/block bitmap to 0 or 1 according to the user
void setbit(unsigned char *bm, int index, int set_bit_value) {
    //Get the location of the target bit that we are trying to change.
    unsigned char *target_bit = index/8 + bm;

    //We go here if we are trying to free the bit
    if (set_bit_value == 0){
        //Sets the bit to 0
        *target_bit &= ~(1 << (index % 8));
    }
    //We go here if we are trying to use the bit    
    if (set_bit_value == 1) {
        //Sets bit to 1
        *target_bit |= 1 << (index % 8);
    } 

}


//finds the next available bit for the inode/block bitmap
int searchbitmap(unsigned char *bm, int size, int reserved) {
    for(int byte = 0; byte < size; byte++) {
	    for(int bit = 0; bit < 8;bit++) {
	        int in_use = bm[byte] & (1 << bit);
	        if(!in_use) {
                int pos = 8 * byte + bit;
                if(pos >= reserved) {
                    return pos + 1;
                }
            }
        }
    }
    return -1;
}

//finds next available free inode
int nextinode() {
    unsigned char *inodebitmap = get_inodebitmap();
    struct ext2_super_block *sb = get_sb();
    return searchbitmap(inodebitmap, sb->s_inodes_count / 8, sb->s_first_ino);
}

//finds next available free block
int nextblock() {
    unsigned char *blockbitmap = get_blockbitmap();
    struct ext2_super_block *sb = get_sb();
    return searchbitmap(blockbitmap, sb->s_blocks_count / 8,0);
}

//returns the reclen of a dir_entry
int getreclen(int namelen) {
    int pad = 4 - (namelen % 4);
    int reclen = 8 + namelen + pad;
    return reclen;
}

//returns inode number of given token name starting at the inode number 
int findidir(struct ext2_inode* itable, int id, char *token) {
    struct ext2_inode *cinodetable = &itable[id - 1];
    for(int i = 0; i < cinodetable->i_blocks/2;i++) {
	    int j = 0;
	    while(j < EXT2_BLOCK_SIZE) {
	        struct ext2_dir_entry *ent = (struct ext2_dir_entry *)(disk + EXT2_BLOCK_SIZE * cinodetable->i_block[i] + j);
            if(ent->name_len == strlen(token) && strncmp(ent->name, token, strlen(token)) == 0) {
                return ent->inode;
            }
	        j+= ent->rec_len;

    	}
    }
    return -1;
}
//Used for copy.
int new_block_copy(){
    struct ext2_super_block *sb = get_sb();
    struct ext2_group_desc *gd = get_gd();
    unsigned char *bbm = get_blockbitmap();
    int freeblock = nextblock();
    if(freeblock == -1) {
        return -1;
    }
    setbit(bbm, freeblock - 1 ,1);
    (sb->s_free_blocks_count)--;
    (gd->bg_free_blocks_count)--;
    return freeblock;
}

int new_block(struct ext2_inode *inode, int index) {
    struct ext2_super_block *sb = get_sb();
    struct ext2_group_desc *gd = get_gd();
    unsigned char *bbm = get_blockbitmap();
    int freeblock = nextblock();
    if(freeblock == -1) {
        return -1;
    }
    inode->i_block[index] = freeblock; //starts at next free block
    struct ext2_dir_entry *dir = (struct ext2_dir_entry*)(disk + (EXT2_BLOCK_SIZE * freeblock));
    dir->rec_len = EXT2_BLOCK_SIZE;

    setbit(bbm, freeblock -1 ,1);
    (sb->s_free_blocks_count)--;
    (gd->bg_free_blocks_count)--;
    return freeblock;
}

int insert_dir(struct ext2_inode *inodes, int index, char *name, int dir_entry_inodeIndex,  int file_type) {
    struct ext2_inode *inode = &inodes[index-1]; //parent inode
    int namelen = strlen(name);
    int counter = 0;
    while(counter < 12){
        //Goes through every data block.
        //loop through the directory entries in each data block
        int tracker = 0;
        if(inode->i_block[counter] == 0){//Means we found a inode pointer not in use so we can set it variables
            int k = new_block(inode,counter);
            if(k == -1) {
                return ENOSPC;
            }
            insert_empt_dir(inodes, index, name, dir_entry_inodeIndex, counter, file_type);
            return 0;
        }
        while(tracker < EXT2_BLOCK_SIZE) {//Goes through the whole block
            //last index
            struct ext2_dir_entry *block = (struct ext2_dir_entry *)((EXT2_BLOCK_SIZE * inode->i_block[counter]) + disk + tracker);
            if(block->rec_len == EXT2_BLOCK_SIZE - tracker){
               //Now tracker has the index of the last data block.
               //We have to check if there is space now.
               int space_left = block->rec_len - (block->name_len + 8);
               if(getreclen(namelen) < space_left){ //parent has enough space
                  //Update previous last entries rec length
                  int test = getreclen(block->name_len); // change test to block->rec_len
                  //Now we go the location of where the new entry will be.
                  struct ext2_dir_entry *last_entry =(struct ext2_dir_entry *)((EXT2_BLOCK_SIZE * inode->i_block[counter]) + disk + tracker + test);
                  block->rec_len = getreclen(block->name_len); //update previous blocks reclength

                  last_entry->inode = dir_entry_inodeIndex;
                  last_entry->name_len = namelen;
                  last_entry->rec_len = 1024 - block->rec_len - tracker;
                  last_entry->file_type = file_type;
                  strcpy(last_entry->name, name);
                  return 0;
               }
            }
            tracker = tracker + block->rec_len;
        }
        counter++;
    }
    return -1;
}


int insert_empt_dir(struct ext2_inode *inodes, int index, char *name, int dir_entry_inodeIndex, int block_index, int file_type) {
    struct ext2_inode *inode = &inodes[index-1];
    int namelen = strlen(name);
    struct ext2_dir_entry *block = (struct ext2_dir_entry *)((EXT2_BLOCK_SIZE * inode->i_block[block_index]) + disk);
    block->inode = dir_entry_inodeIndex;
    block->name_len = namelen;
    block->rec_len = EXT2_BLOCK_SIZE;
    block->file_type = file_type;
    strcpy(block->name, name);
    return 0;

}



int createinode(struct ext2_inode *inodetable, int parent, char* name) {
    int freeinode = nextinode(); 
    unsigned char *bm = get_inodebitmap();
    struct ext2_super_block *sb = get_sb();
    struct ext2_group_desc *gd = get_gd();
    if(freeinode == -1) {
        return ENOSPC;
    }
    struct ext2_inode *inode = &inodetable[freeinode-1]; //create new inode at free inode
    struct ext2_inode *parentino = &(inodetable[parent-1]); //get inode from parent directory
    //initialze the new inode
    inode->i_mode = EXT2_S_IFDIR;
    inode->i_uid = 0;
    inode->i_size = EXT2_BLOCK_SIZE;
    inode->i_blocks = 0;
    inode->i_dtime = 0;
    inode->i_ctime = time(NULL);
    for(int i = 0; i < 15; i++) { //set all i_block to 0 
        inode->i_block[i] = 0;
    }
    inode->i_gid = 0;
    inode->i_links_count = 2;
    inode->osd1 = 0;
    inode->i_file_acl = 0;
    inode->i_dir_acl = 0;
    inode->i_faddr = 0;

    //insert directories in required folders
    insert_dir(inodetable, parent, name, freeinode, EXT2_FT_DIR);

    insert_dir(inodetable, freeinode, ".",freeinode,EXT2_FT_DIR);
    insert_dir(inodetable, freeinode, "..",parent,EXT2_FT_DIR); //add new inode to parent inode
    parentino->i_links_count++;
    inode->i_blocks += 2;
    gd->bg_used_dirs_count++;

    //set inode bit to 1 and decrement free blocks and inodes
    setbit(bm, freeinode -1, 1);
    (sb->s_free_inodes_count)--;
    (gd->bg_free_inodes_count)--;

    return 0;
}


int copy2inode(char* file, struct ext2_inode *inode) {
    FILE *f = fopen(file, "r");
    unsigned int br;
    char buffer[EXT2_BLOCK_SIZE + 1]; 
    if(!f) {
        fprintf(stderr, "File path nonexistent\n");
	    return ENOENT;
    }
    buffer[EXT2_BLOCK_SIZE] = '\0';
    //directblock
    for(int i = 0; i< 12; i++) {
        br = fread(buffer, 1, EXT2_BLOCK_SIZE, f);
        if(br <= 0){
            //nothing left to read
            return 0;
        }

        int new = new_block(inode,i);
        unsigned char *b = disk + (EXT2_BLOCK_SIZE * new);
        if(new == -1) {
            return ENOSPC;
        }
        //Now we memcpy
        memcpy(b, buffer,br);
        //Increment the size
        inode->i_size += br;
        inode->i_blocks+=2;
    }
    //Now we are done with our direct blocks and need to check if we need the indirect blocks.
    //So we need more blocks
    if((br = fread(buffer,1,EXT2_BLOCK_SIZE,f)) <= 0) {
        return 0;
    }

    int freeblock = new_block_copy();
    int direct_blocks;
    unsigned int *indirect_block = (unsigned int *)(disk + freeblock * EXT2_BLOCK_SIZE);
    inode->i_block[12] = freeblock;
    inode->i_blocks+=2;

    for(int i = 0;br > 0; i++) {
        //This is where we create the new data blocks for the indirect block
        direct_blocks = new_block_copy();
        unsigned char *dblock = (disk + direct_blocks * EXT2_BLOCK_SIZE);
        indirect_block[i] = direct_blocks;

        //Now we memcpy
        memcpy(dblock, buffer,br);
        //Increment the size
        inode->i_size += br;
        inode->i_blocks+=2;
        br = fread(buffer, 1, EXT2_BLOCK_SIZE,f);
    }
    return 0;
}



int cpinode(struct ext2_inode *inodetable, int parent, char* file_name, char* src_path) {
    int freeinode = nextinode(); 
    unsigned char *bm = get_inodebitmap();
    struct ext2_super_block *sb = get_sb();
    struct ext2_group_desc *gd = get_gd();
    if(freeinode == -1) {
        return ENOSPC;
    }
    struct ext2_inode *inode = &inodetable[freeinode-1]; //create new inode at free inode
    //need to read the file, copy to the inode and put the inode to the destination folder
    
    //initialze the new inode
    inode->i_mode = EXT2_S_IFREG;
    inode->i_uid = 0;
    inode->i_size = 0;
    inode->i_blocks = 0;
    inode->i_dtime = 0;
    inode->i_ctime = time(NULL);
    for(int i = 0; i < 15; i++) { //set all i_block to 0 
        inode->i_block[i] = 0;
    }
    inode->i_gid = 0;
    inode->i_links_count = 1;
    inode->osd1 = 0;
    inode->i_file_acl = 0;
    inode->i_dir_acl = 0;
    inode->i_faddr = 0;

    //read the file and etc.
    int cp = copy2inode(src_path, inode);
    if(cp == ENOENT) {
        return ENOENT;
    }

    insert_dir(inodetable, parent, file_name, freeinode, EXT2_FT_REG_FILE);
    
    setbit(bm, freeinode -1, 1);
    (sb->s_free_inodes_count)--;
    (gd->bg_free_inodes_count)--;
    return 0;
}

void free_all_block(struct ext2_inode *inode, int index){
    struct ext2_inode *cinode = &inode[index - 1];
     struct ext2_super_block *sb = get_sb();
     struct ext2_group_desc *gd = get_gd();
     unsigned char *bbm = get_blockbitmap();
     unsigned char *ibm = get_inodebitmap();
     for(int i = 0; cinode->i_block[i] != 0 && i < 12; i++){
        setbit(bbm, cinode->i_block[i]-1 ,0);
        (sb->s_free_blocks_count)++;
        (gd->bg_free_blocks_count)++;
     }
     if(cinode->i_block[12] != 0){
        unsigned int *indirect_block = (unsigned int *)(disk + cinode->i_block[12] * EXT2_BLOCK_SIZE);
        int counter = 0;
        //Goes through all the indirect blocks.
        while(indirect_block[counter] != 0){
            setbit(bbm, indirect_block[counter]-1 ,0);
            (sb->s_free_blocks_count)++;
            (gd->bg_free_blocks_count)++;      
            counter++;     
        }
        setbit(bbm, cinode->i_block[12] -1, 0);
        (sb->s_free_blocks_count)++;
        (gd->bg_free_blocks_count)++;
    }

    setbit(ibm,index-1,0);
    (sb->s_free_inodes_count)++;
    (gd->bg_free_inodes_count)++;
}

//Use this for Remove.
//returns file type of given token name starting at the inode number 
unsigned char find_fileType(struct ext2_inode* itable, int id, char *token) {
    struct ext2_inode *cinodetable = &itable[id - 1];
    for(int i = 0; i < cinodetable->i_blocks/2;i++) {
        int j = 0;
        while(j < EXT2_BLOCK_SIZE) {
            struct ext2_dir_entry *ent = (struct ext2_dir_entry *)(disk + EXT2_BLOCK_SIZE * cinodetable->i_block[i] + j);
            if(strncmp(ent->name, token, strlen(token)) == 0) {
                return ent->file_type;
            }
            j+= ent->rec_len;

        }
    }
    return -1;
}
//For remove;
int remove_entry(struct ext2_inode *inode_table, int index, char* name){
    struct ext2_inode *cinode = &inode_table[index - 1];
    int ctime = time(NULL);
    
    for(int i = 0; i < 12;i++) {// Goes through every direct block
        int j = 0;
        //Now we go through all the entries in the block looking for the file
        struct ext2_dir_entry *prev_ent = NULL;
        while(j < EXT2_BLOCK_SIZE) {
            struct ext2_dir_entry *ent = (struct ext2_dir_entry *)(disk + EXT2_BLOCK_SIZE * cinode->i_block[i] + j);
            if(ent->name_len == strlen(name) && strncmp(ent->name, name , strlen(name)) == 0) {
                struct ext2_inode *delete = &inode_table[ent->inode-1];
                //Now we found the token.
                if(prev_ent == NULL){//This means we are deleting the first entry
                    ent->inode = 0;
                }else{//This means there id a dir entry before it so we
                    prev_ent->rec_len += ent->rec_len;
                }
                //i links count
                delete->i_links_count--;
                if(delete->i_links_count == 0){
                    delete->i_dtime = ctime;
                    free_all_block(inode_table, ent->inode);   
                }
                return 0;                  
            }
            prev_ent = ent;
            j+= ent->rec_len;

        }
    }
    return 0;
}



//For ln

int copyPath(struct ext2_inode *inodetable, int inode, char *sfile_path) {
    //Need to allocate a block and memcpy into it.
    struct ext2_inode *inode2 = &inodetable[inode-1];

    //case where the symlink needs more than 1 block
    if(strlen(sfile_path) > EXT2_BLOCK_SIZE) {
        //blocks is how many blocks we need to store the symlink
        int block;
        if(strlen(sfile_path) % EXT2_BLOCK_SIZE == 0) {
            block = strlen(sfile_path) / EXT2_BLOCK_SIZE;
        } else {
            block = strlen(sfile_path) / EXT2_BLOCK_SIZE + 1;
        }

        //seperate the first 1024 bytes from the rest
        //and allocate the first 1024 bytes
        char *first = malloc(sizeof(char*) * EXT2_BLOCK_SIZE);
        strncpy(first, sfile_path, EXT2_BLOCK_SIZE);

        int newblock = new_block_copy();
        inode2->i_block[0] = newblock;
        unsigned char* update_block = disk + newblock * EXT2_BLOCK_SIZE;
        memcpy(update_block, first, strlen(first));
        inode2->i_size += strlen(first);
        inode2->i_blocks+=2;

        char *rest = &sfile_path[EXT2_BLOCK_SIZE];
        int i = 1;
        while(i < block) {
            int newblock2 = new_block_copy();
            inode2->i_block[i] = newblock2;
            unsigned char *update_block2 = disk + (newblock2 * EXT2_BLOCK_SIZE) + i;
            memcpy(update_block2, rest, strlen(rest));
            inode2->i_size += strlen(rest);
            inode2->i_blocks+=2;
            rest = &rest[EXT2_BLOCK_SIZE];
        }
        return 0;
    }

    //case where the sym link only needs 1 block;
    int newblock = new_block_copy();
    inode2->i_block[0] = newblock;
    unsigned char* update_block = disk + newblock * EXT2_BLOCK_SIZE;
    memcpy(update_block, sfile_path, strlen(sfile_path));
    inode2->i_size += strlen(sfile_path);
    inode2->i_blocks+=2;
    return 0;

}
int create_softlink(struct ext2_inode *inodetable, int parent, char* file, char *sfile_path){
    int freeinode = nextinode(); 
    unsigned char *bm = get_inodebitmap();
    struct ext2_super_block *sb = get_sb();
    struct ext2_group_desc *gd = get_gd();
    if(freeinode == -1) {
        return ENOSPC;
    }
    struct ext2_inode *inode = &inodetable[freeinode-1]; //create new inode at free inode
    //need to read the file, copy to the inode and put the inode to the destination folder
    
    //initialze the new inode
    inode->i_mode = EXT2_S_IFLNK;
    inode->i_uid = 0;
    inode->i_size = 0;
    inode->i_blocks = 0;
    inode->i_dtime = 0;
    inode->i_ctime = time(NULL);
    for(int i = 0; i < 15; i++) { //set all i_block to 0 
        inode->i_block[i] = 0;
    }
    inode->i_gid = 0;
    inode->i_links_count = 1;
    inode->osd1 = 0;
    inode->i_file_acl = 0;
    inode->i_dir_acl = 0;
    inode->i_faddr = 0;

    //Now we must copy the path of the sourcefile.
    copyPath(inodetable, freeinode,sfile_path);

    insert_dir(inodetable, parent, file, freeinode, EXT2_FT_SYMLINK);

    setbit(bm, freeinode -1, 1);
    (sb->s_free_inodes_count)--;
    (gd->bg_free_inodes_count)--;
    return freeinode;
}

int restore_gap_entry(struct ext2_inode *inode_table ,struct ext2_dir_entry *target_entry){
    //Now we must check if the inode is not in use in the bitmap else return ENOENT
    int inode_check = check_inode((target_entry->inode) - 1);
    if(inode_check == -1){
        //Means the inode is already in use
        return -1; 
    }
    //Now we must check if the blocks is not in use in the bitmap else return ENOENT
    int block_check = check_blocks(inode_table,target_entry->inode);
    if(block_check == -1){
        //Means the inode is already in use
        return -1;  
    }
    //If it gets here this means all its blocks and inodes are not in use and we can restore them.

    //Restore inode and blocks
    restore_inode(inode_table,target_entry->inode);
    //Adjust values of the restored inode(gap_entry)
    update_inode(inode_table,target_entry->inode);
    return 0;
}


int restore(struct ext2_inode *inode_table, int index, char* name){
    //Parent inode
    struct ext2_inode *cinode = &inode_table[index - 1];

    for(int i = 0; i < 12;i++) {// Goes through every direct block
        int j = 0;

        if(cinode->i_block[i] == 0){
            return -1;
        }
        //We need to check if the first block is the same
        struct ext2_dir_entry *first_entry = (struct ext2_dir_entry *)(disk + EXT2_BLOCK_SIZE * cinode->i_block[i] + j);
        if(strncmp(first_entry->name, name , strlen(name)) == 0){
            //return the helper
            return restore_gap_entry(inode_table, first_entry);
        }
        //Now we go through all the entries in the block looking for the file that is between blocks.
        while(j < EXT2_BLOCK_SIZE) {//Loops till the end of each data block
            struct ext2_dir_entry *ent = (struct ext2_dir_entry *)(disk + EXT2_BLOCK_SIZE * cinode->i_block[i] + j);
            //Need to compare its actual length with the rec length to check for gaps
            int actual_length = getreclen(ent->name_len);
            //We have to check if the last block has a gap after it.
            if(ent->rec_len + j == EXT2_BLOCK_SIZE){
                struct ext2_dir_entry *last_entry = (struct ext2_dir_entry *)(disk + EXT2_BLOCK_SIZE * cinode->i_block[i] + j + actual_length);
                if(strncmp(last_entry->name, name , strlen(name)) == 0){
                    //return the helper
                    int check_last = restore_gap_entry(inode_table, last_entry);
                    if(check_last == -1){
                        return -1;
                    }
                    ent->rec_len = actual_length;
                    return 0;
                }                              
            }
            if(actual_length != ent->rec_len){//This means we have a gap
                struct ext2_dir_entry *gap_entry = (struct ext2_dir_entry *)(disk + EXT2_BLOCK_SIZE * cinode->i_block[i] + j + actual_length);
                //Now ent is at the gap and we must check if the name is the same
                if(strncmp(gap_entry->name, name , strlen(name)) == 0){
                    int check_gap = restore_gap_entry(inode_table, gap_entry);
                    if(check_gap == -1){
                        return -1;
                    }                    
                    ent->rec_len = actual_length;
                    return 0;
                }                
                //Only gets here if we do not find a gap
            }
            //If we find a gap then prev becomes the current entry
            j+= ent->rec_len;
        }
    }
    return 0;
}


int check_inode(int inode_index){
    unsigned char *ibm = get_inodebitmap();
    int check = bit_used(ibm, inode_index);
    if(check == 1){
        return -1;
    }
    return 0;
}

//Checks all the blocks in the inode if they are not in use.
int check_blocks(struct ext2_inode *inode, int index){
    struct ext2_inode *cinode = &inode[index - 1];
    unsigned char *bbm = get_blockbitmap();

    for(int i = 0; cinode->i_block[i] != 0 && i < 12; i++){
        //Goes through the first direct blocks
        int block_check = bit_used(bbm,cinode->i_block[i]-1); 
        if(block_check == 1){
            //Means the block is being used
            return -1;
        }
    }
    if(cinode->i_block[12] != 0){
        unsigned int *indirect_block = (unsigned int *)(disk + cinode->i_block[12] * EXT2_BLOCK_SIZE);
        int counter = 0;
        //Goes through all the indirect blocks.
        while(indirect_block[counter] != 0){
            int indirect_block_check = bit_used(bbm,indirect_block[counter]-1);     
            if(indirect_block_check == 1){
                //Means the block is being used
                return -1;
            }             
            counter++;     
        }
    }
    return 0; //Passed the checks
}

//Returns 1 if the bit is used and 0 if it isn't
int bit_used(unsigned char *bm, int index) {
    return (bm[index/8] & (1 << index % 8));
}

void restore_inode(struct ext2_inode *inode, int index){
    struct ext2_inode *cinode = &inode[index - 1];
     struct ext2_super_block *sb = get_sb();
     struct ext2_group_desc *gd = get_gd();
     unsigned char *bbm = get_blockbitmap();
     unsigned char *ibm = get_inodebitmap();
     //Goes through the direct blocks
     for(int i = 0; cinode->i_block[i] != 0 && i < 12; i++){
        setbit(bbm, cinode->i_block[i]-1 ,1);
        (sb->s_free_blocks_count)--;
        (gd->bg_free_blocks_count)--;
     }
     if(cinode->i_block[12] != 0){
        unsigned int *indirect_block = (unsigned int *)(disk + cinode->i_block[12] * EXT2_BLOCK_SIZE);
        int counter = 0;
        //Goes through all the indirect blocks.
        while(indirect_block[counter] != 0){
            setbit(bbm, indirect_block[counter]-1 ,1);
            (sb->s_free_blocks_count)--;
            (gd->bg_free_blocks_count)--;      
            counter++;     
        }

        setbit(bbm, cinode->i_block[12]-1 ,1);
        (sb->s_free_blocks_count)--;
        (gd->bg_free_blocks_count)--; 

     }
    setbit(ibm,index-1,1);
    (sb->s_free_inodes_count)--;
    (gd->bg_free_inodes_count)--;
    return;
}

void update_inode(struct ext2_inode *inode, int index){
    struct ext2_inode *cinode = &inode[index - 1];
    cinode->i_dtime = 0;
    cinode->i_links_count++;
    return;
}


////////////// Checker helpers.

//checks how many free inodes/blocks are in the bitmap
int bitmapchecker(unsigned char *bitmap, int type) {
    struct ext2_super_block *sb = get_sb();
    int count = 0;
    int limit = type;
    if(type == 0){
        limit = sb->s_inodes_count/8; 
    }else{
        limit = sb->s_blocks_count/8;
    }
    int counter = 0;
    for(int byte = 0; byte < limit; byte++) {
	    for(int bit = 0; bit < 8;bit++) {
            if(counter == 127){
                return count;
            }
            counter++;
	        int in_use = bitmap[byte] & (1 << bit);
	        if(!(in_use)) {
                count++;
            }
	    }
    }
    return count;
}

//checks the inode bitmap and block bitmap and see if the bits are aligned
//with the free blocks and inodes in the super block and block group
int mapcheck(){
    int fix = 0;
    unsigned char *ibm = get_inodebitmap();
    unsigned char *bbm = get_blockbitmap();
    struct ext2_super_block *sb = get_sb();
    struct ext2_group_desc *gd = get_gd();  

    int inodes = bitmapchecker(ibm, 0);
    int blocks = bitmapchecker(bbm, 1);

    //checks if superblocks free blocks are aligned with bitmap free blocks
    if(sb->s_free_blocks_count != blocks) {
        int difference = abs(blocks - sb->s_free_blocks_count);
        sb->s_free_blocks_count = blocks;
        printf("Fixed: super blocks's block counter was off by %d compared to the bitmap\n", difference);
        fix++;
    }
    //checks if superblocks free inodes are aligned with bitmap free inodes
    if(sb->s_free_inodes_count != inodes) {
        int difference = abs(inodes - sb->s_free_inodes_count);
        sb->s_free_inodes_count = inodes;
        printf("Fixed: super blocks's inode counter was off by %d compared to the bitmap\n", difference);
        fix++;
    }
    //checks if block group's free blocks are aligned with bitmap free blocks
    if(gd->bg_free_blocks_count != blocks) {
        int difference = abs(blocks - gd->bg_free_blocks_count);
        gd->bg_free_blocks_count = blocks;
        printf("Fixed: block group's block counter was off by %d compared to the bitmap\n", difference);
        fix++;
    }
    //checks if block group's free inodes are aligned with bitmap free inodes
    if(gd->bg_free_inodes_count != inodes) {
        int difference = abs(inodes - gd->bg_free_inodes_count);
        gd->bg_free_inodes_count = inodes;
        printf("Fixed: block group's inode counter was off by %d compared to the bitmap\n", difference);
        fix++;
    }
    return fix;
}

int fixes(struct ext2_inode *inodetable, int index){
    int fix = 0;
    struct ext2_inode *inode = &inodetable[index-1];
    struct ext2_super_block *sb = get_sb();
    struct ext2_group_desc *gd = get_gd(); 
    unsigned char *ibm = get_inodebitmap();
    unsigned char *bbm = get_blockbitmap();
    //deletion time fix
    if(inode->i_dtime != 0) {
        inode->i_dtime = 0;
        printf("Fixed: valid inode marked for deletion: [%d]\n",index);
        fix++;
    }

    //fix inode allocation on bitmap;
    if((bit_used(ibm,index-1)) == 0) {
        setbit(ibm,index-1,1);
        sb->s_free_inodes_count--;
        gd->bg_free_inodes_count--;
        printf("Fixed: inode [%d] not marked as in-use\n", index);
        fix++;
    }
    
    //fix block allocation on bitmap
    int blockfix = 0;
    //direct blocks
    for(int i = 0; inode->i_block[i] != 0 && i < 12; i++){
        if((bit_used(bbm,inode->i_block[i]-1)) == 0) {
            printf("inode i blocks : %d being fixed\n",inode->i_block[i]);
            setbit(bbm,inode->i_block[i]-1,1);
            sb->s_free_blocks_count--;
            gd->bg_free_blocks_count--;
            blockfix++;
        }
    }
    if(inode->i_block[12] != 0){
        unsigned int *indirect_block = (unsigned int *)(disk + inode->i_block[12] * EXT2_BLOCK_SIZE);
        int counter = 0;
        //Goes through all the indirect blocks.
        while(indirect_block[counter] != 0){
            if((bit_used(bbm,indirect_block[counter])) == 0) {
                setbit(bbm, indirect_block[counter]-1 ,1);
                (sb->s_free_blocks_count)--;
                (gd->bg_free_blocks_count)--;      
                counter++;
                blockfix++; 
            }    
        }
     }
    if(blockfix != 0) {
        printf("Fixed: %d in-use data blocks not marked in data bitmap for inode: [%d]\n", blockfix, index);
    }
    fix+= blockfix;


    //recurse through directories
    if(inode->i_mode & EXT2_S_IFDIR) {
        //direct blocks
        for(int i = 0; inode->i_block[i] != 0 && i < 12; i++){
            int t = 0;
            while(t < EXT2_BLOCK_SIZE) {
                struct ext2_dir_entry *dir = (struct ext2_dir_entry*)(disk + (EXT2_BLOCK_SIZE * inode->i_block[i]) + t);
                if(dir->rec_len == 0 || dir->rec_len == EXT2_BLOCK_SIZE) {
                    break;
                }
                struct ext2_inode *cinode = &inodetable[(dir->inode)-1];

                //check if the inodes i_mode is the same as the dir_entry's file type
                //if not then trust the inodes i_mode and change the file type
                if(cinode->i_mode & EXT2_S_IFREG) {
                    if(dir->file_type != EXT2_FT_REG_FILE) {
                        dir->file_type = EXT2_FT_REG_FILE;
                        printf("Fixed: Entry type vs inode mismatch: inode [%d]\n",dir->inode);
                        fix++;
                    }
                } else if(cinode->i_mode & EXT2_S_IFLNK) {
                    if(dir->file_type != EXT2_FT_SYMLINK) {
                        dir->file_type = EXT2_FT_SYMLINK;
                        printf("Fixed: Entry type vs inode mismatch: inode [%d]\n",dir->inode);
                        fix++;
                    }
                } else if(cinode->i_mode & EXT2_S_IFDIR) {
                    if(dir->file_type != EXT2_FT_DIR) {
                        dir->file_type = EXT2_FT_DIR;
                        printf("Fixed: Entry type vs inode mismatch: inode [%d]\n",dir->inode);
                        fix++;
                    }
                }

                //recurse if the current directory is not . or ..
                if(strncmp(dir->name,".",strlen(dir->name)) != 0 && strncmp(dir->name,"..",strlen(dir->name)) !=0) {
                    fix += fixes(inodetable, dir->inode);
                }

                t += dir->rec_len;
            }
        }
        //indirect blocks
        if(inode->i_block[12] != 0){
            unsigned int *indirect_block = (unsigned int *)(disk + (inode->i_block[12] * EXT2_BLOCK_SIZE));
            int counter = 0;
            while(indirect_block[counter] != 0){
                int t = 0;
                while(t < EXT2_BLOCK_SIZE) {
                    struct ext2_dir_entry *dir = (struct ext2_dir_entry*)(disk + (EXT2_BLOCK_SIZE * indirect_block[counter]) + t);
                    if(dir->rec_len == 0 || dir->rec_len == EXT2_BLOCK_SIZE) {
                        break;
                    }
                    struct ext2_inode *cinode = &inodetable[(dir->inode)-1];

                    //check if the inodes i_mode is the same as the dir_entry's file type
                    //if not then trust the inodes i_mode and change the file type
                    if(cinode->i_mode & EXT2_S_IFREG) {
                        if(dir->file_type != EXT2_FT_REG_FILE) {
                            dir->file_type = EXT2_FT_REG_FILE;
                            printf("Fixed: Entry type vs inode mismatch: inode [%d]\n",dir->inode);
                            fix++;
                        }
                    } else if(cinode->i_mode & EXT2_S_IFLNK) {
                        if(dir->file_type != EXT2_FT_SYMLINK) {
                            dir->file_type = EXT2_FT_SYMLINK;
                            printf("Fixed: Entry type vs inode mismatch: inode [%d]\n",dir->inode);
                            fix++;
                        }
                    } else if(cinode->i_mode & EXT2_S_IFDIR) {
                        if(dir->file_type != EXT2_FT_DIR) {
                            dir->file_type = EXT2_FT_DIR;
                            printf("Fixed: Entry type vs inode mismatch: inode [%d]\n",dir->inode);
                            fix++;
                        }
                    }

                    //recurse if the current directory is not . or ..
                    if(strncmp(dir->name,".",strlen(dir->name)) != 0 && strncmp(dir->name,"..",strlen(dir->name)) !=0) {
                        fix += fixes(inodetable, dir->inode);
                    }

                    t += dir->rec_len;
                }
            }
        }
        
    }

    return fix;
}
