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

#include "uECC.h"
#include "../keys/privkey.inc"
#include "sha256.h"

#include "mbedtls/config.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/ecdsa.h"


static int sendMaxPktLen;
static SendCb *sendCb;
static mbedtls_mpi privkey_mpi;
static mbedtls_ecp_group group;

#define ECPARAMS MBEDTLS_ECP_DP_SECP256R1

void signInit(SendCb *cb, int maxlen) {
	int r;
	sendCb=cb;
	sendMaxPktLen=maxlen;
	r=mbedtls_ecp_group_load(&group, ECPARAMS);
	if (r) printf("group load failed\n");
	r=mbedtls_mpi_read_binary(&privkey_mpi, (unsigned char*)&private_key[0], 32);
	if (r) printf("read_binary X failed\n");

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
	mbedtls_mpi hr, hs;
	mbedtls_mpi_init(&hr);
	mbedtls_mpi_init(&hs);
	mbedtls_ecdsa_sign_det(&group, &hr, &hs, &privkey_mpi, hash, 32, MBEDTLS_MD_SHA256);
	mbedtls_mpi_write_binary(&hr, p->sig, 32);
	mbedtls_mpi_write_binary(&hs, p->sig+32, 32);

	//Send
	memcpy(p->data, packet, len);
	sendCb((uint8_t*)p, sendMaxPktLen);
	free(p);
}


int signGetMaxPacketLength() {
	return sendMaxPktLen-sizeof(SignedPacket);
}

