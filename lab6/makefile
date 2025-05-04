CC = gcc
CFLAGS = -W -Wall -Wextra -std=c11 -pedantic -g
LDFLAGS =

all: gen sort_index view

gen: gen.c
	$(CC) $(CFLAGS) -o gen gen.c $(LDFLAGS)

sort_index: sort_index.c
	$(CC) $(CFLAGS) -o sort_index sort_index.c $(LDFLAGS)

view: view.c
	$(CC) $(CFLAGS) -o view view.c $(LDFLAGS)

clean:
	rm -f gen sort_index view

.PHONY: all clean