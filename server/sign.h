#ifndef SIGN_H
#define SIGN_H

#include "sendif.h"



void signInit(SendCb *cb, int maxlen);
void signSend(uint8_t *packet, size_t len);
int signGetMaxPacketLength();

#endif