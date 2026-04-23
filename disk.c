#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <stdint.h>
#include "disk.h"
#include <pthread.h>

#define FAT_SIZE 4096 //#of data blocks
#define FAT_EOF  -1 //#end of file chain
#define MAX_FILENAME 16 //max length of file name
#define MAX_FILES 64 //max of 64 files
#define MAX_FD 32 //Max of 32 file descriptors at the same time (max files open)

/******************************************************************************/
static int active = 0;  /* is the virtual disk open (active) */
static int handle;      /* file handle to virtual disk       */
/******************************************************************************/
//structs

//metadata about the filesystem --> specifically FAT starts, root dir beigins, and data begins
//important to store locations, otherwise cannot find anything
typedef struct {
  int total_blocks;
  int block_size;
  int fat_start;
  int fat_length;
  int root_start;
  int root_length;
  int data_start;
} Boot;

//which blocks belong where
typedef struct {
    int entries[FAT_SIZE]; //each entry is a block, last block of file is EOF (-1)
} FAT; //4096 elements long

typedef struct {
    char name[MAX_FILENAME];

    uint16_t start_block; //wehre in FAT it starts
    uint32_t file_size; //size

    uint8_t used;//slotused or not
} DirEntry;

typedef struct {
  DirEntry entries[MAX_FILES];
} RootDirectory; //an array of entries that contain data for the file, aka main folder

//not saved to the disk!!! current read/write pos
typedef struct {
    int used; //is slot /file open
    int dir_index; //which file in root
    int offset; //where in file
} FileDescriptor;

FileDescriptor fd_table[MAX_FD];

//global variables so all functions have access
Boot boot;
FAT fat;
RootDirectory root;

//low level I/O to create disk
int make_disk(char *name)
{ 
  int f, cnt;
  char buf[BLOCK_SIZE];

  if (!name) {
    fprintf(stderr, "make_disk: invalid file name\n");
    return -1;
  }

  if ((f = open(name, O_WRONLY | O_CREAT | O_TRUNC, 0644)) < 0) {
    perror("make_disk: cannot open file");
    return -1;
  }

  memset(buf, 0, BLOCK_SIZE);
  for (cnt = 0; cnt < DISK_BLOCKS; ++cnt)
    write(f, buf, BLOCK_SIZE);

  close(f);

  return 0;
}
//make_fs, initialize everything
int make_fs(char *disk_name){
  if (make_disk(disk_name) < 0) return -1;
  if (open_disk(disk_name) < 0) return -1;

  //Initialize necessary meta information for this disk
  boot.total_blocks = DISK_BLOCKS;
  boot.block_size = BLOCK_SIZE;

  boot.fat_start = 1;
  boot.fat_length = (FAT_SIZE * sizeof(int)) / BLOCK_SIZE;

  if ((FAT_SIZE * sizeof(int)) % BLOCK_SIZE != 0){
    boot.fat_length++;
  }

  boot.root_start = boot.fat_start + boot.fat_length;
  boot.root_length = (sizeof(RootDirectory)) / BLOCK_SIZE;
  
  if ((sizeof(RootDirectory)) % BLOCK_SIZE != 0){
    boot.root_length++;
  }
  boot.data_start = boot.root_start + boot.root_length;

  //initialize FAT
  for (int i = 0; i < FAT_SIZE; i++) {
    fat.entries[i] = 0; //0 is the free block
  }

  //ROOT directory
  for (int i = 0; i < MAX_FILES; i++) {
    root.entries[i].used = 0; //no files yet
  }

  //WRITE block
  char buf[BLOCK_SIZE];
  memset(buf, 0, BLOCK_SIZE);
  memcpy(buf, &boot, sizeof(Boot));
  block_write(0, buf);
  //write FAT
  int fat_blocks = boot.fat_length;
  char *fat_ptr = (char *)&fat;

  for (int i = 0; i < fat_blocks; i++) {
    memset(buf, 0, BLOCK_SIZE);
    memcpy(buf, fat_ptr + i * BLOCK_SIZE, BLOCK_SIZE);
    block_write(boot.fat_start + i, buf);    
  }

  //write root dir
  int root_blocks = boot.root_length;
  char *root_ptr = (char *)&root;
  for (int i = 0; i < root_blocks; i++) {
    memset(buf, 0, BLOCK_SIZE);
    memcpy(buf, root_ptr + i * BLOCK_SIZE, BLOCK_SIZE);
    block_write(boot.root_start + i, buf);
  }
  close_disk();
  return 0;

  //for your file system so that it can be later used (mounted).
  return 0;
}

