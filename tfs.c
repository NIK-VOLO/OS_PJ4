/*
 *  Copyright (C) 2021 CS416 Rutgers CS
 *	Tiny File System
 *	File:	tfs.c
 *
 */

#define FUSE_USE_VERSION 26

#include <fuse.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#include <sys/time.h>
#include <libgen.h>
#include <limits.h>
#include <time.h>

#include "block.h"
#include "tfs.h"


char diskfile_path[PATH_MAX];

// Declare your in-memory data structures here

//Global in-memory data for the superblock
struct superblock* sb;

//Bitmaps are typedef unsigned char*
bitmap_t inode_map;
bitmap_t data_map;

//Root inode
struct inode* root_inode;


//Helper Function declarations:

static int num_blocks_needed(int block_size, int num_bytes_needed);

/* Returns the number of disk blocks are required to store some amount of bytes */
static int num_blocks_needed(int block_size, int num_bytes_needed){
	return num_bytes_needed/block_size + ((num_bytes_needed % block_size) != 0);
}



/* 
 * Get available inode number from bitmap
 */
int get_avail_ino() {

	// Step 1: Read inode bitmap from disk
	
	// Step 2: Traverse inode bitmap to find an available slot

	// Step 3: Update inode bitmap and write to disk 

	return 0;
}

/* 
 * Get available data block number from bitmap
 */
int get_avail_blkno() {

	// Step 1: Read data block bitmap from disk
	
	// Step 2: Traverse data block bitmap to find an available slot

	// Step 3: Update data block bitmap and write to disk 

	return 0;
}

/* 
 * inode operations
 */
int readi(uint16_t ino, struct inode *inode) {

  // Step 1: Get the inode's on-disk block number

  // Step 2: Get offset of the inode in the inode on-disk block

  // Step 3: Read the block from disk and then copy into inode structure

	return 0;
}

int writei(uint16_t ino, struct inode *inode) {

	// Step 1: Get the block number where this inode resides on disk
	
	// Step 2: Get the offset in the block where this inode resides on disk

	// Step 3: Write inode to disk 

	return 0;
}


/* 
 * directory operations
 */
int dir_find(uint16_t ino, const char *fname, size_t name_len, struct dirent *dirent) {

  // Step 1: Call readi() to get the inode using ino (inode number of current directory)

  // Step 2: Get data block of current directory from inode

  // Step 3: Read directory's data block and check each directory entry.
  //If the name matches, then copy directory entry to dirent structure

	return 0;
}

int dir_add(struct inode dir_inode, uint16_t f_ino, const char *fname, size_t name_len) {

	// Step 1: Read dir_inode's data block and check each directory entry of dir_inode
	
	// Step 2: Check if fname (directory name) is already used in other entries

	// Step 3: Add directory entry in dir_inode's data block and write to disk

	// Allocate a new data block for this directory if it does not exist

	// Update directory inode

	// Write directory entry

	return 0;
}

int dir_remove(struct inode dir_inode, const char *fname, size_t name_len) {

	// Step 1: Read dir_inode's data block and checks each directory entry of dir_inode
	
	// Step 2: Check if fname exist

	// Step 3: If exist, then remove it from dir_inode's data block and write to disk

	return 0;
}

/* 
 * namei operation
 */
int get_node_by_path(const char *path, uint16_t ino, struct inode *inode) {
	
	// Step 1: Resolve the path name, walk through path, and finally, find its inode.
	// Note: You could either implement it in a iterative way or recursive way

	return 0;
}

/* 
 * Make file system
 */
