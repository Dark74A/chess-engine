CC = gcc
CFLAGS = -O3 -Wall

SRC = src/main.c src/bitboard.c src/evaluation.c src/PST.c
OBJ = $(SRC:.c=.o)

chess: $(OBJ)
	$(CC) $(CFLAGS) -o chess $(OBJ)

clean:
	rm -f src/*.o chess
