#ifndef BLOCKDEVIF_H
#define BLOCKDEVIF_H

//Interface to an abstracted block device. Should have the capability of storing both sector change IDs 
//as well as sector data.


typedef struct BlockdevifHandle BlockdevifHandle;

typedef void (BlockdevifForEachBlockFn)(int blockno, uint32_t changeId, void *arg);

BlockdevifHandle *blockdevifInit(char *desc, int size);
void blockdevifSetChangeID(BlockdevifHandle *handle, int sector, uint32_t changeId);
uint32_t blockdevifGetChangeID(BlockdevifHandle *handle, int sector);
int blockdevifGetSectorData(BlockdevifHandle *handle, int sector, uint8_t *buff);
int blockdevifSetSectorData(BlockdevifHandle *handle, int sector, uint8_t *buff, uint32_t changeId);
void blockdevifForEachBlock(BlockdevifHandle *handle, BlockdevifForEachBlockFn *cb, void *arg);

#endif