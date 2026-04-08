#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <stdint.h>
#define FAT_SIZE 4096
#define FAT_EOF  -1
#define MAX_FILENAME 16
#define MAX_FILES 64 //max of 64 files

#include "disk.h"

/******************************************************************************/
static int active = 0;  /* is the virtual disk open (active) */
static int handle;      /* file handle to virtual disk       */
/******************************************************************************/
//structs
typedef struct {
  int total_blocks;
  int block_size;
  int fat_start;
  int fat_length;
  int root_start;
  int root_length;
  int data_start;
} Boot;

typedef struct {
    int entries[FAT_SIZE];
} FAT; //4096 elements long

typedef struct {
    char name[MAX_FILENAME];

    uint16_t start_block;
    uint32_t file_size;

    uint8_t used;
} DirEntry;

typedef struct {
  DirEntry entries[MAX_FILES];
    //bit (short for binary digit) is the smallest unit of digital data, 
    //representing a 0 or 1. 
    //A byte consists of eight bits and is typically used to represent a single
    //character, such as a letter or number
} RootDirectory; //an array of entries that contain data for the file

//global variables so all functions have access
Boot boot;
FAT fat;
RootDirectory root;

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

  //FAT
  for (int i = 0; i < FAT_SIZE; i++) {
    fat.entries[i] = 0; //0 is the free block
  }

  //ROOT directory
  for (int i = 0; i < MAX_FILES; i++) {
    root.entries[i].used = 0;
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

//With the mount operation, a file system becomes "ready for use."
int mount_fs(char *disk_name){
  if (open_disk(disk_name) < 0) return -1;
  char buf[BLOCK_SIZE];

  //load Boot, FAT, Root into memory
  char *fat_ptr = (char *)&fat; //read fat into mem
  for (int i = 0; i < boot.fat_length; i++) {
    if (block_read(boot.fat_start + i, buf) < 0) return -1;
    memcpy(fat_ptr + i * BLOCK_SIZE, buf, BLOCK_SIZE);
  }

  char *root_ptr = (char *)&root; //read root into mem
  for (int i = 0; i < boot.root_length; i++) {
    if (block_read(boot.root_start + i, buf) < 0) return -1;
    memcpy(root_ptr + i * BLOCK_SIZE, buf, BLOCK_SIZE);
  }
  return 0;


  return 0;
}
//this function unmounts your file system from a virtual disk with name disk_name.
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
