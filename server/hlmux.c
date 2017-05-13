/*
HLDemux
Allows registration of sub-protocols and forwards a packet to the handlers for these protocols

*/
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <arpa/inet.h>
#include "sendif.h"
#include "structs.h"


static SendCb *sendCb;
static int sendMaxPktLen;

void hlmuxInit(SendCb *cb, int maxlen) {
	sendCb=cb;
	sendMaxPktLen=maxlen;
}


void hlmuxSend(int type, int subtype, uint8_t *packet, size_t len) {
	int hlmuxMaxPacketLen=(sendMaxPktLen-sizeof(HlPacket)); //max data in a fec packet
	HlPacket *p=malloc(sizeof(HlPacket)+hlmuxMaxPacketLen);
	p->type=htons(type);
	p->subtype=htons(subtype);
	memcpy(p->data, packet, len);
	sendCb((uint8_t*)p, len+sizeof(HlPacket));
	free(p);
}


int hlmuxGetMaxPacketLength() {
	return sendMaxPktLen-sizeof(HlPacket);
}

