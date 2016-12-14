/*
 * mm.c - Memory management functions
 *
 *  Created on: 21 Nov 2016
 *      Author: Ander Juaristi
 */
#include <stdlib.h>
#include "mm.h"

static void __attribute__((noreturn)) __mm_no_memory()
{
	exit(EXIT_FAILURE);
}

void *mm_reallocn(void *ptr, size_t count, size_t len)
{
	void *newptr = realloc(ptr, count * len);

	if (!newptr)
		__mm_no_memory();

	return newptr;
}

void *mm_realloc(void *ptr, size_t count)
{
	return mm_reallocn(ptr, count, 1);
}

void __attribute__((__malloc__)) *mm_malloc0(size_t len)
{
	return mm_mallocn0(1, len);
}

void __attribute__((__malloc__)) *mm_mallocn0(size_t count, size_t len)
{
	void *ptr = calloc(count, len);

	if (!ptr)
		__mm_no_memory();

	return ptr;
}
