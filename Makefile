CC       := gcc
CFLAGS   ?= -O2 -Wall -std=c99
LDFLAGS  ?=
LDLIBS   ?= -lSDL2 -lSDL2_image -lSDL2_ttf -lSDL2_mixer -lm
INSTALL  ?= install
PREFIX   ?= /usr/local
BINDIR   ?= $(PREFIX)/bin

TARGET   = dofus-arena
# la source principale peut s'appeler game.c ou main.c
SRC      = $(if $(wildcard game.c),game.c,main.c)

ifeq ($(OS),Windows_NT)
TARGET   = dofus-arena.exe
LDLIBS  := -lmingw32 -lSDL2main $(LDLIBS) -lws2_32
CFLAGS  += -D_WIN32_WINNT=0x0A00
endif

.PHONY: all install clean

all: $(TARGET)

$(TARGET): $(SRC) tools/bot_model.h
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(SRC) $(LDLIBS)

install: $(TARGET)
	$(INSTALL) -d "$(DESTDIR)$(BINDIR)"
	$(INSTALL) -m 755 $(TARGET) "$(DESTDIR)$(BINDIR)/$(TARGET)"

clean:
	rm -f $(TARGET)
