/* 2013-11826, JuGyeong Lim
 *
 * mm.c
 *
 * - Block structure
 * - Allocated block
 * ------------------------------------------------------------------
 * | HEADER : Size (including header & footer) | | | allocation bit |
 * | 						   PayLoad								|
 * | 						   PayLoad								|
 * | FOOTER : Size (including header & footer) | | | allocation bit |
 * ------------------------------------------------------------------
 *
 * - Freed block
 * ------------------------------------------------------------------
 * | HEADER : Size (including overhead)        | | | allocation bit |
 * |                    Prev free block pointer 					|
 * | 					Next free block pointer						|
 * | FOOTER : Size (including overhead)		   | | | allocation bit |
 * ------------------------------------------------------------------
 *
 * - Free list
 * Using 20 Segregated lists. Each one has blocks that have size of 2^n ~ 2^n-1.
 * seg_listp is a head pointer to the lists.
 * Segregated lists is placed on the bottom of heap (in mm_innit).
 * 20 pointers (WSIZE) point to the head of their list.
 * Each list is sorted by size, Allocator uses best-fit.
 *
 * - ETC
 * 8-byte Alignment, Every block has minimum size of 4 * WSIZE (HEADER, FOOTER, 2 PayLoad or Prev free and Next free pointers)
  */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>

#include "mm.h"
#include "memlib.h"

/* single word (4) or double word (8) alignment */
#define ALIGNMENT 8

/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(size) (((size) + (ALIGNMENT-1)) & ~0x7)

#define SIZE_T_SIZE (ALIGN(sizeof(size_t)))

/* Basic constants and macros */
#define WSIZE 4	// Word size (bytes)
#define DSIZE 8	// Double word size (bytes)
#define CHUNKSIZE (1<<6)	// Extend heap by this amount (bytes)

#define MAX(x, y) ((x) > (y) ? (x) : (y))

/* Pack a size and allocated bit into a word */
#define PACK(size, alloc) ((size) | (alloc))

/* Read and write a word at address p */
#define GET(p) 		(*(unsigned int *)(p))
#define PUT(p, val) (*(unsigned int *)(p) = (val))

/* Read the size and allocated fields from address p */
#define GET_SIZE(p)  (GET(p) & ~0x7)
#define GET_ALLOC(p) (GET(p) & 0x1)

/* Given block ptr bp, compute address of its header and footer */
#define HDRP(bp) ((char *)(bp) - WSIZE)
#define FTRP(bp) ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)

/* Given block ptr bp, compute address of next and previous blocks */
#define NEXT_BLKP(bp) ((char *)(bp) + GET_SIZE(((char *)(bp) - WSIZE)))
#define PREV_BLKP(bp) ((char *)(bp) - GET_SIZE(((char *)(bp) - DSIZE)))

/* Basic constants and macros for segregated lists */
#define MAX_SEGLIST 20	// Maximum count of segregated lists

/* PUT for segregated list */
#define PUT_SEG(p, ptr) (*(unsigned int *)(p) = (unsigned int)(ptr))

/* Get prev or next free block pointer */
#define PREV_BLKP_SEG(bp) ((char *)(bp))
#define NEXT_BLKP_SEG(bp) ((char *)(bp) + WSIZE)

/* Get prev or next free block */
#define PREV_BLK_SEG(bp) (*(char **)(bp))
#define NEXT_BLK_SEG(bp) (*(char **)(NEXT_BLKP_SEG(bp)))

/* Pick a list by the index */
#define GET_LIST(ptr, index) *((char **)ptr + index)

/* Functions */
static void *extend_heap(size_t words);
static void free_insert(void *bp, size_t size);
static void free_remove(void *bp);
static void *free_find(size_t size);
static void *coalesce(void *bp);
static void *addblock(void *bp, size_t size);

/* Static variables */
static char *heap_listp = 0; /* Start point of the heap */
static void *seg_listp;  	 /* Start point of the segregated lists */

