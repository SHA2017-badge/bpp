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
	idcacheSet(h, blockno, changeId);
}

BlkIdCacheHandle *idcacheCreate(int size, BlockdevifHandle *blkdev, BlockdevIf *bdif) {
	BlkIdCacheHandle *ret=malloc(sizeof(BlkIdCacheHandle));
	ret->blkdev=blkdev;
	ret->size=size;
	ret->bdif=bdif;
	for (int i=0; i<LEVELS; i++) {
		ret->bmp[i]=malloc(size/8);
		ret->id[i]=0;
	}
	bdif->forEachBlock(blkdev, initCache, ret);
	return ret;
}

void idcacheFlushToStorage(BlkIdCacheHandle *h) {
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
	//Kill bit in all levels but the one that has the same id. Also check if the id may
	//be newer than anything we have.
	int isNewer=1;
	for (int i=0; i<LEVELS; i++) {
		if (id <= h->id[i]) isNewer=0;
		if (id != h->id[i]) {
			h->bmp[i][block/8] &= ~(1<<(block&7));
		} else {
			h->bmp[i][block/8] |= (1<<(block&7));
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
	}
}

void idcacheSetSectorData(BlkIdCacheHandle *h, int block, uint8_t *data, uint32_t id) {
	idcacheSet(h, block, id);
	h->bdif->setSectorData(h->blkdev, block, data, id);
}


uint32_t idcacheGet(BlkIdCacheHandle *h, int block) {
	for (int i=0; i<LEVELS; i++) {
		if (h->bmp[i][block/8] & (1<<(block&7))) return h->id[i];
	}
	return h->bdif->getChangeID(h->blkdev, block);
}



