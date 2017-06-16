#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <time.h>
#include <assert.h>

#include <openssl/ec.h>
#include <openssl/bn.h>
#include <openssl/obj_mac.h>
#include <openssl/ecdsa.h>
#include <openssl/sha.h>

#include "redundancy.h"

struct packet_hdr {
	uint32_t uid_1;       // e.g. system time
	uint32_t uid_2;       // random value from /dev/urandom
	int size;             // length of data (<n> elements of gbf_int_t)
	gbf_int_t pkt_id;     // 1..MAX
	gbf_int_t num_frag;   // number of packets needed to reassemble
	// gbf_int_t data[size];
	// struct ecc_signature sig;
} __attribute__((packed));

struct block_hdr {
	int size;             // length of data (in bytes)
	// uint8_t data[size];
	// struct ecc_signature sig;
} __attribute__((packed));

#define ECC_SIG_LEN 72
struct ecc_signature {
	uint8_t ecc_sig[ECC_SIG_LEN];  // brainpoolP256r1 ?
} __attribute__((packed));

EC_KEY *eckey = NULL;
#include "network_key_pub.c"
#include "network_key_priv.c"
void
ecc_init_key(void)
{
	if (eckey != NULL)
		return;

	eckey = EC_KEY_new_by_curve_name(NID_X9_62_prime256v1);
	assert(eckey != NULL);

	// set public key
	BIGNUM *pub_x = BN_bin2bn(network_key_pub_x, sizeof(network_key_pub_x), NULL);
	assert(pub_x != NULL);
	BIGNUM *pub_y = BN_bin2bn(network_key_pub_y, sizeof(network_key_pub_y), NULL);
	assert(pub_y != NULL);
	int res = EC_KEY_set_public_key_affine_coordinates(eckey, pub_x, pub_y);
	assert(res == 1); // 0=error, 1=success

	// set private key
	BIGNUM *priv = BN_bin2bn(network_key_priv, sizeof(network_key_priv), NULL);
	assert(priv != NULL);
	res = EC_KEY_set_private_key(eckey, priv);
	assert(res == 1); // 0=error, 1=success

	unsigned int buf_len = ECDSA_size(eckey);
	assert(buf_len == ECC_SIG_LEN);
}

void
ecc_sign_data(uint8_t *block, int block_len)
{
	memset(&block[block_len], 0, ECC_SIG_LEN);

	// generate sha256 hash
	uint8_t sha256[256/8];
	SHA256(block, block_len, sha256);

	ecc_init_key();

	unsigned int buf_len = ECDSA_size(eckey);
	int res = ECDSA_sign(0, sha256, sizeof(sha256), &block[block_len], &buf_len, eckey);
	assert(res == 1); // 0=error, 1=success

	// test signature
	res = ECDSA_verify(0, sha256, sizeof(sha256), &block[block_len], buf_len, eckey);
	assert(res == 1); // -1=invalid something,0=error, 1=success
}

int
main(int argc, char *argv[])
{
	gbf_init(GBF_POLYNOME);
	ecc_init_key();

	int fd = open("../lyric_test/never.txt", O_RDONLY);
	assert(fd != -1);

	uint8_t lyrics[4096];
	int lyrics_len = read(fd, lyrics, sizeof(lyrics));
	assert(lyrics_len > 0);

	close(fd);

	fprintf(stderr, "lyrics-length: %u\n", lyrics_len);

	// calculate number of needed fragments */
	int max_packet_len = 512;
	int max_data_per_packet = (max_packet_len - (sizeof(struct packet_hdr) + sizeof(struct ecc_signature))) / sizeof(gbf_int_t);

	int num_frag = 1;
	int block_len = num_frag * sizeof(gbf_int_t);
	int data_len = sizeof(struct block_hdr) + lyrics_len + sizeof(struct ecc_signature);
	int pkt_data_len = (data_len + block_len - 1) / block_len;

	while (pkt_data_len > max_data_per_packet)
	{
		num_frag++;
		block_len = num_frag * sizeof(gbf_int_t);
		pkt_data_len = (data_len + block_len - 1) / block_len;
	}

	int packet_len = sizeof(struct packet_hdr) + pkt_data_len * sizeof(gbf_int_t) + sizeof(struct ecc_signature);

	fprintf(stderr, "min. number of packets needed: %u\n", num_frag);
	fprintf(stderr, "packet-length: %u\n", packet_len);

	// build encoded block
	uint8_t *data = (uint8_t *) malloc(pkt_data_len * block_len);
	assert(data != NULL);
	memset(data, 0, pkt_data_len * block_len);

	struct block_hdr *blkhdr = (struct block_hdr *) data;
	blkhdr->size = lyrics_len;
	memcpy(&data[sizeof(struct block_hdr)], lyrics, lyrics_len);
	ecc_sign_data(data, sizeof(struct block_hdr) + lyrics_len);

	// build packets
	uint8_t *packet = (uint8_t *) malloc(packet_len);
	assert(packet != NULL);
	memset(packet, 0, packet_len);

	struct packet_hdr *pkthdr = (struct packet_hdr *) packet;
	pkthdr->uid_1 = time(NULL);
	pkthdr->uid_2 = rand(); // FIXME use a real random value; not rand().
	pkthdr->size  = pkt_data_len;
	pkthdr->num_frag = num_frag;

	int pkt_id;
	for (pkt_id=1; pkt_id < 5*num_frag; pkt_id++)
	{
		pkthdr->pkt_id = pkt_id;
		gbf_encode_one((gbf_int_t*) &packet[sizeof(struct packet_hdr)], (gbf_int_t*) data, pkt_id, num_frag, pkt_data_len);
		ecc_sign_data(packet, sizeof(struct packet_hdr) + pkt_data_len * sizeof(gbf_int_t));

		// send packet.
		int i;
		for (i=0; i<packet_len; i++)
			printf("%02x", packet[i]);
		printf("\n");
		fflush(stdout);
	}
	
	return 0;
}
