#ifndef BLOCKDECODE_H
#define BLOCKDECODE_H

#include "blockdevif.h"

int blockdecodeInit(int type, int size, BlockdevIf *bdIf, char *bdevdesc);

#endif