/* 
 * mm_init - Initialize segregated lists and the malloc package.
 * Return : Success 0, Error -1
 */
int mm_init(void)
{
	int list;

	/* Bottom of heap is used segregated lists space */
	seg_listp = mem_sbrk(MAX_SEGLIST * WSIZE);

	/* Initialize segregated lists */
	for(list = 0; list < MAX_SEGLIST; list++) {
		GET_LIST(seg_listp, list) = NULL;
	}

	/* Create the initial empty heap */
	if((heap_listp = mem_sbrk(4 * WSIZE)) == (void *) -1) return -1;
	PUT(heap_listp, 0);								/* Alignment padding */
	PUT(heap_listp + (1 * WSIZE), PACK(DSIZE, 1));	/* Prologue header */
	PUT(heap_listp + (2 * WSIZE), PACK(DSIZE, 1));	/* Prologue footer */
	PUT(heap_listp + (3 * WSIZE), PACK(0, 1));		/* Epilogue header */
	heap_listp += (2 * WSIZE);

	/* Extend the empty heap with a free block of CHUNKSIZE bytes */
	if(extend_heap(CHUNKSIZE/WSIZE) == NULL) return -1;
	
	return 0;
}

/*
 * extend_heap - Extend the heap by words 
 */
static void *extend_heap(size_t words) {
	char *bp;
	size_t size;

	/* 8-bytes alignment */
	size = (((words + 1) >> 1) << 1) * WSIZE;
	if((long)(bp = mem_sbrk(size)) == -1) return NULL;

	/* Initialize free block header/footer and the epilogue header */
	PUT(HDRP(bp), PACK(size, 0));		  /* Free block header */
	PUT(FTRP(bp), PACK(size, 0));		  /* Free block footer */
	PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1)); /* New epilogue header */

	free_insert(bp, size);

	return coalesce(bp);
}

/*
 * free_insert - Insert free block into segregated list 
 */
static void free_insert(void *bp, size_t size) {
	void *list_p = NULL;
	void *insert_p = NULL;
	int list = 0;

	/* Find list for the size */
	while((list < (MAX_SEGLIST - 1)) && (size > 1)) {
		size = size >> 1;
		list++;
	}
	list_p = GET_LIST(seg_listp, list);

	/* Find pointer to insert by size */
	while((list_p != NULL) && (size > GET_SIZE(HDRP(list_p)))) {
		insert_p = list_p;
		list_p = PREV_BLK_SEG(list_p);
	}

	/* Insert the free block */
	/* If list_p is not NULL */
	if(list_p) {
		if(insert_p) {	// Find valid location : list_p > bp > insert_p (by size)
			PUT_SEG(PREV_BLKP_SEG(insert_p), bp);
			PUT_SEG(PREV_BLKP_SEG(bp), list_p);
			PUT_SEG(NEXT_BLKP_SEG(list_p), bp);
			PUT_SEG(NEXT_BLKP_SEG(bp), insert_p);
		}
		else {	// bp is the smallest block in the list
			PUT_SEG(PREV_BLKP_SEG(bp), list_p);
			PUT_SEG(NEXT_BLKP_SEG(list_p), bp);
			PUT_SEG(NEXT_BLKP_SEG(bp), NULL);
			GET_LIST(seg_listp, list) = bp;
		}
	}

	/* Else (list_p is NULL) */
	else {
		if(insert_p) {	// bp is the biggest block in the list
			PUT_SEG(PREV_BLKP_SEG(insert_p), bp);
			PUT_SEG(PREV_BLKP_SEG(bp), NULL);
			PUT_SEG(NEXT_BLKP_SEG(bp), insert_p);
		}
		else {	// New block in the empty list
			PUT_SEG(PREV_BLKP_SEG(bp), NULL);
			PUT_SEG(NEXT_BLKP_SEG(bp), NULL);
			GET_LIST(seg_listp, list) = bp;
		}
	}
	return;
}

