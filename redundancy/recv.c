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
	gbf_int_t data[0];
	// gbf_int_t data[size];
	// struct ecc_signature sig;
} __attribute__((packed));

struct block_hdr {
	int size;             // length of data (in bytes)
	uint8_t data[0];
	// uint8_t data[size];
	// struct ecc_signature sig;
} __attribute__((packed));

#define ECC_SIG_LEN 72
struct ecc_signature {
	uint8_t ecc_sig[ECC_SIG_LEN];  // brainpoolP256r1 ?
} __attribute__((packed));

struct reassemble {
	uint32_t uid_1;
	uint32_t uid_2;
	int size;
	gbf_int_t num_frag;
	gbf_int_t recv_frag;
	gbf_int_t *pkt_ids;
	gbf_int_t *pkt_data;
};

EC_KEY *eckey = NULL;
#include "network_key_pub.c"
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

	unsigned int buf_len = ECDSA_size(eckey);
	assert(buf_len == ECC_SIG_LEN);
}

int
ecc_verify_data(uint8_t *block, int block_len)
{
	// parse ASN1
	if (block[block_len] != 0x30) // ASN1.Seq
		return -1;
	int seq_len = block[block_len+1];

	// generate sha256 hash
	uint8_t sha256[256/8];
	SHA256(block, block_len, sha256);

	ecc_init_key();

	// determine size
	unsigned int buf_len = ECDSA_size(eckey);
	if (seq_len + 2 > buf_len)
		return -1;
	buf_len = seq_len + 2;

	int res = ECDSA_verify(0, sha256, sizeof(sha256), &block[block_len], buf_len, eckey);
	return res;
}

void
reassemble_discard(struct reassemble *ra)
{
	if (ra == NULL)
		return;

	free(ra->pkt_ids);
	free(ra->pkt_data);
	free(ra);
}

struct reassemble *
reassemble_new(struct packet_hdr *pkt)
{
	if (pkt->num_frag == 0)
		return NULL;

	if (pkt->size == 0)
		return NULL;

	if (pkt->num_frag * pkt->size * sizeof(gbf_int_t) < sizeof(struct block_hdr) + sizeof(struct ecc_signature))
		return NULL;

	gbf_int_t *pkt_ids = (gbf_int_t *) malloc(sizeof(gbf_int_t) * pkt->num_frag);
	if (pkt_ids == NULL)
		return NULL; // malloc failed

	gbf_int_t *pkt_data = (gbf_int_t *) malloc(sizeof(gbf_int_t) * pkt->num_frag * pkt->size);
	if (pkt_data == NULL)
	{
		free(pkt_ids);
		return NULL; // malloc failed
	}

	struct reassemble *ra = (struct reassemble *) malloc(sizeof(struct reassemble));
	if (ra == NULL)
	{
		free(pkt_ids);
		free(pkt_data);
		return NULL; // malloc failed
	}
	memset(ra, 0, sizeof(struct reassemble));

	ra->uid_1 = pkt->uid_1;
	ra->uid_2 = pkt->uid_2;
	ra->size = pkt->size;
	ra->num_frag = pkt->num_frag;
	ra->pkt_ids = pkt_ids;
	ra->pkt_data = pkt_data;

	return ra;
}

#include "hexdump.c"

