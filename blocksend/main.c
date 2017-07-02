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
#include "ini.h"
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
#define POSTFIX_LASTPROCESSED_TMP ".lastprocessed_tmp"
#define POSTFIX_TIMESTAMP ".timestamp"
#define POSTFIX_TIMESTAMP_TMP ".timestamp_tmp"

typedef struct {
	char *file;
	int streamid;
	int size;
	char* stateprefix;
	int packetspermin;
	int pctoldpackets;
	int blockflashtimems;
} Config;

Config myConfig;

size_t fileSize;
uint8_t *fileContents;
uint32_t *fileTimestamps;
int bppCon;

int noBlocks() {
	return ((fileSize+BLOCKSIZE-1)/BLOCKSIZE);
}


//Read in block file, see which blocks have changed and update the timestamp data
//Does a fair amount of file juggling to make sure the state is recoverable if it
//is aborted at any time.
//If not updated, returns 0 and updates nothing.
//If updated, results in an updated fileContents and fileTimestamps variable.
uint32_t updateTimestamps() {
	time_t tstamp=time(NULL);
	int f=open(myConfig.file, O_RDONLY);
	if (f<=0) {
		perror("reading block file");
		exit(1);
	}
	fileSize=lseek(f, 0, SEEK_END);
	if (fileSize>myConfig.size) {
		printf("ERROR: File size bigger than size configured! Truncating.\n");
		fileSize=myConfig.size;
	}
	char *newBuf=malloc(myConfig.size);
	memset(newBuf, 0xff, noBlocks()*BLOCKSIZE);
	lseek(f, 0, SEEK_SET);
	int r=read(f, newBuf, fileSize);
	if (r!=fileSize) {
		perror("blockfile: Incomplete read");
		exit(1);
	}
	close(f);
	//Copy data over to lastprocessed_tmp file, so we know what data the timestamp
	//file corresponds to.
	char *fnbuf=malloc(strlen(myConfig.stateprefix)+32);
	sprintf(fnbuf, "%s%s", myConfig.stateprefix, POSTFIX_LASTPROCESSED_TMP);
	f=open(fnbuf, O_WRONLY|O_CREAT|O_TRUNC, 0644);
	if (f<=0) {
		printf("%s: ", fnbuf);
		perror("opening lastprocessed_tmp");
		exit(1);
	}
	r=write(f, newBuf, fileSize);
	if (r!=fileSize) {
		printf("%s: ", fnbuf);
		perror("writing lastprocessed_tmp");
		exit(1);
	}
	close(f);

	//Update timestamps
	int fileChanged=0;
	for (int i=0; i<noBlocks(); i++) {
		if (memcmp(&fileContents[i*BLOCKSIZE], &newBuf[i*BLOCKSIZE], BLOCKSIZE)!=0) {
			printf("updateTimestamps: block %d updated.\n", i);
			fileTimestamps[i]=(uint32_t)tstamp;
			fileChanged=1;
		}
	}
	if (!fileChanged) {
		//Nothing changed; no update needed.
		printf("updateTimestamps: source file didn't change.\n");
		free(fnbuf);
		free(newBuf);
		return 0;
	}

	//Write timestamps to temp timestamp file
	sprintf(fnbuf, "%s%s", myConfig.stateprefix, POSTFIX_TIMESTAMP_TMP);
	f=open(fnbuf, O_WRONLY|O_CREAT|O_TRUNC, 0644);
	if (f<=0) {
		printf("%s: ", fnbuf);
		perror("opening timestamp_tmp");
		exit(1);
	}
	r=write(f, fileTimestamps, (noBlocks())*sizeof(uint32_t));
	if (r!=(noBlocks())*sizeof(uint32_t)) {
		printf("%s: (%d/%zu)", fnbuf, r, fileSize);
		perror("writing timestamp_tmp");
		exit(1);
	}
	close(f);
	
	//Set new filecontents as current file contents
	free(fileContents);
	fileContents=newBuf;

	//Atomically swap tmpfiles over old tmpfiles
	char *fnbuf2=malloc(strlen(myConfig.stateprefix)+32);
	sprintf(fnbuf, "%s%s", myConfig.stateprefix, POSTFIX_TIMESTAMP_TMP);
	sprintf(fnbuf2, "%s%s", myConfig.stateprefix, POSTFIX_TIMESTAMP);
	if (rename(fnbuf, fnbuf2)!=0) {
		printf("Renaming %s to %s:\n", fnbuf, fnbuf2);
		perror("error");
		exit(1);
	}
	sprintf(fnbuf, "%s%s", myConfig.stateprefix, POSTFIX_LASTPROCESSED_TMP);
	sprintf(fnbuf2, "%s%s", myConfig.stateprefix, POSTFIX_LASTPROCESSED);
	if (rename(fnbuf, fnbuf2)!=0) {
		printf("Renaming %s to %s:\n", fnbuf, fnbuf2);
		perror("error");
		exit(1);
	}

	free(fnbuf);
	free(fnbuf2);
	return tstamp;
}


