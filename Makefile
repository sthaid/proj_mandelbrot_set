TARGETS = my_first_mbs mbs

CC = gcc
OUTPUT_OPTION=-MMD -MP -o $@
CFLAGS = -Wall -g -O2 -Iutil -I.

util/util_sdl.o: CFLAGS += $(shell sdl2-config --cflags)

SRC_MY_FIRST_MBS = my_first_mbs.c
SRC_MBS = mbs.c \
          num.c \
          util/util_misc.c \
          util/util_sdl.c \
          util/util_png.c \
          util/util_jpeg.c

DEP = $(SRC_MY_FIRST_MBS:.c=.d) \
      $(SRC_MBS:.c=.d) 

#
# build rules
#

all: $(TARGETS)

my_first_mbs: $(SRC_MY_FIRST_MBS:.c=.o)
	$(CC) -lm -o $@ $(SRC_MY_FIRST_MBS:.c=.o)

mbs: $(SRC_MBS:.c=.o)
	$(CC) -lm -ljpeg -lpng -lSDL2 -lSDL2_ttf -lSDL2_mixer \
              -o $@ $(SRC_MBS:.c=.o)

-include $(DEP)

#
# clean rule
#

clean:
	rm -f $(TARGETS) $(DEP) $(DEP:.d=.o)
