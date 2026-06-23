CC      ?= cc
CFLAGS  = -std=c99 -pedantic -Werror -Wall -Wextra -Wvla -O2
LDLIBS  = -lcriterion

TARGET  = wlkom_control
SRCS    = src/main.c src/crypto.c src/protocol.c src/network.c src/commands.c
OBJS    = $(SRCS:.c=.o)

TEST_SRCS = tests/test_server.c tests/test_commands.c
TEST_OBJS = $(TEST_SRCS:.c=.o)
TEST_BIN  = test_suite

.PHONY: all check clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

$(TEST_BIN): $(TEST_OBJS) src/crypto.o src/protocol.o src/network.o src/commands.o
	$(CC) $(CFLAGS) -I./include -o $@ $^ $(LDLIBS)

%.o: %.c
	$(CC) $(CFLAGS) -I./include -c $< -o $@

check: $(TEST_BIN)
	./$(TEST_BIN)

clean:
	$(RM) $(OBJS) $(TEST_OBJS) $(TARGET) $(TEST_BIN)
