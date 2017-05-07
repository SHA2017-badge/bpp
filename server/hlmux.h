#ifndef HLMUX_H
#define HLMUX_H

#include "sendif.h"

void hlmuxInit(SendCb *cb, int maxlen);
void hlmuxSend(int type, int subtype, uint8_t *packet, size_t len);
int hlmuxGetMaxPacketLength();


#endif