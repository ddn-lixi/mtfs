/*
 * Copyright (C) 2011 Li Xi <pkuelelixi@gmail.com>
 */

#include <debug.h>
#include <memory.h>
#include <bitmap.h>
#include <mtfs_random.h>
#define MAX_BRANCH_NUMBER 128

typedef enum operation_type {
	OPERATION_TYPE_SET = 0,
	OPERATION_TYPE_CLEAR,
	OPERATION_TYPE_CHECK,
	OPERATION_TYPE_NUM,
} operation_type_t;

int random_test(mtfs_bitmap_t *bitmap, int *array)
{
	int ret = 0;
	operation_type_t operation = 0;
	int nbit = 0;
	int output = 0;
	int expect = 0;
	
	MASSERT(bitmap);
	MASSERT(array);
	
	operation = random() % OPERATION_TYPE_NUM;
	nbit = random() % MAX_BRANCH_NUMBER;
	MASSERT(operation >= OPERATION_TYPE_SET && operation < OPERATION_TYPE_NUM);
	MASSERT(nbit >= 0 && nbit < MAX_BRANCH_NUMBER);
	switch(operation) {
	case OPERATION_TYPE_SET:
		mtfs_bitmap_set(bitmap, nbit);
		array[nbit] = 1;
		break;
	case OPERATION_TYPE_CLEAR:
		mtfs_bitmap_clear(bitmap, nbit);
		array[nbit] = 0;
		break;
	case OPERATION_TYPE_CHECK:
		//fprintf(stderr, "%d ", nbit);
		output = mtfs_bitmap_check(bitmap, nbit);
		MASSERT(nbit >= 0 && nbit < MAX_BRANCH_NUMBER);
		expect = array[nbit];
		if (output != expect) {
			MERROR("BUG found: expect %d, got %d\n", expect, output);
			ret = 1;
		}
		break;
	default:
		MERROR("Unexpected operation: %d\n", operation);
		ret = -1;
		break;
	}

	return ret;
}

int main()
{
	int ret = 0;
	mtfs_bitmap_t *bitmap = NULL;
	int *array = NULL;
	int i = 0;
	
	bitmap = mtfs_bitmap_allocate(MAX_BRANCH_NUMBER);
	if (bitmap == NULL) {
		ret = -ENOMEM;
		goto out;
	}
	
	MTFS_ALLOC(array, MAX_BRANCH_NUMBER * sizeof(*array));
	if (array == NULL) {
		ret = -ENOMEM;
		goto out;
	}
	/* MTFS_ALLOC will do this for us */
	//memset(array, 0, MAX_BRANCH_NUMBER * sizeof(*array));

	mtfs_random_init(0);
	for(i = 0; i < 1000000; i++) {
		ret = random_test(bitmap, array);
		if (ret) {
			goto error;
		}
	}

	goto out;
error:
	MPRINT("Seted bits: ");
	mtfs_bitmap_dump(bitmap);
out:
	if (bitmap) {
		mtfs_bitmap_freee(bitmap);
	}
	if (array) {
		MTFS_FREE(array, MAX_BRANCH_NUMBER * sizeof(*array));
	}
	return ret;
}
