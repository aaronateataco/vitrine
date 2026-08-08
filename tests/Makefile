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
SHELVES_SRC  = test_shelves.c  $(SRC)/shelves.c $(SRC)/config.c $(SRC)/entry.c $(SRC)/overrides.c $(SRC)/vendor/cJSON.c
CONFIG_SRC   = test_config.c   $(SRC)/config.c $(SRC)/roms.c $(SRC)/entry.c
OVERRIDE_SRC = test_overrides.c $(SRC)/overrides.c $(SRC)/entry.c $(SRC)/vendor/cJSON.c
ROOMS_SRC    = test_rooms.c     $(SRC)/rooms.c $(SRC)/entry.c $(SRC)/vendor/cJSON.c
RA_SRC       = test_ra.c        $(SRC)/ra.c $(SRC)/vendor/cJSON.c

.PHONY: all plain compile clean

all: clean
	@echo "== building with sanitizers =="
	@$(CC) $(CFLAGS) $(SANFLAGS) -o test_homebrew $(HOMEBREW_SRC)
	@$(CC) $(CFLAGS) $(SANFLAGS) -o test_config   $(CONFIG_SRC)
	@$(CC) $(CFLAGS) $(SANFLAGS) -o test_shelves  $(SHELVES_SRC)
	@$(CC) $(CFLAGS) $(SANFLAGS) -o test_overrides $(OVERRIDE_SRC)
	@$(CC) $(CFLAGS) $(SANFLAGS) -o test_rooms     $(ROOMS_SRC) -lm
	@$(CC) $(CFLAGS) $(SANFLAGS) -o test_ra        $(RA_SRC)
	@echo "== test_homebrew ==" && ./test_homebrew
	@echo "== test_config ==" && ./test_config
	@echo "== test_shelves ==" && ./test_shelves
	@echo "== test_overrides ==" && ./test_overrides
	@echo "== test_rooms ==" && ./test_rooms
	@echo "== test_ra ==" && ./test_ra

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
# vendor/cJSON.c is third-party and deliberately excluded from -Werror.
PORTABLE_SRC = entry.c config.c roms.c homebrew.c titles.c launch.c shelves.c overrides.c rooms.c ra.c

compile:
	@set -e; for f in $(PORTABLE_SRC); do \
		$(CC) $(CFLAGS) -O2 -Werror -c $(SRC)/$$f -o /dev/null; \
	done; echo "portable sources compile clean at -O2 -Wall -Wextra -Werror"

clean:
	@rm -rf test_homebrew test_config test_shelves test_overrides test_rooms test_ra testdata-rooms.json testdata-homebrew testdata-config testdata-shelves.ini testdata-config.json
