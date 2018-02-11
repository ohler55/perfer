CC=cc
CV=$(shell if [ `uname` = "Darwin" ]; then echo "c11"; elif [ `uname` = "Linux" ]; then echo "gnu11"; fi;)
CFLAGS=-c -Wall -O3 -std=$(CV) -pedantic

SRC_DIR=.
BIN_DIR=../bin
SRCS=$(shell find $(SRC_DIR) -type f -name "*.c" -print)
HEADERS=$(shell find $(SRC_DIR) -type f -name "*.h" -print)
OBJS=$(SRCS:.c=.o)
LIBS=-lm -lpthread
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
	$(CC) -I. $(CFLAGS) -o $@ $<
