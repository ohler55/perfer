ifeq ($(shell uname -s), Darwin)
	CV := c11
	CC := cc
else
	CV := gnu11
	CC := gcc
endif

CFLAGS := -c -Wall -O3 -std=$(CV) -pedantic
#CFLAGS := -c -Wall -Og -pg -std=$(CV) -pedantic

HAS_SSL := $(strip $(wildcard /usr/include/openssl/ssl.h))
ifeq ($(HAS_SSL),)
	HAS_SSL := $(strip $(wildcard /usr/local/include/openssl/ssl.h))
endif

SSL_DEF :=
SSL_LIB :=

ifneq ($(ssl),false)
	ifneq ($(HAS_SSL),)
		SSL_DEF := -DWITH_OPENSSL
		SSL_LIB := -lssl -lcrypto
	endif
endif

SRC_DIR := .
BIN_DIR := ../bin
SRCS := $(shell find $(SRC_DIR) -type f -name "*.c" -print)
HEADERS := $(shell find $(SRC_DIR) -type f -name "*.h" -print)
OBJS := $(SRCS:.c=.o)
LIBS := -lm -lpthread $(SSL_LIB)
TARGET := $(BIN_DIR)/perfer

all: $(BIN_DIR) $(TARGET)

clean:
	$(RM) $(OBJS)
	$(RM) $(TARGET)

$(BIN_DIR):
	mkdir -p $@

$(TARGET): $(OBJS)
	$(CC) -o $@ $(OBJS) $(LIBS)

%.o : %.c  $(HEADERS)
	$(CC) -I. $(CFLAGS) $(SSL_DEF) -o $@ $<
