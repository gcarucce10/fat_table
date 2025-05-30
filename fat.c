#include "fat.h"
#include "ds.h"
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#define SUPER 0
#define TABLE 2
#define DIR 1

#define SIZE 1024

// the superblock
#define MAGIC_N           0xAC0010DE
typedef struct{
	int magic;
	int number_blocks;
	int n_fat_blocks;
	char empty[BLOCK_SIZE-3*sizeof(int)];
} super;

super sb;

//item
#define MAX_LETTERS 6
#define OK 1
#define NON_OK 0
typedef struct{
	unsigned char used;
	char name[MAX_LETTERS+1];
	unsigned int length;
	unsigned int first;
} dir_item;

#define N_ITEMS (BLOCK_SIZE / sizeof(dir_item))
dir_item dir[N_ITEMS];

// table
#define FREE 0
#define EOFF 1
#define BUSY 2
unsigned int *fat;

int mountState = 0;

// What fat function is used in every instruction
// formatar -> fat_format()
// montar -> fat_mount()
// criar -> fat_create()
// deletar -> fat_delete()
// debugar -> fat_debug()


int fat_format(){ 
	// Test if the disk is already formatted
	if (mountState != 0){
		printf("Erro: disco ja formatado\n");
		return -1;
	}

	// Define the superblock 
	sb.magic = MAGIC_N;
	sb.number_blocks = ds_size(); // Numbers of directory blocks
	sb.n_fat_blocks = ceil((float)sb.number_blocks / BLOCK_SIZE);

	// Write the superblock to the disk
	ds_write(SUPER, (char *)&sb);

	// Allocate for the directory
	memset(dir, 0, sizeof(dir));

	// Write the directory to the disk
	ds_write(DIR, (char *)dir);

	// Allocate memory for the FAT
	fat = malloc(sb.n_fat_blocks * BLOCK_SIZE);
	if (fat == NULL){
		printf("Erro: falha ao alocar memoria para a FAT\n");
		return -1;
	}

	// Write the FAT to the disk
	ds_write(TABLE, (char *)fat);

	free(fat);

	mountState = 1;

	// Return 0 if everything went well
	return 0;
}

// fat_debug() prints the superblock and directory configuration. It checks
// if the magic number is correct and prints the number of blocks and
// the number of FAT blocks. It also prints the files in the directory
// and their attributes, such as size and blocks. It uses the FAT to
// traverse the blocks of each file and print their numbers.
void fat_debug(){
	// Print the superblock and directory configuration
	printf("superblock:\n");
    if (sb.magic == MAGIC_N)
        printf("magic is ok\n");
    else
        printf("magic is wrong (0x%x)\n", sb.magic);

    printf("%d blocks\n", sb.number_blocks);
    printf("%d block(s) fat\n", sb.n_fat_blocks);

    for (int i = 0; i < N_ITEMS; i++) {
        if (dir[i].used) {
            printf("File \"%s\":\n", dir[i].name);
            printf("  size: %u bytes\n", dir[i].length);
            printf("  Blocks:");

            unsigned int current = dir[i].first;
            while (current != 0 && current != EOFF) {
                printf(" %u", current);
                if (fat[current] == EOFF) break;
                current = fat[current];
            }
            printf("\n");
        }
    }
}

// fat_mount() read the superblock and the directory from the disk and
// check if the disk is already mounted. If the disk is not mounted, it
// allocates memory for the FAT and reads it from the disk. It also
// checks if the superblock is valid by comparing the magic number.
int fat_mount(){
	// Check if the disk is already mounted
	if(mountState == 0){
		printf("Error: disk not mounted\n");
		return -1;
	}

	// Read the superblock from the disk
	ds_read(SUPER, (char *)&sb);

	// Check if the superblock is valid
	if (sb.magic != MAGIC_N){
		printf("Erro: disco nao formatado\n");
		return -1;
	}

	// Read the directory from the disk
	ds_read(DIR, (char *)dir);

	fat = malloc(sb.n_fat_blocks * BLOCK_SIZE);
	if (fat == NULL){
		printf("Error: cannot allocate memory to FAT\n");
		return -1;
	}

	// Read the FAT from the disk
	ds_read(TABLE, (char *)fat);

	mountState = 1;

  	return 0;
}

