/*
 * mm.c - Memory management functions
 *
 *  Created on: 21 Nov 2016
 *      Author: Ander Juaristi
 */
#include <stdlib.h>

struct mm_array {
	void *arr;
	size_t len;
	size_t last_idx;
};
struct mm_array_ptr {
	void **arr;
	size_t len;
	size_t last_idx;
};

static void __attribute__((noreturn)) __mm_no_memory()
{
	exit(EXIT_FAILURE);
}

static void mm_array_increment_and_double_if_needed(struct mm_array *array)
{
	if (++array->last_idx == array->len) {
		array->len <<= 1;
		array->arr = mm_realloc(array->arr, array->len);
	}
}

void *mm_realloc(void *ptr, size_t len)
{
	void *newptr = realloc(ptr, len);

	if (!newptr)
		__mm_no_memory();

	return newptr;
}

void __attribute__((__malloc__)) *mm_malloc0(size_t len)
{
	return mm_mallocn0(len, 1);
}

void __attribute__((__malloc__)) *mm_mallocn0(size_t len, size_t count)
{
	void *ptr = calloc(count, len);

	if (!ptr)
		__mm_no_memory();

	return ptr;
}

/*
 * Does not accept NULL items.
 */
void mm_array_append_ptr(struct mm_array_ptr *array, void *item)
{
	if (!array || !item)
		return;

	mm_array_increment_and_double_if_needed((struct mm_array *) array);
	array->arr[array->last_idx] = item;
}

void mm_array_append_size_t(struct mm_array *array, size_t item)
{
	if (!array)
		return;

	mm_array_increment_and_double_if_needed(array);
	array->arr[array->last_idx] = item;
}

void mm_array_remove(struct mm_array *array, size_t index)
{
	if (!array || index >= array->len)
		return;

	for (size_t j = index; j < array->len; j++) {
		if (j + 1 < array->len)
			array->arr[j] = array->arr[j + 1];
	}
}
