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
#include <pthread.h>

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

pthread_mutex_t lock;


//Helper Function declarations:

static int num_blocks_needed(int block_size, int num_bytes_needed);

/* Returns the number of disk blocks are required to store some amount of bytes */
static int num_blocks_needed(int block_size, int num_bytes_needed){
	return num_bytes_needed/block_size + ((num_bytes_needed % block_size) != 0);
}

static void print_bitmap(bitmap_t bitmap){
	int i;
	for(i = 0; i < sizeof(bitmap); i++){
		printf("%d ", get_bitmap(bitmap, i));
	}
	printf("\n");
}

static void print_bitmap_sum(bitmap_t bitmap){
	int i, total;
	total = 0;
	for(i = 0; i < sizeof(bitmap); i++){
		total += get_bitmap(bitmap, i);
	}
	printf("used = %d\n", total);
}

/* 
 * Get available inode number from bitmap
 */
int get_avail_ino() {

	if (inode_map == NULL) {
		printf("Error in get_avail_ino(): inode_map not allocated");
		return -1;
	}

	// Step 1: Read inode bitmap from disk
	char buffer[BLOCK_SIZE];
	pthread_mutex_lock(&lock);
	int read_ret = bio_read(sb->i_bitmap_blk, buffer);
	pthread_mutex_unlock(&lock);
	if (read_ret < 0) {
		printf("Error in get_avail_ino(): could not read bitmap from disk\n");
		return -1;
	}
	memcpy(inode_map, buffer, MAX_INUM/8);

	// Step 2: Traverse inode bitmap to find an available slot
	int i, bit, next_slot;
	next_slot = -1;
	for (i=0; i<MAX_INUM; i++) {
		bit = get_bitmap(inode_map, i);
		if (bit == 0) {
			// Found an available slot
			next_slot = i;
			break;
		}
	}
	if (next_slot == -1) {
		printf("Error in get_avail_ino(): could not find open slot\n");
		return -1;
	}
	
	// Step 3: Update inode bitmap
	set_bitmap(inode_map, next_slot);

	// Step 4: Copy back into buffer and write to disk
	memcpy(buffer, inode_map, MAX_INUM/8);
	pthread_mutex_lock(&lock);
	int write_ret = bio_write(sb->i_bitmap_blk, buffer);
	pthread_mutex_unlock(&lock);
	if (write_ret < 0) {
		printf("Error in get_avail_ino(): could not write updated bitmap to disk\n");
		return -1;
	}

	return next_slot;
}

/* 
 * Get available data block number from bitmap
 */
int get_avail_blkno() {

	if (data_map == NULL) {
		printf("Error in get_avail_blkno(): data_map not allocated");
		return -1;
	}

	// Step 1: Read data bitmap from disk
	char buffer[BLOCK_SIZE];
	pthread_mutex_lock(&lock);
	int read_ret = bio_read(sb->d_bitmap_blk, buffer);
	pthread_mutex_unlock(&lock);
	if (read_ret < 0) {
		printf("Error in get_avail_blkno(): could not read bitmap from disk\n");
		return -1;
	}
	memcpy(data_map, buffer, MAX_DNUM/8);

	// Step 2: Traverse data bitmap to find an available slot
	int i, bit, next_slot;
	next_slot = -1;
	for (i=0; i<MAX_DNUM; i++) {
		bit = get_bitmap(data_map, i);
		if (bit == 0) {
			// Found an available slot
			next_slot = i;
			break;
		}
	}
	if (next_slot == -1) {
		printf("Error in get_avail_blkno(): could not find open slot\n");
		return -1;
	}
	
	// Step 3: Update inode bitmap
	set_bitmap(data_map, next_slot);

	// Step 4: Copy back into buffer and write to disk
	memcpy(buffer, data_map, MAX_INUM/8);
	pthread_mutex_lock(&lock);
	int write_ret = bio_write(sb->d_bitmap_blk, buffer);
	pthread_mutex_unlock(&lock);
	if (write_ret < 0) {
		printf("Error in get_avail_blkno(): could not write updated bitmap to disk\n");
		return -1;
	}

	printf("get_avail_blkno(): Returning %d\n", next_slot);

	return next_slot;
}

/* 
 * inode operations
 */
int readi(uint16_t ino, struct inode *inode) {

	if (inode == NULL) {
		printf("Error in readi(): given a null pointer to inode\n");
		return -1;
	}

	// Step 1: Get the inode's on-disk block number
	int total_offset = ino * sizeof(struct inode);
	printf("readi(): i start block = %d, ino = %d\n", sb->i_start_blk, ino);
	int block_index = sb->i_start_blk + (int)(total_offset / BLOCK_SIZE);

	// Step 2: Get offset of the inode in the inode on-disk block
	int inner_offset = total_offset % BLOCK_SIZE;

	// Step 3: Read the block from disk and then copy into inode structure
	printf("readi(): About to read block at index %d to buffer\n", block_index);

	char buffer[BLOCK_SIZE];
	pthread_mutex_lock(&lock);
	bio_read(block_index, buffer);
	pthread_mutex_unlock(&lock);
	memcpy(inode, buffer + inner_offset, sizeof(struct inode));

	printf("readi(): Complete! ino = %d, valid = %d\n", ino, inode->valid);

	return 0;
}

int writei(uint16_t ino, struct inode *inode) {

	if (inode == NULL) {
		printf("Error in writei(): given a null pointer to inode\n");
		return -1;
	}

	// Step 1: Get the inode's on-disk block number
	int total_offset = ino * sizeof(struct inode);
	int block_index = sb->i_start_blk + (total_offset / BLOCK_SIZE);

	// Step 2: Get offset of the inode in the inode on-disk block
	int inner_offset = total_offset % BLOCK_SIZE;

	// Step 3: Read the block from disk and then copy into inode structure
	char buffer[BLOCK_SIZE];
	printf("writei(): Check 1...\n");
	pthread_mutex_lock(&lock);
	bio_read(block_index, buffer);
	printf("writei(): Check 2...\n");
	memcpy(&buffer[inner_offset], inode, sizeof(struct inode));
	bio_write(block_index, buffer);
	pthread_mutex_unlock(&lock);
	printf("writei(): Complete! ino = %d\n", ino);
	return 0;
}


/* 
 * directory operations
 */
int dir_find(uint16_t ino, const char *fname, size_t name_len, struct dirent *dirent) {

	// Step 1: Call readi() to get the inode using ino (inode number of current directory)
	struct inode* mynode = malloc(sizeof(struct inode));
	readi(ino, mynode);

	// Step 2: Get data block of current directory from inode
	int dirent_index[16];
	memcpy(dirent_index, mynode->direct_ptr, 16 * sizeof(int));
	char buffer[BLOCK_SIZE];

	int i, j;
	for (i=0; i<16; i++) {

		if (dirent_index[i] == -1) continue;

		// Step 3: Read directory's data block and check each directory entry.
		pthread_mutex_lock(&lock);
		int read_ret = bio_read(dirent_index[i], buffer);
		pthread_mutex_unlock(&lock);
		if (read_ret < 0) {
			printf("Error in dir_find(): Unable to read block of current directory\n");
		}

		// Locate a dirent within this block
		for (j=0; j<BLOCK_SIZE-sizeof(struct dirent); j+=sizeof(struct dirent)) {

			if(j > mynode->size){
				break;
			}

			struct dirent* my_dirent = malloc(sizeof(struct dirent));
			memcpy(my_dirent, buffer + j, sizeof(struct dirent));

			// If the name matches, then copy directory entry to dirent structure
			int strcmp_ret = strcmp(fname, my_dirent->name);
			if (strcmp_ret == 0) {
				// Name matches, copy to dirent structure
				memcpy(dirent, my_dirent, sizeof(struct dirent));
				free(my_dirent);
				free(mynode);
				return 0;
			}

			free(my_dirent);
			
		}
	}

	free(mynode);

	printf("dir_find(): Unable to find name in directory\n");
	return 1;
}

