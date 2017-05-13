#ifndef BD_EMU_H
#define BD_EMU_H


#include "blockdevif.h"

extern BlockdevIf blockdefIfBdemu;


typedef struct {
	const char *file;
} BlockdefIfBdemuDesc;
#endif