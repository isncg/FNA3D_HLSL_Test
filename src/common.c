#include "common.h"

uint8_t *load_file(const char *path, uint32_t *out_len)
{
	size_t size;
	void *data = SDL_LoadFile(path, &size);
	if (data == NULL)
	{
		SDL_Log("Failed to load %s: %s", path, SDL_GetError());
		return NULL;
	}
	*out_len = (uint32_t) size;
	return (uint8_t *) data;
}
