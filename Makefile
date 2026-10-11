CC = gcc
CFLAGS = -O3 -Wall -Wextra -Wpedantic
CPPFLAGS = -Isrc

SRC = src/main.c src/bitboard.c src/evaluation.c src/PST.c
OBJ = $(SRC:.c=.o)

all: chess

chess: $(OBJ)
	$(CC) $(CFLAGS) -o chess $(OBJ)

src/%.o: src/%.c src/bitboard.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c -o $@ $<

-include $(OBJ:.o=.d)

clean:
	rm -f src/*.o src/*.d chess

.PHONY: all clean
