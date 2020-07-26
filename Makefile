TARGETS = my_first_mbset 

CC = gcc
OUTPUT_OPTION=-MMD -MP -o $@
CFLAGS = -g -O2 \
         -Wall -Wextra -Wno-unused-parameter -Wno-sign-compare -Wno-clobbered 

SRC_CTLR    = my_first_mbset.c

DEP=$(SRC_CTLR:.c=.d) 

#
# build rules
#

all: $(TARGETS)

my_first_mbset: $(SRC_CTLR:.c=.o)
	$(CC) -pthread -lreadline -lm -o $@ $(SRC_CTLR:.c=.o)

-include $(DEP)

#
# clean rule
#

clean:
	rm -f $(TARGETS) $(DEP) $(DEP:.d=.o)