//With the mount operation, a file system becomes "ready for use" by reading boot,fat,root
//Open a disk file and load its filesystem structures to memory
int mount_fs(char *disk_name){
  if (open_disk(disk_name) < 0) return -1;
  char buf[BLOCK_SIZE];

  //load Boot, FAT, Root into memory
  char *fat_ptr = (char *)&fat; //read fat into mem
  if (block_read(0, buf) < 0) return -1;
  memcpy(&boot, buf, sizeof(Boot));
  for (int i = 0; i < boot.fat_length; i++) {
    if (block_read(boot.fat_start + i, buf) < 0) return -1;
    memcpy(fat_ptr + i * BLOCK_SIZE, buf, BLOCK_SIZE);
  }

  char *root_ptr = (char *)&root; //read root into mem
  for (int i = 0; i < boot.root_length; i++) {
    if (block_read(boot.root_start + i, buf) < 0) return -1;
    memcpy(root_ptr + i * BLOCK_SIZE, buf, BLOCK_SIZE);
  }
  for (int i = 0; i < MAX_FD; i++) {
    fd_table[i].used = 0;
  }
  return 0;
}
//this function unmounts your file system from a virtual disk with name disk_name.
//writes memory to disk (saves root and FAT)
int umount_fs(char *disk_name){
  char buf[BLOCK_SIZE];
  char *fat_ptr = (char *)&fat;
  for (int i = 0; i < boot.fat_length; i++) {
    memset(buf, 0, BLOCK_SIZE);
    memcpy(buf, fat_ptr + i * BLOCK_SIZE, BLOCK_SIZE);
    if (block_write(boot.fat_start + i, buf) < 0) return -1;
  }

  char *root_ptr = (char *)&root;

  for (int i = 0; i < boot.root_length; i++) {
    memset(buf, 0, BLOCK_SIZE);
    memcpy(buf, root_ptr + i * BLOCK_SIZE, BLOCK_SIZE);
    if (block_write(boot.root_start + i, buf) < 0) return -1;
  }
  if (close_disk() < 0) return -1;
  return 0;
}


int open_disk(char *name)
{
  int f;

  if (!name) { //if name could be NULL
    fprintf(stderr, "open_disk: invalid file name\n");
    return -1;
  }  
  
  if (active) {
    fprintf(stderr, "open_disk: disk is already open\n");
    return -1;
  }
  
  if ((f = open(name, O_RDWR, 0644)) < 0) { //opens for reading + writing, owner (6 r+w), group and others r only
    perror("open_disk: cannot open file");
    return -1;
  }

  handle = f; //fd
  active = 1; //makes it active 

  return 0;
}

int close_disk()
{
  if (!active) { //if not active, exit
    fprintf(stderr, "close_disk: no open disk\n");
    return -1;
  }
  close(handle); //close the file descriptor
  active = handle = 0; //set both var to 0

  return 0;
}

int block_write(int block, char *buf)
{
  if (!active) {
    fprintf(stderr, "block_write: disk not active\n");
    return -1;
  }

  if ((block < 0) || (block >= DISK_BLOCKS)) {
    fprintf(stderr, "block_write: block index out of bounds\n");
    return -1;
  }

  if (lseek(handle, block * BLOCK_SIZE, SEEK_SET) < 0) {
    perror("block_write: failed to lseek");
    return -1;
  }

  if (write(handle, buf, BLOCK_SIZE) < 0) {
    perror("block_write: failed to write");
    return -1;
  }

  return 0;
}

int block_read(int block, char *buf)
{
  if (!active) {
    fprintf(stderr, "block_read: disk not active\n");
    return -1;
  }

  if ((block < 0) || (block >= DISK_BLOCKS)) {
    fprintf(stderr, "block_read: block index out of bounds\n");
    return -1;
  }

  if (lseek(handle, block * BLOCK_SIZE, SEEK_SET) < 0) { //moves to correct block location
    perror("block_read: failed to lseek");
    return -1;
  }

  if (read(handle, buf, BLOCK_SIZE) < 0) {
    perror("block_read: failed to read");
    return -1;
  }

  return 0;
}