int dir_add(struct inode dir_inode, uint16_t f_ino, const char *fname, size_t name_len) {

	// Step 1: Read dir_inode's data block and check each directory entry of dir_inode
	
	// Step 2: Check if fname (directory name) is already used in other entries
	
	printf("dir_add(): Starting...\n");
	printf("dir_add(): Original Inode#%d values:\n\tLink Count: %d\n\tSize: %d\n", dir_inode.ino, dir_inode.link, dir_inode.size);
	int dirent_index[16];
	memcpy(dirent_index, dir_inode.direct_ptr, 16 * sizeof(int));
	char buffer[BLOCK_SIZE];


	int i, j;
	struct dirent* my_dirent = malloc(sizeof(struct dirent));
	struct dirent* target_dirent = malloc(sizeof(struct dirent));
	for (i=0; i<16; i++) {

		if (dirent_index[i] == -1) break;
		
		pthread_mutex_lock(&lock);
		int read_ret = bio_read(dirent_index[i], buffer);
		pthread_mutex_unlock(&lock);
		if (read_ret < 0) {
			printf("Error in dir_add(): Unable to read dir_inode data block\n");
			free(my_dirent);
			return -1;
		}

		printf("dir_add(): Reading data block %d for existing dir name '%s'. . .\n", dirent_index[i], fname);
		for (j=0; j<BLOCK_SIZE-sizeof(struct dirent); j+=sizeof(struct dirent)) {

			memcpy(my_dirent, buffer + j, sizeof(struct dirent));

			//Set target_dirent to the earliest open position
			if(strcmp(my_dirent->name, "") == 0){
				printf("\tFound an empty dirent! setting target_dirent\n");
				memcpy(target_dirent, buffer+j, sizeof(struct dirent));
			}

			// Check if name is already used
			int strcmp_ret = strcmp(fname, my_dirent->name);
			if (strcmp_ret == 0) {
				// Name matches, not allowed to add this name
				printf("dir_add(): %s is already a name in current directory\n", fname);
				free(my_dirent);
				return 2;
			}
		}
	}

	
	printf("dir_add(): Target_dirent:\n\tValid = %d\n", target_dirent->valid);

	// Update my_diren to the first open dirent position
	if(target_dirent->valid == 0){
		memcpy(my_dirent, target_dirent, sizeof(struct dirent));
		my_dirent->ino = f_ino;
		my_dirent->valid = 1;
		memcpy(&my_dirent->name, fname, name_len);
		my_dirent->len = name_len;
	}else{
		my_dirent->ino = f_ino;
		my_dirent->valid = 1;
		memcpy(&my_dirent->name, fname, name_len);
		my_dirent->len = name_len;
	}
	
	

	// Look through existing blocks for space
	char buffer2[BLOCK_SIZE];
	struct dirent* temp_dirent = malloc(sizeof(struct dirent));
	for (i=0; i<16; i++) {
		if (dirent_index[i] == -1) continue;

		printf("dir_add(): Trying to read absolute block %d to buffer\n", dirent_index[i]);

		pthread_mutex_lock(&lock);
		int read_ret = bio_read(dirent_index[i], buffer2);
		pthread_mutex_unlock(&lock);
		printf("dir_add(): Successfully read absolute block %d to buffer\n", dirent_index[i]);
		if (read_ret < 0) {
			printf("Error in dir_add(): Unable to read dir_inode data block (2)\n");
			free(my_dirent);
			free(temp_dirent);
			return -1;
		}
		
		for (j=0; j<BLOCK_SIZE-sizeof(struct dirent); j+=sizeof(struct dirent)) {

			if(j > dir_inode.size){
				break;
			}

			// Look for zeroes
			memcpy(temp_dirent, buffer2 + j, sizeof(struct dirent));
			printf("\t--> dir_add(): temp_dirent->valid = %d\n", temp_dirent->valid);

			// temp_dirent->valid > 1 is a fix for when the data is unreadable and shows a random# > 1
			if (temp_dirent->valid == 0 || temp_dirent->valid > 1) {

				printf("dir_add(): Writing in existing block\n");

				// This is a good spot
				memcpy(buffer2 + j, my_dirent, sizeof(struct dirent));
				pthread_mutex_lock(&lock);
				bio_write(dirent_index[i], buffer2);
				pthread_mutex_unlock(&lock);

				printf("dir_add(): Wrote dirent:\n\t-- ino # %d\n\t-- Name: %s\n", my_dirent->ino, my_dirent->name);

				// Update directory inode
				memcpy(&dir_inode.direct_ptr, dirent_index, 16*sizeof(int));
				int new_size = dir_inode.size + sizeof(struct dirent);
				dir_inode.size = new_size;
				dir_inode.link++;
				printf("dir_add(): New Inode#%d values:\n\tLink Count: %d\n\tSize: %d\n", dir_inode.ino, dir_inode.link, dir_inode.size);
				writei(dir_inode.ino, &dir_inode);

				printf("dir_add(): Writing in existing block complete!\n");

				free(my_dirent);
				free(temp_dirent);
				return 0;
			}

		}
	}

	printf("dir_add(): *** NO POSITIONS FOUND -- LOOK IN NEW BLOCK\n");

	// Step 3: Add directory entry in dir_inode's data block and write to disk
	for (i=0; i<16; i++) {
		// Finding the next empty block
		printf("dir_add(): Searching for empty block... This one has %d\n", dirent_index[i]);
		if (dirent_index[i] == -1) break;
	}
	if (i >= 16) {
		// No more room for direct pointers in directory
		printf("dir_add(): no direct pointer space to add file in current directory\n");
		free(my_dirent);
		free(temp_dirent);
		return 1;
	}

	printf("dir_add(): Writing in new block\n");

	// Allocate a new data block for this directory if it does not exist
	dirent_index[i] = sb->d_start_blk + get_avail_blkno();
	printf("\tNEW BLOCK# = %d\n", dirent_index[i]);
	pthread_mutex_lock(&lock);
	bio_read(dirent_index[i], buffer2);
	printf("dir_add(): DEBUG INFO: j = %d\n", j);
	memcpy(buffer2, my_dirent, sizeof(struct dirent)); // Too lazy to check validity
	bio_write(dirent_index[i], buffer2);
	pthread_mutex_unlock(&lock);

	// Update directory inode
	memcpy(&dir_inode.direct_ptr, dirent_index, 16*sizeof(int));
	int new_size = dir_inode.size + sizeof(struct dirent);
	dir_inode.size = new_size;
	dir_inode.link++;
	printf("dir_add(): New Inode#%d values:\n\tLink Count: %d\n\tSize: %d\n", dir_inode.ino, dir_inode.link, dir_inode.size);
	writei(dir_inode.ino, &dir_inode);

	printf("dir_add(): Writing in new block complete!\n");

	free(my_dirent);
	free(temp_dirent);
	return 0;
}

