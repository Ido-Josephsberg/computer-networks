CC      = cc
CFLAGS  = -Wall -Wextra -O2 -std=c11 -D_POSIX_C_SOURCE=200809L
LDFLAGS =

OBJS = bfproc.o bfnode.o net.o

all: bfproc

bfproc: $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJS)

bfproc.o: bfproc.c bf.h bfnode.h net.h proto.h time_util.h
bfnode.o: bfnode.c bfnode.h proto.h bf.h
net.o:    net.c net.h proto.h

clean:
	rm -f bfproc $(OBJS)

.PHONY: all clean
