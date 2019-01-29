CV=$(shell if [ `uname` = "Darwin" ]; then echo "c11"; elif [ `uname` = "Linux" ]; then echo "gnu11"; fi;)
CC=$(shell if [ `uname` = "Darwin" ]; then echo "cc"; elif [ `uname` = "Linux" ]; then echo "gcc"; fi;)
CFLAGS=-c -Wall -O3 -std=$(CV) -pedantic
#CFLAGS=-c -Wall -Og -pg -std=$(CV) -pedantic

ifeq ($(ssl),false)
	SSL_DEF=
	SSL_LIB=
else
	SSL_DEF=$(shell if [ `uname` = "Darwin" ]; then echo ""; elif [ `uname` = "Linux" ]; then echo "-DWITH_OPENSSL"; fi;)
	SSL_LIB=$(shell if [ `uname` = "Darwin" ]; then echo ""; elif [ `uname` = "Linux" ]; then echo "-lssl -lcrypto"; fi;)
endif

SRC_DIR=.
BIN_DIR=../bin
SRCS=$(shell find $(SRC_DIR) -type f -name "*.c" -print)
HEADERS=$(shell find $(SRC_DIR) -type f -name "*.h" -print)
OBJS=$(SRCS:.c=.o)
LIBS=-lm -lpthread $(SSL_LIB)
TARGET=$(BIN_DIR)/perfer

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
