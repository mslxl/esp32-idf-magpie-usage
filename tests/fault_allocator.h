#ifndef NOTE4_FAULT_ALLOCATOR_H
#define NOTE4_FAULT_ALLOCATOR_H
#include <stddef.h>
#include <stdlib.h>
void *note4_test_malloc(size_t size);
void *note4_test_calloc(size_t count, size_t size);
void *note4_test_realloc(void *pointer, size_t size);
void note4_test_free(void *pointer);
#define malloc note4_test_malloc
#define calloc note4_test_calloc
#define realloc note4_test_realloc
#define free note4_test_free
#endif
