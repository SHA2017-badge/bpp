
#include <stdint.h>


typedef struct {
	uint8_t sig[64];
	uint8_t data[];
} __attribute__ ((packed)) SignedPacket;


//The idea is: the serial indicates the id of the packet.
//Say we have a FEC which converts X packets into X+Y FECced packets,
//then with i=(serial mod (x+y)), packets with an i of 0-(X-1) are data
//packets, X-(y-1) are redundancy packets.
typedef struct {
	uint32_t serial;
	uint8_t data[];
}  __attribute__ ((packed)) FecPacket;


//Randomly chosen
#define SERDES_MAGIC 0x1A014AF5

typedef struct {
	uint32_t magic; //must be SERDES_MAGIC
	uint16_t len;
	uint16_t crc16;
}  __attribute__ ((packed)) SerdesHdr;


/*
Hashing: Hmm. Every now and then (8 packets?) een ECC-encrypted pakket met de hashes van
de voorgaande X pakketten?
*/

#define BLOCKDEV_BLKSZ 4096

#define HLPACKET_TYPE_HK			0		//Housekeeping packets
#define HLPACKET_TYPE_BDSYNC		1		//Filesync packets
#define HLPACKET_TYPE_SUBTITLES		2		//

typedef struct {
	uint16_t type;
	uint16_t subtype;
	uint8_t data[];
} __attribute__ ((packed)) HlPacket;


/* These are always sent in this order:
BDSYNC_SUBTYPE_BITMAP * n
BDSYNC_SUBTYPE_OLDERMARKER
(BDSYNC_SUBTYPE_CHANGE interspersed by BDSYNC_SUBTYPE_CATALOGPTR)

The changeIdNew of bitmap packets and the changeId of change packets will always be the same.
If they're not, it indicates something has gone wrong (missed a catalog) and the logic should 
wait till the next catalog.
*/
#define BDSYNC_SUBTYPE_BITMAP		0
#define BDSYNC_SUBTYPE_OLDERMARKER	1
#define BDSYNC_SUBTYPE_CHANGE		2
#define BDSYNC_SUBTYPE_CATALOGPTR	3

/*
 Bitmap type. If the sectors marked by an 1 in the bitmap have a changeID that is newer than
 changeIdOrig, they can be marked as changeID changeIdNew without changing the contents.
*/
typedef struct {
	uint32_t changeIdOrig;
	uint32_t changeIdNew;
	uint8_t bitmap[];
} __attribute__ ((packed)) BDPacketBitmap;

/*
 OlderMarker. Tells clients that updates for sectors from secIdStart to secIdEnd will be sent after
 delayMs milliseconds from now.
*/
typedef struct {
	uint32_t oldestNewTs; //Last timestamp sent for 'new' packets.
	uint16_t secIdStart;
	uint16_t secIdEnd;
	uint32_t delayMs;
} __attribute__ ((packed)) BDPacketOldermarker;


/*
 CatalogPtr. Tells clients how long it'll take for the next round of bitmaps etc will be sent.
*/
typedef struct {
	uint32_t delayMs;
} __attribute__ ((packed)) BDPacketCatalogPtr;


/*
 Change. Contains a sector ID and the info therein.
 */
typedef struct {
	uint32_t changeId;
	uint16_t sector;
	uint8_t data[];
} __attribute__ ((packed)) BDPacketChange;

/*
 Opaque bdPacket struct.
*/
typedef struct {
	uint8_t type;
	uint8_t data[];
} __attribute__ ((packed)) BDPacket;



