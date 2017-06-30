#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "sendif.h"
#include "structs.h"
#include "fec.h"
#include "redundancy.h"

//Reed-Solomon FECcing.


static uint8_t *packets;
static int packetsStored;
static int parK, parN;
static int maxPacketLen=0;


static int rsInit(int k, int n, int maxsize) {
	parK=k; parN=n;
	packets=malloc(maxsize*k);
	maxPacketLen=maxsize;
	packetsStored=0;
	return 1;
}

static int rsSend(uint8_t *packet, size_t len, int serial, FecSendFeccedPacket sendFn) {
	if (packetsStored==0) {
		//See if we're still in sync. If not, send a bunch of dummy packets to get in sync.
		//Shouldn't happen outside maybe a switch to this algo.
		int rp=serial%parN;
		if (rp!=0) {
			int toSend=parN-rp;
			printf("Fec_RS: Out of sync! Need to send %d dummy packets.\n", toSend);
			memset(packets, 0, maxPacketLen);
			for (int i=0; i<toSend; i++) sendFn(packets, maxPacketLen);
		}
	}

	int p=(serial+packetsStored)%(parN);
	printf("RS: Packet %d/%d (/%d)\n", p, parK, parN);
	if (p<parK) {
		memcpy(&packets[p*maxPacketLen], packet, len);
		packetsStored++;
	}
	if (p==parN-1) {
		uint8_t *out=malloc(maxPacketLen);
		//Received last of parK packets. Encode and send.
		for (int i=0; i<parN; i++) {
			gbf_encode_one((gbf_int_t*)out, (gbf_int_t*) packets, serial, parN, parK);
			serial=sendFn(out, maxPacketLen);
		}
		packetsStored=0;
		free(out);
	}
	return 1;
}

static void rsDeinit() {
	if (packets) {
		free(packets);
		packets=NULL;
	}
	return;
}

FecGenerator fecGenRs={
	.name="rs",
	.desc="Reed-Solomon RAID6-ish error correction, from TSDs code",
	.genId=FEC_ID_RS,
	.init=rsInit,
	.send=rsSend,
	.deinit=rsDeinit,
};