// fat_create() creates a new file in the directory block of the disk and
// initializes its attributes. First it checks if the disk is mounted and
// if the file name already exists. If everything is ok, it allocates the
// new file in the first free slot of the directory block, inicializes the
// file size as 0 and the first block as 0, because the file is empty.
int fat_create(char *name){
	// Check if the disk is mounted
	if (mountState == 0){
		printf("Error: disk not mounted\n");
		return -1;
	}

	// Check if the file already exists
	for (int i = 0; i < N_ITEMS; i++) {
		if (dir[i].used && strcmp(dir[i].name, name) == 0) {
			printf("Error: file already exists\n");
			return -1;
		}
	}

	// Find the first free slot in the directory and write the file name there
	for (int i = 0; i < N_ITEMS; i++){
		if (!dir[i].used){
			// Create a new file
			dir[i].used = 1;
			strncpy(dir[i].name, name, MAX_LETTERS);
			dir[i].name[MAX_LETTERS] = '\0';
			dir[i].length = 0;
			dir[i].first = 0;

			// Write the directory to the disk	
			ds_write(DIR, (char *)dir);
		
  			return 0;
		}
	}
}

// fat_delete() deletes a file from the directory block of the disk and
// frees its blocks in the FAT. It first checks if the disk is mounted and
// if the file exists. If everything is ok, it frees the blocks of the
// file in the FAT and marks the directory entry as unused. It also
// updates the directory block on the disk.
int fat_delete( char *name){

	// Check if the disk is mounted
	if (mountState == 0){
		printf("Error: disk not mounted\n");
		return -1;
	}

	int flag_file = 0;
	// Check if the file exists
	for (int i = 0; i < N_ITEMS; i++){
		if (dir[i].used && strcmp(dir[i].name, name) == 0){
			flag_file = 1;
		}
	}

	if(flag_file == 0){
		printf("Error: file not found\n");
		return -1;
	}

	// Find the file in the directory and free its blocks in the FAT
	for (int i = 0; i < N_ITEMS; i++){
		if (dir[i].used && strcmp(dir[i].name, name) == 0){
			// Free the blocks of the file in the FAT
			unsigned int current = dir[i].first;

			while (current != 0 && current != EOFF) {
				unsigned int next = fat[current];
				fat[current] = FREE;
				current = next;
			}

			dir[i].used = 0;

			ds_write(TABLE, (char *)fat);

			ds_write(DIR, (char *)dir);

			printf("File \"%s\" deleted\n", name);

			return 0;
		}

		printf("Error: file not found\n");
		return -1;
	}
}

// fat_getsize() returns the size of a file in bytes.
int fat_getsize( char *name){ 
	// Check if the disk is mounted
	if (mountState == 0){
		printf("Error: disk not mounted\n");
		return -1;
	}

	int flag_file = 0;
	// Check if the file exists
	for (int i = 0; i < N_ITEMS; i++){
		if (dir[i].used && strcmp(dir[i].name, name) == 0){
			flag_file = 1;
		}
	}

	if(flag_file == 0){
		printf("Error: file not found\n");
		return -1;
	}

	// Find the file in the directory and return its size
	for (int i = 0; i < N_ITEMS; i++){
		if (dir[i].used && strcmp(dir[i].name, name) == 0){
			return dir[i].length;
		}
	}

	printf("Error: file not found\n");
	return -1;
}

