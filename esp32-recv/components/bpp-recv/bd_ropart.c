/*
Blockdev iface for a block dev that is incrementally updated (eg a filesystem)
*/
#include <stdio.h>
#include <stdint.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include "esp_partition.h"
#include "structs.h"
#include "blockdevif.h"
#include "bd_ropart.h"
#include "esp_system.h"


//Change indicator. Special: if physSector and virtSector are both 0xff, it indicates the changeId as indicated is entirely available in memory.
//(That is: if you take the most recent sectors that have the indicated changeId and below, you have the full state of the filesystem at that
//point in time.
typedef struct {
	uint32_t changeId;
	unsigned int physSector:12;
	unsigned int virtSector:12;
	uint8_t chsum;				//Should be to all the bytes in this struct added with carry-wraparound, with chsum=0 during calculation
}   __attribute__ ((packed)) FlashSectorDesc;
//Desc is 8 bytes, so I should be able to fit 512 in a 4K block...


struct BlockdevifHandle {
	int size;	//in blocks; after this the descs start
	int fsSize;	//in blocks; size of the virtual fs.
	const esp_partition_t *part;
	int descDataSizeBlks; //In blocks
	spi_flash_mmap_handle_t descDataHandle;
	const FlashSectorDesc* descs;	//Mmapped sector descs
	int descPos;			//Position to write next desc (tail of descs)
	int descStart;			//1st desc (head of descs). Should be at the start of a block.
	char* freeBitmap;		//Bitmap of sectors that are still free
};

/*
This essentially is a journalling filesystem, where every sector is tagged with a ChangeID. Essentially, the
sector descriptions are stored on-flash as one big array that is written to in a ringbuffer fashion. If a
sector is written, it is written to a random free sector, and an entry is added to the ringbuffer to associate
the physical sector with the virtual sector.
*/

static bool descValid(const FlashSectorDesc *desc) {
	uint8_t *data=(uint8_t*)desc;
	int chs=0;
	for (int i=0; i<sizeof(FlashSectorDesc); i++) chs+=data[i];
	if (chs==sizeof(FlashSectorDesc)*0xff) {
		return true; //erased sector is also valid
	}
	chs-=desc->chsum; //chsum is calculated with chsum field itself == 0
	chs=(chs&0xff)+(chs>>8);
	return (chs==desc->chsum);
}

static bool descEmpty(const FlashSectorDesc *desc) {
	uint8_t *data=(uint8_t*)desc;
	for (int i=0; i<sizeof(FlashSectorDesc); i++) {
		if (data[i]!=0xff) return false;
	}
	return true;
}

//Shorthand routine to calculate the amount of descs in the desc space of the fs.
static inline int descCount(BlockdevifHandle *h) {
	return (h->descDataSizeBlks*BLOCKDEV_BLKSZ)/sizeof(FlashSectorDesc);
}

static void markPhysUsed(BlockdevifHandle *h, int physSect) {
	h->freeBitmap[physSect/8]&=~(1<<(physSect&7));
}

static void markPhysFree(BlockdevifHandle *h, int physSect) {
	h->freeBitmap[physSect/8]|=(1<<(physSect&7));
}

