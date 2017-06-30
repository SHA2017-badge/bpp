/*
Packet signing

Every packet sent out is signed using ECDSA.
*/
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <arpa/inet.h>
#include "sendif.h"
#include "structs.h"

#include "ed25519.h"
#include "../keys/privkey.inc"
#include "../keys/pubkey.inc"
#include "sha256.h"


static int sendMaxPktLen;
static SendCb *sendCb;

void signInit(SendCb *cb, int maxlen) {
	sendCb=cb;
	sendMaxPktLen=maxlen;
}

void signSend(uint8_t *packet, size_t len) {
	int signMaxPacketLen=(sendMaxPktLen-sizeof(SignedPacket));
	SignedPacket *p=malloc(sizeof(SignedPacket)+signMaxPacketLen);
	SHA256_CTX sha;
	uint8_t hash[32];
	//Calculate hash of packet
	sha256_init(&sha);
	sha256_update(&sha, packet, len);
	sha256_final(&sha, hash);

	//Sign packet
	ed25519_sign(p->sig, packet, len, public_key, private_key);
	
	//Send
	memcpy(p->data, packet, len);
	sendCb((uint8_t*)p, sizeof(SignedPacket)+len);
	free(p);
}


int signGetMaxPacketLength() {
	return sendMaxPktLen-sizeof(SignedPacket);
}