int tfs_mkfs() {

	int open;
	int num_sup_blocks;
	int imap_size;
	int dmap_size;
	int num_imap_blocks;
	int num_dmap_blocks;
	int max_inode_bytes;
	//int max_data_bytes;
	int num_inode_blocks;
	//int num_data_blocks;
	int block_count = 0;
	int wretstat;
	struct stat* rstat;
	time_t seconds; 
	

	// Call dev_init() to initialize (Create) Diskfile
	// dev_init() internally checks if the disk file has been created yet.
	printf("tfs_mkfs(): Check 1 . . . \n");
	dev_init(diskfile_path);

	// Write superblock information &
		// Block 0 reserved for superblock
		// Create Superblock struct
		// Copy data to buffer
		// write the buffer contents to disk

	
	//TODO: This might not be necessary --> disk file already opened in dev_init
	printf("tfs_mkfs(): Check 2 . . . \n");
	open = dev_open(diskfile_path);
	if(open == -1){
		return -1;
	}

	//Allocate space in memory for superblock
	sb = malloc(sizeof(struct superblock));
	printf("Size of superblock: %ld\n",sizeof(sb));
	if(sb == NULL){
		// Malloc Failed somehow
		return -1;
	}

	//Calculate the number of blocks needed to store the superblock
	num_sup_blocks = num_blocks_needed(BLOCK_SIZE, sizeof(struct superblock));

	//Calculate the size of the inode bitmap
	imap_size = MAX_INUM/8;

	//Calc size of data bitmap
	dmap_size = MAX_DNUM/8;

	//Calc the number of blocks needed to store the inode bitmap
	num_imap_blocks = num_blocks_needed(BLOCK_SIZE, imap_size);

	//Calc the number of blocks needed to store the data bitmap
	num_dmap_blocks = num_blocks_needed(BLOCK_SIZE, dmap_size);

	//Calc the number of maximum possible bytes needed to store all inodes
	max_inode_bytes = sizeof(struct inode) * MAX_INUM;

	//Calc the number of blocks needed for all of the inodes
	num_inode_blocks = num_blocks_needed(BLOCK_SIZE, max_inode_bytes);

	sb->magic_num = MAGIC_NUM;
	sb->max_inum = MAX_INUM;
	sb->max_dnum = MAX_DNUM;
	//block_count starts at 0. Incremented by how many blocks are required for each section
	//Increment by how many blocks needed by superblock data
	block_count += num_sup_blocks;
	sb->i_bitmap_blk = block_count; // Should == 1
	//Increment by how many blocks needed by inode bitmap
	block_count += num_imap_blocks;
	sb->d_bitmap_blk = block_count;
	//Increment by how many blocks needed by data bitmap
	block_count += num_dmap_blocks;
	sb->i_start_blk = block_count;
	//Increment by how many blocks needed by inodes section
	block_count += num_inode_blocks;
	sb->d_start_blk = block_count;
	

	// initialize inode bitmap
	inode_map = malloc(imap_size);
	if(inode_map == NULL){
		// Malloc Failed somehow
		return -1;
	}
	memset(inode_map, 0, imap_size);

	// initialize data block bitmap
	data_map = malloc(dmap_size);
	if(data_map == NULL){
		// Malloc Failed somehow
		return -1;
	}
	memset(inode_map, 0, dmap_size);

	//update bitmap information for root directory
	set_bitmap(inode_map, 0);


	//TODO: update inode for root directory
	root_inode = malloc(sizeof(struct inode));
	if(root_inode == NULL){
		// Malloc Failed somehow
		return -1;
	}
	root_inode->ino = 0;
	root_inode->valid = I_VALID; //TODO: Double check this
	root_inode->size = 0; //TODO: Temporarily set to 0
	root_inode->type = TFS_DIR; //TODO: Double check this
	root_inode->link = 2; //TODO: Double check this
	//root_inode->direct_ptr; //TODO: Temporarily not set
	//root_inode->indirect_ptr; //TODO: Temporarily not set
	rstat = malloc(sizeof(struct stat));
	memset (rstat, 0, sizeof(struct stat));
	time(&seconds);
	rstat->st_atime = seconds;
	rstat->st_mtime = seconds;
	root_inode->vstat = *rstat;

	//Write to disk (Super Block, Bitmaps, Root inode)
	wretstat = bio_write(0, sb);
	if(wretstat < 0){
		return -1;
	}
	wretstat = bio_write((int)sb->i_bitmap_blk, inode_map);
	if(wretstat < 0){
		return -1;
	}
	wretstat = bio_write((int)sb->d_bitmap_blk, data_map);
	if(wretstat < 0){
		return -1;
	}

	//TODO: Write root inode to inode block (-- writei() --)
	wretstat = bio_write((int)sb->i_start_blk, root_inode);
	if(wretstat < 0){
		return -1;
	}

	printf("tfs_mkfs(): * DONE * \n");
	return 0;
}


/* 
 * FUSE file operations
 */
