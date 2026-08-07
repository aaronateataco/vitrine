# Host-side test suite. Builds the platform-independent sources against a stub
# of the libnx API so the parsing and scanning logic can be exercised without a
# Switch or a cross toolchain.
#
#   make        build and run under AddressSanitizer + UBSan
#   make plain  build and run without sanitizers

CC      ?= gcc
SRC      = ../source
CFLAGS   = -std=gnu11 -Wall -Wextra -g -I stub -I $(SRC)
SANFLAGS = -O1 -fsanitize=address,undefined -fno-omit-frame-pointer

HOMEBREW_SRC = test_homebrew.c $(SRC)/homebrew.c $(SRC)/entry.c
SHELVES_SRC  = test_shelves.c  $(SRC)/shelves.c $(SRC)/config.c $(SRC)/entry.c
CONFIG_SRC   = test_config.c   $(SRC)/config.c $(SRC)/roms.c $(SRC)/entry.c

.PHONY: all plain compile clean

all: clean
	@echo "== building with sanitizers =="
	@$(CC) $(CFLAGS) $(SANFLAGS) -o test_homebrew $(HOMEBREW_SRC)
	@$(CC) $(CFLAGS) $(SANFLAGS) -o test_config   $(CONFIG_SRC)
	@$(CC) $(CFLAGS) $(SANFLAGS) -o test_shelves  $(SHELVES_SRC)
	@echo "== test_homebrew ==" && ./test_homebrew
	@echo "== test_config ==" && ./test_config
	@echo "== test_shelves ==" && ./test_shelves

plain: clean
	@$(CC) $(CFLAGS) -O2 -o test_homebrew $(HOMEBREW_SRC)
	@$(CC) $(CFLAGS) -O2 -o test_config   $(CONFIG_SRC)
	@$(CC) $(CFLAGS) -O2 -o test_shelves  $(SHELVES_SRC)
	@./test_homebrew && ./test_config && ./test_shelves

# Compiles at -O2, which surfaces the format-truncation and array-bounds
# diagnostics that only run with optimisation enabled.
#
# Only the platform-independent sources are listed. render.c, icons.c, ui.c and
# main.c need real SDL2 headers, so they are covered by the cross build in CI
# rather than here.
PORTABLE_SRC = entry.c config.c roms.c homebrew.c titles.c launch.c shelves.c

compile:
	@set -e; for f in $(PORTABLE_SRC); do \
		$(CC) $(CFLAGS) -O2 -Werror -c $(SRC)/$$f -o /dev/null; \
	done; echo "portable sources compile clean at -O2 -Wall -Wextra -Werror"

clean:
	@rm -rf test_homebrew test_config test_shelves testdata-homebrew testdata-config testdata-shelves.ini
