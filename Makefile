CC := gcc
FLEX := $(shell command -v flex 2>/dev/null || echo ./tools/bin/flex)
BISON := $(shell command -v bison 2>/dev/null || echo ./tools/bin/bison)

CFLAGS := -D_POSIX_C_SOURCE=200809L -std=c11 -Wall -Wextra -pedantic -g
LDFLAGS :=

TARGET := compiler
SRCS := main.c ast.c error.c semantic.c ir.c ircode.c optimizer.c mips.c parser.tab.c lex.yy.c
OBJS := $(SRCS:.c=.o)

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDFLAGS)

parser.tab.c parser.tab.h: parser.y
	$(BISON) -d -Wall -Wno-precedence -o parser.tab.c parser.y

lex.yy.c: lexer.l parser.tab.h
	$(FLEX) -o lex.yy.c lexer.l

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(TARGET) $(OBJS) parser.tab.c parser.tab.h lex.yy.c