int dir_remove(struct inode dir_inode, const char *fname, size_t name_len) {

	// Step 1: Read dir_inode's data block and checks each directory entry of dir_inode
	
	// Step 2: Check if fname exist

	// Step 3: If exist, then remove it from dir_inode's data block and write to disk
	int block_no;
	int dirent_index[16];
	memcpy(dirent_index, dir_inode.direct_ptr, 16 * sizeof(int));
	char buffer[BLOCK_SIZE];

	printf("dir_remove(): Original Inode#%d values:\n\tLink Count: %d\n\tSize: %d\n", dir_inode.ino, dir_inode.link, dir_inode.size);

	int i, j;
	struct dirent* my_dirent = malloc(sizeof(struct dirent));
	for (i=0; i<16; i++) {

		block_no = dir_inode.direct_ptr[i];
		if (block_no == -1) continue;

		pthread_mutex_lock(&lock);
		int read_ret = bio_read(dirent_index[i], buffer);
		pthread_mutex_unlock(&lock);
		if (read_ret < 0) {
			printf("Error in dir_remove(): Unable to dir_inode data block\n");
			free(my_dirent);
			return -1;
		}

		for (j=0; j<BLOCK_SIZE-sizeof(struct dirent); j+=sizeof(struct dirent)) {

			// Break loop if j exceeds the size of this directory
			if(j > dir_inode.size){
				break;
			}

			memcpy(my_dirent, buffer + j, sizeof(struct dirent));

			// Check if name is already used
			int strcmp_ret = strcmp(fname, my_dirent->name);
			if (strcmp_ret == 0) {
				// Name matches, delete this
				
				memset(buffer + j, 0, sizeof(struct dirent));
				pthread_mutex_lock(&lock);
				bio_write(dirent_index[i], buffer);
				pthread_mutex_unlock(&lock);

				//Invalidate direct ptr to block
				//dir_inode.direct_ptr[i] = -1;
				//Reduce link count and size
				dir_inode.link--;
				dir_inode.size -= sizeof(struct dirent);

				printf("dir_remove(): New Inode#%d values:\n\tLink Count: %d\n\tSize: %d\n", dir_inode.ino, dir_inode.link, dir_inode.size);

				//Write inode to disk
				int ret = writei(dir_inode.ino, &dir_inode);
				if(ret < 0){
					free(my_dirent);
					return -1;
				}

				free(my_dirent);
				return 0;
			}
		}
	}

	//Write inode to disk
	int ret = writei(dir_inode.ino, &dir_inode);
	if(ret < 0){
		return -1;
	}
	free(my_dirent);
	return 1;
}

/* 
 * namei operation
 */