static void *tfs_init(struct fuse_conn_info *conn) {

	int mkfs_res = -1;
	int rretstat;
	int imap_size;
	int dmap_size;

	// Step 1a: If disk file is not found, call mkfs
	// Partition bitmap block, inode block, data block
	printf("tfs_init(): Check 1 . . . \n");
	if(dev_open(diskfile_path) < 0){

		mkfs_res = tfs_mkfs();

		if(mkfs_res < 0){
			perror("tfs_mkfs failed");
			exit(EXIT_FAILURE);
		}
	}else{
		// Step 1b: If disk file is found, just initialize in-memory data structures
		// and read superblock from disk

		//Check if space allocated in memory for superblock
		printf("tfs_init(): Check 2 . . . \n");
		if(sb == NULL){
			//Allocate 
			sb = malloc(sizeof(struct superblock));
			printf("Size of superblock: %ld\n",sizeof(sb));
			if(sb == NULL){
				// Malloc Failed somehow
				perror("tfs_init failed:");
				exit(EXIT_FAILURE);
			}
		}


		//Calculate the size of the inode bitmap
		imap_size = MAX_INUM/8;

		//Check if space allocated in memory for inode bitmap
		printf("tfs_init(): Check 3 . . . \n");
		if(inode_map == NULL){
			//Allocate 
			inode_map = malloc(imap_size);
			if(inode_map == NULL){
				/// Malloc Failed somehow
				perror("tfs_init failed:");
				exit(EXIT_FAILURE);
			}
		}

		//Calculate the size of the data bitmap
		dmap_size = MAX_DNUM/8;

		//Check if space allocated in memory for inode bitmap
		printf("tfs_init(): Check 4 . . . \n");
		if(data_map == NULL){
			//Allocate 
			data_map = malloc(dmap_size);
			if(data_map == NULL){
				/// Malloc Failed somehow
				perror("tfs_init failed:");
				exit(EXIT_FAILURE);
			}
		}

		//Load superblock from disk
		printf("tfs_init(): Check 5 . . . \n");
		rretstat = bio_read(0, sb);
		if(rretstat < 0){
			perror("tfs_init disk read failure:");
			exit(EXIT_FAILURE);
		}

		//Load inode bitmap from disk
		printf("tfs_init(): Check 6 . . . \n");
		rretstat = bio_read((int)sb->i_bitmap_blk, inode_map);
		if(rretstat < 0){
			perror("tfs_init disk read failure:");
			exit(EXIT_FAILURE);
		}

		//Load data bitmap from disk
		printf("tfs_init(): Check 7 . . . \n");
		rretstat = bio_read((int)sb->d_bitmap_blk, data_map);
		if(rretstat < 0){
			perror("tfs_init disk read failure:");
			exit(EXIT_FAILURE);
		}


		//TODO: Read the root inode
	}

	printf("tfs_init(): * DONE *\n");

	//Optional return value
	return NULL;
}

static void tfs_destroy(void *userdata) {

	// Step 1: De-allocate in-memory data structures
	printf("tfs_destroy(): Check 1 . . . \n");
	free(sb);
	printf("tfs_destroy(): Check 2 . . . \n");
	free(inode_map);
	printf("tfs_destroy(): Check 3 . . . \n");
	free(data_map);
	printf("tfs_destroy(): Check 4 . . . \n");
	free(&root_inode->vstat);
	printf("tfs_destroy(): Check 5 . . . \n");
	free(root_inode);
	printf("tfs_destroy(): Check 6.1 . . . \n");
	if(userdata != NULL){
		printf("tfs_destroy(): Check 6.2 . . . \n");
		free(userdata);
	}

	// Step 2: Close diskfile
	printf("tfs_destroy(): Check 7 . . . \n");
	dev_close();

	printf("tfs_destroy(): * DONE *\n");
}	

static int tfs_getattr(const char *path, struct stat *stbuf) {

	// Step 1: call get_node_by_path() to get inode from path

	// Step 2: fill attribute of file into stbuf from inode

		stbuf->st_mode   = S_IFDIR | 0755;
		stbuf->st_nlink  = 2;
		time(&stbuf->st_mtime);

	//TODO: Temporarily set to -1
	return -1;
}

static int tfs_opendir(const char *path, struct fuse_file_info *fi) {

	// Step 1: Call get_node_by_path() to get inode from path

	// Step 2: If not find, return -1

    return 0;
}

static int tfs_readdir(const char *path, void *buffer, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi) {

	// Step 1: Call get_node_by_path() to get inode from path

	// Step 2: Read directory entries from its data blocks, and copy them to filler

	return 0;
}


static int tfs_mkdir(const char *path, mode_t mode) {

	// Step 1: Use dirname() and basename() to separate parent directory path and target directory name

	// Step 2: Call get_node_by_path() to get inode of parent directory

	// Step 3: Call get_avail_ino() to get an available inode number

	// Step 4: Call dir_add() to add directory entry of target directory to parent directory

	// Step 5: Update inode for target directory

	// Step 6: Call writei() to write inode to disk
	

	return 0;
}

