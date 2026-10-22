CC ?= cc
PKG_CONFIG ?= pkg-config
CFLAGS ?= -O2 -pipe
WARNINGS := -Wall -Wextra -Wpedantic
GTK_CFLAGS := $(shell $(PKG_CONFIG) --cflags gtk+-3.0)
GTK_LIBS := $(shell $(PKG_CONFIG) --libs gtk+-3.0)

BIN := build/gravacaotela
SRC := src/main.c

.PHONY: all clean dist

all: $(BIN)

$(BIN): $(SRC)
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNINGS) $(GTK_CFLAGS) -o $@ $< $(LDFLAGS) $(GTK_LIBS)

dist: $(BIN)
	@mkdir -p dist/package
	cp $(BIN) README.md dist/package/
	tar -czf dist/gravacaotela-linux-x86_64.tar.gz -C dist/package .

clean:
	rm -rf build dist