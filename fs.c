#include "fs.h"
#include "disk.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <math.h>

#define FS_MAGIC           0xf0f03410
#define INODES_PER_BLOCK   128
#define POINTERS_PER_INODE 5
#define POINTERS_PER_BLOCK 1024

struct fs_superblock {
	int magic;
	int nblocks;
	int ninodeblocks;
	int ninodes;
};

struct fs_inode {
	int isvalid;
	int size;
	int direct[POINTERS_PER_INODE];
	int indirect;
};

union fs_block {
	struct fs_superblock super;
	struct fs_inode inode[INODES_PER_BLOCK];
	int pointers[POINTERS_PER_BLOCK];
	char data[DISK_BLOCK_SIZE];
};

int MOUNTED = 0; 
int *bitmap;

int fs_format()
{
    if (MOUNTED)
    {
        return 0; 
    }

    int diskSize = disk_size();

    int inodes = diskSize / 10; 
    if (inodes == 0)
    {
        inodes = 1;
    }

    union fs_block sb;

    sb.super.magic = FS_MAGIC; 
    sb.super.nblocks = diskSize;
    sb.super.ninodeblocks = inodes;
    sb.super.ninodes = 0; 

    disk_write(0, sb.data);

    int i;

    for (i = 1; i <= inodes; i++)
    {
        union fs_block block;
        disk_write(i, block.data);
    }

    return 1;
}

void fs_debug()
{
	union fs_block block;

	disk_read(0,block.data);

	printf("superblock:\n");
	if (block.super.magic == FS_MAGIC)
    {
        printf("    magic number is valid\n");
    } else
    {
        printf("    magic number is not valid\n");
    }
    printf("    %d blocks\n",block.super.nblocks);
	printf("    %d inode blocks\n",block.super.ninodeblocks);
	printf("    %d inodes\n",block.super.ninodes);
    int inodeblocks = block.super.ninodeblocks;
    int inodes = block.super.ninodes;
    int currInodes = 0;
    
    int i, j, k, l;
    for (i = 0; i < inodeblocks; i++)
    {
        disk_read(i, block.data);

    //    printf("%d", block.super.magic);
        for (j = 1; j < INODES_PER_BLOCK; j++)
        {
            if (block.inode[j].isvalid && currInodes < inodes)
            {
                currInodes++;
                printf("Inode %d: valid\n", j);
                printf("     size: %d bytes\n", block.inode[j].size);
                printf("     direct blocks: ");
                for (k = 0; k < POINTERS_PER_INODE; k++)
                {
                    if (block.inode[j].direct[k] != 0)
                        printf("%d ", block.inode[j].direct[k]);
                }
                printf("\n");
                if (block.inode[j].indirect != 0) 
                {
                    printf("     indirect block: %d\n", block.inode[j].indirect);
                    printf("     indirect data blocks: ");
                    union fs_block indir;
                    disk_read(block.inode[j].indirect, indir.data);
                    for (l = 0; l < POINTERS_PER_BLOCK; l++)
                    {
                        if (indir.pointers[l] > 0)
                        {
                            printf("%d ", indir.pointers[l]);
                        }
                    }
                    printf("\n");
                }
            }
        }
    }
}

int fs_mount()
{
    if (MOUNTED == 1)
    {
        printf("on no!");
        return 0;
    }
    
    union fs_block sbTest;

    disk_read(0, sbTest.data);

    if (sbTest.super.magic != FS_MAGIC)
    {
        return 0;
    }   

    int diskSize = disk_size();
    bitmap = malloc(sizeof(int)*diskSize);
    
    
    bitmap[0] = 1;

    int i, j, k;
    
    for (i = 0; i <= sbTest.super.ninodeblocks; i++)
    {
        bitmap[i] = 1;
    }
    int curr = sbTest.super.ninodeblocks + 1;
    for (i = 1; i <= sbTest.super.ninodeblocks; i++)
    {
        union fs_block block;
        disk_read(i, block.data);
        for (j = 0; j < INODES_PER_BLOCK; j++)
        {
            if (block.inode[j].isvalid != 0)
            {
                //bitmap[curr] = 1;
                //curr++;
                int nBlocks = ceil(block.inode[j].size / 4096);
                if (nBlocks <= 5)
                {
                    for (k = 0; k < nBlocks; k++)
                    {
                        if (block.inode[j].direct[k] != 0)
                        {
                            bitmap[curr] = 1; 
                            curr++;
                        }
                    }
                }
                else if (nBlocks > 5)
                {
                    for (k = 0; k < 6; k++)
                    {
                        bitmap[curr] = 1;
                        curr++;
                    }
                    union fs_block indir;
                    disk_read(block.inode[j].indirect, indir.data);
                    for (k = 0; k < nBlocks - 5; k++)
                    {
                        bitmap[curr] = 1;
                        curr++;
                    }
                }
            }
        }
    }

    for (i = curr; i < diskSize; i++)
    {
        bitmap[i] = 0;
    }
    MOUNTED = 1;
	return 1;
}

