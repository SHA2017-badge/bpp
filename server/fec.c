/*
Forward Error Correction.

This is a simple bit of code that can handle one deletion every FEC_M packets. It does this in the
most simple way possible: every FEC_M-1 packets, it outputs a packet that is the XOR of the FEC_M-1
packets before it. One missing packet can then be recovered by XORring all the packets plus the 
parity packet.

*/
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <arpa/inet.h>
#include "sendif.h"
#include "structs.h"
#include <time.h>
#include "fec.h"

extern FecGenerator fecGenParity;

static FecGenerator *gens[]={
	&fecGenParity,
	NULL
};

static FecGenerator *currGen=NULL;

static int currK, currN;

static int sendMaxPktLen;
static SendCb *sendCb;
static int serial=0;

static time_t tsLastSaved;

#define TSFILE "lastfecid.txt"

void fecInit(SendCb *cb, int maxlen) {
	sendCb=cb;
	sendMaxPktLen=maxlen;
	char buff[128];
	FILE *f=fopen(TSFILE, "r");
	if (f!=NULL) {
		fgets(buff, 127, f);
		serial=atoi(buff);
		fclose(f);
	}
	if (serial==0) serial=1; //because serial==0 is special
	tsLastSaved=time(NULL);
	currGen=gens[0];
	currK=3;
	currN=4;
	currGen->init(currK, currN, maxlen);
}

void fecSendFecced(uint8_t *packet, size_t len) {
	FecPacket *p=malloc(sizeof(FecPacket)+len);
	p->serial=htonl(serial);
	memcpy(p->data, packet, len);
	sendCb((uint8_t*)p, sizeof(FecPacket)+len);
	serial++;
	free(p);
}


void fecSend(uint8_t *packet, size_t len) {
	currGen->send(packet, len, serial, fecSendFecced);
	//Save timestamp every 10 secs in case of crash/quit
	if (time(NULL)-tsLastSaved > 10) {
		FILE *f;
		f=fopen(TSFILE".tmp", "w");
		fprintf(f, "%d", (int)serial);
		fclose(f);
		rename(TSFILE".tmp", TSFILE);
		tsLastSaved=time(NULL);

		//Semi-hack: We use the same timer to send out the FEC parameters
		FecPacket *p=malloc(sizeof(FecPacket)+sizeof(FecDesc));
		p->serial=htonl(0);
		FecDesc *dsc=(FecDesc*)p->data;
		dsc->k=htons(currK);
		dsc->n=htons(currN);
		dsc->fecAlgoId=currGen->genId;
		sendCb(p, sizeof(FecPacket)+sizeof(FecDesc));
		free(p);
	}
}


int fecGetMaxPacketLength() {
	return sendMaxPktLen-sizeof(FecPacket);
}

