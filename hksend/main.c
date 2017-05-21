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
		printf("Next catalog in %d ms\n", remaining);
		//Make sure hk packet is in catalog as well.
		if (remaining>interval) {
			usleep(interval*1000);
		} else {
			usleep(remaining*1000);
			printf("Sleeping a while for catalog start...\n");
			usleep(3000000); //Give logic a bit to send out packet markers
		}
	}
}