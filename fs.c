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

int nextOpen() //look for the next free block using the FBB
{
    int i;

    for (i = 0; i < disk_size(); i++)
    {
        if (bitmap[i] == 0)
        {
            union fs_block block;
            disk_read(0, block.data);
            int openBlock = i;
            bitmap[i] = 1;
            return openBlock;
        }
    }

    return -1;
}

int fs_format()
{
    if (MOUNTED)
    {
        printf("fs_format Error: a filesystem is already mounted\n");
        return 0;
    }

    int diskSize = disk_size();

    int inodes = diskSize / 10; //reserve 10% for inodes
    if (inodes == 0)
    {
        inodes = 1;
    }

    union fs_block sb;

    sb.super.magic = FS_MAGIC; //set the data for the suberblock
    sb.super.nblocks = diskSize;
    sb.super.ninodeblocks = inodes;
    sb.super.ninodes = INODES_PER_BLOCK*inodes;

    disk_write(0, sb.data);

    int i, j, k;

    for (i = 1; i <= inodes; i++) // initialize all inods to not valid, size 0, direct blocks to 0, and indirect 0 
    {
        union fs_block block;
        disk_read(i, block.data);
        for ( j = 0; j < INODES_PER_BLOCK; j++)
        {
            block.inode[j].isvalid = 0;
            block.inode[j].size = 0;
            for (k = 0; k < 5; k++)
            {
                block.inode[j].direct[k] = 0;
            }
            block.inode[j].indirect = 0;
        }
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
    }
    else
    {
        printf("    magic number is not valid\n");
    }

    printf("    %d blocks\n",block.super.nblocks);
	printf("    %d inode blocks\n",block.super.ninodeblocks);
	printf("    %d inodes\n",block.super.ninodes);

    int inodeblocks = block.super.ninodeblocks;
    int inodes = block.super.ninodes;
    int currInodes = 0;

    int i, j, k;
    for (i = 1; i <= inodeblocks; i++)
    {
        disk_read(i, block.data);

        for (j = 1; j < INODES_PER_BLOCK; j++)
        {
            if (block.inode[j].isvalid && currInodes < inodes)
            {
                currInodes++;
                printf("Inode %d: valid\n", j);
                printf("     size: %d bytes\n", block.inode[j].size);
                if (block.inode[j].size > 0)
                {
                    printf("     direct blocks: ");
                    int nBlocks = ceil(block.inode[j].size / (double)4096);
                    if (nBlocks < 6)
                    {
                        for (k = 0; k < nBlocks; k++)
                        {
                            if (block.inode[j].direct[k] != 0)
                            {
                                printf("%d ", block.inode[j].direct[k]);
                            }
                        }
                        printf("\n");
                    }
                    else
                    {
                        for (k = 0; k < 5; k++)
                        {
                            printf("%d ", block.inode[j].direct[k]);
                        }
                        printf("\n");
                        printf("     indirect block: %d\n", block.inode[j].indirect);
                        printf("     indirect data blocks: ");
                        union fs_block indir;
                        disk_read(block.inode[j].indirect, indir.data);
                        for (k = 0; k < nBlocks - POINTERS_PER_INODE; k++)
                        {
                            printf("%d ", indir.pointers[k]);
                        }
                        printf("\n");
                    }
                }
            }
        }
    }
}

int fs_mount()
{
    if (MOUNTED == 1)
    {
        printf("fs_mount Error: a filesystem is already mounted\n");
        return 0;
    }

    union fs_block sbTest;

    disk_read(0, sbTest.data);

    if (sbTest.super.magic != FS_MAGIC)
    {
        printf("fs_mount Error: bad magic number on superblock\n");
        return 0;
    }

    // initialize the bitmap
    int diskSize = disk_size();
    bitmap = malloc(sizeof(int)*diskSize);

    int i, j, k;

    for (i = 0; i < disk_size(); i++) // initialize all to 0
    {
        bitmap[i] = 0;
    }

    bitmap[0] = 1; // superblock to 1

    for (i = 0; i <= sbTest.super.ninodeblocks; i++) // inodes to 1
    {
        bitmap[i] = 1;
    }
    for (i = 1; i <= sbTest.super.ninodeblocks; i++)
    {
        union fs_block block;
        disk_read(i, block.data);
        for (j = 0; j < INODES_PER_BLOCK; j++)
        {
            if (block.inode[j].isvalid != 0) // see if direct and indirect blocks in use, if so, set their bitmap to 1
            {
                int nBlocks = ceil(block.inode[j].size / (double)4096);
                if (nBlocks <= POINTERS_PER_INODE) // if only direct blocks
                {
                    for (k = 0; k < nBlocks; k++)
                    {
                        if (block.inode[j].direct[k] != 0)
                        {
                            bitmap[block.inode[j].direct[k]] = 1;
                        }
                    }
                    for (k = nBlocks; k < POINTERS_PER_INODE; k++)
                    {
                        block.inode[j].direct[k] = 0;
                    }
                    disk_write(i, block.data);
                }
                else if (nBlocks > POINTERS_PER_INODE) // if there are indirect blocks
                {
                    for (k = 0; k < POINTERS_PER_INODE; k++)
                    {
                        bitmap[block.inode[j].direct[k]] = 1;
                    }
                    disk_write(i, block.data);
                    union fs_block indir;
                    disk_read(block.inode[j].indirect, indir.data);
                    for (k = 0; k < nBlocks - POINTERS_PER_INODE; k++)
                    {
                        bitmap[indir.pointers[k]] = 1;
                    }
                    for (k = nBlocks - POINTERS_PER_INODE; k < POINTERS_PER_BLOCK; k++)
                    {
                        indir.pointers[k] = 0;
                    }
                    disk_write(block.inode[j].indirect, indir.data);
                }
            }
        }
    }

    MOUNTED = 1;
	return 1;
}

