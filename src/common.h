#ifndef COMMON_H
#define COMMON_H

#include <SDL3/SDL.h>
#include <stdint.h>

/* Load entire file into memory. Caller must SDL_free() the result. */
uint8_t *load_file(const char *path, uint32_t *out_len);

#endif /* COMMON_H */
