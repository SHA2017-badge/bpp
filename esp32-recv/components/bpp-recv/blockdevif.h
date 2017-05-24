#ifndef BLOCKDEVIF_H
#define BLOCKDEVIF_H

//Interface to an abstracted block device. Should have the capability of storing both sector change IDs 
//as well as sector data.


typedef struct BlockdevifHandle BlockdevifHandle;

typedef void (BlockdevifForEachBlockFn)(int blockno, uint32_t changeId, void *arg);

typedef struct {
	BlockdevifHandle* (*init)(void *desc, int size);
	void (*setChangeID)(BlockdevifHandle *handle, int sector, uint32_t changeId);
	uint32_t (*getChangeID)(BlockdevifHandle *handle, int sector);
	int (*getSectorData)(BlockdevifHandle *handle, int sector, uint8_t *buff);
	int (*setSectorData)(BlockdevifHandle *handle, int sector, uint8_t *buff);
	void (*forEachBlock)(BlockdevifHandle *handle, BlockdevifForEachBlockFn *cb, void *arg);
} BlockdevIf;


#endif