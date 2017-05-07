#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/time.h>
#include <time.h>
#include <arpa/inet.h>
#include "structs.h"

#include "bppsource.h"

/*
Tool to sync one file to a remote one, using broadcast commands only.

It works like this: If we have a trickle of about 10KByte/second (or 1% of an 11MBit 
connection), it will take about 6 minutes to update an 4MB image. Thus, it's not 
smart to just stream that out: it will take a while for the thing to update, even 
if most of the stuff we have in the image is still very recent. Thus, we resort to 
a more pragmatic option of updating stuff: the most recently changed stuff is sent 
out most often, while the more static data is sent out less often.

Maybe the idea is to send out bitmaps. It comes down to this: Every update has a 
certain serial; an 'update' essentially is a steady-state of the file system. This
means there are multiple sectors that can have changed from update to update.

Now, we can go broadcast all sectors plus the update number they now have. This 
allows all badges to happily update to a certain update number provided they catch
all sectors of the update.

Some sectors don't change from update to update. How do we fix this? Well, we send out
bitmaps. Basically, we can say 'If you have these sectors with an update id of x or 
later, they are still current and you can immediately give them a new update ID'. The 
badges will need to wait for the rest of the updates to trickle in, but we can schedule
them later if needed.

Badges will wake every 60 sec. Updating takes (say) 2 sec max. This means the duty cycle
is 1/30th. Say 150mA for an update -> 5mA continuous use. For an 800mA battery, this is 
almost 7 days.

What we can do is intersperse the packet streams with a packet 'Next catalog in x mS'. Then,
after x ms, we can send bitmaps: 1 for the updates since 1 min ago, 1 since 5 min ago, 1 
since an hour ago etc. We then send these sectors in sequence: from last-updated to 
least-last-updated. We limit this to take a certain percentage of the available bandwidth 
for the minute, so we also have time and space to disperse other messages. In this 'remaining'
space, we can disperse 'live' messages as well as slowly trickle the older sectors to the 
devices that were away for a longer time. (Maybe even put a pointer to when these will start 
in a packet in the catalog.)


So basically:
:00
Bitmap for changes since 1 min ago
Bitmap for changes since 5 min ago
Bitmap for changes since 30 min ago
Time marker: streaming of older data starts at :25
Change 1 (1 min ago)
Change 2
Next catalog starts in x msec
Change 3
...
Next catalog starts in x msec
Change X (30 min ago
:25
Rotating older changes
Next catalog starts in x msec
More rotating older changes

:00
Bitmap etc

Nice thing: a bitmap fits in 128 bytes or so. Which means we can add a fairly large amount of bitmaps.
Idea: just schedule a fixed amount of updates and do a fixed amount of bitmaps as in-betweens? Hmm,
this does mean each badge will need to wait for a full update when the image changes >600K... Maybe 
implement a special mode for that.
*/

#define BLOCKSIZE 4096

#define POSTFIX_LASTPROCESSED ".lastprocessed"
#define POSTFIX_LASTPROCESSED_TMP ".timestamp_tmp"
#define POSTFIX_TIMESTAMP ".timestamp"
#define POSTFIX_TIMESTAMP_TMP ".timestamp_tmp"

char *blockfile, *prefix;

size_t fileSize;
uint8_t *fileContents;
uint32_t *fileTimestamps;
int bppCon;


#define CYCLE_MS (60*1000) //cycle every ms
#define PACKETS_PER_CYCLE 150
#define OLDPACKETS_PER_CYCLE 50
#define CATALOGMARKER_EVERY_PACKETS 8

static struct timeval cycleStart;
static int cyclePos=0;

static void startCycle() {
	gettimeofday(&cycleStart, NULL);
	cyclePos=0;
}

static void waitCycle(int i) {
	struct timeval tv;
	int64_t usec, usecTgt, waitTime;
	gettimeofday(&tv, NULL);
	usecTgt=cycleStart.tv_usec+(cycleStart.tv_sec*1000000L);
	usecTgt+=((CYCLE_MS*1000)/PACKETS_PER_CYCLE);
	usec=tv.tv_usec+(tv.tv_sec*1000000L);
	waitTime=usecTgt-usec;
	if (waitTime>0) usleep(waitTime);
}



