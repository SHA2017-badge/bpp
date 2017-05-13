#include <stdio.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <time.h>
#include <string.h>
#include "structs.h"
#include "hldemux.h"
#include "blockdevif.h"
#include "blockdecode.h"
#include "blkidcache.h"


#define ST_WAIT_FOR_CATALOGPTR 0
#define ST_WAIT_CATALOG 1
#define ST_WAIT_OLD 2
#define ST_WAIT_DATA 3


typedef struct {
	int state;
	time_t sleepingUntil;
	BlockdevifHandle *bdev;
	BlockdevIf *bdif;
	BlkIdCacheHandle *idcache;
	int noBlocks;
	int currentChangeID;
} BlockDecodeHandle;


static int allBlocksUpToDate(BlockDecodeHandle *d) {
	for (int i=0; i<d->noBlocks; i++) {
		if (idcacheGet(d->idcache, i) < d->currentChangeID) return 0;
	}
	return 1;
}


static void blockdecodeRecv(int subtype, uint8_t *data, int len, void *arg) {
	BlockDecodeHandle *d=(BlockDecodeHandle*)arg;
	if (time(NULL) < d->sleepingUntil) {
		//Still sleeping.
		const char *tp="unknown";
		if (subtype==BDSYNC_SUBTYPE_BITMAP) tp="bitmap";
		if (subtype==BDSYNC_SUBTYPE_OLDERMARKER) tp="oldermarker";
		if (subtype==BDSYNC_SUBTYPE_CHANGE) tp="change";
		if (subtype==BDSYNC_SUBTYPE_CATALOGPTR) tp="catalogptr";
		printf("Blockdecode: zzzzzzz... (ignoring type %s)\n", tp);
		return;
	}

	if (subtype==BDSYNC_SUBTYPE_BITMAP) {
		BDPacketBitmap *p=(BDPacketBitmap*)data;
		uint32_t idOld=ntohl(p->changeIdOrig);
		uint32_t idNew=ntohl(p->changeIdNew);
		printf("Got bitmap.\n");
		d->currentChangeID=idNew;
		//Update current block map: all blocks newer than changeIdOrig are still up-to-date and
		//can be updated to changeIdNew.
		for (int i=0; i<d->noBlocks; i++) {
			if (p->bitmap[i/8]&(1<<(i&7))) {
				if (idcacheGet(d->idcache, i)>=idOld) idcacheSet(d->idcache, i, d->currentChangeID);
			}
		}
		//See if that action updated all blocks
		if (allBlocksUpToDate(d)) {
			//Yay, we can sleep.
			d->state=ST_WAIT_FOR_CATALOGPTR;
			printf("Got bitmap, still up-to-date.\n");
		} else {
			d->state=ST_WAIT_DATA;
			printf("Got bitmap.\n");
		}
	} else if (subtype==BDSYNC_SUBTYPE_OLDERMARKER) {
		BDPacketOldermarker *p=(BDPacketOldermarker*)data;
		//We're only interested in this if we actually need data.
		if (d->state!=ST_WAIT_FOR_CATALOGPTR && d->state!=ST_WAIT_CATALOG) {
			//See if we're interested in the new blocks. We are if our oldest block
			//is more recent than the oldest block sent in the new blocks.
			//First, find oldest change id.
			uint32_t oldest=0xFFFFFFFF;
			for (int i=0; i<d->noBlocks; i++) {
				int chgid=idcacheGet(d->idcache, i);
				if (chgid<oldest) oldest=chgid;
			}
			//Check if we're interested in the newer or older blocks, or neither.
			if (oldest>ntohl(p->oldestNewTs)) {
				printf("Blockdev: Grabbing new packets.\n");
				//We're not that far behind: all packets we need will be following this announcement
				//right now.
				d->state=ST_WAIT_DATA;
			} else {
				//We're behind: we need the rotation of older packets to get up-to-date.
				//See if the range that will be sent this run is any good.
				int needOldBlocks=0;
				for (int i=ntohs(p->secIdStart); i!=ntohs(p->secIdEnd); i++) {
					if (i>=d->noBlocks) i=0;
					if (idcacheGet(d->idcache, i)!=d->currentChangeID) {
						needOldBlocks=1;
						break;
					}
				}
				if (needOldBlocks) {
					d->sleepingUntil=time(NULL)+(ntohl(p->delayMs)/1000)-1;
					printf("Blockdecode: Skipping new packets. Sleeping %d ms.\n", ntohl(p->delayMs));
					d->state=ST_WAIT_DATA;
				} else {
					printf("Blockdev: Don't need any packets in this cycle. Sleeping\n");
					//Next cycle is entirely useless. Wait for next catalog marker so we can sleep until
					//the next catalog comes.
					d->state=ST_WAIT_FOR_CATALOGPTR;
				}
			}
		}
	} else if (subtype==BDSYNC_SUBTYPE_CHANGE) {
		if (d->state != ST_WAIT_FOR_CATALOGPTR) {
			BDPacketChange *p=(BDPacketChange*)data;
			if (ntohl(p->changeId) != d->currentChangeID) {
				//Huh? Must've missed an entire catalog...
				printf("Data changeid %d, last changeid I know of %d. Sleeping this cycle.\n", ntohl(p->changeId), d->currentChangeID);
				d->state=ST_WAIT_FOR_CATALOGPTR;
			}
			int blk=ntohs(p->sector);
			if (idcacheGet(d->idcache, blk)>d->currentChangeID) {
				printf("WtF? Got newer block than sent? (us: %d, remote: %d)\n", idcacheGet(d->idcache, blk), d->currentChangeID);
			} else if (idcacheGet(d->idcache, blk)!=d->currentChangeID) {
				//Write block
				d->bdif->setSectorData(d->bdev, blk, p->data, ntohl(p->changeId));
				printf("Blockdecode: Got change for block %d. Writing to disk.\n", blk);
			} else {
				printf("Blockdecode: Got change for block %d. Already had this change.\n", blk);
			}
			//See if we have everything we need.
			if (allBlocksUpToDate(d)) {
				//Yay, we can sleep.
				printf("Blockdecode: Received change final packet. Waiting for catalog ptr to sleep.\n");
				d->state=ST_WAIT_FOR_CATALOGPTR;
			}
		}
	} else if (subtype==BDSYNC_SUBTYPE_CATALOGPTR) {
		if (d->state == ST_WAIT_FOR_CATALOGPTR) {
			BDPacketCatalogPtr *p=(BDPacketCatalogPtr*)data;
			d->state=ST_WAIT_CATALOG;
			printf("Blockdecode: Got catalog ptr. Sleeping %d sec.\n", ntohl(p->delayMs)/1000);
			d->sleepingUntil=time(NULL)+(ntohl(p->delayMs)/1000)-1;
		}
	}
}

int blockdecodeInit(int type, int size, BlockdevIf *bdIf, char *bdevdesc) {
	BlockDecodeHandle *d=malloc(sizeof(BlockDecodeHandle));
	if (d==NULL) {
		return 0;
	}
	memset(d, 0, sizeof(BlockDecodeHandle));
	d->bdev=bdIf->init(bdevdesc, size);
	if (d->bdev==NULL) {
		free(d);
		return 0;
	}
	d->state==ST_WAIT_FOR_CATALOGPTR;
	d->noBlocks=size/BLOCKDEV_BLKSZ;
	d->idcache=idcacheCreate(d->noBlocks, d->bdev, bdIf);
	d->bdif=bdIf;

	hldemuxAddType(type, blockdecodeRecv, d);
	return 1;
}

