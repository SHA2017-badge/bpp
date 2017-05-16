#
# Component Makefile
#


COMPONENT_ADD_INCLUDEDIRS := . common
COMPONENT_SOURCES := . common
COMPONENT_OBJS := bd_flatflash.o blkidcache_mlvl.o blockdecode.o chksign_mbedtls.o defec.o hkpackets.o \
					hldemux.o powerdown.o serdec.o subtitle.o


