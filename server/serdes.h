#ifndef SERDES_H
#define SERDES_H

#include "sendif.h"



void serdesInit(SendCb *cb, int maxlen);
int serdesGetMaxPacketLength();
void serdesSend(uint8_t *packet, size_t len);
int serdesWaitAfterSendingNext(int delayMs);

#endif