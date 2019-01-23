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
        unsigned char file_type = find_fileType(inodetable, inode, token);
        //Goes into here if there isnt an inode

        if(next_token == NULL){//Goes in if were looking at the last token of the path.
            if(check != -1){//File exists.
                if(!(file_type & EXT2_S_IFDIR) || file_type != EXT2_FT_DIR){
                    //This is where we remove the file, we have inode of that file.
                    //ENTER HERE
                    remove_entry(inodetable,inode,token);

                }else{//Its a directory which we cannot remove so error
                    fprintf(stderr, "Cannot remove directory\n");
                    return EISDIR;                    
                }
            }else{//File does not exist
                fprintf(stderr, "Last token does not exist\n");
                return ENOENT;
            }
        }

        
        //Means we are not at the last one so Token has to be a directory.
        else{
            if(check != -1){//Means name exists and has a inode.
                if(!(file_type & EXT2_FT_DIR)){//If its not a directory we throw an error.
                    fprintf(stderr, "Path before last file is not a directory\n");
                    return ENOENT;
                }
            }else{//File does not exist so invalid.
                fprintf(stderr, "Directory does not exist\n");
                return ENOENT;
            }

        }
        inode = check;
        token = next_token;
    }

    return 0;
}