int get_node_by_path(const char *path, uint16_t ino, struct inode *inode) {
	
	/*
	valid = 0 for invalid path--return
	valid = 1 for more path left--recursive call
	valid = 2 for end of path--set inode and return
	*/

	// Step 1: Resolve the path name, walk through path, and finally, find its inode.
	// Note: You could either implement it in a iterative way or recursive way
	printf("get_node_by_path(): PATH: %s\n",path);
	char name[208];
	int len = strlen(path);
	int valid = 0;
	int i, j;
	printf("get_node_by_path(): CHECK 1. . . \n");
	if (path[0] == '/') {
		path = &path[1];
	}
	for (i=0; i<len; i++) {
		if (path[i] == '\0') {
			// End of path
			name[i] = '\0';
			valid = 2;
			break;
		}
		if (path[i] == '/') {
			// Partway through path
			name[i] = '\0';
			valid = 1;
			if (i == len-1) {
				// Actually end of path
				valid = 2;
			}
			i++;
			break;
		}
		name[i] = path[i];
	}
	if (i == len) valid = 2;
	printf("get_node_by_path(): %s\n", name);
	if (valid == 0) {
		printf("get_node_by_path(): No inode for this path\n");
		return -1;
	}
	printf("get_node_by_path(): CHECK 2. . . \n");
	if (valid == 1) {
		// This is a dirent
		// Recursion needed
		const char *new_path = &path[i];
		printf("get_node_by_path(): New Path: %s\n", new_path);
		int new_ino = -1;

		char buffer[BLOCK_SIZE];
		struct inode *mynode = malloc(sizeof(struct inode));
		readi(ino, mynode);
		printf("get_node_by_path(): CHECK 2.1. . . \n");
		for (i=0; i<16; i++) {
			printf("get_node_by_path(): i = %d, direct pointer = %d\n", i, mynode->direct_ptr[i]);
			if (mynode->direct_ptr[i] < sb->d_start_blk) continue;

			pthread_mutex_lock(&lock);
			int read_ret = bio_read(mynode->direct_ptr[i], buffer);
			pthread_mutex_unlock(&lock);
			if (read_ret < 0) {
				printf("get_node_by_path(): unnable to load direct pointer\n");
				free(mynode);
				return -1;
			}

			// Locate dirent within block
			for (j=0; i<BLOCK_SIZE-sizeof(struct dirent); j+=sizeof(struct dirent)) {

				// Break loop if j exceeds the size of this directory
				if(j > mynode->size){
					break;
				}
				printf("get_node_by_path(): Size of Dir: %d -- Bytes read: %d\n", mynode->size, j);

				struct dirent* my_dirent = malloc(sizeof(struct dirent));
				memcpy(my_dirent, buffer + j, sizeof(struct dirent));

				printf("get_node_by_path(): Trying to match '%s' with '%s' in dirent %ld in block %d\n", name, my_dirent->name, j / sizeof(struct dirent), mynode->direct_ptr[i]);
				int strcmp_ret = strcmp(name, my_dirent->name);
				if (strcmp_ret == 0) {
					new_ino = my_dirent->ino;
					printf("get_node_by_path(): Found it boss! name is '%s', new ino is %d\n", my_dirent->name, new_ino);
					printf("get_node_by_path(): Recursing...\n");
					free(mynode);
					return get_node_by_path(new_path, new_ino, inode);
				}

			}

		}

		// Couldn't find directory
		printf("get_node_by_path(): '%s' not found\n", name);
		return ENOENT;
	}
	printf("get_node_by_path(): CHECK 3. . . \n");
	if (valid == 2) {
		// Read the inode
		
		int new_ino = -1;
		char buffer[BLOCK_SIZE];
		struct inode* mynode = malloc(sizeof(struct inode));
		int cur_ptr;
		readi(ino, mynode); 
		printf("get_node_by_path(): CHECK 3.3. . . \n");
		for (i=0; i<16; i++) {
			// Read the block corresponding to this direct pointer
			cur_ptr = mynode->direct_ptr[i];
			//printf("\tCurrent ptr: %d\n", cur_ptr);
			if (cur_ptr == -1) break;
			pthread_mutex_lock(&lock);
			bio_read(cur_ptr, buffer);
			pthread_mutex_unlock(&lock);

			j = 0;
			printf("get_node_by_path(): Size of Dir: %d -- Link Count: %d\n", mynode->size, mynode->link);
			while (j < BLOCK_SIZE) {
				// Search through the dirents

				// Break loop if j exceeds the size of this directory
				if(j >= mynode->size){
					break;
				}
				
				struct dirent* my_dirent = malloc(sizeof(struct dirent));
				memcpy(my_dirent, buffer + j, sizeof(struct dirent));

				printf("get_node_by_path(): Trying to match '%s' with '%s' in dirent %ld in block %d\n", name, my_dirent->name, j / sizeof(struct dirent), mynode->direct_ptr[i]);
				if(strcmp(name, "") == 0){
					printf("\tTrying to match root node. . . set new_ino = 0\n");
					new_ino = 0;
				}
				printf("\tBytes Read: %d\n", j);
				int ret = strcmp(name, my_dirent->name);
				if (ret == 0) {
					// Matched name, this has the new ino
					new_ino = my_dirent->ino;
					printf("get_node_by_path(): Found it boss! name is '%s', new ino is %d\n", my_dirent->name, new_ino);
					break;
				}else if(ret != 0 && j == mynode->size){
					printf("\t* Reached end without finding dir '%s'\n", name);
					break;
				}
				

				j += sizeof(struct dirent);
			}
		}

		if (new_ino == -1) {
			printf("get_node_by_path(): Unable to find inode corresponding to '%s'\n", name);
			free(mynode);
			return -1;
		}

		readi(new_ino, inode);
		free(mynode);
		return 0;
	}

	// Shouldn't get here
	return -3;
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
	struct dirent dirents[2];
	char buf[BLOCK_SIZE];

	//Clear the buffer
	memset(buf, 0, BLOCK_SIZE);
	
	// Initilialize the mutex lock
	pthread_mutex_init(&lock, NULL);

	// Call dev_init() to initialize (Create) Diskfile
	// dev_init() internally checks if the disk file has been created yet.
	printf("tfs_mkfs(): Check 1 . . . \n");
	dev_init(diskfile_path);

	// Write superblock information &
		// Block 0 reserved for superblock
		// Create Superblock struct
		// Copy data to buffer
		// write the buffer contents to disk

	
	printf("tfs_mkfs(): Check 2 . . . \n");
	open = dev_open(diskfile_path);
	if(open == -1){
		return -1;
	}

	//Allocate space in memory for superblock
	sb = malloc(sizeof(struct superblock));
	printf("Size of superblock: %ld\n",sizeof(struct superblock));
	if(sb == NULL){
		// Malloc Failed somehow
		return -1;
	}


	printf("tfs_mkfs(): Check 3 . . . \n");	
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

	//PRINT DEBUG INFO:
	printf("******************************************************\n");
	printf("Block Size: %d\n", BLOCK_SIZE);
	printf("Number of blocks for super block: %d\n", num_sup_blocks);
	printf("Size of inode bitmap: %d bytes\n", imap_size);
	printf("Size of data bitmap: %d bytes\n", dmap_size);
	printf("Blocks needed for inode bitmap: %d\n", num_imap_blocks);
	printf("Blocks needed for data bitmap: %d\n", num_dmap_blocks);
	printf("Size of inode struct: %ld bytes\n", sizeof(struct inode));
	printf("Max bytes needed for all inodes: %d\n", max_inode_bytes);
	printf("Number of blocks needed to store all inodes: %d\n", num_inode_blocks);
	printf("******************************************************\n");

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
	
	printf("tfs_mkfs(): Check 4 . . . \n");
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
	memset(data_map, 0, dmap_size);

	//update bitmap information for root directory
	set_bitmap(inode_map, 0);
	set_bitmap(data_map, 0); 	

	printf("tfs_mkfs(): Check 5 . . . \n");
	
	root_inode = malloc(sizeof(struct inode));
	if(root_inode == NULL){
		// Malloc Failed somehow
		return -1;
	}
	root_inode->ino = 0;
	root_inode->valid = I_VALID;
	root_inode->type = TFS_DIR;
	root_inode->link = 2;
	root_inode->direct_ptr[0] = sb->d_start_blk;

	//Set remaining pointers to -1 (not yet set)
	int n;
	for(n = 1; n < 16; n++){
		root_inode->direct_ptr[n] = -1;
	}

	//Create dirents for "." and ".." (Both point to root dir)
	struct dirent* d1 = malloc(sizeof(struct dirent));
	struct dirent* d2 = malloc(sizeof(struct dirent));

	//Dirent 1
	d1->ino = 0;
	d1->valid = I_VALID;
	strcpy(d1->name, ".");
	d1->len = 1;

	//Dirent 2
	d2->ino = 0;
	d2->valid = I_VALID;
	strcpy(d2->name, "..");
	d2->len = 2;

	//Add both dirents to dirent array
	dirents[0] = *d1;
	dirents[1] = *d2;
	free(d1);
	free(d2);

	printf("Size of 1 dirent: %ld\n", sizeof(struct dirent));
	printf("Size of dirents array: %ld\n", sizeof(dirents));

	root_inode->size = 2 * sizeof(struct dirent);
	rstat = malloc(sizeof(struct stat));
	memset (rstat, 0, sizeof(struct stat));
	time(&seconds);
	rstat->st_atime = seconds;
	rstat->st_mtime = seconds;
	root_inode->vstat = *rstat;
	free(rstat);

	printf("tfs_mkfs(): Check 6 . . . \n");
	//Write to disk (Super Block, Bitmaps, Root inode)
	//Copy data to buffer
	memcpy (buf, sb, sizeof(struct superblock));
	//Write buffer to disk
	pthread_mutex_lock(&lock);
	wretstat = bio_write(0, buf);
	pthread_mutex_unlock(&lock);
	if(wretstat < 0){
		return -1;
	}

	//Buffer Operations
	memset(buf, 0, BLOCK_SIZE);
	printf("Size of bitmap: %d\n", imap_size);
	memcpy (buf, inode_map, imap_size);
	pthread_mutex_lock(&lock);
	wretstat = bio_write((int)sb->i_bitmap_blk, buf);
	pthread_mutex_unlock(&lock);
	if(wretstat < 0){
		return -1;
	}

	//Buffer Operations
	memset(buf, 0, BLOCK_SIZE);
	memcpy (buf, data_map, dmap_size);
	pthread_mutex_lock(&lock);
	wretstat = bio_write((int)sb->d_bitmap_blk, data_map);
	pthread_mutex_unlock(&lock);
	if(wretstat < 0){
		return -1;
	}

	//Buffer Operations
	memset(buf, 0, BLOCK_SIZE);
	memcpy (buf, root_inode, sizeof(struct inode));
	pthread_mutex_lock(&lock);
	wretstat = bio_write((int)sb->i_start_blk, buf);
	pthread_mutex_unlock(&lock);
	if(wretstat < 0){
		return -1;
	}

	//Write dirent array to first data block
	//Buffer Operations
	memset(buf, 0, BLOCK_SIZE);
	memcpy (buf, dirents, sizeof(dirents));
	pthread_mutex_lock(&lock);
	wretstat = bio_write((int)sb->d_start_blk, buf);
	pthread_mutex_unlock(&lock);
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
	char buf[BLOCK_SIZE];

	//Clear the buffer
	memset(buf, 0, BLOCK_SIZE);

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
			printf("Size of superblock: %ld\n",sizeof(struct superblock));
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

		//Allocate space for root inode
		printf("tfs_init(): Check 4.5 . . . \n");
		if(root_inode == NULL){
			root_inode = malloc(sizeof(struct inode));
			if(root_inode == NULL){
				// Malloc Failed somehow
				perror("tfs_init failed:");
				exit(EXIT_FAILURE);
			}
		}

		//Load superblock from disk
		printf("tfs_init(): Check 5 . . . \n");
		//Read into buffer
		pthread_mutex_lock(&lock);
		rretstat = bio_read(0, buf);
		pthread_mutex_unlock(&lock);
		//Copy the desired memory
		memcpy(sb, buf, sizeof(struct superblock));
		if(rretstat < 0){
			perror("tfs_init disk read failure:");
			exit(EXIT_FAILURE);
		}
		printf("Loaded Super Block. . .\n|-- Start of data block region: %d\n", sb->d_start_blk);

		//Load inode bitmap from disk
		printf("tfs_init(): Check 6 . . . \n");
		//clear buffer
		memset(buf, 0, BLOCK_SIZE);
		//read into buffer
		pthread_mutex_lock(&lock);
		rretstat = bio_read((int)sb->i_bitmap_blk, buf);
		pthread_mutex_unlock(&lock);
		//Copy the desired memory
		memcpy(inode_map, buf, imap_size);
		if(rretstat < 0){
			perror("tfs_init disk read failure:");
			exit(EXIT_FAILURE);
		}
		printf("Loaded inode bitmap. . .\n|-- Index at 0: %d\n", get_bitmap(inode_map,0));

		//Load data bitmap from disk
		printf("tfs_init(): Check 7 . . . \n");
		//clear buffer
		memset(buf, 0, BLOCK_SIZE);
		//read into buffer
		pthread_mutex_lock(&lock);
		rretstat = bio_read((int)sb->d_bitmap_blk, buf);
		pthread_mutex_unlock(&lock);
		//Copy the desired memory
		memcpy(data_map, buf, dmap_size);
		if(rretstat < 0){
			perror("tfs_init disk read failure:");
			exit(EXIT_FAILURE);
		}
		printf("Loaded data bitmap. . .\n|-- Index at 0: %d\n", get_bitmap(data_map,0));

		printf("tfs_init(): Check 8 . . . \n");
		//clear buffer
		memset(buf, 0, BLOCK_SIZE);
		pthread_mutex_lock(&lock);
		rretstat = bio_read((int)sb->i_start_blk, buf);
		pthread_mutex_unlock(&lock);
		//Copy the desired memory
		memcpy(root_inode, buf, sizeof(struct inode));
		if(rretstat < 0){
			perror("tfs_init disk read failure:");
			exit(EXIT_FAILURE);
		}
		printf("Loaded Root Inode. . .\n|-- Number of links in root: %d\n", root_inode->link);
		
	}

	printf("tfs_init(): * DONE *\n");

	//Optional return value
	return NULL;
}

