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

    if(argc != 4) {
        fprintf(stderr, "Usage: %s <ext2 name> <path to file> <absolute path on ext2> \n", argv[0]);
        exit(1);        
    }

    //initialize the file system
    disk = ext2_init(argv[1]);

    //checks if the target path is absolute
    if(is_absolute_path(argv[3]) != 0) {
        fprintf(stderr, "%s : Not an Absolute Path\n",argv[3]);
        return ENOENT;
    }

    //need the whole path of the file we are reading
    char *readfile_path = malloc(sizeof(char*) * strlen(argv[2]));
    strcpy(readfile_path,argv[2]);


    //Grab last token of source file.
    char *src_token = strtok(argv[2],"/");
    while(src_token != NULL){
        char *next_src_token = strtok(NULL, "/");
        if(next_src_token == NULL){
            break;
        }
        src_token = next_src_token;
    }

    //Now src_token has the last token of the source file.
    char *token = strtok((char*)argv[3],"/");
    struct ext2_inode *inodetable = get_inodetable();

    //check validity of path

    //case if the target path is the root
    int inode = EXT2_ROOT_INO;
    if(strcmp(argv[3], "/") == 0) {
        //checks if the file already exists in the parent directory
        int check_file = findidir(inodetable, inode, src_token);
        if(check_file != -1) {
            fprintf(stderr, "Source file already exists in the image\n");
            return EEXIST;
        } else {
            int cp = cpinode(inodetable, inode, src_token, readfile_path);
            if(cp == ENOSPC) {
                fprintf(stderr, "No more free inodes\n");
                return ENOSPC;
            }
            return cp;
        }
    }

    while(token != NULL) {
        char *next_token = strtok(NULL, "/");
        int check = findidir(inodetable, inode, token);
        unsigned char file_type = find_fileType(inodetable, inode, token);
        if(next_token == NULL){//Then we are at the last token

            if(check == -1){ //Does not exist so must not be a directory 
                //Case we did not account for where you copy the source file with the last token as the name of the copied file
                //copy with name
                int check_file = findidir(inodetable, inode, token);
                if(check_file != -1) {
                    fprintf(stderr, "Source file already exists in the image\n");
                    return EEXIST;
                }
                int copy_with_path_name = cpinode(inodetable, inode, token, readfile_path);
                if(copy_with_path_name == ENOSPC) {
                    fprintf(stderr, "No more free inodes\n");
                    return ENOSPC;
                }
                return copy_with_path_name;

            }else{//Goes into here if there is a file then it must be a directory
                if(!(file_type & EXT2_FT_DIR)){//If its not a directory we throw an error.
                    //If not a directory then it already exists
                    fprintf(stderr, "File trying to be copied already exists\n");
                    return EEXIST;
                }else{
                    //Goes here if its a directory and it exists.
                    int check_file2 = findidir(inodetable, check, src_token);
                    //So we can execute the copy
                    if(check_file2 != -1){
                        fprintf(stderr, "File trying to be copied already exists\n");
                        return EEXIST;                        
                    }
                    int copy = cpinode(inodetable, check, src_token, readfile_path);
                    if(copy == ENOSPC) {
                        fprintf(stderr, "No more free inodes\n");
                        return ENOSPC;
                    }
                    return copy;
                }
            }

        }

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
