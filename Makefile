#
# Atari STE YM2149 PCM compiler
#
# Native compiler: GCC. Override with `make CC=m68k-atari-mintelf-gcc`
# when building the compiler for an Atari/m68k target.
#
# Output:
#   steymc
#
#


CC      = gcc

CFLAGS  += -std=c17 -O3 -Wall -Wextra -Iinclude

LDFLAGS += -lm

TARGET := steymc

SRC := \
	src/main.c \
	src/optimizer.c \
	src/scheduler.c \
	src/viterbi.c \
	src/player_codegen.c \
	src/devpac.c

OBJ := $(SRC:.c=.o)

BUILD := build

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(OBJ) $(LDFLAGS) -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ)
	rm -f $(TARGET)

install:
	mkdir -p /usr/local/bin
	cp $(TARGET) /usr/local/bin/

.PHONY: all clean install