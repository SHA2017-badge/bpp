#ifndef BD_FLATFLASH_H
#define BD_FLATFLASH_H

#include "blockdevif.h"

extern BlockdevIf blockdefIfRoPart;


typedef struct {
	int major;
	int minor;
} BlockdefIfRoPartDesc;

#endif