#ifndef FEC_H
#define FEC_H

#include "sendif.h"

void fecInit(SendCb *cb, int maxlen);
int fecGetMaxPacketLength();
void fecSend(uint8_t *packet, size_t len);

#endif