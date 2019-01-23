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
    int num_fixes = 0;
    if(argc != 2) {
        fprintf(stderr, "Usage: <file image>\n");
        exit(1);
    }
    //initialize the file system
    disk = ext2_init(argv[1]);
    struct ext2_inode *inodetable = get_inodetable();

    //checks if the free inodes/block are aligned with the bitmap
    num_fixes += mapcheck(); 
    //fixes allocated blocks and inodes for each inode starting at root
    num_fixes += fixes(inodetable, EXT2_ROOT_INO);

    if (num_fixes == 0) {
        printf("No file system inconsistencies detected!\n");
    } else {
        printf("%d file system inconsistencies repaired!\n", num_fixes);
    }
    return 0;
}