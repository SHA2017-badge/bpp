#ifndef BPPSOURCE_H
#define BPPSOURCE_H

#include <stdint.h>

int bppGetResponse(int sockfd, int *resp);
int bppQuery(int sockfd, int cmd, int *ret);
int bppSet(int sockfd, int cmd, int val);
int bppSend(int sockfd, int subtype, uint8_t *data, int len);
int bppCreateConnection(char *hostname, int type);
void bppClose(int sockfd);


#endif