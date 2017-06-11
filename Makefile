all: libraries servers

libraries: ed25519 bpp

servers: blocksend hksend lyric_test server

.PHONY: ed25519 bpp keys blocksend hksend lyric_test server

ed25519:
	make -C ed25519/src

bpp:
	make -C bppsource

keys:
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
