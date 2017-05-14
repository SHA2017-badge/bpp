OBJS:=main.o
CFLAGS:=-ggdb -I../bppsource -I../common
LDFLAGS:=-L../bppsource -lbppsource

hkserver: $(OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

.PHONY: clean
clean: 
	rm -f $(OBJS) hkserver
