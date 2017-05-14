#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include "bppsource.h"
#include "structs.h"
#include <arpa/inet.h>

//Housekeeping server. Sends around housekeeping packets.

int main(int argc, char **argv) {
	if (argc<2) {
		printf("Usage: %s hk-interval-ms\n", argv[0]);
		exit(0);
	}
	
	int interval=atoi(argv[1]);
	int con=bppCreateConnection("localhost", HLPACKET_TYPE_HK);
	if (con<0) exit(1);
	while(1) {
		int remaining;
		HKPacketNextCatalog n;
		bppQuery(con, 'e', &remaining);
		n.delayMs=htonl(remaining);
		bppSend(con, HKPACKET_SUBTYPE_NEXTCATALOG, (uint8_t*)&n, sizeof(n));
		usleep(interval*1000);
	}
}