static void tfs_destroy(void *userdata) {

	// Print use of data and inode blocks
	printf("inode blocks ");
	print_bitmap_sum(inode_map);
	printf("data blocks ");
	print_bitmap_sum(data_map);

	// Step 1: De-allocate in-memory data structures
	printf("tfs_destroy(): Check 1 . . . \n");
	free(sb);
	printf("tfs_destroy(): Check 2 . . . \n");
	free(inode_map);
	printf("tfs_destroy(): Check 3 . . . \n");
	free(data_map);
	printf("tfs_destroy(): Check 4 . . . \n");
	//free(&root_inode->vstat);
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

	struct inode* mynode = malloc(sizeof(struct inode));
	int ret = get_node_by_path(path, 0, mynode);
	if(ret == ENOENT){
		return -ENOENT;
	}
	if(ret < 0){
		return -ENOENT;
	}


	// Step 2: fill attribute of file into stbuf from inode
	stbuf->st_uid = getuid();
	stbuf->st_gid = getgid();
	stbuf->st_size = mynode->size;

	//Check if the inode is for the root dir
	if ( mynode->type == TFS_DIR ){
		stbuf->st_mode   = S_IFDIR | 0755;
	}else{
		stbuf->st_mode = S_IFREG | 0644;
	}
	stbuf->st_nlink  = mynode->link;
	//time(&stbuf->st_mtime);
	stbuf->st_mtime = mynode->vstat.st_mtime;
	stbuf->st_atime =  mynode->vstat.st_atime;

	printf("tfs_getattr(): COMPLETED . . .\n");
	return 0;
}

static int tfs_opendir(const char *path, struct fuse_file_info *fi) {

	// Step 1: Call get_node_by_path() to get inode from path

	struct inode* mynode = malloc(sizeof(struct inode));
	int ret = get_node_by_path(path, 0, mynode);

	// Step 2: If not find, return -1
	if (ret < 0 || ret == ENOENT){
		free(mynode);
		return -1;
	} 

	free(mynode);
    return 0;
}

static int tfs_readdir(const char *path, void *buffer, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi) {
	
	int i, j;
	struct dirent* my_dirent;
	char mybuffer[BLOCK_SIZE];
	int block_no;
	int link_count = 0;
	//int dblock_start = sb->d_start_blk;

	// Step 1: Call get_node_by_path() to get inode from path
	struct inode* mynode = malloc(sizeof(struct inode));
	int ret = get_node_by_path(path, 0, mynode);

	if (ret < 0 || ret == ENOENT){
		free(mynode);
		return -1;
	} 
	printf("***********************************************************************************\n");
	// Step 2: Read directory entries from its data blocks, and use filler to copy to buffer  //copy them to filler
	
	for (i=0; i<16; i++) {

		block_no = mynode->direct_ptr[i];

		//NOTE: Data blocks start at block number ~67
		//TODO: Change to check if the pointer number is < sb->d_start_blk (67)
		if (block_no == -1) continue;
		printf("tfs_readdir(): Inode #%d Direct Pointer = %d\n", mynode->ino, block_no);

		// Step 3: Read directory's data block and check each directory entry.
		pthread_mutex_lock(&lock);
		int read_ret = bio_read(block_no, mybuffer);
		pthread_mutex_unlock(&lock);
		if (read_ret < 0) {
			printf("Error in dir_find(): Unable to read block of current directory\n");
			//TODO: Handle this error
			continue;
		}

		// Locate a dirent within this block
		printf("tfs_readdir(): Size of Dir: %d -- Link Count: %d\n", mynode->size, mynode->link);
		for (j=0; j<BLOCK_SIZE-sizeof(struct dirent); j+=sizeof(struct dirent)) {

			

			// Break loop if j exceeds the size of this directory
			//TODO: Change this so it compares the number of valid names to the link count
			if(j > mynode->size && link_count >= mynode->link){
				break;
			}
			
			
			my_dirent = malloc(sizeof(struct dirent));

			//Copy buffer mem into my_dirent
			memcpy(my_dirent, mybuffer+j, sizeof(struct dirent));

			//Check if the name is valid. Increment link_count if the name isn't empty
			// if(strcmp(my_dirent->name, "") != 0){
			// 	link_count++;
			// }
			printf("\ttfs_readdir(): Size of Dir: %d -- Bytes read: %d -- Link# %d\n", mynode->size, j, link_count);
			if(my_dirent->valid){
				printf("tfs_readdir(): Dirent Name: %s -- Ino: %d\n", my_dirent->name, my_dirent->ino);
				link_count++;
				filler(buffer, my_dirent->name, NULL, 0);
			}
			free(my_dirent);
			
		}
	}

	printf("tfs_readdir(): COMPLETE. . . \n");
	free(mynode);
	printf("***********************************************************************************\n");
	return 0;
}


