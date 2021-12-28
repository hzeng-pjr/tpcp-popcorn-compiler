###############################################################################
# Common
###############################################################################

BUILD := ../build
INC   := ../include
SRC   := src
UTILS := utils
LIB   := $(BUILD)/libremote_io.a
INST  := /tmp/popcorn

CFLAGS := -g -Wall -D_GNU_SOURCE -Wno-unused-function -I$(INC)

ifeq ($(type),debug)
CFLAGS += -O0 -D_DEBUG -D_CHECKS -D_LOG
else
CFLAGS += -O3
endif

all: server client

server: test_server.c test.h
	$(CC) $(CFLAGS) -o $@ $< $(BUILD)/libremote_io.a

client: test_client.c test.h
	$(CC) $(CFLAGS) -o $@ $< $(BUILD)/libremote_io.a

clean:
	rm -f *~ client server
