#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>

#include "disk.h"

/******************************************************************************/
static int active = 0;  /* is the virtual disk open (active) */
static int handle;      /* file handle to virtual disk       */

/******************************************************************************/
//structs
typedef struct {
    int fat_location;
    int root_location;
    int data_start;
    int free_location;
    int num_free;
    int num_files;
} Boot;

typedef struct {
    int worker_id;
    int error;
} FAT; //4096 elements long

typedef struct {
    //name 15 bytes
    //attribute 1 byte
    //create time 16 bits
    //create date 16 bits
    //last access date 16 bits
    //last modified time 16 bits
    //last modified date 16 bits
    //starting cluster number in FAT 16 bits
    //File size 32 bits


    //bit (short for binary digit) is the smallest unit of digital data, 
    //representing a 0 or 1. 
    //A byte consists of eight bits and is typically used to represent a single
    //character, such as a letter or number

} RootDirectory; //max of 64 files

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
//make_fs
int make_fs(char *disk_name){
  //This function creates a fresh (and empty) file system on a virtual disk with name disk_name.
  //FIRST INVOKE MAKE_DISK
  make_disk(disk_name);
  //Then, open this disk and write/initialize the necessary meta-information 
  //for your file system so that it can be later used (mounted).
  return 0;
}

//With the mount operation, a file system becomes "ready for use."
int mount_fs(char *disk_name){
  //You need to open the disk and then load to memory the 
  //meta-information that is necessary to handle the file system 
  //operations that are discussed below.

  //the function returns 0 on success, and -1 when the disk disk_name could not 
  //be opened or when the disk does not contain a valid file system (that you 
  //previously created with make_fs).


  /*
  Make fs is just creating the disk and writing boot to it
Create a disk (code provided)
Open the disk (code provided)
Completely blank file 
Add initial info (boot, fat, root directory)
Close disk 
Return 0 on success and -1 on fail
*/
  return 0;
}
//this function unmounts your file system from a virtual disk with name disk_name.
int umount_fs(char *disk_name){
  /*you need to write back to disk all meta-information cached in memory 
  so that the disk persistently reflects all changes that were made to 
  the file system (such as new files that are created, data that is written, ...). 
  You should also close the disk.

  function returns 0 on success, and -1 when the disk disk_name could not be closed 
  or when data could not be written to the disk (this should not happen).
  */
  return 0;
}


int open_disk(char *name)
{
  int f;

  if (!name) {
    fprintf(stderr, "open_disk: invalid file name\n");
    return -1;
  }  
  
  if (active) {
    fprintf(stderr, "open_disk: disk is already open\n");
    return -1;
  }
  
  if ((f = open(name, O_RDWR, 0644)) < 0) {
    perror("open_disk: cannot open file");
    return -1;
  }

  handle = f;
  active = 1;

  return 0;
}

int close_disk()
{
  if (!active) {
    fprintf(stderr, "close_disk: no open disk\n");
    return -1;
  }
  
  close(handle);

  active = handle = 0;

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

  if (lseek(handle, block * BLOCK_SIZE, SEEK_SET) < 0) {
    perror("block_read: failed to lseek");
    return -1;
  }

  if (read(handle, buf, BLOCK_SIZE) < 0) {
    perror("block_read: failed to read");
    return -1;
  }

  return 0;
}
