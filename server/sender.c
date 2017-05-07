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

typedef struct SenderDstItem SenderDstItem;

struct SenderDstItem{
	char *name;
	struct sockaddr *addr;
	socklen_t addrlen;
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

int senderAddDest(char *hostname) {
	const char* portname="2017";
	struct addrinfo hints;
	SenderDstItem *dest;
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
	//Allocate new dest struct, and everything in it
	dest=malloc(sizeof(SenderDstItem));
	memset(dest, 0, sizeof(SenderDstItem));
	dest->addr=malloc(res->ai_addrlen);
	dest->name=malloc(strlen(hostname)+1);
	//Copy over info
	strcpy(dest->name, hostname);
	memcpy(dest->addr, res->ai_addr, res->ai_addrlen);
	dest->addrlen=res->ai_addrlen;
	//Add into linked list
	dest->next=senderDest;
	senderDest=dest;
	//All done.
	freeaddrinfo(res);
	return 1;
}

void senderSendPkt(uint8_t *packet, size_t len) {
	int r;
	SenderDstItem *dst=senderDest;
	while(dst) {
		r=sendto(senderFd, packet, len, 0, dst->addr, dst->addrlen);
		if (r==-1) {
			perror(dst->name);
		}
		dst=dst->next;
	}
}


int senderGetMaxPacketLength() {
	return 1024; //fixed for now
}

