CC=gcc
CFLAGS=-Wall
OBJ=main.c

main: $(OBJ)
	gcc -o $@ $^ $(CFLAGS)