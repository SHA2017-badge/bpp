OBJS=main.o sender.o fec.o serdes.o ../common/crc16.o sha256.o uECC.o sign.o packetloss.o
TARGET=bbpsender
CFLAGS=-ggdb -I ../common -I ../micro-ecc -I ../sha256
LDFLAGS=

all: $(TARGET)

sha256.o: ../sha256/sha256.c ../sha256/sha256.h
	$(CC) $(CFLAGS) -c -o $@ $<

uECC.o: ../micro-ecc/uECC.c ../micro-ecc/uECC.h
	$(CC) $(CFLAGS) -c -o $@ $<

$(TARGET): $(OBJS)
	$(CC) -o $@ $(LDFLAGS) $^

clean:
	rm -f $(OBJS) $(TARGET)