static int tfs_mkdir(const char *path, mode_t mode) {
	printf("***********************************************************************************\n");
	char* pc, *tc, *parent, *target;
	struct inode* mynode, *new_node;
	int new_ino;
	size_t tar_len;
	struct stat* rstat;
	int ret;

	
	// Step 1: Use dirname() and basename() to separate parent directory path and target directory name
	printf("tfs_mkdir(): CHECK 1. . . \n");
	pc = strdup(path);
	tc = strdup(path);
	parent = dirname(pc);
	target = basename(tc);
	printf("tfs_mkdir(): Parent path: %s -- Target (base) name: %s\n", parent, target);

	// Step 2: Call get_node_by_path() to get inode of parent directory
	printf("tfs_mkdir(): CHECK 2. . . \n");
	mynode = malloc(sizeof(struct inode));
	ret = get_node_by_path(parent, 0, mynode);

	if (ret < 0 || ret == ENOENT){
		perror("tfs_mkdir() failed");
		free(mynode);
		printf("***********************************************************************************\n");
		return -1;
	} 

	// Step 3: Call get_avail_ino() to get an available inode number
	printf("tfs_mkdir(): CHECK 3. . . \n");
	new_ino = get_avail_ino();
	printf("tfs_mkdir(): New inode Number = %d\n", new_ino);
	if(new_ino < 0){
		perror("tfs_mkdir() failed");
		free(mynode);
		printf("***********************************************************************************\n");
		return -1;
	}

	//TODO: Increase the size of the parent directory (I think this should be done in dir_add)

	// Step 4: Call dir_add() to add directory entry of target directory to parent directory
	printf("tfs_mkdir(): CHECK 4. . . \n");
	tar_len = strlen(target);
	printf("tfs_mkdir(): Passing to dir_add(): #%d - Name: %s - Len: %ld\n", new_ino, target, tar_len);
	ret = dir_add(*mynode, new_ino, target, tar_len);
	if(ret < 0 || ret > 0){
		printf("tfs_mkdir(): failed. . . \n");
		free(mynode);
		printf("***********************************************************************************\n");
		return -1;
	}

	//TODO: Increment the link count of the parent dir (Maybe in dir_add?)

	// Step 5: Update inode for target directory
	printf("tfs_mkdir(): CHECK 5. . . \n");
	new_node = malloc(sizeof(struct inode));
	new_node->ino = new_ino;
	new_node->valid = I_VALID;
	new_node->size = sizeof(struct dirent) * 2; //TODO: Size of a new inode? (0 because the new dir doesn't contain any dirents?)
	new_node->type = TFS_DIR;
	new_node->link = 2;

	// Create dirents for . and ..
	struct dirent* d1 = malloc(sizeof(struct dirent));
	d1->ino = new_ino;
	d1->valid = I_VALID;
	strcpy(d1->name, ".");
	d1->len = 1;
	struct dirent* d2 = malloc(sizeof(struct dirent));
	d2->ino = mynode->ino;
	d2->valid = I_VALID;
	strcpy(d2->name, "..");
	d2->len = 2;
	// Find an available block for these dirents
	int next_blockno = sb->d_start_blk + get_avail_blkno();
	// Put the dirents onto disk
	char dir_buffer[BLOCK_SIZE];
	memcpy(dir_buffer, d1, sizeof(struct dirent));
	memcpy(dir_buffer + sizeof(struct dirent), d2, sizeof(struct dirent));
	pthread_mutex_lock(&lock);
	bio_write(next_blockno, dir_buffer);
	pthread_mutex_unlock(&lock);
	printf("tfs_mkdir(): Just made dirents '%s' and '%s' with inos %d and %d in block %d\n", d1->name, d2->name, new_ino, mynode->ino, next_blockno);

	new_node->direct_ptr[0] = next_blockno; // Corresponds to . and ..
	for (int i = 1; i < 16; i++) {
		new_node->direct_ptr[i] = -1;
	}


	//Create stat struct for new node's vstat
	rstat = malloc(sizeof(struct stat));
	memset (rstat, 0, sizeof(struct stat));
	//Set time (access & modification) to now
	rstat->st_atime = time(NULL); 
	rstat->st_mtime = time(NULL);
	new_node->vstat = *rstat;
	free(rstat);

	// Step 6: Call writei() to write inode to disk
	printf("tfs_mkdir(): CHECK 6. . . \n");
	ret = writei(new_ino, new_node);
	if (ret < 0 || ret == ENOENT){
		perror("tfs_mkdir() failed");
		free(mynode);
		free(new_node);
		return -1;
		printf("***********************************************************************************\n");
	} 

	free(mynode);
	free(new_node);

	printf("tfs_mkdir(): COMPLETE. . .\n");
	return 0;
	printf("***********************************************************************************\n");
}

static int tfs_rmdir(const char *path) {

	printf("***********************************************************************************\n");
	char* pc, *tc, *parent, *target;
	struct inode* mynode;
	int ret;
	int i;
	int dsb = sb->d_start_blk;
	

	// Step 1: Use dirname() and basename() to separate parent directory path and target directory name
	printf("tfs_rmdir(): CHECK 1. . . \n");
	pc = strdup(path);
	tc = strdup(path);
	parent = dirname(pc);
	target = basename(tc);
	printf("tfs_rmdir(): Parent path: %s -- Target (base) name: %s\n", parent, target);

	// Step 2: Call get_node_by_path() to get inode of target directory
	printf("tfs_rmdir(): CHECK 2. . . \n");
	mynode = malloc(sizeof(struct inode));
	ret = get_node_by_path(path, 0, mynode);

	if (ret < 0 || ret == ENOENT){
		perror("tfs_rmdir() failed");
		free(mynode);
		printf("***********************************************************************************\n");
		return -1;
	} 

	//Check the directory's link count. If it's greater than the minimum (2), failure.
	if(mynode->link > 2){
		errno = ENOTEMPTY;
		perror("tfs_rmdir() failed");
		printf("***********************************************************************************\n");
		return -ENOTEMPTY;
	}

	// Step 3: Clear data block bitmap of target directory
	printf("tfs_rmdir(): CHECK 3. . . \n");
	//Search through the inode's direct ptrs and free the corresponding bits
	printf("tfs_rmdir(): Unsetting data bits -------\n");
	int bit;
	int abs_block;
	for(i = 0; i < 16; i++){
		//Get the absolute block position
		abs_block = mynode->direct_ptr[i];
		// Get the bit position by subtracting the data block start number
		bit = abs_block - dsb;
		if(bit > 0) {
			unset_bitmap(data_map, bit);
			printf("%d\t", bit);
		}
	}
	printf("\ntfs_rmdir(): DONE UNSETTING -----------: \n");

	// Step 4: Clear inode bitmap and its data block
	printf("tfs_rmdir(): CHECK 4. . . \n");

	// Use node's ino to clear bit
	printf("\tUNSETTING inode bits (%d):\n\tOriginal Bitmap: ", mynode->ino);
	print_bitmap(inode_map);
	unset_bitmap(inode_map, mynode->ino);
	printf("\tNew bitmap: ");
	print_bitmap(inode_map);

	//Write bitmaps to disk
	pthread_mutex_lock(&lock);
	ret = bio_write(sb->i_bitmap_blk, inode_map);
	if(ret < 0){
		free(mynode);
		return -1;
	}
	ret = bio_write(sb->d_bitmap_blk, data_map);
	pthread_mutex_unlock(&lock);
	if(ret < 0){
		free(mynode);
		return -1;
	}

	// Save needed information from the inode being deleted
	int tmp_ino = mynode->ino;
	// Use writei() to clear the inode data from the block
	memset(mynode, 0, sizeof(struct inode));
	writei(tmp_ino, mynode);
	free(mynode);

	// Step 5: Call get_node_by_path() to get inode of parent directory
	printf("tfs_rmdir(): CHECK 5. . . \n");

	// Reuse the mynode variable for the parent inode
	mynode = malloc(sizeof(struct inode));
	ret = get_node_by_path(parent, 0, mynode);
	if (ret < 0 || ret == ENOENT){
		perror("tfs_rmdir() failed");
		free(mynode);
		printf("***********************************************************************************\n");
		return -1;
	} 

	// Step 6: Call dir_remove() to remove directory entry of target directory in its parent directory
	printf("tfs_rmdir(): CHECK 6. . . \n");
	dir_remove(*mynode, target, strlen(target));

	free(mynode);
	printf("tfs_rmdir(): * COMPLETE * \n");
	printf("***********************************************************************************\n");
	return 0;
}

static int tfs_releasedir(const char *path, struct fuse_file_info *fi) {
	// For this project, you don't need to fill this function
	// But DO NOT DELETE IT!
    return 0;
}