int fs_create()
{

    if (!MOUNTED)
    {
        printf("fs_create Error: please mount a filesystem first\n");
        return 0;
    }

    union fs_block block;
    disk_read(0, block.data); // read superblock
    
    int inodeBlocks = block.super.ninodeblocks;
    
    int i, j;
    for (i = 1; i <= inodeBlocks; i++) // check each inode block for free inode
    {
        union fs_block curr;
        disk_read(i, curr.data);
        for (j = 1; j < INODES_PER_BLOCK; j++)
        {
            if (curr.inode[j].isvalid == 0)
            {
                curr.inode[j].isvalid = 1;
                curr.inode[j].size = 0;
                disk_write(0, block.data); // save changes
                disk_write(i, curr.data);
                return (i-1)*INODES_PER_BLOCK + j; // return position of inode
            }
        }
    }

	return 0;
}

int fs_delete( int inumber )
{
    if (inumber < 1)
    {
        printf("fs_delete Error: invalid inode number\n");
        return 0;
    }

    int index = 1 + inumber / INODES_PER_BLOCK;

    union fs_block block;

    disk_read(index, block.data);

    index = index % INODES_PER_BLOCK;

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
        printf("fs_getsize Error: invalid inode number\n");
        return -1;
    }

    int index = 1 + inumber / INODES_PER_BLOCK;

    union fs_block block;

    disk_read(index, block.data);

    index = index % INODES_PER_BLOCK;

    if (block.inode[index].isvalid == 0)
    {
        printf("fs_getsize Error: invalid inode number\n");
        return -1;
    }
    return block.inode[index].size;

}

int fs_read( int inumber, char *data, int length, int offset )
{

    if (inumber < 1)
    {
        printf("fs_read Error: invalid inode number\n");
        return -1;
    }

    if (!MOUNTED)
    {
        printf("fs_read Error: no filesystem mounted\n");
        return -1;
    }

    int index = 1 + inumber / INODES_PER_BLOCK;  // block to read

    union fs_block block;

    disk_read(index, block.data);   // read in block that has inode

    index = inumber % INODES_PER_BLOCK;          // get inode from inode block

    if (length > block.inode[index].size - offset) //don't read over size
    {
        length = block.inode[index].size - offset;
    }

    int curr = ceil(offset / (double)4096); // which block to start at

    int tmpOff = offset % 4096;     // what index to start at

    int currData = 0;               // amount we've copied

    int i;

    while (currData < length)
    {
        union fs_block copyBlock;
        if (curr < POINTERS_PER_INODE)      // if we are still in direct blocks
        {
            if (block.inode[index].direct[curr] == 0)
            {
                return currData;
            }

            disk_read(block.inode[index].direct[curr], copyBlock.data); // readin direct block
            for (i = tmpOff; i < 4096; i++)
            {
                if (currData == length)
                {
                    return currData; // return if all data read in
                }
                data[currData] = copyBlock.data[i]; // copy data
                currData++;
            }
            tmpOff = 0;
            curr++;
        }
        else if (curr >= POINTERS_PER_INODE)    // going into indirect blocks
        {
            disk_read(block.inode[index].indirect, copyBlock.data);
            int j = curr % POINTERS_PER_INODE;
            while (copyBlock.pointers[j] > 0)
            {
                union fs_block indir;
                disk_read(copyBlock.pointers[j], indir.data);

                for (i = tmpOff; i < 4096; i++)
                {
                    if (currData == length)
                    {
                        return currData; // return if all data read in
                    }
                    data[currData] = indir.data[i];
                    currData++;
                }
                tmpOff = 0;
                j++;
            }
            curr++;
            return currData;
        }
    }

    return currData;
}

