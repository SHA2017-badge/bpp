#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "uECC.h"


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


int main(int argc, char **argv) {
	uint8_t *pub, *priv;
	FILE *f;
	int puSize=uECC_curve_public_key_size(uECC_secp256r1());
	int prSize=uECC_curve_private_key_size(uECC_secp256r1());
	pub=malloc(puSize);
	priv=malloc(prSize);
	if (!uECC_make_key(pub, priv, uECC_secp256r1())) {
		printf("Making key failed.\n");
		exit(1);
	}
	output_c_code("pubkey.inc", "public_key", pub, puSize);
	output_c_code("privkey.inc", "private_key", priv, prSize);
	exit(0);
}

