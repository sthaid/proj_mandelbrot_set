TARGETS = mbs

CC = gcc
OUTPUT_OPTION=-MMD -MP -o $@
CFLAGS = -Wall -g -O2 -Iutil -I.

util/util_sdl.o: CFLAGS += $(shell sdl2-config --cflags)

SRC_MBS = mbs.c

DEP = $(SRC_MBS:.c=.d)

#
# build rules
#

all: $(TARGETS)

mbs: $(SRC_MBS:.c=.o)
	$(CC) -lm -o $@ $(SRC_MBS:.c=.o)

-include $(DEP)

#
# clean rule
#

clean:
	rm -f $(TARGETS) $(DEP) $(DEP:.d=.o)
