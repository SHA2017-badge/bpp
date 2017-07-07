/*
Multi-level block cache. Keeps a few changeids in memory, plus the sectors which have
that changeid. Delegates to the underlying block device for everything else.
*/
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "blkidcache.h"

#define LEVELS 3

void idcacheSetInt(BlkIdCacheHandle *h, int block, uint32_t id, int writeback);


struct BlkIdCacheHandle {
	BlockdevifHandle *blkdev;
	uint8_t *bmp[LEVELS];
	uint32_t id[LEVELS];
	int size;
	BlockdevIf *bdif;
};

//called for each block when cache is created
static void initCache(int blockno, uint32_t changeId, void *arg) {
	BlkIdCacheHandle *h=(BlkIdCacheHandle*)arg;
	idcacheSetInt(h, blockno, changeId, 0);
}

BlkIdCacheHandle *idcacheCreate(int size, BlockdevifHandle *blkdev, BlockdevIf *bdif) {
	BlkIdCacheHandle *ret=malloc(sizeof(BlkIdCacheHandle));
	ret->blkdev=blkdev;
	ret->size=size;
	ret->bdif=bdif;
	for (int i=0; i<LEVELS; i++) {
		ret->bmp[i]=malloc((size+7)/8);
		ret->id[i]=0;
	}
	bdif->forEachBlock(blkdev, initCache, ret);
	return ret;
}

void idcacheFlushToStorage(BlkIdCacheHandle *h) {
	printf("Flushing idcache to storage.\n");
	for (int bl=0; bl<h->size; bl++) {
		for (int i=0; i<LEVELS; i++) {
			if (h->bmp[i][bl/8] & (1<<(bl&7))) {
				h->bdif->setChangeID(h->blkdev, bl, h->id[i]);
				break;
			}
		}
	}
}

void idcacheSet(BlkIdCacheHandle *h, int block, uint32_t id) {
	idcacheSetInt(h, block, id, 1);
}

void idcacheSetInt(BlkIdCacheHandle *h, int block, uint32_t id, int writeback) {
	//Kill bit in all levels but the one that has the same id. Also check if the id may
	//be newer than anything we have.
	int isNewer=1;
	int isSet=0;
	for (int i=0; i<LEVELS; i++) {
		if (id <= h->id[i]) isNewer=0;
		if (id != h->id[i]) {
			h->bmp[i][block/8] &= ~(1<<(block&7));
		} else {
			h->bmp[i][block/8] |= (1<<(block&7));
			isSet=1;
		}
	}

	if (isNewer) {
		//Okay, we need to clean up a new level and use it for this ID.
		int oldest=0;
		for (int i=0; i<LEVELS; i++) {
			if (h->id[i] < h->id[oldest]) oldest=i;
		}
		h->id[oldest]=id;
		memset(h->bmp[oldest], 0, h->size/8);
		h->bmp[oldest][block/8] |= (1<<(block&7));
		isSet=1;
	}
	if (!isSet) {
//		printf("idcacheSet: Huh, cache changeid (blk %d id %d) can't be handled by cache.", block, id);
		if (writeback) h->bdif->setChangeID(h->blkdev, block, id);
	}
}

void idcacheSetSectorData(BlkIdCacheHandle *h, int block, uint8_t *data, uint32_t id) {
	idcacheSetInt(h, block, id, 1);
	h->bdif->setSectorData(h->blkdev, block, data, id);
}


uint32_t idcacheGet(BlkIdCacheHandle *h, int block) {
	for (int i=0; i<LEVELS; i++) {
		if (h->bmp[i][block/8] & (1<<(block&7))) {
//			printf("Got cached id %d for block %d\n", h->id[i], block);
			return h->id[i];
		}
	}
	uint32_t id=h->bdif->getChangeID(h->blkdev, block);
	idcacheSetInt(h, block, id, 0); //may just as well update our own cache...
	return id;
}