void
reassemble_add(struct reassemble *ra, struct packet_hdr *pkt)
{
	if (ra == NULL)
		return;

	if (ra->uid_1 != pkt->uid_1)
		return;
	if (ra->uid_2 != pkt->uid_2)
		return;
	if (ra->size != pkt->size)
		return;
	if (ra->num_frag != pkt->num_frag)
		return;

	if (ra->recv_frag >= ra->num_frag)
		return;

	int i;
	for (i=0; i<ra->recv_frag; i++)
	{
		if (ra->pkt_ids[i] == pkt->pkt_id)
			return;
	}
	i = ra->recv_frag;

	ra->pkt_ids[i] = pkt->pkt_id;
	memcpy(&ra->pkt_data[i * ra->size], pkt->data, sizeof(gbf_int_t) * ra->size);
	ra->recv_frag++;

	if (ra->recv_frag == ra->num_frag)
	{
		fprintf(stderr, "received enough fragments!\n");
		gbf_int_t *pkt_out = (gbf_int_t *) malloc(sizeof(gbf_int_t) * pkt->num_frag * pkt->size);
		assert(pkt_out != NULL);

		gbf_decode(pkt_out, ra->pkt_data, ra->pkt_ids, ra->num_frag, ra->size);

		struct block_hdr *blk = (struct block_hdr *) pkt_out;
		if (blk->size + sizeof(struct block_hdr) + sizeof(struct ecc_signature) > sizeof(gbf_int_t) * pkt->num_frag * pkt->size)
			return; // invalid size?

		int res = ecc_verify_data((uint8_t *) pkt_out, blk->size + sizeof(struct block_hdr));
		if (res != 1)
		{
			printf("signature failed.\n");
			return;
		}

		printf("decoded buf:\n");
		hexdump(blk->data, blk->size);
	}
}

int
main(int argc, char *argv[])
{
	gbf_init(GBF_POLYNOME);
	ecc_init_key();

	struct reassemble *ra = NULL;

	while (1)
	{
		unsigned char packet[4096];
		char *line = fgets((char *) packet, sizeof(packet), stdin);
		if (line == NULL)
			break;

		int packet_len = 0;
		while (packet[packet_len*2] != 0 && packet[packet_len*2+1] != 0)
		{
			unsigned char c_hi = packet[packet_len*2];
			if (c_hi < '0')
				break;
			else if (c_hi <= '9')
				c_hi -= '0';
			else if (c_hi < 'a')
				break;
			else if (c_hi <= 'f')
				c_hi -= 'a' - 10;
			else
				break;

			unsigned char c_lo = packet[packet_len*2+1];
			if (c_lo < '0')
				break;
			else if (c_lo <= '9')
				c_lo -= '0';
			else if (c_lo < 'a')
				break;
			else if (c_lo <= 'f')
				c_lo -= 'a' - 10;
			else
				break;
			packet[packet_len++] = (c_hi << 4) | c_lo;
		}
		fprintf(stderr, "received packet with length %u.\n", packet_len);

		if (packet_len < sizeof(struct packet_hdr) + sizeof(struct ecc_signature))
			continue; // too short

		struct packet_hdr *pkthdr = (struct packet_hdr *) packet;
		if (pkthdr->size > sizeof(packet)/sizeof(gbf_int_t))
			continue; // very unlikely; avoid integer overflows.

		int real_packet_len = sizeof(struct packet_hdr) + pkthdr->size * sizeof(gbf_int_t) + sizeof(struct ecc_signature);
		if (real_packet_len > packet_len)
			continue; // truncated? or probably not what we're looking for..
		packet_len = real_packet_len;

		int res = ecc_verify_data(packet, packet_len - sizeof(struct ecc_signature));
		if (res != 1)
		{
			printf("signature failed.\n");
			continue;
		}

		printf("received signed packet.\n");
		// FIXME: handle packet

		if (ra == NULL || ra->uid_1 < pkthdr->uid_1 || (ra->uid_1 == pkthdr->uid_1 && ra->uid_2 < pkthdr->uid_2))
		{
			reassemble_discard(ra);
			ra = reassemble_new(pkthdr);
			reassemble_add(ra, pkthdr);
		}
		else if (ra->uid_1 == pkthdr->uid_1 && ra->uid_2 == pkthdr->uid_2)
		{
			if (ra->num_frag != pkthdr->num_frag)
			{
				fprintf(stderr, "same id, but num_frag mismatch!\n");
				continue;
			}

			if (ra->size != pkthdr->size)
			{
				fprintf(stderr, "same id, but size mismatch!\n");
				continue;
			}
			reassemble_add(ra, pkthdr);
		} // else received old packet
	}

	return 0;
}