static int tfs_create(const char *path, mode_t mode, struct fuse_file_info *fi) {
	printf("***********************************************************************************\n");
	char* pc, *tc, *parent, *target;
	struct inode* mynode, *new_node;;
	int new_ino;
	size_t tar_len;
	struct stat* rstat;
	int ret;

	// Step 1: Use dirname() and basename() to separate parent directory path and target file name
	printf("tfs_create(): CHECK 1. . . \n");
	pc = strdup(path);
	tc = strdup(path);
	parent = dirname(pc);
	target = basename(tc);
	printf("tfs_create(): Parent path: %s -- Target (base) name: %s\n", parent, target);

	// Step 2: Call get_node_by_path() to get inode of parent directory
	printf("tfs_create(): CHECK 2. . . \n");
	mynode = malloc(sizeof(struct inode));
	ret = get_node_by_path(parent, 0, mynode);

	if (ret < 0 || ret == ENOENT){
		perror("tfs_create() failed");
		free(mynode);
		printf("***********************************************************************************\n");
		return -1;
	} 

	// Step 3: Call get_avail_ino() to get an available inode number
	printf("tfs_create(): CHECK 3. . . \n");
	new_ino = get_avail_ino();
	if(new_ino < 0){
		perror("tfs_create() failed");
		free(mynode);
		printf("***********************************************************************************\n");
		return -1;
	}
	// Step 4: Call dir_add() to add directory entry of target file to parent directory
	printf("tfs_create(): CHECK 4. . . \n");
	tar_len = strlen(target);
	ret = dir_add(*mynode, new_ino, target, tar_len);
	//If the function returns anything but 0, then it's unable to create file
	if(ret < 0 || ret > 0){
		printf("tfs_create(): failed. . . \n");
		free(mynode);
		printf("***********************************************************************************\n");
		return -1;
	}

	//TODO: Increment the link count of the parent dir (Maybe in dir_add?)

	// Step 5: Update inode for target file
	printf("tfs_create(): CHECK 5. . . \n");
	new_node = malloc(sizeof(struct inode));
	new_node->ino = new_ino;
	new_node->valid = I_VALID;
	new_node->size = 0; //TODO: Size of a new inode? (0 because the new file doesn't contain any data?)
	new_node->type = TFS_REG;
	new_node->link = 1;
	//Create stat struct for new node's vstat
	rstat = malloc(sizeof(struct stat));
	memset (rstat, 0, sizeof(struct stat));
	//Set time (access & modification) to now
	rstat->st_atime = time(NULL); 
	rstat->st_mtime = time(NULL);
	new_node->vstat = *rstat;
	free(rstat);

	//Create direct pointer for this file
	new_node->direct_ptr[0] = sb->d_start_blk + get_avail_blkno();
	//Set the rest of the pointers to default
	int n;
	for(n = 1; n < 16; n++){
		new_node->direct_ptr[n] = -1;
	}

	// Step 6: Call writei() to write inode to disk
	printf("tfs_create(): CHECK 6. . . \n");
	ret = writei(new_ino, new_node);
	if (ret < 0 || ret == ENOENT){
		perror("tfs_create() failed");
		free(mynode);
		free(new_node);
		printf("***********************************************************************************\n");
		return -1;
	} 

	free(mynode);
	free(new_node);

	printf("tfs_create(): * COMPLETE * \n");
	printf("***********************************************************************************\n");
	return 0;
}

static int tfs_open(const char *path, struct fuse_file_info *fi) {
	
	struct inode* mynode;
	int ret;

	// Step 1: Call get_node_by_path() to get inode from path
	printf("tfs_open(): CHECK 1. . . \n");
	mynode = malloc(sizeof(struct inode));
	ret = get_node_by_path(path, 0, mynode);

	if (ret < 0 || ret == ENOENT){
		perror("tfs_open() failed");
		free(mynode);
		return -1;
	} 
	// Step 2: If not find, return -1

	//TODO: Is this all we have to do?

	free(mynode);
	return 0;
}

static int tfs_read(const char *path, char *buffer, size_t size, off_t offset, struct fuse_file_info *fi) {

	char mybuffer[BLOCK_SIZE * 16];
	int block_ptr;
	int read_ret;
	int i;
	int blocks_read = 0;
	int dsb = sb->d_start_blk;
	int mybuff_offset;


	printf("tfs_read(): Starting. . .\n\tRead Size: %ld\n\tOffset amount: %ld\n", size, offset);
	

	// Read all valid data blocks into mybuffer
	// Then copy the data from mybuffer starting at 'offset' for 'size' bytes
	// Doing this should handle unaligned reads, as the entirety of blocks are being read and should appear whole after writing to "buffer"

	// Step 1: You could call get_node_by_path() to get inode from path
	struct inode* mynode = malloc(sizeof(struct inode));
	int ret = get_node_by_path(path, 0, mynode);

	if (ret < 0 || ret == ENOENT){
		free(mynode);
		return -1;
	} 

	printf("tfs_read(): CHECKING INODE %d\n", mynode->ino);

	if(offset > mynode->size){
		printf("tfs_read() Error: Offset is larger than file size\n");
		return -1;
	}

	// Step 2: Based on size and offset, read its data blocks from disk
	//Loop through data block pointers
	for(i = 0; i < 16; i++){
		block_ptr = mynode->direct_ptr[i];

		//ignore invalid pointers (less than the datablock region) //TODO: Should we make -1 count as a stopping point? The direct pointers should be contiguous I think.
		if (block_ptr < dsb) continue;

		// Indicates at which point in mybuffer you would like to load block data into
		mybuff_offset = BLOCK_SIZE * blocks_read;
		blocks_read++;

		//Read whole block into mybuff starting at mybuff_offset
		pthread_mutex_lock(&lock);
		read_ret = bio_read(block_ptr, mybuffer + mybuff_offset);
		pthread_mutex_unlock(&lock);

		if (read_ret < 0) {
			printf("Error in tfs_read(): Unable to read data block\n");
			//No bytes read
			return 0;
		}

	}
	// Step 3: copy the correct amount of data from offset to buffer
	memcpy(buffer, mybuffer+offset, size);
	printf("tfs_read(): New Output buffer contents: \n%s", buffer);
	// Note: this function should return the amount of bytes you copied to buffer
	return size;
}