/*
 * free_remove - Remove free block from segregated list 
 */
static void free_remove(void *bp) {
	int list = 0;
	size_t size = GET_SIZE(HDRP(bp));

	/* If bp is head of the list */
	if(NEXT_BLK_SEG(bp) == NULL) {
		while((list < (MAX_SEGLIST - 1)) && size > 1) {
			size = size >> 1;
			list++;
		}
		GET_LIST(seg_listp, list) = PREV_BLK_SEG(bp);

		/* If new head of the list is not NULL, set its NEXT is NULL */
		if(GET_LIST(seg_listp, list) != NULL) PUT_SEG(NEXT_BLKP_SEG(GET_LIST(seg_listp, list)), NULL);
	
		return;
	}

	/* If bp is not head of the list */
	PUT_SEG(PREV_BLKP_SEG(NEXT_BLK_SEG(bp)), PREV_BLK_SEG(bp));

	/* If PREV of bp is not NULL, set its NEXT */
	if(PREV_BLK_SEG(bp) != NULL) {
		PUT_SEG(NEXT_BLKP_SEG(PREV_BLK_SEG(bp)), NEXT_BLK_SEG(bp));
	}
}

/*
 * free_find - Find the best-fit location into the free list for the new block
 */
static void *free_find(size_t size) {
	size_t s = size;
	int list = 0;
	void *list_p = NULL;

	while(list < MAX_SEGLIST) {
		if((list == MAX_SEGLIST - 1) || ((s <= 1) && (GET_LIST(seg_listp, list) != NULL))) {	// Find valid list
			list_p = GET_LIST(seg_listp, list);

			/* Find best-fit block */
			while((list_p != NULL) && (size > GET_SIZE(HDRP(list_p)))) {
				list_p = PREV_BLK_SEG(list_p);			
			}
			if(list_p != NULL) break;	// Success
		}
		list++;
		s = s >> 1;
	}

	return list_p;
}

/*

 * coalesce - Join the freed blocks by case 1~4
 */
static void *coalesce(void *bp) {
	size_t prev = GET_ALLOC(HDRP(PREV_BLKP(bp)));
	size_t next = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
	size_t size = GET_SIZE(HDRP(bp));

	/* Case 1 : Prev - A, Next - A */
	if(prev && next) return bp;
	
	/* Case 2 : Prev - A, Next - F */
	else if(prev && !next) {
		size += GET_SIZE(HDRP(NEXT_BLKP(bp)));
		free_remove(bp);
		free_remove(NEXT_BLKP(bp));
		PUT(HDRP(bp), PACK(size, 0));
		PUT(FTRP(bp), PACK(size, 0));
	}
	
	/* Case 3 : Prev - F, Next - A */
	else if(!prev && next) {
		size += GET_SIZE(HDRP(PREV_BLKP(bp)));
		free_remove(bp);
		free_remove(PREV_BLKP(bp));
		PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
		PUT(FTRP(bp), PACK(size, 0));
		bp = PREV_BLKP(bp);	
	}

	/* Case 4 : Prev - F, Next - F */
	else {
		size += GET_SIZE(HDRP(PREV_BLKP(bp))) + GET_SIZE(FTRP(NEXT_BLKP(bp)));
		free_remove(PREV_BLKP(bp));
		free_remove(bp);
		free_remove(NEXT_BLKP(bp));
		PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
		PUT(FTRP(NEXT_BLKP(bp)), PACK(size, 0));
		bp = PREV_BLKP(bp);	
	}

	free_insert(bp, size);

	return bp;
}

/*
 * addblock - Add block into the valid place 
 */
