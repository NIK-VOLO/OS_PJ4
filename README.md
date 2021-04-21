# OS_PJ4
File System



## Office Hour Notes:

- Goal:
    - FUSE driver redirects operations that would usually be sent to the OS to the user space instead.
    - If a FUSE operation is registered, it will use the user's version of the function. If not, it will use the default function that is present.

- Initialize using function pointers to the user functions that should replace the defaults
- Block Layer: (block.c)
    - File system is an abstraction
    - Block device is a set of N sized blocks (ex: 4KB)
        - We are using a file that represents the disk (ex: 32 MB)
    - Any time we provide an offset to find a particular block -- read/write to the block
    - Cannot read/write 1 byte from the block --> Must use the entire block
        - To make a change to the blockl: read entire block into a buffer --> Make modifications here --> Then write back to block

    - Need to figure out how to determine how to find the correct block for a particular piece of data

- File System Structure:
    - As long as things are stored sequentially it's easy to find out where some data resides
    - Super Block: 
        - What's the max # of inodes in the system (Every file has it's own inode)
            - Files and Directories have their inodes
        - Max # of data blocks the system has
        - Starting point of the inode bitmap block and of the data bitmap block
    - Bitmaps:
        - Inode Bitmaps:
            - Where to store inodes in the inode blocks
            - Make sure if there is space in an existing block that we put the inode in that block
        - Data bitmaps: 
            - How many blocks are avaiable/not available

    - Inode Structure:
        - node number
        - Valid
        - Size
        - Type
        - Direct/indirect pointers: 
            - Direct: What physical blocks is the file using
                - IDEA: The data type is an int --> Consider the int as an offset from the start of the data block region ***
        - Each block can have block_size/inode_size inodes
        - Once we know the inode number we can fetch inode information by going to the block and calculating the position where it should be
        - stat() will store information about the inode

    - Directory Structure:
        - Like a file, BUT data of directory is a list of all the other files present in the dir
        - Has direct pointers to blocks that contain information about other files in that directory
            - dirent: entry to a directory data block
                - Stores filename and inode# of the file
                - Check for valid dir

    - get_avail_inode():
        - First: Read inode bitmap -> Where is the next open position
            - Shows next inode number that can be allocated (and location)
        - Set the bitmap and save it to the storage

    - get_avail_block()
        - Next inode data block
        - Read data bitmap and find the block number/location
        - Set the bit and write bitmap to the storage

    - readi/writei():
        - Reading/writing inode blocks
        - Each inode block can have more than 1 inode
        - readi --> If inode# known, it knows the size of the inode block and the size of the inode struct, finds the exact block and position the inode resides. 
            - Fetches inode information from disk and assigns it to an inode struct

        
         # Left off here, review video