static int tfs_write(const char *path, const char *buffer, size_t size, off_t offset, struct fuse_file_info *fi) {
	printf("***********************************************************************************\n");
	int num_needed;
	int starting_block;
	int rel_offset;
	int i;
	//int dsb = sb->d_start_blk; //TODO: REMOVE?
	int block_ptr;
	int blocks_read = 0;


	printf("tfs_write(): Starting. . . \n\tSize: %ld\n\tOffset:%ld\n", size, offset);

	// Handle unaligned write:
	


	//Calculate which block to start in based on offset
	//Check if the offset is larger than BLOCK_SIZE. If offset is greater than BLOCK_SIZE then the value will be >= 1 (0 indexed)
	starting_block = offset / BLOCK_SIZE;
	printf("tfs_write(): direct_ptr start index: %d\n", starting_block);

	if(starting_block > 16){
		printf("\ntfs_write() Error: Offset exceeds the max file size\n");
		printf("***********************************************************************************\n");
		return 0;
	}

	//Calc the offset within the specific block
	rel_offset = offset - (BLOCK_SIZE * starting_block);
	printf("tfs_write(): Relative block offset: %d\n", rel_offset);

	// calc number of blocks needed to read in order to transfer data
	// May need an extra block because offset causes overflow into another block
	num_needed = num_blocks_needed(BLOCK_SIZE, size+rel_offset);
	printf("tfs_write(): Number of blocks needed to change: %d\n", num_needed);

	// Create local buffer (new_buffer) that will store the data in a block-aligned manner 
	// Now we can read/write from this block-by-block
	char new_buffer[BLOCK_SIZE * num_needed]; //TODO: This may be redundant
	memcpy(new_buffer, buffer, size);


	// Step 1: You could call get_node_by_path() to get inode from path
	struct inode* mynode = malloc(sizeof(struct inode));
	int ret = get_node_by_path(path, 0, mynode);

	if (ret < 0 || ret == ENOENT){
		free(mynode);
		printf("***********************************************************************************\n");
		return -1;
	} 

	// Step 2: Based on size and offset, read its data blocks from disk
	// Read blocks starting from 'starting_block' to num_needed into a super-buffer (db_buff) where the data is contiguous
	char db_buff[BLOCK_SIZE * num_needed];
	int tblock;
	int db_buff_offset;
	printf("tfs_write(): READING DATA BLOCKS. . . \n");
	for(i = 0; i < num_needed; i++){
		tblock = starting_block + i;
		if(tblock > 16){
			printf("tfs_write(): EXCEEDED AVAILABLE BLOCK SPACE (tblock: %d > 16) *** \n", tblock);
			return -1;
			//break; //TODO: HANDLE THIS CASE
		}
		block_ptr = mynode->direct_ptr[tblock];
		printf("tfs_write(): Direct Ptr#: %d -- Block#: %d\n", tblock, block_ptr);

		//if(block_ptr == -1) continue; //TODO: Change to a stopping point?
		if(block_ptr == -1){
			printf("\t--> Block not yet allocated! Allocating now. . .\n");
			mynode->direct_ptr[tblock] = sb->d_start_blk + get_avail_blkno();
			block_ptr = mynode->direct_ptr[tblock];
			printf("\t ALLOCATED NEW BLOCK#: %d\n", block_ptr);
			
		}

		// Indicates at which point in db_buff you would like to load block data into
		db_buff_offset = BLOCK_SIZE * blocks_read;
		blocks_read++;

		//Read whole block into db_buff starting at db_buff_offset
		pthread_mutex_lock(&lock);
		printf("tfs_write(): About to read data block %d into db_buff offset %d\n", block_ptr, db_buff_offset);
		ret = bio_read(block_ptr, db_buff + db_buff_offset);
		pthread_mutex_unlock(&lock);

		if (ret < 0) {
			printf("Error in tfs_write(): Unable to read data block\n");
			//No bytes read
			free(mynode);
			printf("***********************************************************************************\n");
			return 0;
		}
		
	}
	printf("tfs_write(): * DONE * READING DATA BLOCKS. . . \n");
	
	/* Now the disk blocks are loaded into db_buff, we transfer the data from our write buffer into 
	db_buff starting at rel_offset
	
	The db_buff should have enough room to directly write the write-buffer data into it
	*/
	printf("tfs_write(): OLD BUFFER CONTENTS:\n%s\n", db_buff);
	memcpy(db_buff+rel_offset, buffer, size);

	printf("tfs_write(): NEW BUFFER CONTENTS:\n%s\n", db_buff);

	// Step 3: Write the correct amount of data from offset to disk
	printf("tfs_write(): WRITING MODIFIED DATA BLOCKS. . . \n");
	for(i = 0; i < num_needed; i++){
		tblock = starting_block + i;
		block_ptr = mynode->direct_ptr[tblock];
		printf("tfs_write(): Ptr#: %d -- Block#: %d\n", tblock, block_ptr);
		// Write to file blocks using the respective positions in db_buff
		pthread_mutex_lock(&lock);
		ret = bio_write(block_ptr, db_buff+(BLOCK_SIZE * i)); //TODO: Double check this
		pthread_mutex_unlock(&lock);
	}
	printf("tfs_write(): * DONE * WRITING MODIFIED DATA BLOCKS. . . \n");

	// Step 4: Update the inode info and write it to disk
	mynode->vstat.st_atime = time(NULL);
	mynode->vstat.st_mtime = time(NULL);

	//TODO: Update size in inode 
	mynode->size += size;

	ret = writei(mynode->ino, mynode);
	if (ret < 0 || ret == ENOENT){
		free(mynode);
		printf("***********************************************************************************\n");
		return -1;
	} 
	// Note: this function should return the amount of bytes you write to disk
	free(mynode);
	printf("***********************************************************************************\n");
	return size;
}

static int tfs_unlink(const char *path) {
	printf("***********************************************************************************\n");
	char* pc, *tc, *parent, *target;
	struct inode* mynode;
	int ret;
	int i;
	int dsb = sb->d_start_blk;
	int dm_bit;
	int block_ptr;

	// Step 1: Use dirname() and basename() to separate parent directory path and target file name
	printf("tfs_unlink(): CHECK 1. . . \n");
	pc = strdup(path);
	tc = strdup(path);
	parent = dirname(pc);
	target = basename(tc);
	printf("tfs_unlink(): Parent path: %s -- Target (base) name: %s\n", parent, target);

	// Step 2: Call get_node_by_path() to get inode of target file
	printf("tfs_unlink(): CHECK 2. . . \n");
	mynode = malloc(sizeof(struct inode));
	ret = get_node_by_path(path, 0, mynode);

	if (ret < 0 || ret == ENOENT){
		perror("tfs_unlink() failed");
		free(mynode);
		printf("***********************************************************************************\n");
		return -1;
	} 

	printf("tfs_unlink(): BITMAPS BEFORE:\n");
	printf("\tData Map: ");
	print_bitmap(data_map);
	printf("\tInode Map: ");
	print_bitmap(inode_map);

	// Step 3: Clear data block bitmap of target file
	printf("tfs_unlink(): CHECK 3. . . \n");
	// Loop through 'target' inode block ptrs, find what data blocks it's using and free the bits in data_map
	for(i = 0; i < 16; i++){
		// Absolute data block number
		block_ptr = mynode->direct_ptr[i];
		if(block_ptr < dsb) continue; //TODO: Double check this

		// Index of bit in the data_map
		dm_bit = block_ptr - dsb;

		unset_bitmap(data_map, dm_bit);
	}

	// Step 4: Clear inode bitmap and its data block
	printf("tfs_unlink(): CHECK 4. . . \n");
	unset_bitmap(inode_map, mynode->ino);

	printf("tfs_unlink(): BITMAPS AFTER:\n");
	printf("\tData Map: ");
	print_bitmap(data_map);
	printf("\tInode Map: ");
	print_bitmap(inode_map);

	//Write bitmaps to disk
	pthread_mutex_lock(&lock);
	ret = bio_write(sb->i_bitmap_blk, inode_map);
	if(ret < 0){
		free(mynode);
		return -1;
	}
	ret = bio_write(sb->d_bitmap_blk, data_map);
	pthread_mutex_unlock(&lock);
	if(ret < 0){
		free(mynode);
		return -1;
	}

	// Save needed information from the inode being deleted
	int tmp_ino = mynode->ino;
	// Use writei() to clear the inode data from the block
	memset(mynode, 0, sizeof(struct inode));
	writei(tmp_ino, mynode);
	free(mynode);

	// Step 5: Call get_node_by_path() to get inode of parent directory
	printf("tfs_unlink(): CHECK 5. . . \n");

	// Reuse the mynode variable for the parent inode
	mynode = malloc(sizeof(struct inode));
	ret = get_node_by_path(parent, 0, mynode);
	if (ret < 0 || ret == ENOENT){
		perror("tfs_unlink() failed");
		free(mynode);
		printf("***********************************************************************************\n");
		return -1;
	} 

	// Step 6: Call dir_remove() to remove directory entry of target file in its parent directory
	printf("tfs_unlink(): CHECK 6. . . \n");
	dir_remove(*mynode, target, strlen(target));

	free(mynode);
	printf("tfs_unlink(): * COMPLETE * \n");
	printf("***********************************************************************************\n");
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

