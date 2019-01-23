#include <stdlib.h>
#include <errno.h>
#include <time.h>
#include "ext2.h"

unsigned char *ext2_init(char *path);
int is_absolute_path(char* path);
struct ext2_super_block *get_sb();
struct ext2_group_desc *get_gd();
struct ext2_inode *get_inodetable();
unsigned char *get_blockbitmap();
unsigned char *get_inodebitmap();
int searchbitmap(unsigned char *bm, int size, int reserved);
void setbit(unsigned char *bm, int index, int set_bit_value);
int nextinode();
int nextblock();
int getreclen(int namelen);
int findidir(struct ext2_inode* itable, int id, char *token);
int new_block_copy();
int new_block(struct ext2_inode *inode, int index);
int insert_dir(struct ext2_inode *inodes, int index, char *name, int dir_entry_inodeIndex, int file_type);
int insert_empt_dir(struct ext2_inode *inodes, int index, char *name, int dir_entry_inodeIndex, int block_index, int file_type);
int createinode(struct ext2_inode *inodetable, int parent, char* name);
int copy2inode(char* file, struct ext2_inode *inode);
int cpinode(struct ext2_inode *inodetable, int parent, char* file_name, char* src_path);
void free_all_block(struct ext2_inode *inode, int index);
unsigned char find_fileType(struct ext2_inode* itable, int id, char *token);
int remove_entry(struct ext2_inode *inode_table, int index, char* name);
int copyPath(struct ext2_inode *inodetable, int inode, char *sfile_path);
int create_softlink(struct ext2_inode *inodetable, int parent, char* file, char *sfile_path);
int bitmapchecker(unsigned char *bitmap, int type);
int mapcheck();
int restore_gap_entry(struct ext2_inode *inode_table ,struct ext2_dir_entry *target_entry);
int restore(struct ext2_inode *inode_table, int index, char* name);
int check_inode(int inode_index);
int check_blocks(struct ext2_inode *inode, int index);
int bit_used(unsigned char *bm, int index);
void restore_inode(struct ext2_inode *inode, int index);
void update_inode(struct ext2_inode *inode, int index);
int bitmapchecker(unsigned char *bitmap, int type);
int mapcheck();
int fixes(struct ext2_inode *inodetable, int index);