// Reads data from a valid file. Copies length bytes from the file to buff,
// starting offset bytes from the beginning of the file. Returns the total 
//bytes read. This value may be less than length if the end of the file is reached.
// In case of an error, returns -1. 
int fat_read( char *name, char *buff, int length, int offset){
	if (!mountState) return -1;
    if (length <= 0 || offset < 0) return -1;

	int i;
    for (i = 0; i < N_ITEMS; i++) {
        if (dir[i].used && strcmp(dir[i].name, name) == 0)
            break;
    }
    if (i == N_ITEMS) return -1;

	unsigned int filesize = dir[i].length;
	if (offset >= filesize) return -1; 

	if (offset + length > filesize)
        length = filesize - offset;

	unsigned int current = dir[i].first;
    int pos = 0;
    int read_bytes = 0;
    int block_offset = offset;

    while (block_offset >= BLOCK_SIZE && current != EOFF) {
        current = fat[current];
        block_offset -= BLOCK_SIZE;
        pos += BLOCK_SIZE;
    }

    while (read_bytes < length && current != EOFF) {
        char data[BLOCK_SIZE];
        ds_read(current, data);

        int to_read = BLOCK_SIZE - block_offset;
        if (to_read > (length - read_bytes))
            to_read = length - read_bytes;

        memcpy(buff + read_bytes, data + block_offset, to_read);
        read_bytes += to_read;
        block_offset = 0;

        current = fat[current];
    }

    return read_bytes;
}

// Writes data to a file. Copies length bytes from buff to the file, 
// starting offset bytes from the beginning of the file. Generally, 
// this operation involves allocating free blocks. Returns the total
// bytes written. This value can be less than length, for example, if 
//the disk becomes full. In case of an error, returns -1.
int fat_write(char *name, const char *buff, int length, int offset) {
    if (!mountState) return -1;
    if (length <= 0 || offset < 0) return -1;

    int i;
    for (i = 0; i < N_ITEMS; i++) {
        if (dir[i].used && strcmp(dir[i].name, name) == 0) break;
    }
    if (i == N_ITEMS) return -1; 

    unsigned int filesize = dir[i].length;
    unsigned int first = dir[i].first;

    if (first == 0) {
        int b;
        for (b = 3; b < sb.number_blocks; b++) {
            if (fat[b] == FREE) {
                dir[i].first = b;
                fat[b] = EOFF;
                break;
            }
        }
        if (dir[i].first == 0) return -1;
        first = dir[i].first;
    }

    unsigned int current = first;
    unsigned int prev = 0;
    int pos = 0;
    while (pos + BLOCK_SIZE <= offset) {
        if (fat[current] == EOFF) {
            int novo = -1;
            for (int b = 3; b < sb.number_blocks; b++) {
                if (fat[b] == FREE) {
                    novo = b;
                    break;
                }
            }
            if (novo == -1) return pos; 
            fat[current] = novo;
            fat[novo] = EOFF;
        }
        prev = current;
        current = fat[current];
        pos += BLOCK_SIZE;
    }

    int written = 0;
    int buff_pos = 0;
    int write_offset = offset - pos;

    while (written < length) {
        char block_data[BLOCK_SIZE];

        ds_read(current, block_data);

        int to_copy = BLOCK_SIZE - write_offset;
        if (to_copy > (length - written)) {
            to_copy = length - written;
        }

        memcpy(block_data + write_offset, buff + buff_pos, to_copy);
        ds_write(current, block_data);

        written += to_copy;
        buff_pos += to_copy;
        write_offset = 0; 

        if (written < length) {
            if (fat[current] == EOFF) {
                int novo = -1;
                for (int b = 3; b < sb.number_blocks; b++) {
                    if (fat[b] == FREE) {
                        novo = b;
                        break;
                    }
                }
                if (novo == -1) break; 
                fat[current] = novo;
                fat[novo] = EOFF;
            }
            current = fat[current];
        }
    }

    if (offset + written > dir[i].length)
        dir[i].length = offset + written;

    ds_write(DIR, (char *)dir);
    ds_write(TABLE, (char *)fat);

    return written;
}