#ifndef PACKETLOSS_H
#define PACKETLOSS_H

#include "sendif.h"


void packetlossInit(SendCb *cb, int maxlen);
void packetlossSend(uint8_t *packet, size_t len);
int packetlossGetMaxPacketLength();

#endif
