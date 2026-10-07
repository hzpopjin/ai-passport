#include <stddef.h>
void *fullsave_probe_malloc(size_t size);
void fullsave_probe_free(void *pointer);
#define malloc fullsave_probe_malloc
#define free fullsave_probe_free