//Send out a bitmap structure. Every bit indicates the status of a block: 1 if it's older than
//the timestamp indicated, 0 if it's newer.
//The idea is that if a client has a sector marked with 1 and an update id newer than ts, it can
//update its update ID to nowts without receiving any new data; the old data is still current.
void sendBitmapFor(uint32_t ts, uint32_t nowts) {
	BDPacketBitmap *p;
	int bitmapBytes=((noBlocks())+7)/8;
	p=malloc(sizeof(BDPacketBitmap)+bitmapBytes);
	memset(p, 0, sizeof(BDPacketBitmap)+bitmapBytes);
	p->changeIdOrig=htonl(ts);
	p->changeIdNew=htonl(nowts);
	p->noBits=htons(noBlocks());
	int i;
	for (i=0; i<(noBlocks()); i++) {
		if (fileTimestamps[i]<ts) p->bitmap[i/8]|=(1<<(i&7));
	}
	//Fill last bits of byte with FF
	if ((i&7)!=0) p->bitmap[i/8]|=(0xff<<(i&7));
	int r=bppSend(bppCon, BDSYNC_SUBTYPE_BITMAP, (uint8_t*)p, sizeof(BDPacketBitmap)+bitmapBytes);
	if (!r) {
		printf("Error sending bitmap packet!\n");
		exit(1);
	}
	free(p);
}

//Send a change for block i.
void sendChange(int i, uint32_t changeId) {
	BDPacketChange *p;
	p=malloc(sizeof(BDPacketChange)+BLOCKSIZE);
	memcpy(p->data, &fileContents[i*BLOCKSIZE], BLOCKSIZE);
	p->changeId=htonl(changeId);
	p->sector=htons(i);
	bppSet(bppCon, 'W', myConfig.blockflashtimems);
	int r=bppSend(bppCon, BDSYNC_SUBTYPE_CHANGE, (uint8_t*)p, sizeof(BDPacketChange)+BLOCKSIZE);
	if (!r) {
		printf("Error sending bitmap packet!\n");
		exit(1);
	}
	free(p);
}

