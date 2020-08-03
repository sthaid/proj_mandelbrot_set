TARGETS = mbs

CC = gcc
OUTPUT_OPTION=-MMD -MP -o $@
CFLAGS = -Wall -g -O2 -Iutil -I.
#CFLAGS = -Wall -g     -Iutil -I.

# ^^^ O2

util/util_sdl.o: CFLAGS += $(shell sdl2-config --cflags)

SRC_MBS = mbs.c \
          util/util_misc.c \
          util/util_sdl.c \
          util/util_png.c \
          util/util_jpeg.c

DEP = $(SRC_MBS:.c=.d) 

#
# build rules
#

all: $(TARGETS)

mbs: $(SRC_MBS:.c=.o)
	$(CC) -lpthread -lm -ljpeg -lpng -lSDL2 -lSDL2_ttf -lSDL2_mixer \
              -o $@ $(SRC_MBS:.c=.o)

-include $(DEP)

#
# clean rule
#

clean:
	rm -f $(TARGETS) $(DEP) $(DEP:.d=.o)
