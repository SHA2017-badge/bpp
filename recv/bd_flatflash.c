/*
Blockdev iface for a flat flash file.

Meant to do e.g. firmware updates where it's important to have the data in one sequential
run. Disadvantage is that incremental updates are impossible. Needs an id cache because
it only saves the last change id and the bitmap of sectors that have that ID.

*/
#include <stdio.h>
#include <stdint.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include "esp_partition.h"
#include "structs.h"
#include "blockdevif.h"


typedef struct {
	uint32_t changeId; //current most recent changeid
	uint8_t bitmap[]; //Warning: inverted: 1 means sector does NOT have changeId.
} FlashMgmtSector;


struct BlockdevifHandle {
	int size;	//in blocks
	const esp_partition_t *part;
	FlashMgmtSector *msec;
};

//Desc in this case is a hex number: HHLL, with h the major and l the minor partition ID.
//Eg "2011" for major 32 minor 17
BlockdevifHandle *blockdevifInit(char *desc, int size) {
	BlockdevifHandle *h=malloc(sizeof(BlockdevifHandle));
	if (h==NULL) goto error1;
	int partid=strtol(desc, NULL, 16);
	assert(partid>=0x100);
	h->part=esp_partition_find_first(partid>>8, partid&0xff, NULL);
	if (h->part==NULL) goto error2;
	
	h->size=size/BLOCKDEV_BLKSZ;
	h->msec=malloc(h->size*8+sizeof(FlashMgmtSector));
	h->msec->changeId=0;
	if (h->msec->bitmap==NULL) goto error2;

	if (h->part->size < size+BLOCKDEV_BLKSZ) {
		printf("bd_flatflash: Part %s is %d bytes. Need %d bytes.\n", desc, h->part->size, size+BLOCKDEV_BLKSZ);
		goto error3;
	}

	//Read in management data
	esp_err_t r=esp_partition_read(h->part, h->size*BLOCKDEV_BLKSZ, h->msec, sizeof(FlashMgmtSector)+(h->size/8));
	return h;

error3:
	free(h->msec);
error2:
	free(h);
error1:
	return NULL;
}

static void setNewChangeId(BlockdevifHandle *handle, uint32_t changeId) {
	handle->msec->changeId=changeId;
	for (int i=0; i<handle->size/8; i++) handle->msec->bitmap[i]=0xff;
	esp_partition_erase_range(handle->part, handle->size*BLOCKDEV_BLKSZ, BLOCKDEV_BLKSZ);
}

static void flushMgmtSector(BlockdevifHandle *handle) {
	esp_partition_write(handle->part, handle->size*BLOCKDEV_BLKSZ, handle->msec, sizeof(FlashMgmtSector)+(handle->size/8));
}

void blockdevifSetChangeID(BlockdevifHandle *handle, int sector, uint32_t changeId) {
	if (changeId > handle->msec->changeId) setNewChangeId(handle, changeId);
	if (changeId == handle->msec->changeId) {
		handle->msec->bitmap[sector/8] &= ~(1<<(sector&7));
		flushMgmtSector(handle);
	}
}

uint32_t blockdevifGetChangeID(BlockdevifHandle *handle, int sector) {
	if (handle->msec->bitmap[sector/8] & (1<<(sector&7))) {
		return 0;
	} else {
		return handle->msec->changeId;
	}
}

int blockdevifGetSectorData(BlockdevifHandle *handle, int sector, uint8_t *buff) {
	esp_err_t r=esp_partition_read(handle->part, sector*BLOCKDEV_BLKSZ, buff, BLOCKDEV_BLKSZ);
	return (r==ESP_OK);
}

int blockdevifSetSectorData(BlockdevifHandle *handle, int sector, uint8_t *buff, uint32_t changeId) {
	if (sector>=handle->size) printf("Huh? Trying to write sector %d\n", sector);
	esp_partition_erase_range(handle->part, sector*BLOCKDEV_BLKSZ, BLOCKDEV_BLKSZ);
	esp_partition_write(handle->part, sector*BLOCKDEV_BLKSZ, buff, BLOCKDEV_BLKSZ);
	blockdevifSetChangeID(handle, sector, changeId);
}

void blockdevifForEachBlock(BlockdevifHandle *handle, BlockdevifForEachBlockFn *cb, void *arg) {
	for (int i=0; i<handle->size; i++) {
		uint32_t chid=blockdevifGetChangeID(handle, i);
		cb(i, chid, arg);
	}
}

BlockdevIf blockdefIfFlatFlash={
	.init=blockdevifInit,
	.setChangeID=blockdevifSetChangeID,
	.getChangeID=blockdevifGetChangeID,
	.getSectorData=blockdevifGetSectorData,
	.setSectorData=blockdevifSetSectorData,
	.forEachBlock=blockdevifForEachBlock
};


