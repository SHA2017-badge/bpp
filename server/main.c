#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "unistd.h"
#include "sender.h"
#include "fec.h"
#include "sign.h"
#include "serdes.h"
#include "packetloss.h"

int main(int argc, char **argv) {
	char tst[128];
	senderInit();
	senderAddDest("192.168.5.255");
	
#if 0
	signInit(senderSendPkt, senderGetMaxPacketLength());
#else
	packetlossInit(senderSendPkt, senderGetMaxPacketLength());
	signInit(packetlossSend, packetlossGetMaxPacketLength());
#endif
	fecInit(signSend, signGetMaxPacketLength());
	serdesInit(fecSend, fecGetMaxPacketLength());

	int i;
	while(1) {
		usleep(50000);
		sprintf(tst, "Hello World! This is packet number %d!", i++);
		serdesSend(tst, strlen(tst));
	}
}