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

//Emit a parity packet every FEC_M packets
#define FEC_M 3

static int sendMaxPktLen;
static SendCb *sendCb;
static int serial=0;
static uint8_t *parPacket;


void fecInit(SendCb *cb, int maxlen) {
	sendCb=cb;
	sendMaxPktLen=maxlen;
	parPacket=malloc(maxlen);
	memset(parPacket, 0, maxlen);
	serial=0;
}


void fecSend(uint8_t *packet, size_t len) {
	int fecMaxPacketLen=(sendMaxPktLen-sizeof(FecPacket)); //max data in a fec packet
	FecPacket *p=malloc(sizeof(FecPacket)+fecMaxPacketLen);
	p->serial=htonl(serial);
	memcpy(p->data, packet, len);
	//Clear rest of packet to not leak stuff
	memset(p->data+len, 0, fecMaxPacketLen-len);
	//Add packet to parity
	for (int x=0; x<fecMaxPacketLen; x++) parPacket[x]^=p->data[x];
	sendCb((uint8_t*)p, sendMaxPktLen);
	serial++;
	//See if it's time to send the parity packet already.
	if ((serial % (FEC_M+1)) == FEC_M) {
		//Yes it is! Re-use p to send parity data.
		p->serial=htonl(serial);
		memcpy(p->data, parPacket, fecMaxPacketLen);
		sendCb((uint8_t*)p, sendMaxPktLen);
		serial++;
		//Zero out parity array for next set of packets.
		memset(parPacket, 0, fecMaxPacketLen);
	}
	free(p);
}


int fecGetMaxPacketLength() {
	return sendMaxPktLen-sizeof(FecPacket);
}

