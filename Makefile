all: libraries servers

libraries: ed25519 bpp

servers: blocksend hksend lyric_test server

.PHONY: ed25519 bpp keys blocksend hksend lyric_test server clean

ed25519:
	make -C ed25519/src

bpp:
	make -C bppsource

keys: ed25519
	make -C keys
	cd keys && ./genkey

blocksend:
	make -C blocksend

hksend:
	make -C hksend

lyric_test:
	make -C lyric_test

server:
	make -C server

clean:
	make -C ed25519/src clean
	make -C bppsource clean
	make -C blocksend clean
	make -C hksend clean
	make -C lyric_test clean
	make -C server clean
	
