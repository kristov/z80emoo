CC=gcc
CFLAGS=-Wall -Werror -ggdb

INCLUDE=-I. -Iz80ex/include -Ictk

all: z80emoo

z80emoo: z80emoo.c z80ex/z80ex.o ctk/ctk.o
	$(CC) $(CFLAGS) $(INCLUDE) -lpthread -lncursesw -o $@ $< z80ex/z80ex.o ctk/ctk.o

z80ex/z80ex.o:
	mkdir -p z80ex/lib
	cd z80ex && make

ctk/ctk.o:
	cd ctk && make

clean:
	rm -f z80emoo
	cd z80ex && make clean
	cd ctk && make clean
