#include <stdio.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "structs.h"
#include "chksign.h"
#include "defec.h"
#include "serdec.h"

static int createListenSock() {
	int sock;
	struct sockaddr_in addr;
	unsigned short port=2017;

	if ((sock = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0) {
		perror("creating socket");
		exit(1);
	}

	/* Construct bind structure */
	memset(&addr, 0, sizeof(addr));	/* Zero out structure */
	addr.sin_family=AF_INET;
	addr.sin_addr.s_addr=htonl(INADDR_ANY);
	addr.sin_port=htons(port);

	if (bind(sock, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
		perror("bind");
		exit(1);
	}
	return sock;
}

void myRecv(uint8_t *packet, size_t len) {
	printf("Got packet size %d: ", len);
	if (packet!=NULL) {
		packet[len]=0;
		printf("%s\n", packet);
	}
}

int main(int argc, char** argv) {
	int sock=createListenSock();
	int len;
	uint8_t buff[1400];

	chksignInit(defecRecv);
	defecInit(serdecRecv, 1400);
	serdecInit(myRecv);

	while((len = recvfrom(sock, buff, sizeof(buff), 0, NULL, 0)) >= 0) {
		printf("Got UDP packet of %d byte\n", len);
		chksignRecv(buff, len);
	}
	perror("recv");
	
	close(sock);
	exit(0);
}