void sendOlderMarker(uint32_t oldestNewTs, int secIdStart, int secIdEnd, int delayMs) {
	BDPacketOldermarker p;
	p.oldestNewTs=htonl(oldestNewTs);
	p.secIdStart=htons(secIdStart);
	p.secIdEnd=htons(secIdEnd);
	p.delayMs=htonl(delayMs);
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

//for qsort
int compareSortedTs(const void *a, const void *b) {
	SortedTs *sa=(SortedTs*)a;
	SortedTs *sb=(SortedTs*)b;
	if (sa->ts==sb->ts) return (sa->block<sb->block)?-1:1;
	return (sa->ts>sb->ts)?-1:1;
}



void waitTilRemaining(int timeMs) {
	int remainingMs;
	bppQuery(bppCon, 'e', &remainingMs);
	if (remainingMs>(timeMs*3)) {
		//Assume clock has rolled over; return immediately
		return;
	}
	if (timeMs<remainingMs) {
		usleep((remainingMs-timeMs)*1000);
	}
}


void mainLoop() {
	int oldPacketPos=0;
	uint32_t currId=(uint32_t)time(NULL);
	time_t bitmapTimes[]={
		60*1, 60*3, 60*5, 60*10, 60*15, 60*20, 60*30, 60*60,
		60*60*3, 60*60*12, 60*60*24, 60*60*48, 0
	};
	SortedTs *sortedTs=malloc(sizeof(SortedTs)*(noBlocks()));
	while(1) {
		printf("Updating timestamps.\n");
		int newId=updateTimestamps();
		//Only change current ID if file actually updated.
		if (newId!=0) currId=newId;

		printf("Send out bitmap catalogue\n");
		//Send out the bitmap catalogue
		for (int i=0; bitmapTimes[i]!=0; i++) {
			sendBitmapFor(time(NULL)-bitmapTimes[i], currId);
		}

		//Sort timestamps in a descending order (newest-first) so we can send out data that has changed
		//the most recent the earliest.
		for (int i=0; i<noBlocks(); i++) {
			sortedTs[i].ts=fileTimestamps[i];
			sortedTs[i].block=i;
		}
		qsort(sortedTs, noBlocks(), sizeof(SortedTs), compareSortedTs);

		//Decide how many new and old packets we can send out.
		int remainingMs;
		bppQuery(bppCon, 'e', &remainingMs);
		int pktCount=(myConfig.packetspermin*remainingMs)/60000;
		int oldPktCount=(myConfig.pctoldpackets*pktCount)/100;
		int newPktCount=pktCount-oldPktCount;

		printf("Send oldermarker\n");
		//Send oldermarker
		int lastPacketPos=oldPacketPos+oldPktCount;
		if (lastPacketPos>=(noBlocks())) lastPacketPos-=(noBlocks()); //wraparound
		sendOlderMarker(sortedTs[(newPktCount)].ts, 
				oldPacketPos, lastPacketPos, 
				(remainingMs*(100-myConfig.pctoldpackets))/100);

		int packet;
		//Send new packets first.
		for (packet=0; packet<newPktCount; packet++) {
			waitTilRemaining(((pktCount-packet)*remainingMs)/pktCount);
			printf("Sending (new) block %d.\n", sortedTs[packet].block);
			sendChange(sortedTs[packet].block, currId);
		}
		//Followed by older packets.
		for (; packet<pktCount; packet++) {
			printf("Sending (old) block %d.\n", oldPacketPos);
			waitTilRemaining(((pktCount-packet)*remainingMs)/pktCount);
			sendChange(oldPacketPos, currId);
			oldPacketPos++;
			if (oldPacketPos>=(noBlocks())) oldPacketPos=0;
		}
		bppQuery(bppCon, 'e', &remainingMs);
		if (remainingMs<3000) usleep(remainingMs*1000);
	}
}

int iniHandler(void* user, const char* section, const char* name, const char* value) {
	Config *cfg=(Config*)user;
	if (strcmp(name, "file")==0) {
		free(cfg->file);
		cfg->file=strdup(value);
		if (cfg->stateprefix==NULL) cfg->stateprefix=strdup(value);
	} else if (strcmp(name, "stateprefix")==0) {
		free(cfg->stateprefix);
		cfg->stateprefix=strdup(value);
	} else if (strcmp(name, "streamid")==0) {
		cfg->streamid=strtol(value, NULL, 0);
	} else if (strcmp(name, "size")==0) {
		cfg->size=strtol(value, NULL, 0);
	} else if (strcmp(name, "packetspermin")==0) {
		cfg->packetspermin=strtol(value, NULL, 0);
	} else if (strcmp(name, "pctoldpackets")==0) {
		cfg->pctoldpackets=strtol(value, NULL, 0);
	} else if (strcmp(name, "blockflashtimems")==0) {
		cfg->blockflashtimems=strtol(value, NULL, 0);
	} else {
		printf("Unable to parse key \"%s\".", name);
	}
}


int main(int argc, char **argv) {
	char *fnbuf;
	int r;
	if (argc<2) {
		printf("Usage: %s config.ini\n", argv[0]);
		exit(0);
	}

	myConfig.file=NULL;
	myConfig.streamid=-1;
	myConfig.size=0;
	myConfig.stateprefix=NULL;
	myConfig.packetspermin=60;
	myConfig.blockflashtimems=300;
	myConfig.pctoldpackets=30;
	r=ini_parse(argv[1], iniHandler, (void*)&myConfig);
	if (r!=0) {
		printf("Couldn't parse %s: line %d\n", argv[1], r);
		exit(1);
	}

	fnbuf=malloc(strlen(myConfig.stateprefix)+32);

	bppCon=bppCreateConnection("localhost", myConfig.streamid);
	if (bppCon<=0) {
		perror("connecting to bppserver");
		exit(1);
	}

	//Open last-processed blockfile state
	sprintf(fnbuf, "%s%s", myConfig.stateprefix, POSTFIX_LASTPROCESSED);
	int f=open(fnbuf, O_RDONLY);
	if (f<=0) {
		//No (valid) lastprocessed file. Use main file.
		printf("No valid lastprocessed file found. Using main file as starting point.\n");
		f=open(myConfig.file, O_RDONLY);
		if (f<=0) {
			perror(myConfig.file);
			exit(1);
		}
	}
	//Allocate buffers and read in file
	fileSize=lseek(f, 0, SEEK_END);
	if (fileSize>myConfig.size) {
		printf("ERROR: File size bigger than size configured! Truncating.\n");
		fileSize=myConfig.size;
	}
	lseek(f, 0, SEEK_SET);
	fileContents=malloc(myConfig.size);
	fileTimestamps=malloc(sizeof(uint32_t)*(myConfig.size/BLOCKSIZE));
	r=read(f, fileContents, fileSize);
	if (r!=fileSize) {
		printf("Eek! Couldn't load in entire lastprocessed file.\n");
		exit(1);
	}
	close(f);

	//Pre-set timestamps to current time, for partial reads.
	for (int i=0; i<(myConfig.size/BLOCKSIZE); i++) {
		fileTimestamps[i]=time(NULL);
	}
	//Read in old timestamps
	sprintf(fnbuf, "%s%s", myConfig.stateprefix, POSTFIX_TIMESTAMP);
	f=open(fnbuf, O_RDONLY);
	if (f<=0) {
		printf("Can't read blocktimestamp file; seting timestamps to current time.\n");
	} else {
		read(f, fileTimestamps, sizeof(uint32_t)*(myConfig.size/BLOCKSIZE));
		close(f);
	}
	free(fnbuf);

	printf("Initialized. File size is %zu K, %d blocks.\n", fileSize/1024, noBlocks());
	mainLoop();
}

