/*

Debugging: Mangle/drop a few packets here and there

*/
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <arpa/inet.h>
#include "sendif.h"
#include "structs.h"


#define MANGLE_PML 100
#define DROP_PML 100

static int sendMaxPktLen;
static SendCb *sendCb;


void packetlossInit(SendCb *cb, int maxlen) {
	sendCb=cb;
	sendMaxPktLen=maxlen;
}


void packetlossSend(uint8_t *packet, size_t len) {
	int i=rand()%1000;
	int j=rand()%1000;

	if (i<DROP_PML) return;

	if (j<MANGLE_PML) {
		packet[rand()%len]=rand();
	}
	sendCb(packet, len);
}


int packetlossGetMaxPacketLength() {
	return sendMaxPktLen;
}