//Read in block file, see which blocks have changed and update the timestamp data
//Does a fair amount of file juggling to make sure the state is recoverable if it
//is aborted at any time.
//Results in an updated fileContents and fileTimestamps variable.
void updateTimestamps() {
	char *newBuf=malloc(fileSize);
	time_t tstamp=time(NULL);
	int f=open(blockfile, O_RDONLY);
	if (f<=0) {
		perror("reading block file");
		exit(1);
	}
	int r=read(f, newBuf, fileSize);
	if (r!=fileSize) {
		perror("blockfile: Incomplete read");
		exit(1);
	}
	close(f);
	//Copy data over to lastprocessed_tmp file, so we know what data the timestamp
	//file corresponds to.
	char *fnbuf=malloc(strlen(prefix)+32);
	sprintf(fnbuf, "%s%s", prefix, POSTFIX_LASTPROCESSED_TMP);
	free(fnbuf);
	f=open(fnbuf, O_WRONLY|O_CREAT|O_TRUNC, 0644);
	if (f<=0) {
		perror(fnbuf);
		exit(1);
	}
	r=write(f, newBuf, fileSize);
	if (r!=fileSize) {
		perror(fnbuf);
		exit(1);
	}
	close(f);

	//Update timestamps
	for (int i=0; i<fileSize/BLOCKSIZE; i++) {
		if (memcmp(&fileContents[i*BLOCKSIZE], &newBuf[i*BLOCKSIZE], BLOCKSIZE)!=0) {
			fileTimestamps[i]=(uint32_t)tstamp;
		}
	}

	//Write timestamps to temp timestamp file
	sprintf(fnbuf, "%s%s", prefix, POSTFIX_TIMESTAMP_TMP);
	f=open(fnbuf, O_WRONLY|O_CREAT|O_TRUNC, 0644);
	if (f<=0) {
		perror(fnbuf);
		exit(1);
	}
	r=write(f, fileTimestamps, (fileSize/BLOCKSIZE)*sizeof(uint32_t));
	if (r!=fileSize) {
		perror(fnbuf);
		exit(1);
	}
	close(f);
	
	//Set new filecontents as current file contents
	free(fileContents);
	fileContents=newBuf;

	//Atomically swap tmpfiles over old tmpfiles
	char *fnbuf2=malloc(strlen(prefix)+32);
	sprintf(fnbuf, "%s%s", prefix, POSTFIX_TIMESTAMP_TMP);
	sprintf(fnbuf2, "%s%s", prefix, POSTFIX_TIMESTAMP);
	if (rename(fnbuf, fnbuf2)!=0) {
		printf("Renaming %s to %s:\n", fnbuf, fnbuf2);
		perror("error");
		exit(1);
	}
	sprintf(fnbuf, "%s%s", prefix, POSTFIX_LASTPROCESSED_TMP);
	sprintf(fnbuf2, "%s%s", prefix, POSTFIX_LASTPROCESSED);
	if (rename(fnbuf, fnbuf2)!=0) {
		printf("Renaming %s to %s:\n", fnbuf, fnbuf2);
		perror("error");
		exit(1);
	}

	free(fnbuf);
	free(fnbuf2);
}


//Send out a bitmap structure. Every bit indicates the status of a block: 1 if it's older than
//the timestamp indicated, 0 if it's newer.
//The idea is that if a client has a sector marked with 1 and an update id newer than ts, it can
//update its update ID to nowts without receiving any new data; the old data is still current.
void sendBitmapFor(uint32_t ts, uint32_t nowts) {
	BDPacketBitmap *p;
	int bitmapBytes=((fileSize/BLOCKSIZE)+7)/8;
	p=malloc(sizeof(BDPacketBitmap)+bitmapBytes);
	memset(p, 0, sizeof(BDPacketBitmap)+bitmapBytes);
	p->changeIdOrig=ts;
	p->changeIdNew=nowts;
	for (int i=0; i<(fileSize/BLOCKSIZE); i++) {
		if (fileTimestamps[i]<ts) p->bitmap[i/8]|=(1<<(i&7));
	}
	int r=bppSend(bppCon, BDSYNC_SUBTYPE_BITMAP, (uint8_t*)p, sizeof(BDPacketBitmap)+bitmapBytes);
	if (!r) {
		printf("Error sending bitmap packet!\n");
		exit(1);
	}
	free(p);
}

//Send a change for block i.
void sendChange(int i) {
	BDPacketChange *p;
	p=malloc(sizeof(BDPacketChange)+BLOCKSIZE);
	memcpy(p->data, &fileContents[i*BLOCKSIZE], BLOCKSIZE);
	p->changeId=htonl(fileTimestamps[i]);
	p->sector=htons(i);
	int r=bppSend(bppCon, BDSYNC_SUBTYPE_CHANGE, (uint8_t*)p, sizeof(BDPacketChange)+BLOCKSIZE);
	if (!r) {
		printf("Error sending bitmap packet!\n");
		exit(1);
	}
	free(p);
}

//Send a packet indicating roughly when the next catalog will be. We're sending packet i now.
void sendCatalogPtr(int i) {
	BDPacketCatalogPtr p;
	p.delayMs=htons((CYCLE_MS*(PACKETS_PER_CYCLE-i))/PACKETS_PER_CYCLE);
	int r=bppSend(bppCon, BDSYNC_SUBTYPE_CATALOGPTR, (uint8_t*)&p, sizeof(BDPacketCatalogPtr));
	if (!r) {
		printf("Error sending bitmap packet!\n");
		exit(1);
	}
}

void sendOlderMarker(uint32_t oldestNewTs, int secIdStart, int secIdEnd, int delayMs) {
	BDPacketOldermarker p;
	p.oldestNewTs=oldestNewTs;
	p.secIdStart=secIdStart;
	p.secIdEnd=secIdEnd;
	p.delayMs=delayMs;
	int r=bppSend(bppCon, BDSYNC_SUBTYPE_OLDERMARKER, (uint8_t*)&p, sizeof(BDPacketOldermarker));
	if (!r) {
		printf("Error sending bitmap packet!\n");
		exit(1);
	}
}

