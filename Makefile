CC=gcc
CFLAGS=-Wall -Werror -ggdb
Z80EXT_LOC=/home/ceade/build/z80ex
CTK_LOC=/home/ceade/src/personal/github/ctk

INCLUDE=-I. -I$(Z80EXT_LOC)/include -I$(CTK_LOC)

all: z80emoo

z80emoo: z80emoo.c
	$(CC) $(CFLAGS) $(INCLUDE) -lpthread -lncursesw -o $@ $< $(Z80EXT_LOC)/z80ex.o $(CTK_LOC)/ctk.o

clean:
	rm -f z80emoo
