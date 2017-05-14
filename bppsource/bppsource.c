#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <stdint.h>

int bppGetResponse(int sockfd, int *resp) {
	char buf[128]={0};
	int p=0;
	if (sockfd<=0) return -1;
	while (memchr(buf, '\n', p)==NULL && p!=sizeof(buf)) {
		int r=read(sockfd, &buf[p], sizeof(buf)-p);
		if (r<=0) return -1;
		p+=r;
	}
	if (resp!=NULL) *resp=strtol(&buf[1], NULL, 0);
	return (buf[0]=='+')?1:0;
}

int bppSend(int sockfd, int subtype, uint8_t *data, int len, int *resp) {
	char buf[len*2+8];
	int i;
	sprintf(buf, "p %02x \n", subtype);
	for (i=0; i<len; i++) {
		sprintf(&buf[5+i*2], "%02X\n", data[i]);
	}
//	printf("%s", buf);
	i=write(sockfd, buf, 6+len*2);
	if (i<=0) return -1;
	return bppGetResponse(sockfd, resp);
}

int bppCreateConnection(char *hostname, int type) {
	int sockfd, portno, n;
	struct sockaddr_in serveraddr;
	struct hostent *server;
	char buf[20];

	portno=2017;

	server = gethostbyname(hostname);
	if (server == NULL) {
		perror("dnslookup");
		return -1;
	}

	sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if (sockfd < 0) {
		perror("opening socket");
		return -1;
	}

	memset(&serveraddr, 0, sizeof(serveraddr));
	serveraddr.sin_family=AF_INET;
	memcpy(&serveraddr.sin_addr.s_addr, server->h_addr, server->h_length);
	serveraddr.sin_port = htons(portno);

	/* connect: create a connection with the server */
	if (connect(sockfd, (struct sockaddr *)&serveraddr, sizeof(serveraddr)) < 0) {
		close(sockfd);
		perror("connect");
		return -1;
	}

	sprintf(buf, "t %x\n", type);
	write(sockfd, buf, strlen(buf));
	if (!bppGetResponse(sockfd)) {
		printf("Error setting type\n");
		close(sockfd);
		return -1;
	}

	return sockfd;
}

void bppClose(int sockfd) {
	close(sockfd);
}
