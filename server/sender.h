#ifndef SENDER_H
#define SENDER_H

#include "sendif.h"

int senderInit();
int senderAddDest(char *hostname, int timeout);
int senderAddDestSockaddr(struct sockaddr *addr, socklen_t addrlen, int timeout);
void senderSendPkt(uint8_t *packet, size_t len);
int senderGetMaxPacketLength();

#endif