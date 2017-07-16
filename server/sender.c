#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/uio.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct SenderDstItem SenderDstItem;

struct SenderDstItem{
	struct sockaddr *addr;
	socklen_t addrlen;
	time_t timeout;
	SenderDstItem *next;
};

static int senderFd;
static SenderDstItem *senderDest;


int senderInit() {
	senderDest=NULL;
	senderFd=socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (!senderFd) {
		perror("creating udp socket");
		return 0;
	}
	int broadcastPermission = 1;
	if (setsockopt(senderFd, SOL_SOCKET, SO_BROADCAST, (void *) &broadcastPermission,  sizeof(broadcastPermission)) < 0) {
		perror("setsockopt() failed");
		return 0;
	}
	return 1;
}

#define str(a) str2(a)
#define str2(a) #a

int senderAddDestSockaddr(struct sockaddr *addr, socklen_t addrlen, int timeout) {
	SenderDstItem *dest;
	for (dest=senderDest; dest!=NULL; dest=dest->next) {
		if (dest->addrlen==addrlen && memcmp(dest->addr, addr, addrlen)==0) break;
	}
	if (dest!=NULL) {
		//Just modify timeout.
		dest->timeout=time(NULL)+timeout;
	} else {
		//Allocate new dest struct, and everything in it
		dest=malloc(sizeof(SenderDstItem));
		memset(dest, 0, sizeof(SenderDstItem));
		dest->addr=malloc(addrlen);
		//Copy over info
		memcpy(dest->addr, addr, addrlen);
		dest->addrlen=addrlen;
		dest->timeout=time(NULL)+timeout;
		//Add into linked list
		dest->next=senderDest;
		senderDest=dest;
	}
	return 1;
}


int senderAddDest(char *hostname, int timeout) {
	const char* portname="2017";
	struct addrinfo hints;
	memset(&hints,0,sizeof(hints));
	//Grab address info for hostname
	hints.ai_family=AF_UNSPEC;
	hints.ai_socktype=SOCK_DGRAM;
	hints.ai_protocol=0;
	hints.ai_flags=AI_ADDRCONFIG;
	struct addrinfo* res=0;
	int err=getaddrinfo(hostname,portname,&hints,&res);
	if (err!=0) {
		printf("getaddrinfo for %s: %s\n", hostname, gai_strerror(err));
		perror(hostname);
		return 0;
	}
	return senderAddDestSockaddr(res->ai_addr, res->ai_addrlen, timeout);
}

#define PAD_LENGTH 0

void senderSendPkt(uint8_t *packet, size_t len) {
	int r;
	SenderDstItem *dst=senderDest;
	len+=PAD_LENGTH; //HACK! Esp32 promiscuous mode seems to eat up some bytes.
	uint8_t *ppacket=malloc(len+PAD_LENGTH);
	memcpy(ppacket, packet, len);
	len+=PAD_LENGTH;
	while(dst) {
//		for (int i=0; i<len; i++) ppacket[i]=i;
//		printf("Sending 0x%X bytes\n", len);
		r=sendto(senderFd, ppacket, len, 0, dst->addr, dst->addrlen);
		if (r==-1) {
			dst->timeout=1; //instantly remove next
		}
		dst=dst->next;
	}
	free(ppacket);

	//Housekeeping: see if there are dests that need to be killed
	int notDone=1;
	do {
		SenderDstItem *dest, *pdest=NULL;
		for (dest=senderDest; dest!=NULL; dest=dest->next) {
			if (dest->timeout!=0 && dest->timeout<time(NULL)) break;
			pdest=dest;
		}
		if (dest) {
			if (pdest) {
				pdest->next=dest->next;
			} else {
				senderDest=dest->next;
			}
			free(dest);
		} else {
			notDone=0;
		}
	} while (notDone);
}


int senderGetMaxPacketLength() {
	return 1024; //fixed for now
}

