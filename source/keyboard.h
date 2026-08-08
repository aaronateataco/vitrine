#pragma once

#include <stdbool.h>
#include <stddef.h>

/*
 * Wraps the system software keyboard. Text entry on a console has to go through
 * swkbd; there is no other sane way to type an API key.
 */
bool keyboard_prompt(const char *header, const char *guide, const char *initial,
                     char *out, size_t out_size);
