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

CFLAGS  += -O3 -Wall -Wextra
LDLIBS  += -lm

# Keep the 68000 instruction set and software floating point at compile AND
# link time, so the driver also selects compatible runtime libraries.
MACHINE := $(shell $(CC) -dumpmachine 2>/dev/null)
ARCH_FLAGS ?= $(if $(filter m68k%,$(MACHINE)),-m68000 -msoft-float)

TARGET := steymc

SRC := \
	src/main.c \
	src/optimizer.c \
	src/scheduler.c \
	src/viterbi.c \
	src/player_codegen.c \
	src/devpac.c

BUILD := build
OBJ := $(patsubst src/%.c,$(BUILD)/%.o,$(SRC))
DEP := $(OBJ:.o=.d)

# Quote configuration values as shell data, including embedded single quotes.
quote = '$(subst ','"'"',$(1))'
CONFIG = $(call quote,CC=$(CC)) $(call quote,CPPFLAGS=$(CPPFLAGS)) $(call quote,CFLAGS=$(CFLAGS)) $(call quote,ARCH_FLAGS=$(ARCH_FLAGS)) $(call quote,LDFLAGS=$(LDFLAGS)) $(call quote,LDLIBS=$(LDLIBS))
# A phony dependency also works with coarse Atari/FAT modification times.
CONFIG_CHANGED := $(shell printf '%s\n' $(CONFIG) | cmp -s - $(BUILD)/.config || printf FORCE)

all: $(TARGET)

$(TARGET): $(OBJ) $(BUILD)/.config Makefile $(CONFIG_CHANGED)
	$(CC) $(CFLAGS) $(ARCH_FLAGS) $(LDFLAGS) $(OBJ) $(LDLIBS) -o $@

$(BUILD)/%.o: src/%.c Makefile $(BUILD)/.config $(CONFIG_CHANGED) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(ARCH_FLAGS) -std=c17 -Iinclude -MMD -MP -c $< -o $@

$(BUILD):
	mkdir -p $@

# Changing compiler or options must rebuild existing objects, including when
# alternating native and cross builds. Preserve the timestamp when unchanged.
$(BUILD)/.config: FORCE | $(BUILD)
	@printf '%s\n' $(CONFIG) > $@.tmp
	@if ! cmp -s $@.tmp $@; then mv $@.tmp $@; else rm -f $@.tmp; fi

FORCE:

-include $(DEP)

clean:
	rm -f $(OBJ) $(DEP) $(SRC:.c=.o) $(BUILD)/.config $(BUILD)/.config.tmp
	rm -f $(TARGET)

install: $(TARGET)
	mkdir -p /usr/local/bin
	cp $(TARGET) /usr/local/bin/

.PHONY: all clean install FORCE
