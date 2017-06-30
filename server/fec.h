#ifndef FEC_H
#define FEC_H

#include "sendif.h"

//returns new serial
typedef uint32_t (*FecSendFeccedPacket)(uint8_t *packet, size_t len);

//WARNING: it is assumed that every packet sent through these functions will have length=maxsize
//Emit n packets out for every k packets in
typedef int (*FecGeneratorInit)(int k, int n, int maxsize);
typedef int (*FecGeneratorSend)(uint8_t *packet, size_t len, int serial, FecSendFeccedPacket sendfn);
typedef void (*FecGeneratorDeinit)();
typedef struct {
	const char *name;
	const char *desc;
	const int genId;
	FecGeneratorInit init;
	FecGeneratorSend send;
	FecGeneratorDeinit deinit;
} FecGenerator;


void fecInit(SendCb *cb, int maxlen);
int fecGetMaxPacketLength();
void fecSend(uint8_t *packet, size_t len);

#endif