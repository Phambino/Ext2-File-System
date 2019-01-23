#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <errno.h>
#include <string.h>
#include "ext2_helpers.h"

unsigned char *disk;

int main(int argc, char **argv) {
    if(argc != 4 && argc != 5) {
        fprintf(stderr, "Usage: %s <ext2 name> <path to source file> <absolute path to where hardlink will be> \n", argv[0]);
        exit(1);        
    }

    //initialize the filesystem
    disk = ext2_init(argv[1]);

    char *src_path;
    char *full_path;
    char *hardlink_path;
    int file_type; //0 if hardlink and 1 if softlink.

    if(argc == 4){//Means we are creating a hardlink
        src_path = malloc(sizeof(char*) * strlen(argv[2]));
        hardlink_path = malloc(sizeof(char*) * strlen(argv[3]));
    	strcpy(src_path,argv[2]);
    	strcpy(hardlink_path,argv[3]);
    	file_type = 0;
    }


    if (argc == 5){//Means we are at a softlink
        src_path = malloc(sizeof(char*) * strlen(argv[3]));
        full_path = malloc(sizeof(char*) * strlen(argv[3]));
        hardlink_path = malloc(sizeof(char*) * strlen(argv[4]));
    	if(strcmp(argv[2], "-s") == 0){
	    	strcpy(src_path,argv[3]);
	    	strcpy(hardlink_path,argv[4]);
            strcpy(full_path, argv[3]);
	    	file_type = 1;
    	}else{
    		fprintf(stderr, "Usage: %s <ext2 name> <-s> <path to source file> <absolute path to where hardlink will be> \n", argv[0]);
    		exit(1);
    	}
    }


    //Now we have extraced all the information we need and need to check
    //Going to start checking the source file so I can grab the inode of the sourcefile.
    char *token = strtok(src_path,"/");
    struct ext2_inode *inodetable = get_inodetable();
    //check validity of path
    int inode = EXT2_ROOT_INO;
    int src_inode;

    //This section goes through the source files path and gets the inode of the last token.
    while(token != NULL) {
        char *next_token = strtok(NULL, "/");
        int check = findidir(inodetable, inode, token); //-1 if the file is not there.
        unsigned char src_type = find_fileType(inodetable, inode, token);

        if(next_token == NULL){
        	//Checks if we are trying to hardlink a directory or if the source file does not exist.
        	if(((src_type & EXT2_FT_DIR) && file_type == 0) || check == -1){
        		fprintf(stderr, "Cannot hardlink a directory\n");
        		return EISDIR;
        	}
        	src_inode = check;


        }else{//We are going through the path of the sourcefile.
        	if(!(src_type & EXT2_FT_DIR) || check == -1){
    		fprintf(stderr, "Cannot hardlink a directory or source file does not exist\n");
    		return ENOENT;
        	}
        }
        inode = check;
        token = next_token;
    }

    //Now src_inode should have the inode of the sourcefile being linked
    char *link_token = strtok(hardlink_path,"/");
    int linode = EXT2_ROOT_INO;

    while(link_token != NULL) {
        char *next_ltoken = strtok(NULL, "/");
        int lcheck = findidir(inodetable, linode, link_token); //-1 if the file is not there.
        unsigned char link_type = find_fileType(inodetable, inode, link_token);

        if(next_ltoken == NULL){
        	if(lcheck != -1){
        		fprintf(stderr, "Link trying to be created already has a dir entry with the same name\n");
        		return EEXIST;
        	}
        	//Now linode has the inode of the directory we are trying to insert the link into.
        	//And we have the inode of the source file we are using.
        	//linode, src_inode
        	if(file_type == 0){//Just a simple hardlink
        		insert_dir(inodetable, linode, link_token, src_inode, EXT2_FT_REG_FILE);
        		struct ext2_inode *parent_inode = &inodetable[src_inode-1];
    			parent_inode->i_links_count++;
    			return 0;
        	}

        	if(file_type == 1){
        		//We are dealing with a soft link and must create a new inode.
        		create_softlink(inodetable, linode, link_token, full_path);
        		return 0;

        	}

        }else{//We are going through the path of the sourcefile.
        	if(!(link_type & EXT2_FT_DIR) || lcheck == -1){
    		    fprintf(stderr, "Cannot hardlink a directory\n");
    		    return ENOENT;
        	}
        }
        linode = lcheck;
        link_token = next_ltoken;
    }
    return 0;
}