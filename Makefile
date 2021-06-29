CC=gcc
CFLAGS=-Wall -Werror -ggdb

Z80EX=../z80ex
INCLUDE=-I. -I$(Z80EX)/include $(shell pkg-bee --cflags ctk)

all: z80emoo

z80emoo: z80emoo.c $(Z80EX)/z80ex.o
	$(CC) $(CFLAGS) $(INCLUDE) -lpthread -lncursesw -o $@ $(Z80EX)/z80ex.o $< $(shell pkg-bee --cflags --libs ctk)

clean:
	rm -f z80emoo
