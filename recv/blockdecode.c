#include <stdio.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <time.h>
#include "structs.h"
#include "hldemux.h"
#include "blockdevif.h"
#include "blockdecode.h"
#include "blkidcache.h"


#define ST_WAIT_FOR_CATALOGPTR 0
#define ST_WAIT_CATALOG 1
#define ST_WAIT_OLD 2
#define ST_WAIT_DATA 3

static int state=ST_WAIT_FOR_CATALOGPTR;
static time_t sleepingUntil=0;
static BlockdevifHandle *bdev;
static BlkIdCacheHandle *idcache;
static int noBlocks;
static int currentChangeID=0;

static int allBlocksUpToDate(int changeId) {
	for (int i=0; i<noBlocks; i++) {
		if (idcacheGet(idcache, i)<changeId) return 0;
	}
	return 1;
}


static void blockdecodeRecv(int subtype, uint8_t *data, int len) {
	if (time(NULL)<sleepingUntil) {
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
		currentChangeID=idNew;
		//Update current block map: all blocks newer than changeIdOrig are still up-to-date and
		//can be updated to changeIdNew.
		for (int i=0; i<noBlocks; i++) {
			if (p->bitmap[i/8]&(1<<(i&7))) {
				if (idcacheGet(idcache, i)>=idOld) idcacheSet(idcache, i, idNew);
			}
		}
		//See if that action updated all blocks
		if (allBlocksUpToDate(currentChangeID)) {
			//Yay, we can sleep.
			state=ST_WAIT_FOR_CATALOGPTR;
			printf("Got bitmap, still up-to-date.\n");
		} else {
			state=ST_WAIT_DATA;
			printf("Got bitmap.\n");
		}
	} else if (subtype==BDSYNC_SUBTYPE_OLDERMARKER) {
		BDPacketOldermarker *p=(BDPacketOldermarker*)data;
		//We're only interested in this if we actually need data.
		if (state!=ST_WAIT_FOR_CATALOGPTR && state!=ST_WAIT_CATALOG) {
			//See if we're interested in the new blocks. We are if our oldest block
			//is more recent than the oldest block sent in the new blocks.
			//First, find oldest change id.
			uint32_t oldest=0xFFFFFFFF;
			for (int i=0; i<noBlocks; i++) {
				int chgid=idcacheGet(idcache, i);
				if (chgid<oldest) oldest=chgid;
			}
			//Check if we're interested in the newer or older blocks, or neither.
			if (oldest>ntohl(p->oldestNewTs)) {
				printf("Blockdev: Grabbing new packets.\n");
				//We're not that far behind: all packets we need will be following this announcement
				//right now.
				state=ST_WAIT_DATA;
			} else {
				//We're behind: we need the rotation of older packets to get up-to-date.
				//See if the range that will be sent this run is any good.
				int needOldBlocks=0;
				for (int i=ntohs(p->secIdStart); i!=ntohs(p->secIdEnd); i++) {
					if (i>=noBlocks) i=0;
					if (idcacheGet(idcache, i)!=currentChangeID) {
						needOldBlocks=1;
						break;
					}
				}
				if (needOldBlocks) {
					sleepingUntil=time(NULL)+(ntohl(p->delayMs)/1000)-1;
					printf("Blockdecode: Skipping new packets. Sleeping %d ms.\n", ntohl(p->delayMs));
					state=ST_WAIT_DATA;
				} else {
					printf("Blockdev: Don't need any packets in this cycle. Sleeping\n");
					//Next cycle is entirely useless. Wait for next catalog marker so we can sleep until
					//the next catalog comes.
					state=ST_WAIT_FOR_CATALOGPTR;
				}
			}
		}
	} else if (subtype==BDSYNC_SUBTYPE_CHANGE) {
		if (state != ST_WAIT_FOR_CATALOGPTR) {
			BDPacketChange *p=(BDPacketChange*)data;
			if (ntohl(p->changeId) != currentChangeID) {
				//Huh? Must've missed an entire catalog...
				printf("Data changeid %d, last changeid I know of %d. Sleeping this cycle.\n", ntohl(p->changeId), currentChangeID);
				state=ST_WAIT_FOR_CATALOGPTR;
			}
			int blk=ntohs(p->sector);
			if (idcacheGet(idcache, blk)!=currentChangeID) {
				//Write block
				blockdevifSetSectorData(bdev, blk, p->data, ntohl(p->changeId));
				printf("Blockdecode: Got change for block %d. Writing to disk.\n", blk);
			} else {
				printf("Blockdecode: Got change for block %d. Already had this change.\n", blk);
			}
			//See if we have everything we need.
			if (allBlocksUpToDate(currentChangeID)) {
				//Yay, we can sleep.
				printf("Blockdecode: Received change final packet. Waiting for catalog ptr to sleep.\n");
				state=ST_WAIT_FOR_CATALOGPTR;
			}
		}
	} else if (subtype==BDSYNC_SUBTYPE_CATALOGPTR) {
		if (state == ST_WAIT_FOR_CATALOGPTR) {
			BDPacketCatalogPtr *p=(BDPacketCatalogPtr*)data;
			state=ST_WAIT_CATALOG;
			printf("Blockdecode: Got catalog ptr. Sleeping %d sec.\n", ntohl(p->delayMs)/1000);
			sleepingUntil=time(NULL)+(ntohl(p->delayMs)/1000)-1;
		}
	}
}

void blockdecodeInit(int size) {
	hldemuxAddType(HLPACKET_TYPE_BDSYNC, blockdecodeRecv, NULL);
	bdev=blockdevifInit("tst/blockdev", size);
	if (bdev==NULL) {
		exit(1);
	}
	noBlocks=size/BLOCKDEV_BLKSZ;
	idcache=idcacheCreate(noBlocks, bdev);
}

