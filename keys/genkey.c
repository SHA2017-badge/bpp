#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "mbedtls/config.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/ecdsa.h"

#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"

#include "ed25519.h"

mbedtls_entropy_context entropy;



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



#ifdef USE_MBEDTLS
//#define ECPARAMS MBEDTLS_ECP_DP_SECP256R1
#define ECPARAMS MBEDTLS_ECP_DP_CURVE25519

int main(int argc, char **argv) {
	mbedtls_ecp_keypair key;

    mbedtls_entropy_init( &entropy );

	mbedtls_ctr_drbg_context ctr_drbg;
	char *personalization = "l;afj;lkasdf;lkjasd;lfkja;sldkjfjksd";
	mbedtls_ctr_drbg_init( &ctr_drbg );
	int ret = mbedtls_ctr_drbg_seed( &ctr_drbg , mbedtls_entropy_func, &entropy,
					 (const unsigned char *) personalization,
					 strlen( personalization ) );
	if( ret != 0 ) {
		printf("Derp, seeding rng\n");
		exit(1);
	}
	mbedtls_ctr_drbg_set_prediction_resistance( &ctr_drbg, MBEDTLS_CTR_DRBG_PR_ON );

//	mbedtls_ecp_group_init(&group);
///	ret=mbedtls_ecp_group_load(&group, ECPARAMS);
//	if (ret) printf("group load failed\n");

	mbedtls_ecp_keypair_init(&key);
	mbedtls_ecp_gen_key(ECPARAMS, &key, mbedtls_ctr_drbg_random, &ctr_drbg);

	printf("Size d (priv) %d\n", mbedtls_mpi_size(&key.d));
	printf("Size X (pub) %d\n", mbedtls_mpi_size(&key.Q.X));
	printf("Size Y (pub) %d\n", mbedtls_mpi_size(&key.Q.Y));
	printf("Size Z (pub, n/u) %d\n", mbedtls_mpi_size(&key.Q.Z));

	char buff[128];
	ret=mbedtls_mpi_write_binary(&key.d, buff, mbedtls_mpi_size(&key.d));
	assert(ret==0);
	output_c_code("privkey.inc", "private_key", buff, mbedtls_mpi_size(&key.d));

	ret=mbedtls_mpi_write_binary(&key.Q.X, buff, mbedtls_mpi_size(&key.Q.X));
	assert(ret==0);
	ret=mbedtls_mpi_write_binary(&key.Q.Y, buff+mbedtls_mpi_size(&key.Q.X), mbedtls_mpi_size(&key.Q.Y));
	assert(ret==0);
	output_c_code("pubkey.inc", "public_key", buff, mbedtls_mpi_size(&key.Q.X)+mbedtls_mpi_size(&key.Q.Y));

	exit(0);
}

#endif

#define USE_ED25519

#ifdef USE_ED25519
int main(int argc, char **argv) {
	unsigned char seed[32], pubkey[32], privkey[64];
	ed25519_create_seed(seed);
	ed25519_create_keypair(pubkey, privkey, seed);
	output_c_code("privkey.inc", "private_key", privkey, 64);
	output_c_code("pubkey.inc", "public_key", pubkey, 32);
}
#endif