static BlockdevifHandle *blockdevifInit(void *desc, int size) {
	BlockdevIfRoPartDesc *bdesc=(BlockdevIfRoPartDesc*)desc;
	BlockdevifHandle *h=malloc(sizeof(BlockdevifHandle));
	if (h==NULL) goto error2;
	//Make sure FlashSectorDesc fits in BLOCKDEV_BLKSZ an integer amount of times
	_Static_assert(BLOCKDEV_BLKSZ%sizeof(FlashSectorDesc)==0, "FlashSectorDesc does not fit in BLOCKDEV_BLKSZ an integer amount of times!");

	h->part=esp_partition_find_first(bdesc->major, bdesc->minor, NULL);
	if (h->part==NULL) goto error2;
	if (h->part->size < size+BLOCKDEV_BLKSZ) {
		printf("bd_flatflash: Part 0x%X-0x%X is %d bytes. Need %d bytes.\n", bdesc->major, bdesc->minor, h->part->size, size+BLOCKDEV_BLKSZ);
		goto error2;
	}

	h->descDataSizeBlks=(size/BLOCKDEV_BLKSZ*sizeof(FlashSectorDesc))/BLOCKDEV_BLKSZ+2;
	h->size=(h->part->size/BLOCKDEV_BLKSZ)-h->descDataSizeBlks;
	h->fsSize=(size+BLOCKDEV_BLKSZ-1)/BLOCKDEV_BLKSZ;
	if (h->fsSize*BLOCKDEV_BLKSZ < size) {
		printf("Can't put fs of %d blocks on rodata part with %d blocks available!\n", size/BLOCKDEV_BLKSZ, h->fsSize);
		goto error2;
	}

	printf("bd_ropart: Managing partition of %d blocks (%dK). Bdesc is %d blocks, reserved len is %d blocks (%d K), fs size is %d blocks (%dK).\n",
			h->part->size/BLOCKDEV_BLKSZ, h->part->size/1024, h->descDataSizeBlks, h->size-h->fsSize, (h->size-h->fsSize)*4, h->fsSize, h->fsSize*4);

	//Mmap page descriptor buffer
	esp_err_t r=esp_partition_mmap(h->part, h->size*BLOCKDEV_BLKSZ, h->descDataSizeBlks*BLOCKDEV_BLKSZ, SPI_FLASH_MMAP_DATA, (const void**)&h->descs, &h->descDataHandle);
	if (r!=ESP_OK) {
		printf("Couldn't map page descriptors!\n");
		goto error2;
	}
	
	//Check the page descriptors. If there's only crap there, we probably need to re-initialize the fs.
	int invalidDescs=0;
	for (int i=0; i<(h->descDataSizeBlks*BLOCKDEV_BLKSZ)/sizeof(FlashSectorDesc); i++) {
		if (!descValid(&h->descs[i])) invalidDescs++;
	}
	if (invalidDescs>10) {
		printf("bd_ropart: Filesystem has %d invalid sectors. Assuming uninitialized; initializing.\n", invalidDescs);
		r=esp_partition_erase_range(h->part, h->size*BLOCKDEV_BLKSZ, h->descDataSizeBlks*BLOCKDEV_BLKSZ);
		assert(r==ESP_OK);
	}
	
	//allocate free bitmap; mark all as free
	h->freeBitmap=malloc((h->size+7)/8);
	if (h->freeBitmap==NULL) goto error2;
	memset(h->freeBitmap, 0xff, (h->size+7)/8);
	
	//We need to find the start and the end of the journal. Start is empty -> nonempty, valid is nonempty->empty.
	h->descPos=0;
	h->descStart=0;
	bool prevEmpty=descEmpty(&h->descs[descCount(h)-1]);
	for (int i=0; i<descCount(h); i++) {
		bool curEmpty=descEmpty(&h->descs[i]);
		if (curEmpty && !prevEmpty) {
			h->descPos=i;
		} else if (prevEmpty && !curEmpty) {
			h->descPos=i;
		}
		prevEmpty=curEmpty;
		//See if this desc is valid and non-free. If so, the phys sector is in use and we can mark it as
		//such.
		if (!curEmpty && descValid(&h->descs[i])) {
			markPhysUsed(h, i);
		}
	}
	
	int freeDescs=h->descPos-h->descStart;
	if (freeDescs<0) freeDescs+=descCount(h);
	printf("bd_ropart: %d descriptors, of which %d in use.\n", descCount(h), freeDescs);
	//ToDo: what if the free count is not enough? Attempt a cleanup?

	int descstartOffsetBlockstart=h->descStart%(BLOCKDEV_BLKSZ/sizeof(FlashSectorDesc));
	if (descstartOffsetBlockstart != 0) {
		printf("bd_ropart: Warning: descStart isn't on a block boundary! Correcting.\n");
		h->descStart+=(BLOCKDEV_BLKSZ/sizeof(FlashSectorDesc))-descstartOffsetBlockstart;
	}

	return h;

error2:
	free(h);
	return NULL;
}


//Will return the most recent descriptor describing Vsector vsect, or -1 if none found.
static int lastDescForVsect(BlockdevifHandle *h, int vsect) {
	int i=h->descPos;
	while (i!=h->descStart) {
		i--;
		if (i<0) i=descCount(h)-1;
		if (descValid(&h->descs[i]) && h->descs[i].virtSector==vsect) {
			return i;
		}
	}
	return -1;
}

static void writeNewDescNoClean(BlockdevifHandle *h, const FlashSectorDesc *d) {
	assert(h->descStart!=h->descPos);
	FlashSectorDesc newd;
	memcpy(&newd, d, sizeof(FlashSectorDesc));

	//Make checksum valid.
	newd.chsum=0;
	int chs=0;
	uint8_t *bytes=(uint8_t*)&newd;
	for (int i=0; i<sizeof(FlashSectorDesc); i++) {
		chs+=bytes[i];
	}
	chs=(chs&0xff)+(chs>>8);
	newd.chsum=chs;

	//Write to flash
	esp_err_t r=esp_partition_write(h->part, (h->size*BLOCKDEV_BLKSZ)+(h->descPos*sizeof(FlashSectorDesc)), &newd, sizeof(FlashSectorDesc));
	printf("bd_ropart: Written desc %d\n", h->descPos);
	h->descPos++;
	assert(r==ESP_OK);
}