typedef struct {
	uint32_t ts;
	int block;
} SortedTs;

int compareSortedTs(const void *a, const void *b) {
	SortedTs *sa=(SortedTs*)a;
	SortedTs *sb=(SortedTs*)b;
	if (sa->ts==sb->ts) return (sa->block<sb->block)?-1:1;
	return (sa->ts<sb->ts)?-1:1;
}


void mainLoop() {
	int oldPacketPos=0;
	int bitmapTimes[]={
		60*1, 60*3, 60*5, 60*10, 60*15, 60*20, 60*30, 60*60,
		60*60*3, 60*60*12, 60*60*24, 60*60*48, 0
	};
	SortedTs *sortedTs=malloc(sizeof(SortedTs)*(fileSize/BLOCKSIZE));
	while(1) {
		updateTimestamps();

		//Send out the bitmap catalogue
		uint32_t currTs=(uint32_t)time(NULL);
		for (int i=0; bitmapTimes[i]!=0; i++) {
			sendBitmapFor(currTs-bitmapTimes[i], currTs);
		}

		//Sort timestamps in a descending order (newest-first) so we can send out data that has changed
		//the most recent the earliest.
		for (int i=0; i<fileSize/BLOCKSIZE; i++) {
			sortedTs[i].ts=fileTimestamps[i];
			sortedTs[i].block=i;
		}
		qsort(sortedTs, fileSize/BLOCKSIZE, sizeof(SortedTs), compareSortedTs);

		//Send oldermarker
		int lastPacketPos=oldPacketPos+OLDPACKETS_PER_CYCLE;
		if (lastPacketPos>=(fileSize/BLOCKSIZE)) oldPacketPos-=(fileSize/BLOCKSIZE); //wraparound
		sendOlderMarker(sortedTs[(PACKETS_PER_CYCLE-OLDPACKETS_PER_CYCLE)].ts, 
				oldPacketPos, lastPacketPos, 
				(CYCLE_MS*(PACKETS_PER_CYCLE-OLDPACKETS_PER_CYCLE))/PACKETS_PER_CYCLE);

		startCycle();
		int packet;
		//Send new packets first.
		for (packet=0; packet<(PACKETS_PER_CYCLE-OLDPACKETS_PER_CYCLE); packet++) {
			waitCycle(packet);
			if ((packet%CATALOGMARKER_EVERY_PACKETS)==0) sendCatalogPtr(packet);
			sendChange(sortedTs[packet].block);
		}
		//Followed by older packets.
		for (; packet<PACKETS_PER_CYCLE; packet++) {
			waitCycle(packet);
			if ((packet%CATALOGMARKER_EVERY_PACKETS)==0) sendCatalogPtr(packet);
			sendChange(oldPacketPos);
			oldPacketPos++;
			if (oldPacketPos>=(fileSize/BLOCKSIZE)) oldPacketPos=0;
		}
	}
}


int main(int argc, char **argv) {
	char *fnbuf;
	int r;
	if (argc<2) {
		printf("Usage: %s blockfile [stateprefix]\n", argv[0]);
		exit(0);
	}
	blockfile=argv[1];
	char *prefix=(argc>2)?argv[2]:argv[1];
	fnbuf=malloc(strlen(prefix)+32);

	bppCon=bppCreateConnection("localhost", 1);
	if (bppCon<=0) {
		perror("connecting to bppserver");
		exit(1);
	}

	//Open last-processed blockfile state
	sprintf(fnbuf, "%s%s", prefix, POSTFIX_LASTPROCESSED);
	int f=open(fnbuf, O_RDONLY);
	if (f<=0) {
		//No (valid) lastprocessed file. Use main file.
		printf("No valid lastprocessed file found. Using main file as starting point.\n");
		f=open(blockfile, O_RDONLY);
		if (f<=0) {
			perror(blockfile);
			exit(1);
		}
	}
	//Allocate buffers and read in file
	fileSize=lseek(f, 0, SEEK_END);
	lseek(f, 0, SEEK_SET);
	fileContents=malloc(fileSize);
	fileTimestamps=malloc(sizeof(uint32_t)*(fileSize/BLOCKSIZE));
	r=read(f, fileContents, fileSize);
	if (r!=fileSize) {
		printf("Eek! Couldn't load in entire lastprocessed file.");
		exit(1);
	}
	close(f);
	//Read in old timestamps
	sprintf(fnbuf, "%s%s", prefix, POSTFIX_TIMESTAMP);
	f=open(fnbuf, O_RDONLY);
	if (f<=0) {
		printf("Can't read blocktimestamp file; zeroing timestamps.");
		memset(fileTimestamps, 0, sizeof(uint32_t)*(fileSize/BLOCKSIZE));
	} else {
		read(f, fileTimestamps, sizeof(uint32_t)*(fileSize/BLOCKSIZE));
		close(f);
	}
	free(fnbuf);

	printf("Initialized. File size is %d K, %d blocks.\n", fileSize/1024, fileSize/BLOCKSIZE);
	mainLoop();
}