static void *addblock(void *bp, size_t size) {
	size_t size_freed = GET_SIZE(HDRP(bp));
	void *np = NULL;

	free_remove(bp);

	/* Remaining block size >= Minimum block size, splitting */
	if((size_freed - size) >= (2 * DSIZE)) {
		if((size_freed - size) >= 200) {
			PUT(HDRP(bp), PACK(size_freed - size, 0));
			PUT(FTRP(bp), PACK(size_freed - size, 0));
			np = NEXT_BLKP(bp);
			PUT(HDRP(np), PACK(size, 1));
			PUT(FTRP(np), PACK(size, 1));
			free_insert(bp, size_freed - size);
			return np;		
		}
		else {
			PUT(HDRP(bp), PACK(size, 1));
			PUT(FTRP(bp), PACK(size, 1));
			np = NEXT_BLKP(bp);
			PUT(HDRP(np), PACK(size_freed - size, 0));
			PUT(FTRP(np), PACK(size_freed - size, 0));
			free_insert(np, size_freed - size);	
		}
	}

	/* Else, coalesce */
	else {
		PUT(HDRP(bp), PACK(size_freed, 1));
		PUT(FTRP(bp), PACK(size_freed, 1));
	}

	return bp;
}

/* 
 * mm_malloc - First, find valid location into the free list, if there is no valid location, extend heap
 *     Always allocate a block whose size is a multiple of the alignment.
 */
void *mm_malloc(size_t size)
{
    size_t new_size;
	size_t add_heap_size;
	char *bp;
	char *p;

	/* Size is 0 */
	if(size == 0) return NULL;

	/* Set size considering overhead */
	if(size <= DSIZE) new_size = 2 * DSIZE;
	else new_size = DSIZE * ((size + (DSIZE) + (DSIZE - 1)) / DSIZE);
	
	/* Find valid location into the free list */
	if((bp = free_find(new_size)) != NULL) {
		p = addblock(bp, new_size);
		return p;
	}

	/* Not found, extend heap */
	add_heap_size = MAX(new_size, CHUNKSIZE);
	if((bp = extend_heap(add_heap_size/WSIZE)) == NULL) return NULL;
	p = addblock(bp, new_size);	
	
	return p;
}

/*
 * mm_free - Freeing a block does nothing.
 * 			 Insert freed block into the free list and coalesce
 */
void mm_free(void *ptr)
{
	size_t size = GET_SIZE(HDRP(ptr));

	PUT(HDRP(ptr), PACK(size, 0));
	PUT(FTRP(ptr), PACK(size, 0));
	free_insert(ptr, size);
	coalesce(ptr);
}

/*
 * mm_realloc - Special case : ptr is NULL, size is 0, New Size = Old Size
 * 				3 Cases : New Size < Old Size, Old Size + Next Size > New Size > Old Size, New Size > Old Size + Next Size  
 */
