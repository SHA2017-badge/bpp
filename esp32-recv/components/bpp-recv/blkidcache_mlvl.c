/*
Multi-level block cache. Keeps a few changeids in memory, plus the sectors which have
that changeid. Delegates to the underlying block device for everything else.
*/
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "blkidcache.h"
#include "bma.h"

#define LEVELS 5

void idcacheSetInt(BlkIdCacheHandle *h, int block, uint32_t id, int writeback);


struct BlkIdCacheHandle {
	BlockdevifHandle *blkdev;
	Bma *bmp[LEVELS];
	uint32_t id[LEVELS];
	int size;			//in blocks
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
		ret->bmp[i]=bmaCreate(size);
		ret->id[i]=0;
	}
	bdif->forEachBlock(blkdev, initCache, ret);
	return ret;
}

void idcacheFlushToStorage(BlkIdCacheHandle *h) {
	printf("Flushing idcache to storage.\n");
	for (int bl=0; bl<h->size; bl++) {
		for (int i=0; i<LEVELS; i++) {
			if (bmaIsSet(h->bmp[i], bl)) {
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
	int isSet=-1; //contains the level bit was set in, or -1 if not set
	for (int i=0; i<LEVELS; i++) {
//		printf("Lvl %d chid %d\n", i, h->id[i]);
		if (id != h->id[i]) {
			bmaSet(h->bmp[i], block, 0);
		} else {
			bmaSet(h->bmp[i], block, 1);
			isSet=i;
		}
	}

	if (isSet==-1) {
		//No level to store this. See if the ID is newer than any of the IDs we have already; if so we can kick
		//out the oldest and add this one.
		int oldest=0;
		for (int j=1; j<LEVELS; j++) {
			if (h->id[j] < h->id[oldest]) oldest=j;
		}
		if (h->id[oldest] < id) {
			h->id[oldest]=id;
			bmaSetAll(h->bmp[oldest], 0);
			bmaSet(h->bmp[oldest], block, 1);
			isSet=oldest;
		}
	}

	if (isSet==-1) {
		if (writeback) h->bdif->setChangeID(h->blkdev, block, id);
	}
	if (isSet!=-1 && h->bdif->notifyComplete) {
//		printf("Cache: lvl %d chid %d: ", isSet, h->id[isSet]);
//		bmaDump(h->bmp[isSet]);
		if (bmaIsAllSet(h->bmp[isSet])) {
			h->bdif->notifyComplete(h->blkdev, id);
		}
	}
}

void idcacheSetSectorData(BlkIdCacheHandle *h, int block, uint8_t *data, uint32_t id) {
	idcacheSetInt(h, block, id, 1);
	h->bdif->setSectorData(h->blkdev, block, data, id);

	static int setCtr=0;
	setCtr++;
	if (setCtr>=50) {
		idcacheFlushToStorage(h);
		setCtr=0;
	}
}

uint32_t idcacheGetLastChangeId(BlkIdCacheHandle *h) {
	uint32_t ret=0;
	for (int i=0; i<LEVELS; i++) {
		if (h->id[i] > ret) ret=h->id[i];
	}
	return ret;
}



uint32_t idcacheGet(BlkIdCacheHandle *h, int block) {
	for (int i=0; i<LEVELS; i++) {
		if (bmaIsSet(h->bmp[i], block)) {
//			printf("Got cached id %d for block %d\n", h->id[i], block);
			return h->id[i];
		}
	}
	printf("Blk %d not in cache.\n", block);
	uint32_t id=h->bdif->getChangeID(h->blkdev, block);
	idcacheSetInt(h, block, id, 0); //may just as well update our own cache...
	return id;
}