static int tfs_rmdir(const char *path) {

	// Step 1: Use dirname() and basename() to separate parent directory path and target directory name

	// Step 2: Call get_node_by_path() to get inode of target directory

	// Step 3: Clear data block bitmap of target directory

	// Step 4: Clear inode bitmap and its data block

	// Step 5: Call get_node_by_path() to get inode of parent directory

	// Step 6: Call dir_remove() to remove directory entry of target directory in its parent directory

	return 0;
}

static int tfs_releasedir(const char *path, struct fuse_file_info *fi) {
	// For this project, you don't need to fill this function
	// But DO NOT DELETE IT!
    return 0;
}

static int tfs_create(const char *path, mode_t mode, struct fuse_file_info *fi) {

	// Step 1: Use dirname() and basename() to separate parent directory path and target file name

	// Step 2: Call get_node_by_path() to get inode of parent directory

	// Step 3: Call get_avail_ino() to get an available inode number

	// Step 4: Call dir_add() to add directory entry of target file to parent directory

	// Step 5: Update inode for target file

	// Step 6: Call writei() to write inode to disk

	return 0;
}

static int tfs_open(const char *path, struct fuse_file_info *fi) {

	// Step 1: Call get_node_by_path() to get inode from path

	// Step 2: If not find, return -1

	return 0;
}

static int tfs_read(const char *path, char *buffer, size_t size, off_t offset, struct fuse_file_info *fi) {

	// Step 1: You could call get_node_by_path() to get inode from path

	// Step 2: Based on size and offset, read its data blocks from disk

	// Step 3: copy the correct amount of data from offset to buffer

	// Note: this function should return the amount of bytes you copied to buffer
	return 0;
}

static int tfs_write(const char *path, const char *buffer, size_t size, off_t offset, struct fuse_file_info *fi) {
	// Step 1: You could call get_node_by_path() to get inode from path

	// Step 2: Based on size and offset, read its data blocks from disk

	// Step 3: Write the correct amount of data from offset to disk

	// Step 4: Update the inode info and write it to disk

	// Note: this function should return the amount of bytes you write to disk
	return size;
}

static int tfs_unlink(const char *path) {

	// Step 1: Use dirname() and basename() to separate parent directory path and target file name

	// Step 2: Call get_node_by_path() to get inode of target file

	// Step 3: Clear data block bitmap of target file

	// Step 4: Clear inode bitmap and its data block

	// Step 5: Call get_node_by_path() to get inode of parent directory

	// Step 6: Call dir_remove() to remove directory entry of target file in its parent directory

	return 0;
}

static int tfs_truncate(const char *path, off_t size) {
	// For this project, you don't need to fill this function
	// But DO NOT DELETE IT!
    return 0;
}

static int tfs_release(const char *path, struct fuse_file_info *fi) {
	// For this project, you don't need to fill this function
	// But DO NOT DELETE IT!
	return 0;
}

static int tfs_flush(const char * path, struct fuse_file_info * fi) {
	// For this project, you don't need to fill this function
	// But DO NOT DELETE IT!
    return 0;
}

static int tfs_utimens(const char *path, const struct timespec tv[2]) {
	// For this project, you don't need to fill this function
	// But DO NOT DELETE IT!
    return 0;
}


static struct fuse_operations tfs_ope = {
	.init		= tfs_init,
	.destroy	= tfs_destroy,

	.getattr	= tfs_getattr,
	.readdir	= tfs_readdir,
	.opendir	= tfs_opendir,
	.releasedir	= tfs_releasedir,
	.mkdir		= tfs_mkdir,
	.rmdir		= tfs_rmdir,

	.create		= tfs_create,
	.open		= tfs_open,
	.read 		= tfs_read,
	.write		= tfs_write,
	.unlink		= tfs_unlink,

	.truncate   = tfs_truncate,
	.flush      = tfs_flush,
	.utimens    = tfs_utimens,
	.release	= tfs_release
};


int main(int argc, char *argv[]) {
	int fuse_stat;

	printf("main(): Check 1 . . . \n");
	getcwd(diskfile_path, PATH_MAX);
	printf("main(): Check 2 . . . \n");
	strcat(diskfile_path, "/DISKFILE");

	printf("main(): Check 3 . . . \n");
	fuse_stat = fuse_main(argc, argv, &tfs_ope, NULL);

	printf("main(): * DONE *\n");
	return fuse_stat;
}

