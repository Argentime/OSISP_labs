CC = gcc
CFLAGS = -W -Wall -Wextra -std=c11 -pedantic -g
LDFLAGS = -pthread

all: main5_1 main5_2

main5_1: main5_1.c
	$(CC) $(CFLAGS) -o main5_1 main5_1.c $(LDFLAGS)

main5_2: main5_2.c
	$(CC) $(CFLAGS) -o main5_2 main5_2.c $(LDFLAGS)

clean:
	rm -f main5_1 main5_2

.PHONY: all clean