/*
This routine will do a cleanup of the journal: it will take the block where the tail of the descs journal is located
and will write only the entries that are still up-to-date to the head of the journal.
*/
static void doCleanup(BlockdevifHandle *h) {
	int descsPerBlock=(BLOCKDEV_BLKSZ/sizeof(FlashSectorDesc));
	int tailblkEnd=(h->descStart/descsPerBlock+1)*descsPerBlock;
	printf("bd_ropart: Starting cleanup of desc %d to %d...\n", h->descStart, tailblkEnd);
	int rel=0, dis=0;
	for (int i=h->descStart; i<tailblkEnd; i++) {
		if (descValid(&h->descs[i]) && !descEmpty(&h->descs[i])) {
			int mostRecent=lastDescForVsect(h, h->descs[i].virtSector);
			assert(mostRecent>=0); //can't be null because it always can return the sector we're looking at.
			if (mostRecent==i) {
				//Descriptor is still actual. Re-write.
				writeNewDescNoClean(h, &h->descs[i]);
				rel++;
			} else {
				//Sector itself shouldn't be referenced anymore.
				markPhysFree(h, h->descs[i].physSector);
				dis++;
			}
		}
	}
	printf("bd_ropart: Cleanup done. Of %d descs, relocated %d and discarded %d valid ones.\n", descsPerBlock, rel, dis);
	//Okay, all data in block should be safe now. Nuke block.
	esp_err_t r=esp_partition_erase_range(h->part, (h->size+h->descStart/descsPerBlock)*BLOCKDEV_BLKSZ, BLOCKDEV_BLKSZ);
	//We can shift start pointer by a block.
	h->descStart=tailblkEnd;
	if (h->descStart >= descCount(h)) h->descStart=0;
	assert(r==ESP_OK);
}

static int freeDescs(BlockdevifHandle *h) {
	int freeDescNo;
	freeDescNo= h->descStart - h->descPos;
	if (freeDescNo<0) freeDescNo+=descCount(h);
	return freeDescNo;
}


//Writes a new descriptor. Runs a cleanup cycle if needed.
static void writeNewDesc(BlockdevifHandle *h, FlashSectorDesc *d) {
	int tries=0;
	while (freeDescs(h)<(BLOCKDEV_BLKSZ/sizeof(FlashSectorDesc))) {
		printf("bd_ropart: Less than one block (%d) of descs left. Doing cleanup...\n", freeDescs(h));
		doCleanup(h);
		printf("bd_ropart: %d descs left.\n", freeDescs(h));
		tries++;
		assert(tries<h->descDataSizeBlks);
	}
	writeNewDescNoClean(h, d);
}


static void blockdevifSetChangeID(BlockdevifHandle *h, int sector, uint32_t changeId) {
	int i=lastDescForVsect(h, sector);
	if (i==-1) {
		printf("bd_ropart: requested new changeid for sector %d but sector isn't written yet!\n", sector);
		return;
	}
	
	if (h->descs[i].changeId==changeId) return; //nothing needs to be done
	FlashSectorDesc newd;
	memcpy(&newd, &h->descs[i], sizeof(FlashSectorDesc));
	newd.changeId=changeId;
	writeNewDesc(h, &newd);
}



static uint32_t blockdevifGetChangeID(BlockdevifHandle *h, int sector) {
	int i=lastDescForVsect(h, sector);
	if (i==-1) return 0;
	return h->descs[i].changeId;
}

static int blockdevifGetSectorData(BlockdevifHandle *h, int sector, uint8_t *buff) {
	int i=lastDescForVsect(h, sector);
	if (i==-1) return 0;
	esp_err_t r=esp_partition_read(h->part, h->descs[i].physSector*BLOCKDEV_BLKSZ, buff, BLOCKDEV_BLKSZ);
	return (r==ESP_OK);
}

static int blockdevifSetSectorData(BlockdevifHandle *h, int sector, uint8_t *buff, uint32_t adv_id) {
	static int searchPos=0;
	if (sector>=h->fsSize) {
		printf("bt_ropart: Huh? Trying to write sector %d\n", sector);
		return false;
	}

	int tries=0;
	int i;
	while(1) {
		//Find free block
		i=searchPos+1;
		while (i!=searchPos) {
			if (i>=h->size) i=0;
			if (h->freeBitmap[i/8] & (1<<(i&7))) break;
			printf("Desc %d in use.\n", i);
			i++;
		}
		if (i!=searchPos) break; //found a free sector
		//No free sector found. Try a cleanup run to free up a sector.
		doCleanup(h);
		tries++;
		assert(tries<h->descDataSizeBlks);
	}
	searchPos=i;

	markPhysUsed(h, i);
	FlashSectorDesc newd;
	newd.changeId=adv_id;
	newd.physSector=i;
	newd.virtSector=sector;
	writeNewDesc(h, &newd);

	esp_partition_erase_range(h->part, i*BLOCKDEV_BLKSZ, BLOCKDEV_BLKSZ);
	esp_partition_write(h->part, i*BLOCKDEV_BLKSZ, buff, BLOCKDEV_BLKSZ);

	return 1;
}

static void blockdevifForEachBlock(BlockdevifHandle *handle, BlockdevifForEachBlockFn *cb, void *arg) {
	for (int i=0; i<handle->fsSize; i++) {
		int j=lastDescForVsect(handle, i);
		if (j>=0) {
			uint32_t chid=handle->descs[j].changeId;
			cb(i, chid, arg);
		} else {
			cb(i, 0, arg);
		}
	}
}

BlockdevIf blockdevIfRoPart={
	.init=blockdevifInit,
	.setChangeID=blockdevifSetChangeID,
	.getChangeID=blockdevifGetChangeID,
	.getSectorData=blockdevifGetSectorData,
	.setSectorData=blockdevifSetSectorData,
	.forEachBlock=blockdevifForEachBlock
};