int fs_write( int inumber, const char *data, int length, int offset )
{
    if (inumber < 1)
    {
        printf("fs_write Error: invalid inode number\n");
        return 0;
    }

    if (!MOUNTED)
    {
        printf("fs_write Error: filesystem not mounted\n");
        return 0;
    }

    int origBlock = 1 + inumber / INODES_PER_BLOCK;  // block to write to

    union fs_block block;

    disk_read(origBlock, block.data);   // read in block that has inode

    int index = inumber % INODES_PER_BLOCK;          // get inode from inode block

    if (block.inode[index].isvalid == 0)        // if inode is not valid
    {
        printf("fs_write Error: inode not valid\n");
        return 0;
    }

    int curr = ceil(offset / 4096); // which pointer to start at

    int tmpOff = offset % 4096;     // what bytes to start at

    int toCopy = length;            // amount we still need to write

    int overLap = block.inode[index].size - length; // bytes of data that will be overwritten

    int newSize = block.inode[index].size;          // size of stuff writen, only grows when writing new data

    if (offset > newSize)
    {
        newSize = offset; // in case we are writing past the current data
    }

    int currData = 0;               // amount we've writen

    int i;
    while (toCopy > 0)
    {
        union fs_block writeBlock;
        if (curr < POINTERS_PER_INODE)      // if we are still in direct blocks
        {
            if (block.inode[index].direct[curr] == 0) // no if direct block, find next open block 
            {
                block.inode[index].direct[curr] = nextOpen();

                if (block.inode[index].direct[curr] < 1)
                {
                    printf("fs_write Error: No more open blocks\n");
                    return currData;
                }
            }

            disk_read(block.inode[index].direct[curr], writeBlock.data); // read in direct pointer
            for (i = tmpOff; i < 4096; i++)
            {
                if (toCopy == 0) // if we are finished writing...
                {
                    disk_write(block.inode[index].direct[curr], writeBlock.data);
                    block.inode[index].size = newSize;
                    disk_write(origBlock, block.data);
                    return currData;
                }

                if (overLap <= 0) // if we are done overwriting data and now writing new data
                {
                    newSize++;
                }
                writeBlock.data[i] = data[currData];
                toCopy--;
                currData++;
                overLap--;
            }
            disk_write(block.inode[index].direct[curr], writeBlock.data);
            disk_write(origBlock, block.data);
            tmpOff = 0;
            curr++;
        }
        else if (curr >= POINTERS_PER_INODE) // if we are in the indirect blocks
        {
            if (block.inode[index].indirect == 0)
            { // if no indirect block set yet...
                block.inode[index].indirect = nextOpen();
                disk_read(block.inode[index].indirect, writeBlock.data);
                int g;
                for (g = 0; g < POINTERS_PER_BLOCK; g++)
                { // initialize pointers to 0
                    writeBlock.pointers[g] = 0;
                }
            }
            else 
            {
                disk_read(block.inode[index].indirect, writeBlock.data);
            }

            if (block.inode[index].indirect <= 0)
            {
                block.inode[index].indirect = 0;
                block.inode[index].size = newSize;
                disk_write(origBlock, block.data);
                printf("fs_write Error: No more open blocks\n");
                return currData;
            }
        
            int j = curr % POINTERS_PER_INODE;
                // find current block
            while (toCopy > 0)
            {
                union fs_block indir;
                if (writeBlock.pointers[j] == 0)
                { // if indirect pointer not set to block....
                    writeBlock.pointers[j] = nextOpen();
                    if (writeBlock.pointers[j] <= 0)
                    {
                        writeBlock.pointers[j] = 0;
                        block.inode[index].size = newSize;
                        disk_write(block.inode[index].indirect, writeBlock.data);
                        disk_write(origBlock, block.data);
                        printf("fs_write Error: no more open blocks\n");
                        return currData;
                    }
                }
                disk_read(writeBlock.pointers[j], indir.data);

                for (i = tmpOff; i < 4096; i++)
                {
                    if (toCopy == 0) // if done writing....
                    {
                        block.inode[index].size = newSize;
                        disk_write(writeBlock.pointers[j], indir.data);
                        disk_write(block.inode[index].indirect, writeBlock.data);
                        disk_write(origBlock, block.data);
                        return currData;
                    }

                    if (overLap <= 0)
                    {
                        newSize++;
                    }
                    indir.data[i] = data[currData];
                    toCopy--;
                    currData++;
                    overLap--;
                }

                disk_write(writeBlock.pointers[j], indir.data);
                disk_write(block.inode[index].indirect, writeBlock.data);
                tmpOff = 0;
                j++;
            }
            curr++;
            return currData;
        }
    }

    return currData;
}







