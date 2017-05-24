/*
Serializer

This actually converts a stream of variable-length packets into one that only has fixed-length 
packets. The idea is that we can do fec, encryption etc over the fixed-length packets (which is
easier and more effective) but can feed variable-length packets into this (which is more 
user-friendly and flexible).
*/

#define _XOPEN_SOURCE

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <arpa/inet.h>
#include "sendif.h"
#include "structs.h"
#include "crc16.h"

#define OUR_MAX_PACKET_LENGTH (8*1024) //semi-randonly chosen

static int sendMaxPktLen;
static SendCb *sendCb;
static uint8_t *serdesBuf;
static int serdesPos;


void serdesInit(SendCb *cb, int maxlen) {
	sendCb=cb;
	sendMaxPktLen=maxlen;
	serdesBuf=malloc(maxlen+sizeof(SerdesHdr));
	serdesPos=0;
}



//Somewhat evil hack to stop transmitting when the badges are likely to be out to lunch because writing flash
static int waitTimeMs=0;
static int waitTimeThisBufMs=0;

int serdesWaitAfterSendingNext(int delayMs) {
	waitTimeMs=delayMs;
}


static void appendToBuf(uint8_t *data, int len) {
	while (len >= sendMaxPktLen-serdesPos) { //while packet does not fit in buffer
		int alen=sendMaxPktLen-serdesPos; //room left in buffer
		//We can only push the packet partially in. Do that and send the packet.
		memcpy(serdesBuf+serdesPos, data, alen);
		//Adjust data and len to be current
		data+=alen;
		len-=alen;
		//Send and clear buffer
		sendCb(serdesBuf, sendMaxPktLen);
		serdesPos=0;
		if (waitTimeThisBufMs) {
			printf("Sleeping %d ms to allow flash writes...\n", waitTimeThisBufMs);
			usleep(waitTimeThisBufMs*1000);
		}
		waitTimeThisBufMs=0;
	}
	//(rest of) packet is guaranteed to fit in remaining buffer space
	memcpy(serdesBuf+serdesPos, data, len);
	serdesPos+=len;
}

void serdesSend(uint8_t *packet, size_t len) {
	uint16_t crc;
	SerdesHdr h;
	h.magic=htonl(SERDES_MAGIC);
	h.len=htons(len);
	h.crc16=0;
//	crc=crc16_block(0, (uint8_t*)&h, sizeof(SerdesHdr));
//	h.crc16=htons(crc16_block(crc, packet, len));
	crc=crc16_ccitt(0, (uint8_t*)&h, sizeof(SerdesHdr));
	h.crc16=htons(crc16_ccitt(crc, packet, len));
	appendToBuf((uint8_t*)&h, sizeof(SerdesHdr));
	//Send entire contents, but trigger wait time after sending last byte to lower layer.
	appendToBuf(packet, len-1);
	waitTimeThisBufMs=waitTimeMs;
	waitTimeMs=0;
	appendToBuf(&packet[len-1], 1);
//	printf("Serdes: buf %d/%d\n", serdesPos, sendMaxPktLen);
}


int serdesGetMaxPacketLength() {
	return OUR_MAX_PACKET_LENGTH;
}

