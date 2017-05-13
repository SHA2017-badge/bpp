/*
Try to ressurect missing packets using FEC
*/
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <arpa/inet.h>
#include "recvif.h"
#include "structs.h"

#include "uECC.h"
#include "../keys/pubkey.inc"
#include "sha256.h"


static RecvCb *recvCb;

#define FEC_M 3

static uint8_t* parPacket[FEC_M];
static uint32_t parSerial[FEC_M];
static int lastSentSerial=0; //sent to upper layer, that is

void defecInit(RecvCb *cb, int maxLen) {
	int i;
	recvCb=cb;
	for (i=0; i<FEC_M; i++) {
		parPacket[i]=malloc(maxLen);
		parSerial[i]=0;
	}
}


void defecRecv(uint8_t *packet, size_t len) {
	if (len<sizeof(FecPacket)) return;
	FecPacket *p=(FecPacket*)packet;
	int plLen=len-sizeof(FecPacket);

	int serial=ntohl(p->serial);
	int spos=serial%(FEC_M+1);
//	printf("FEC: %d (%d) %s\n", serial, spos, (spos<FEC_M)?"D":"P");
	if (spos<FEC_M) {
		//Normal packet.
		//First, check if we missed a parity packet.
		int missedParityPacket=0;
		for (int i=spos; i<FEC_M; i++) {
			if (parSerial[i]!=0) missedParityPacket=1;
		}
		if (missedParityPacket) {
//			printf("Fec: missed parity packet\n");
			//Yes, we did. Dump out what's left in buffer for next time.
			lastSentSerial++; //because we missed that parity packet
			for (int i=spos; i<FEC_M; i++) {
				if (parSerial[i]>=lastSentSerial) {
					if (parSerial[i]!=lastSentSerial) recvCb(NULL, 0);
					recvCb(parPacket[i], plLen);
					lastSentSerial=parSerial[i];
				}
				parSerial[i]=0;
			}
		}

		//Okay, we should be up to date with this sequence again.
		memcpy(parPacket[spos], p->data, plLen);
		parSerial[spos]=serial;
		if (serial==lastSentSerial+1) {
			//Nothing weird, just send.
			recvCb(p->data, plLen);
			lastSentSerial=serial;
		}
	} else {
		//Parity packet. See if we need to recover something.
		int exSerial=serial-FEC_M;
		int missing=-1;
		for (int i=0; i<FEC_M; i++) {
			if (parSerial[i]!=exSerial) {
				if (missing==-1) missing=i; else missing=-2;
				parSerial[i]=exSerial; //we'll recover this packet later if we can
			}
			exSerial++;
		}
		if (missing==-2) {
			printf("Fec: Missed too many packets in segment\n");
			//Still send packets we do have.
			int exSerial=serial-FEC_M;
			for (int i=missing; i<FEC_M; i++) {
				if (parSerial[i]==exSerial) {
					recvCb(parPacket[i], plLen);
				} else {
					recvCb(NULL, 0);
				}
				exSerial++;
			}
		} else if (missing==-1) {
			//Nothing missing in *this* segment. Discard parity packet.
			int exSerial=serial-FEC_M-1; //We expect at least the last datapacket in the prev seq as the last one sent
			if (exSerial>lastSentSerial) {
				//Seems we're missing entire previous segments.
				printf("FEC: Missing multiple segments! Packets %d - %d.\n", lastSentSerial, exSerial);
				recvCb(NULL, 0);
			}
		} else {
			//Missing one packet. We can recover this.
			//Xor the parity packet with the packets we have to magically allow the
			//missing packet to appear
			for (int i=0; i<FEC_M; i++) {
				if (i!=missing) {
					for (int j=0; j<plLen; j++) {
						p->data[j]^=parPacket[i][j];
					}
				}
			}
			//We expect at least the last datapacket in the prev seq as the last one sent.
			int exSerial=serial-FEC_M-1;
			if (exSerial>lastSentSerial) {
				//Seems we're missing entire segments.
				printf("FEC: Fixed 1 packet in this seg, but missing multiple segments! Packets %d - %d.\n", lastSentSerial, exSerial);
				recvCb(NULL, 0);
			}
			for (int i=missing; i<FEC_M; i++) {
				if (i==missing) {
					recvCb(p->data, plLen);
				} else {
					recvCb(parPacket[i], plLen);
				}
			}
//			printf("FEC: Restored packet.\n");
		}
		lastSentSerial=serial;
		for (int i=0; i<FEC_M; i++) parSerial[i]=0;
	}
}
