#ifndef BD_FLATFLASH_H
#define BD_FLATFLASH_H

#include "blockdevif.h"

extern BlockdevIf blockdefIfFlatFlash;

typedef void (*BlockdefIfFlatFlashDoneCb)(void *arg);

typedef struct {
	int major;
	int minor;
	uint32_t minChangeId;
	BlockdefIfFlatFlashDoneCb doneCb;
	void *doneCbArg;
} BlockdefIfFlatFlashDesc;

#endif