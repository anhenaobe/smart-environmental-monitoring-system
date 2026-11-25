CC = gcc
CFLAGS = -Wall -Wextra -Werror -std=c11 -g -Isrc
LDFLAGS = -lcmocka

SRC = src/metrics.c
TEST_SRC = tests/test_metrics.c
TARGET = tests/test_metrics

all: $(TARGET)

$(TARGET): $(SRC) $(TEST_SRC)
	$(CC) $(CFLAGS) $(SRC) $(TEST_SRC) -o $(TARGET) $(LDFLAGS)

test: $(TARGET)
	./$(TARGET)

valgrind: $(TARGET)
	valgrind --leak-check=full --error-exitcode=1 ./$(TARGET)

clean:
	rm -f $(TARGET)

.PHONY: all test valgrind clean