int fs_create()
{
    union fs_block block;
    disk_read(0, block.data);
    int inodeBlocks = block.super.ninodeblocks;
    int i, j;
    for (i = 1; i <= inodeBlocks; i++)
    {
        union fs_block curr;
        disk_read(i, curr.data);
        for (j = 0; j < INODES_PER_BLOCK; j++)
        {
            if (curr.inode[j].isvalid == 0)
            {
                curr.inode[j].isvalid = 1;
                curr.inode[j].size = 0; 
                return (i-1)*128 + j;
            }
        }
    }
    
	return 0;
}

int fs_delete( int inumber )
{
    if (inumber < 1)
    {
        return 0;
    }
    
    int index = 1 + inumber / 128;

    union fs_block block;

    disk_read(index, block.data);

    index = index % 128;

    if (block.inode[index].isvalid == 0)
    {
        return 0;
    }

    block.inode[index].isvalid = 0;
    block.inode[index].size = 0;

    int i;

    for (i = 0; i < POINTERS_PER_INODE; i++)
    {
        block.inode[index].direct[i] = 0;
    }

    block.inode[index].indirect = 0;

	return 1;
}

int fs_getsize( int inumber )
{
    if (inumber < 1)
    {
        return -1;
    }
    
    int index = 1 + inumber / 128;

    union fs_block block;

    disk_read(index, block.data);

    index = index % 128;

    if (block.inode[index].isvalid == 0)
    {
        return -1;
    }
    return block.inode[index].size;

}

int fs_read( int inumber, char *data, int length, int offset )
{

    if (inumber < 1)
    {
        return -1;
    }
   
    if (!MOUNTED)
    {
        printf("not mounted\n");
        return -1;
    }
    
    int index = 1 + inumber / 128;  // block to read

    union fs_block block;

    disk_read(index, block.data);   // read in block that has inode

    index = inumber % 128;          // get inode from inode block

    int cLen = ceil(length % 4096); // number of blocks to copy
    //int tmpLen = length;            // amount left to copy
    
    int curr = ceil(offset / 4096); // which pointer to start at

    int tmpOff = offset % 4096;     // what bytes to start at

    int currData = 1;               // amount we've copied

    int i;
    
    //printf("starting at block %d and ending at %d\n", curr, cLen + curr);

    //union fs_block indir;
    while (currData < length)
    {
        union fs_block copyBlock;
        if (curr < 5)               // if we are still in direct blocks
        {
    //        printf("\n\nReading direct block %d\n\n", curr);
            disk_read(block.inode[index].direct[curr], copyBlock.data);
            for (i = tmpOff - 1; i < 4096; i++)
            {
                if (currData == length)
                {
                    return currData;
                }
                data[currData] = copyBlock.data[i];
                currData++;
            }
            cLen--;
            tmpOff = 0;
            curr++;
        } 
        else if (curr == 5)
        {
            //union fs_block
            disk_read(block.inode[index].indirect, copyBlock.data); 
            //union fs_block indir;
            int j = 0;

            while (copyBlock.pointers[j] > 0)
            {
      //          printf("\n\n\nReading indirect block %d\n\n\n", j);
                union fs_block indir;
                disk_read(copyBlock.pointers[j], indir.data);

                for (i = tmpOff; i < 4096; i++)
                {
                    if (currData == length)
                    {
                        return currData;
                    }
                    data[currData] = indir.data[i];
                    currData++;
                }
                tmpOff = 0;
                //curr = 0;
                j++;
 //               printf("i1n\n");
            }
            curr++;
            return currData;
        }
    //    printf("Curr: %d Goal: %d\n", currData, length);
    }

    return currData;
}

int fs_write( int inumber, const char *data, int length, int offset )
{
	return 0;
}