//functions for week 2

//finds file and assigns file descriptor
int fs_open(char *name){
    int dir_idx = find_file(name);
    if (dir_idx == -1) return -1;

    int fd = find_free_fd();
    if (fd == -1) return -1;

    fd_table[fd].used = 1;
    fd_table[fd].dir_index = dir_idx;
    fd_table[fd].offset = 0;

    return fd;
  /*File descriptor is returned
Max of 32 file descriptors at the same time
Need to keep a file descriptor table (relate file descriptor to… ?)
(Can have the same file open multiple times, so name isn’t an option)
File descriptor table is NOT metadata- should be erased every time you
mount/unmount, do NOT write it back to your file system*/
}

//marks descriptors unused (frees them)
int fs_close(int fildes){
  if (fildes < 0 || fildes >= MAX_FD) return -1;
    if (!fd_table[fildes].used) return -1;
    fd_table[fildes].used = 0;
    return 0;
  /*
  Close the file
Make sure file descriptor can be reused for a different file after this
Again, DO NOT touch the metadata
*/
  return 0;
}

int find_file(char *name) {
    for (int i = 0; i < MAX_FILES; i++) {
        if (root.entries[i].used &&
            strcmp(root.entries[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

int find_free_dir() {
    for (int i = 0; i < MAX_FILES; i++) {
        if (!root.entries[i].used) return i;
    }
    return -1;
}

int find_free_fd() {
    for (int i = 0; i < MAX_FD; i++) {
        if (!fd_table[i].used) return i;
    }
    return -1;
}

int find_free_block() {
  for (int i = 0; i < FAT_SIZE; i++) {
    if (fat.entries[i] == 0) {
      return i;
    }
  }
    return -1;  
}
//adds file meta data
int fs_create(char *name){
  if (strlen(name) >= MAX_FILENAME) return -1;
    if (find_file(name) != -1) return -1; // already exists
    int idx = find_free_dir();
    if (idx == -1) return -1;

    strcpy(root.entries[idx].name, name);
    root.entries[idx].start_block = FAT_EOF;
    root.entries[idx].file_size = 0;
    root.entries[idx].used = 1;
/*Does not open the file, just creates it
Need to edit the metadata of your file system to indicate that the file has been
created*/
  return 0;

}
//deletes file
int fs_delete(char *name){
  int idx = find_file(name);
  if (idx == -1) return -1;

  int block = root.entries[idx].start_block;
    //free FAT chain
  while (block != FAT_EOF && block != 0) {
      int next = fat.entries[block];
      fat.entries[block] = 0;
      block = next;
  }
  root.entries[idx].used = 0;
  root.entries[idx].start_block = FAT_EOF;
  root.entries[idx].file_size = 0;
  /*must free all data blocks and metadata
Also need to indicate that those data blocks have been freed
*/
  return 0;
}

//finds file via file descriptor, locates the starting block, 
//traverses FAT, then read block by block
int fs_read(int fildes, void *buf, size_t nbyte){
  if (fildes < 0 || fildes >= MAX_FD) return -1;
  if (!fd_table[fildes].used) return -1;

  FileDescriptor *fd = &fd_table[fildes];
  DirEntry *file = &root.entries[fd->dir_index];

  if (fd->offset >= file->file_size) return 0;

  int bytes_read = 0;
  int block = file->start_block;
  int offset = fd->offset;

  char block_buf[BLOCK_SIZE];

    //adjust accordingly!
  while (offset >= BLOCK_SIZE && block != FAT_EOF) {
      offset -= BLOCK_SIZE;
      block = fat.entries[block];
  }

  while (block != FAT_EOF && bytes_read < nbyte) {
    block_read(boot.data_start + block, block_buf);
    int to_copy = BLOCK_SIZE - offset;
      if (to_copy > (nbyte - bytes_read)){
        to_copy = nbyte - bytes_read;
      }
        memcpy((char *)buf + bytes_read, block_buf + offset, to_copy);

        bytes_read += to_copy;
        fd->offset += to_copy;

        offset = 0;
        block = fat.entries[block];
    }
    return bytes_read;
  /*Need to find the file descriptor in the file descriptor table, 
  find which file it points to, find that file in the metadata, 
  then follow the file through the data blocks until nbytes are read*/
}

//allocate first block that is empty, write into block, if full finds new block, and linked via FAT for dynamic growth
int fs_write(int fildes, void *buf, size_t nbyte){
    if (fildes < 0 || fildes >= MAX_FD) return -1;
    if (!fd_table[fildes].used) return -1;

    FileDescriptor *fd = &fd_table[fildes];
    DirEntry *file = &root.entries[fd->dir_index];

    int bytes_written = 0;
    int block = file->start_block;
    int offset = fd->offset;

    while (offset >= BLOCK_SIZE && block != FAT_EOF) { //moves to correct block
      offset -= BLOCK_SIZE;
      block = fat.entries[block];
    }   

    // if file is empty, allocate first block
    if (block == FAT_EOF) {
        block = find_free_block();
        if (block == -1) return 0;

        file->start_block = block;
        fat.entries[block] = FAT_EOF;
    }

    char block_buf[BLOCK_SIZE];

    while (bytes_written < nbyte) {
        block_read(boot.data_start + block, block_buf);

        int space = BLOCK_SIZE - offset;
        int to_copy;
        if ((nbyte - bytes_written) < space) {
          to_copy = nbyte - bytes_written;
        } else {
          to_copy = space;
        }

        memcpy(block_buf + offset, (char *)buf + bytes_written, to_copy);

        block_write(boot.data_start + block, block_buf);

        bytes_written += to_copy;
        fd->offset += to_copy;

        offset = 0;
        if (bytes_written<nbyte) {
          if (fat.entries[block] == FAT_EOF) {
            int new_block = find_free_block();
            if (new_block == -1) break;

            fat.entries[block] = new_block;
            fat.entries[new_block] = FAT_EOF;
          }
          block = fat.entries[block];
        }
    }
    if (fd->offset > file->file_size) {
      file->file_size = fd->offset;
    }

    return bytes_written;

    /*Make sure to return the number of bytes actually written*/
}
//moves reader and write pointer
int fs_lseek(int fildes, off_t offset){
  if (fildes < 0 || fildes >= MAX_FD) return -1;
    if (!fd_table[fildes].used) return -1;

    if (offset < 0) return -1;

    fd_table[fildes].offset = offset;
  /*
  Just move the pointer in the file description table
Can set to end of file to append when writing
*/
  return 0;
}
//freeing unused FAT blocks
int fs_truncate(int fildes, off_t length){
  if (fildes < 0 || fildes >= MAX_FD) return -1;
  if (!fd_table[fildes].used) return -1;

  DirEntry *file = &root.entries[fd_table[fildes].dir_index];

  if (length > file->file_size) return -1; //cannot extend file thru truncatefunc
  if (length == 0) {
    int block = file->start_block;

    while (block != FAT_EOF && block != 0) {
      int next = fat.entries[block];
      fat.entries[block] = 0;  // mark free
      block = next;
    }

      file->start_block = FAT_EOF;
      file->file_size = 0;
      fd_table[fildes].offset = 0;
      return 0;
    }

    //traverse to fat to find trunc point
    int block = file->start_block;
    int prev = -1;
    int remaining = length;

    while (block != FAT_EOF && remaining > BLOCK_SIZE) {
      remaining -= BLOCK_SIZE;
      prev = block;
      block = fat.entries[block];
    }

    if (block == FAT_EOF) return 0;
    //free rest of chain
    int to_free = fat.entries[block];
    fat.entries[block] = FAT_EOF;

    while (to_free != FAT_EOF) {
        int next = fat.entries[to_free];
        fat.entries[to_free] = 0;
        to_free = next;
    }
    //update file size
    file->file_size = length;
    //adjust fildes offset
    if (fd_table[fildes].offset > length) {
        fd_table[fildes].offset = length;
    }
  /*When the file pointer is larger than the new length, 
  then it is also set to length (the end of the file)
  must free data blocks!!!*/
  return 0;
}

int fs_allNecessaryFunc(){
  return 1;
}

int main(){
  pthread_t thread_id;
  if (pthread_create(&thread_id, NULL, NULL, NULL) == 0) {
        printf("Thread created successfully\n");}

}
//Non-contiguous memory allocation in computer science is a technique 
//where a process's data or code is stored in separate, scattered memory 
//locations rather than a single, adjacent block