void *mm_realloc(void *ptr, size_t size)
{
    void *oldptr = ptr;
    void *newptr;
	void *nextptr;
    size_t copySize;
	size_t newSize, nextSize;

	/* ptr is NULL */
	if(ptr == NULL) return mm_malloc(size);

	/* size is 0 */
	if(size == 0) {
		mm_free(ptr);
		return NULL;	
	}

	newSize = ALIGN(size);
	copySize = GET_SIZE(HDRP(oldptr)) - DSIZE;

	/* New Size is same as Old Size, do nothing */
	if(newSize == copySize) return ptr;

	/* New Size < Old Size */
	if(newSize < copySize) {
		if(copySize - newSize - DSIZE <= DSIZE) return oldptr;	// 8-byte alignment

		PUT(HDRP(oldptr), PACK(newSize + DSIZE, 1));
		PUT(FTRP(oldptr), PACK(newSize + DSIZE, 1));
		newptr = oldptr;
		oldptr = NEXT_BLKP(newptr);

		/* Free remaining block */
		PUT(HDRP(oldptr), PACK(copySize - newSize, 0));
		PUT(FTRP(oldptr), PACK(copySize - newSize, 0));
		free_insert(oldptr, GET_SIZE(HDRP(oldptr)));
		coalesce(oldptr);

		return newptr;
	}
	
	/* New Size > Old Size */
	nextptr = NEXT_BLKP(oldptr);

	/* Next block is valid and freed */
	if((nextptr != NULL) && !(GET_ALLOC(HDRP(nextptr)))) { 
		nextSize = GET_SIZE(HDRP(nextptr));

		/* Enough space */
		if(nextSize + copySize >= newSize) {
			free_remove(nextptr);
			if(nextSize + copySize - newSize <= DSIZE) {	// 8-byte alignment
				PUT(HDRP(oldptr), PACK(copySize + nextSize + DSIZE, 1));
				PUT(FTRP(oldptr), PACK(copySize + nextSize + DSIZE, 1));
				return oldptr;		
			}
			else {
				PUT(HDRP(oldptr), PACK(newSize + DSIZE, 1));
				PUT(FTRP(oldptr), PACK(newSize + DSIZE, 1));
				newptr = oldptr;
				oldptr = NEXT_BLKP(newptr);
				PUT(HDRP(oldptr), PACK(copySize + nextSize - newSize, 0));
				PUT(FTRP(oldptr), PACK(copySize + nextSize - newSize, 0));
				free_insert(oldptr, GET_SIZE(HDRP(oldptr)));
				coalesce(oldptr);
				return newptr;
			}
		}
	}

	/* Need new fit block */		
	newptr = mm_malloc(size);
	
	if(newptr == NULL) return NULL;

	memcpy(newptr, oldptr, copySize);
	mm_free(oldptr);
	return newptr;
}

/*
 * mm_check - Heap consistency checker for debugging
 * Return : 1 - OK, 0 - Error
 */
static int mm_check(void) {
	int e = 1;
	int list = 0;
	void *bp = NULL;
	void *np = NULL;
	void *tmp = NULL;

	/* Is every block in the free list marked as free? */
	while(list < MAX_SEGLIST) {
		bp = GET_LIST(seg_listp, list);
		while(bp != NULL) {
			if(GET_ALLOC(bp)) {
				printf("Error : Free block marking\n");
				e = 0;
			}
			bp = PREV_BLK_SEG(bp);		
		}
		list++;
	}
	
	bp = heap_listp;

	while(!(GET_ALLOC(HDRP(bp)) == 1) && !GET_SIZE(HDRP(bp))) {
		np = NEXT_BLKP(bp);

		/* Are there any contiguous free blocks that somehow escaped coalescing? */
		if(!GET_ALLOC(HDRP(bp)) && !GET_ALLOC(HDRP(np))) {
			printf("Error : Contiguous free blocks that escaped coalescing\n");
			e = 0;
		}

		/* Is every free block actually in the free list? */
		if(!GET_ALLOC(HDRP(bp))) {
			list = 0;
			while(list < MAX_SEGLIST) {
				tmp = GET_LIST(seg_listp, list);
				while(tmp != NULL) {
					if(tmp == bp) break;
					tmp = PREV_BLK_SEG(tmp);
				}
				if(tmp == bp) break;
				list++;			
			}

			if(tmp != bp) {
				printf("Error : Free block is not in the free list\n");
				e = 0;
			}
		}

		/* Check Header / Footer size data */
		if(GET_SIZE(HDRP(bp)) != GET_SIZE(FTRP(bp))) {
			printf("Error : Header and Footer size data are different\n");
			e = 0;		
		}
		
		/* Check Header / Footer allocation bit */
		if(GET_ALLOC(HDRP(bp)) != GET_ALLOC(FTRP(bp))) {
			printf("Error : Header and Footer allocation bits are different\n");
			e = 0;		
		}

		/* Check 8-byte alignment */
		if((size_t)bp % DSIZE) {
			printf("Error : 8-byte alignment is broken\n");
			e = 0;
		}
		
		bp = NEXT_BLKP(bp);
	}

	return e;
}

