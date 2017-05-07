OBJS:=main.o
CFLAGS:=-ggdb -I../bppsource
LDFLAGS:=-L../bppsource -lbppsource

lyrics: $(OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

.PHONY: clean
clean: 
	rm -f $(OBJS) lyrics
