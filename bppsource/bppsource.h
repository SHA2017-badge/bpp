#ifndef BPPSOURCE_H
#define BPPSOURCE_H

#include <stdint.h>

int bppGetResponse(int sockfd);
int bppSend(int sockfd, int subtype, uint8_t *data, int len);
int bppCreateConnection(char *hostname, int type);
void bppClose(int sockfd);


#endif