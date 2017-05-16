#ifndef DEFEC_H
#define DEFEC_H

#include "recvif.h"

void defecInit(RecvCb *cb, int maxLen);
void defecRecv(uint8_t *packet, size_t len);

#endif