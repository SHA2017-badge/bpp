#ifndef DEFEC_H
#define DEFEC_H

#include "recvif.h"

typedef struct {
	int packetsInTotal;
	int packetsInMissed;
} FecStatus;


void defecInit(RecvCb *cb, int maxLen);
void defecRecv(uint8_t *packet, size_t len);
void defecGetStatus(FecStatus *st);

#endif