CC=gcc
CFLAGS=-g -Wall -D_FILE_OFFSET_BITS=64
LDFLAGS=-lfuse

OBJ=tfs.o block.o

%.o: %.c
	$(CC) -lpthread -c $(CFLAGS) $< -o $@

tfs: $(OBJ)
	$(CC) $(OBJ) $(LDFLAGS) -lpthread -o tfs

.PHONY: clean
clean:
	rm -f *.o tfs

