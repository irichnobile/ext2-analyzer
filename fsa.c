/*******************************************************************************
 * fsa.c      Author: Ian Nobile
 *
 * This leak-free program written in the C programming language can read
 * and analyze the partitions of a virtual disk image file formatted with an
 * ext2 file system in raw mode using the superblock, inode, group descriptor
 * and directory entry structures as defined by the E2fsprogs package and
 * report some general file system statistics, individual group statistics  and
 * the entries contained within the root directory. To run it successfully,
 * pass the absolute path of a virtual disk image file as a command line
 * argument.
 *
*******************************************************************************/

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <ext2fs/ext2fs.h>
#include <ext2fs/ext2_fs.h>
#include <stdbool.h>

#define BOOT_OFFSET 1024
#define BLOCK_SIZE 4096

//------------------------------------------------------------------------------
//  The Main Function
//------------------------------------------------------------------------------
int main(int argc, char *argv[]) {

    //  handle command line args: (./server <filename>)
    char cFileName[256];
    if (argc < 2 || argc > 2) {
        printf("Sorry, but something's not quite right about your invocation.\n");
        return 1;
    } else {
        strcpy(cFileName, argv[1]);
    }

    //--------------------------------------------------------------------------
    //  Part One
    //--------------------------------------------------------------------------
    struct ext2_super_block *sb2 = NULL;
    int rv, fd;

    fd = open(argv[1], O_RDONLY);
    if (fd == -1) {
        perror("disk_image_file open failed");
        exit(1);
    }

    sb2 = malloc(sizeof(struct ext2_super_block));
    if (sb2 == NULL) {
        fprintf(stderr, "%s: Error in malloc\n", "sb");
        exit(1);
    }

    // set the file offset to block 0 (offset 1024)
    if (lseek(fd, BOOT_OFFSET, SEEK_SET) != BOOT_OFFSET) {
        perror("File seek failed");
        exit(1);
    }

    // read the whole superblock and load into the ext2_super_block struct
    // assumes the struct fields are laid out in the order they are defined
    rv = read(fd, sb2, sizeof(struct ext2_super_block));
    if (rv == -1) {
        perror("File read failed");
        exit(1);
    }
    if (rv == sizeof(struct ext2_super_block)) {
        printf("--General File System Information--\n");
        printf("Block Size in Bytes : %d\n", (1024 << sb2->s_log_block_size));
        printf("Total Number of Blocks : %d\n", sb2->s_blocks_count);
        printf("Disk Size in Bytes : %d\n", sb2->s_blocks_count * (1024 << sb2->s_log_block_size));
        printf("Maximum Number of Blocks Per Group : %d\n", sb2->s_blocks_per_group);
        printf("Inode Size in Bytes : %d\n", (sb2->s_inode_size));
        printf("Number of Inodes Per Group : %d\n", sb2->s_inodes_per_group);
        printf("Number of Inode Blocks Per Group : %d\n", (sb2->s_inodes_per_group / ((1024 << sb2->s_log_block_size) / sb2->s_inode_size)));
        printf("Number of Groups : %d\n\n", (sb2->s_inodes_count / sb2->s_inodes_per_group));
    }

    //--------------------------------------------------------------------------
    //  Part Two
    //--------------------------------------------------------------------------
    struct ext2_group_desc *gd2 = NULL;

    gd2 = malloc(sizeof(struct ext2_group_desc) * (sb2->s_inodes_count / sb2->s_inodes_per_group));
    if (gd2 == NULL) {
        fprintf(stderr, "%s: Error in malloc\n", "gd");
        exit(1);
    }

    // set the file offset to block 1 (offset 4096)
    if (lseek(fd, 1 * BLOCK_SIZE, SEEK_SET) != 1 * BLOCK_SIZE) {
        perror("File seek failed");
        exit(1);
    }

    // read entire group descriptor table and load into ext2_group_desc struct
    // assumes the struct fields are laid out in the order they are defined
    rv = read(fd, gd2, sizeof(struct ext2_group_desc) * (sb2->s_inodes_count / sb2->s_inodes_per_group));
    if (rv == -1) {
        perror("File read failed");
        exit(1);
    }
    if (rv == sizeof(struct ext2_group_desc) * (sb2->s_inodes_count / sb2->s_inodes_per_group)) {
        printf("--Individual Group Information--\n");

        int iTotalNoBlocks = sb2->s_blocks_count;
        int iBlockRangeL = sb2->s_first_data_block;
        int iBlockRangeH = sb2->s_first_data_block - 1;

        // then, for each group:
        for (int i = 0; i < (sb2->s_inodes_count / sb2->s_inodes_per_group); i++) {
            printf("-Group %d -\n", i);
            iBlockRangeL = iBlockRangeH + 1;
            if (iTotalNoBlocks < sb2->s_blocks_per_group) {
                iBlockRangeH += iTotalNoBlocks;
            } else {
                iTotalNoBlocks -= sb2->s_blocks_per_group;
                iBlockRangeH += sb2->s_blocks_per_group;
            }
            printf("Block IDs : %d-%d\n", iBlockRangeL, iBlockRangeH);
            printf("Block Bitmap Block ID : %d\n", gd2[i].bg_block_bitmap);
            printf("Inode Bitmap Block ID : %d\n", gd2[i].bg_inode_bitmap);
            printf("Inode Table Block ID : %d\n", gd2[i].bg_inode_table);
            printf("Number of Free Blocks : %d\n", gd2[i].bg_free_blocks_count);
            printf("Number of Free Inodes : %d\n", gd2[i].bg_free_inodes_count);
            printf("Number of Directories : %d\n", gd2[i].bg_used_dirs_count);
            printf("Free Block IDs : ");

            // set the file offset to block eight
            if (lseek(fd, gd2[i].bg_block_bitmap * BLOCK_SIZE, SEEK_SET) != gd2[i].bg_block_bitmap * BLOCK_SIZE) {
                perror("File seek failed");
                exit(1);
            }

            unsigned char byte;
            unsigned int iBit;
            bool tripped = false; // on the first zero entry found
            int iLastRead = -1;
            int iLastLastRead = -1;

            // to read the block bitmap byte by byte
            for (int j = 0; j < 4095; ++j) {

                // read block bitmap into byte variable
                rv = read(fd, &byte, 1);
                if (rv == -1) {
                    perror("File read failed");
                    exit(1);
                }

                for (int k = 0; k < 8; k++) { // for each bit 0 - 7
                    iBit = (byte >> k) & 1; // to extract bits from byte
                    // if it starts with 0
                    if (iBit == 0 && iLastRead == -1) {
                        tripped = true;
                        printf("%d", j * 8 + k);
                    } else if (iBit == 0 && iLastRead == 0 && iLastLastRead == -1) {
                        // if it started with a 0 and is still going
                        printf("-");
                    } else if (iBit == 1 && iLastRead == 0 && iLastLastRead == 0) {
                        // when the string of zeroes ends
                        printf("-%d", (j * 8 - 1) + k);
                    } else if (iBit == 0 && iLastRead == 1) {
                        // when a new zero appears
                        if (tripped) { printf(", "); } // for comma formatting
                        tripped = true;
                        printf("%d", j * 8 + k);
                    } else if (iBit == 1 && iLastRead == 0 && iLastLastRead == 0) {
                        // when the new string of zeroes ends
                        printf("-%d", (j * 8 - 1) + k);
                    }
                    iLastLastRead = iLastRead;
                    iLastRead = iBit;
                } // end kfor
            } // end jfor
            if (iBit == 0 && iLastRead == 0 && iLastLastRead == 0) {
                // if the bitmap ended in zero:
                printf("-%d", iBlockRangeH);
            }
            printf("\n");

            printf("Free Inode IDs : ");

            // set the file offset to inode bitmap
            if (lseek(fd, gd2[i].bg_inode_bitmap * BLOCK_SIZE, SEEK_SET) != gd2[i].bg_inode_bitmap * BLOCK_SIZE) {
                perror("File seek failed");
                exit(1);
            }

            // reset the variables
            tripped = false;
            iLastRead = -1;
            iLastLastRead = -1;

            // now read the inode bitmap byte by byte
            for (int j = 0; j < 4095; ++j) {

                // read inode bitmap into byte variable
                rv = read(fd, &byte, 1);
                if (rv == -1) {
                    perror("File read failed");
                    exit(1);
                }

                for (int k = 0; k < 8; k++) { // for each bit 0 - 7
                    iBit = (byte >> k) & 1; // to extract bits from byte
                    if (iBit == 0 && iLastRead == -1) {
                        // if it starts with 0
                        printf("%d", j * 8 + (k + 1)); // numbering change
                        tripped = true;
                    } else if (iBit == 0 && iLastRead == 0 && iLastLastRead == -1) {
                        // if it started with a 0 and is still going
                        printf("-");
                    } else if (iBit == 1 && iLastRead == 0 && iLastLastRead == 0) {
                        // when the string of zeroes ends
                        printf("-%d", (j * 8 - 1) + (k + 1));
                    } else if (iBit == 0 && iLastRead == 1) {
                        // a new zero appears
                        if (tripped) { printf(", "); }
                        printf("%d", j * 8 + (k + 1));
                        tripped = true;

                    } else if (iBit == 1 && iLastRead == 0 && iLastLastRead == 0) { // when the new string of zeroes ends
                        printf("-%d", (j * 8 - 1) + (k + 1));
                    }
                    iLastLastRead = iLastRead;
                    iLastRead = iBit;
                } // end kfor
            } // end jfor
            if (iBit == 0 && iLastRead == 0 && iLastLastRead == 0) {
                // if the bitmap ended in zero:
                printf("-%d", iBlockRangeH + 1);
            }
            printf("\n\n");
        } // end group for
    }

    //--------------------------------------------------------------------------
    //  Part Three
    //--------------------------------------------------------------------------
    struct ext2_inode *ino2 = NULL;
    ino2 = malloc(sizeof(struct ext2_inode));

    if (ino2 == NULL) {
        fprintf(stderr, "%s: Error in ino2 malloc\n", "gd");
        exit(1);
    }

    // set the file offset to root inode (inode 2)
    if (lseek(fd, gd2[0].bg_inode_table * BLOCK_SIZE + (2 - 1) * sizeof(struct ext2_inode), SEEK_SET) != gd2[0].bg_inode_table * BLOCK_SIZE + (2 - 1) * sizeof(struct ext2_inode)) {
        perror("File seek failed");
        exit(1);
    }

    // read inode into variable
    rv = read(fd, ino2, sizeof(struct ext2_inode));
    if (rv == -1) {
        perror("File read failed");
        exit(1);
    }

    struct ext2_dir_entry_2 *de2 = NULL;
    de2 = malloc(sizeof(struct ext2_dir_entry_2));

    if (de2 == NULL) {
        fprintf(stderr, "%s: Error in de2 malloc\n", "gd");
        exit(1);
    }

    // set the file offset to the inode pointed to by the root (inode 2)
    if (lseek(fd, ino2->i_block[0] * BLOCK_SIZE, SEEK_SET) != ino2->i_block[0] * BLOCK_SIZE) {
        perror("File seek failed");
        exit(1);
    }

    // read inode bitmap into byte variable
    rv = read(fd, de2, sizeof(struct ext2_dir_entry_2));
    if (rv == -1) {
        perror("File read failed");
        exit(1);
    }

    // the prints begin:
    printf("--Root Directory Entries--\n");
    int iNextDir = ino2->i_block[0] * BLOCK_SIZE;
    while (iNextDir < ino2->i_block[0] * BLOCK_SIZE + ino2->i_size) {
        printf("Inode: %d\n", de2->inode);
        printf("Entry Length : %d\n", de2->rec_len);
        printf("Name Length : %d\n", de2->name_len);
        printf("File Type : %d\n", de2->file_type);
        printf("Name : %s\n\n", de2->name);

        // to increment next directory location
        iNextDir += de2->rec_len;

        // set file offset to next directory entry
        if (lseek(fd, iNextDir, SEEK_SET) != iNextDir) {
            perror("File seek failed");
            exit(1);
        }
        // read next directory entry into entry variable
        rv = read(fd, de2, sizeof(struct ext2_dir_entry_2));
        if (rv == -1) {
            perror("File read failed");
            exit(1);
        }
    }

    free(de2);
    de2 = NULL;
    free(ino2);
    ino2 = NULL;
    free(gd2);
    gd2 = NULL;
    free(sb2);
    sb2 = NULL;
    close(fd);

    //  stop Valgrind's "FILE DESCRIPTORS open at exit" error:
    fclose(stdin);
    fclose(stdout);
    fclose(stderr);

    //  obtain user confirmation before exiting
    getchar();
    return 0;

}
