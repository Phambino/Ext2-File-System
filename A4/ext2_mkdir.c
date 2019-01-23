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

    if(argc != 3) {
        fprintf(stderr, "Usage: %s <ext2 name> <absolute path on ext2>\n", argv[0]);
        exit(1);        
    }
    //initialize the file system
    disk = ext2_init(argv[1]);

    //check absolute path
    if(is_absolute_path(argv[2]) != 0) {
        fprintf(stderr, "Not an Absolute Path\n");
        return ENOENT;
    }

    //strtok already does the check for trailing slashes for all cases
    char *token = strtok((char*)argv[2],"/");
    struct ext2_inode *inodetable = get_inodetable();

    //check validity of path
    int inode = EXT2_ROOT_INO;
    while(token != NULL) {
        char *next_token = strtok(NULL, "/");
        int check = findidir(inodetable, inode, token);
        if(next_token == NULL && check == -1) {
            if(createinode(inodetable, inode, token) == ENOSPC) {
                fprintf(stderr,"No more available inodes\n");
                return ENOSPC;

            }  
            return 0;
        }
        if(next_token != NULL && check == -1) {
            fprintf(stderr, "Directory : %s does not exist\n",token);
            return ENOENT;
        }
        if(next_token == NULL && check != -1) {
            fprintf(stderr, "Directory : %s already exists\n", token);
            return EEXIST;
        }

        //next token        
        inode = check; // inode becomes the parent inode index
        token = next_token;
    }

    return 0;
}


