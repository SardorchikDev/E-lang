CC = gcc
CFLAGS = -std=c11 -Wall -Wextra -pedantic -g
TARGET = e-lang
SOURCES = main.c lexer.c parser.c interpreter.c runtime.c builtins.c files.c dump.c formatter.c analyzer.c
OBJECTS = $(SOURCES:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJECTS) -lm

test: $(TARGET)
	./$(TARGET) --test

clean:
	rm -f $(TARGET) $(OBJECTS)

.PHONY: all test clean
