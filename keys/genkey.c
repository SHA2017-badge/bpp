#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#define USE_ED25519
#include "ed25519.h"

void output_c_code(char *fname, char *varname, uint8_t *data, int size) {
	int i;
	FILE *f=fopen(fname, "w");
	if (f==NULL) {
		printf("Couldn't write to %s\n", fname);
	}
	fprintf(f, "static uint8_t %s[%d]={", varname, size);
	for (i=0; i<size; i++) {
		if ((i&15)==0) {
			fprintf(f, "\n");
		} else {
			fprintf(f, " ");
		}
		fprintf(f, "0x%02X", data[i]);
		if (i!=size-1) fprintf(f, ",");
	}
	fprintf(f, "};\n");
	fclose(f);
	printf("Written %s: var %s has %d bytes.\n", fname, varname, size);
}

#ifdef USE_ED25519
int main(int argc, char **argv) {
	unsigned char seed[32], pubkey[32], privkey[64];
	ed25519_create_seed(seed);
	ed25519_create_keypair(pubkey, privkey, seed);
	output_c_code("privkey.inc", "private_key", privkey, 64);
	output_c_code("pubkey.inc", "public_key", pubkey, 32);
}
#endif
