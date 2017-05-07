#ifndef SENDER_H
#define SENDER_H

#include "sendif.h"

int senderInit();
int senderAddDest(char *hostname);
void senderSendPkt(uint8_t *packet, size_t len);
int senderGetMaxPacketLength();

#endif