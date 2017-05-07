#ifndef CHKSIGN_H
#define CHKSIGN_H

#include "recvif.h"


void chksignInit(RecvCb *cb);
void chksignRecv(uint8_t *packet, size_t len);

#endif