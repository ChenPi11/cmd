#!/usr/bin/make -f

ifneq (,)
This makefile requires GNU Make.
endif

CC ?= cc
RM ?= rm -f
INSTALL ?= install
LN ?= ln -sfn

CFLAGS ?= -O3 -Wall -Wextra -Wpedantic
LDFLAGS ?=
PREFIX ?= /usr/local

SRCS = $(wildcard *.c)
CMD_OBJS = $(patsubst %.c,%.cmd.o,$(SRCS))
COMMAND_OBJS = $(patsubst %.c,%.COMMAND.o,$(SRCS))

.PHONY: all
all: cmd.exe COMMAND.COM

%.cmd.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

cmd.exe : $(CMD_OBJS)
	$(CC) $(LDFLAGS) $^ -o $@

%.COMMAND.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@ -DENABLE_AUTOEXEC=1

COMMAND.COM: $(COMMAND_OBJS)
	$(CC) $(LDFLAGS) $^ -o $@

.PHONY: clean
clean:
	$(RM) cmd.exe COMMAND.COM $(CMD_OBJS) $(COMMAND_OBJS)

.PHONY: install
install: all
	$(INSTALL) -m 755 cmd.exe $(PREFIX)/bin/
	$(INSTALL) -m 755 COMMAND.COM $(PREFIX)/bin/
	$(LN) $(PREFIX)/bin/cmd.exe $(PREFIX)/bin/cmd
	@echo "Successfully installed to $(PREFIX)"
	@echo "You may want to copy AUTOEXEC.BAT to your root directory so that COMMAND.COM is automatically executed when you start COMMAND.COM."
