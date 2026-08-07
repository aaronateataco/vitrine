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
CONFIG_SRC   = test_config.c   $(SRC)/config.c $(SRC)/roms.c $(SRC)/entry.c

.PHONY: all plain compile clean

all: clean
	@echo "== building with sanitizers =="
	@$(CC) $(CFLAGS) $(SANFLAGS) -o test_homebrew $(HOMEBREW_SRC)
	@$(CC) $(CFLAGS) $(SANFLAGS) -o test_config   $(CONFIG_SRC)
	@echo "== test_homebrew ==" && ./test_homebrew
	@echo "== test_config ==="  && ./test_config

plain: clean
	@$(CC) $(CFLAGS) -O2 -o test_homebrew $(HOMEBREW_SRC)
	@$(CC) $(CFLAGS) -O2 -o test_config   $(CONFIG_SRC)
	@./test_homebrew && ./test_config

# Compiles every source at -O2, which surfaces the format-truncation and
# array-bounds diagnostics that only run with optimisation enabled.
compile:
	@set -e; for f in $(SRC)/*.c; do \
		$(CC) $(CFLAGS) -O2 -Werror -c $$f -o /dev/null; \
	done; echo "all sources compile clean at -O2 -Wall -Wextra -Werror"

clean:
	@rm -rf test_homebrew test_config testdata-homebrew testdata-config
