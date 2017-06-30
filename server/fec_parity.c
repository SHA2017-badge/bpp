#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "sendif.h"
#include "structs.h"
#include "fec.h"


static uint8_t *parPacket;
int parM; //after how many packets to send a parity packet
int biggestLen=0;

int parInit(int k, int n, int maxsize) {
	if (n!=k+1) return 0;
	parM=k;
	parPacket=malloc(maxsize);
	memset(parPacket, 0, maxsize);
	biggestLen=0;
	return 1;
}

int parSend(uint8_t *packet, size_t len, int serial, FecSendFeccedPacket sendFn) {
	//Add to parity packet
	for (int i=0; i<len; i++) parPacket[i]^=packet[i];
	if (biggestLen<len) biggestLen=len;
	//Send packet
	serial=sendFn(packet, len);
	//See if we need to send parity packet
	int p=serial%(parM+1);
	if (p==parM) {
		sendFn(parPacket, biggestLen);
		memset(parPacket, 0, biggestLen);
		biggestLen=0;
	}
	return 1;
}

void parDeinit() {
	free(parPacket);
	return;
}

FecGenerator fecGenParity={
	.name="parity",
	.desc="Single parity packet. N must be K+1; K can be arbitrary.",
	.genId=FEC_ID_PARITY,
	.init=parInit,
	.send=parSend,
	.deinit=parDeinit,
};

