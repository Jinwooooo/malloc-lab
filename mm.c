#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>

#include "mm.h"
#include "memlib.h"

team_t team = {
    "Lost Ark",
    "Thamine",
    "nocturn@kazeroth.petra",
    "",
    ""
};

/***** [MOD] FUNCTION CALL *****/
static void *coalesce(void *curr_ptr);
static void *extend_heap(size_t size);
static void *find_first_fit(size_t size);
static void place(void *curr_ptr, size_t a_size);
static void remove_free_block(void *curr_ptr);
// static int mm_check();

// skeletal code from CS:APP - diagram 9.43
/***** DECLARING CONSTANTS *****/
#define ALIGNMENT           8 
#define SIZE4               4       // word and hdr/ftr size (4 bytes)
#define SIZE8               8       // double word size (8 bytes)
#define INITSIZE            16      // initial size of free list
#define BLOCKSIZE           16      // default block size

/***** DECLARING MACRO *****/
#define ALIGN(size)         (((size) + (ALIGNMENT-1)) & ~0x7) 
#define MAX(x,y)            ((x) > (y) ? (x) : (y))
#define PACK(size, alloc)   ((size) | (alloc))
#define GET(curr_ptr)       (*(unsigned int *)(curr_ptr))
#define PUT(curr_ptr,val)   (*(unsigned int *)(curr_ptr) = (val))
#define GET_SIZE(curr_ptr)  (GET(curr_ptr) & ~0x7)  // ~0x7 = 00000111 = masks out three LSB, used for memory alignment
#define GET_ALLOC(curr_ptr) (GET(curr_ptr) & 0x1)   // 0x1 = 00000001 = isolates LSB => LSB = 1 means memory is considered alloated 0 otherwise 
#define HDRP(curr_ptr)      ((char *)(curr_ptr) - SIZE4)                                    // HeaDeR Pointer
#define FTRP(curr_ptr)      ((char *)(curr_ptr) + GET_SIZE(HDRP(curr_ptr)) - SIZE8)         // FooTeR Pointer
#define NEXT_BLKP(curr_ptr) ((char *)(curr_ptr) + GET_SIZE(((char *)(curr_ptr) - SIZE4)))   // NEXT BLocK
#define PREV_BLKP(curr_ptr) ((char *)(curr_ptr) + GET_SIZE(((char *)(curr_ptr) - SIZE8)))   // PREV BLocK

// [MOD] to traverse free list
#define NEXT_FREE(free_ptr)  (*(void **)(free_ptr))
#define PREV_FREE(free_ptr)  (*(void **)(free_ptr + SIZE4))

static char *heap_head_ptr = 0;
static char *free_list_ptr = 0;

/* 
structure visualized
mm_init - Initializes the heap like that shown below.

    4 bytes        4 bytes               8+ bytes               4 byte          4 byte
|--------------|--------------|--------------|--------------|--------------|--------------|
|   PROLOGUE   |    HEADER    |           PAYLOAD           |    FOOTER    |    HEADER    |
|--------------|--------------|--------------|--------------|--------------|--------------|
^              ^              ^       
heap_head_ptr  free_list_ptr  curr_ptr 
*/

int mm_init(void) {
    // retrieving 16 byte; (void *) - 1 means that it has failed to retrive memory
    if ((heap_head_ptr = mem_sbrk(4 * SIZE4)) == (void *) - 1)
        return -1;

    PUT(heap_head_ptr + (0 * SIZE4), PACK(BLOCKSIZE,1));    // Prologue Header
    PUT(heap_head_ptr + (1 * SIZE4), PACK(BLOCKSIZE,0));    // Free Block Header

    PUT(heap_head_ptr + (2 * SIZE4), PACK(0,0));            // Prev Pointer Space 
    PUT(heap_head_ptr + (3 * SIZE4), PACK(0,0));            // Next Pointer Space

    PUT(heap_head_ptr + (4 * SIZE4), PACK(BLOCKSIZE,0));    // Free Block Footer
    PUT(heap_head_ptr + (5 * SIZE4), PACK(0,1));            // Eplilogue Header

    // Prologue is for heap, move 4 bytes to point the free block header
    free_list_ptr = heap_head_ptr + SIZE4;

    return 0;
}

void *mm_malloc(size_t size) {
    // if size is 0, no need to malloc anything
    if(size == 0)
        return NULL;

    size_t alloc_size = MAX(ALIGN(size) + SIZE8, BLOCKSIZE);
    size_t extend_size;
    char *curr_ptr;

    // search free list to fit the block
    if((curr_ptr = find_first_fit(alloc_size))) {
        place(curr_ptr, alloc_size);
        return curr_ptr;
    }

    // if free list cannot allocate, get more memory and place block
    extend_size = MAX(alloc_size, BLOCKSIZE);
    if((curr_ptr = extend_heap(extend_size / SIZE4)) == NULL)
        return NULL;

    // place the newly allocated block
    place(curr_ptr, alloc_size);

    return curr_ptr;
}

void *mm_realloc(void *curr_ptr, size_t size) {
    // void *oldptr = ptr;
    // void *newptr;
    // size_t copySize;
    
    // newptr = mm_malloc(size);
    // if (newptr == NULL)
    //   return NULL;
    // copySize = *(size_t *)((char *)oldptr - SIZE_T_SIZE);
    // if (size < copySize)
    //   copySize = size;
    // memcpy(newptr, oldptr, copySize);
    // mm_free(oldptr);
    // return newptr;
}

// CS:APP - diagram 9.46
void mm_free(void *curr_ptr) {
    size_t size = GET_SIZE(HDRP(curr_ptr));

    PUT(HDRP(curr_ptr), PACK(size,0));
    PUT(FTRP(curr_ptr), PACK(size,0));

    coalesce(curr_ptr);
}

// skeletal code from CS:APP - diagram 9.46
// [MOD] function to merge unused space
static void *coalesce(void *curr_ptr) {
    // determining the current allocation state of prev and next block
    size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(curr_ptr)));
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(curr_ptr)));

    // get the size of the next free block
    size_t size = GET_SIZE(HDRP(curr_ptr));

    // case 1 : prev block & next block are both allocated (i.e. does not coalesce) so not in code
    // case 2 : prev block is allocated & next block is free (i.e. coalesce next block)
    if(prev_alloc && !next_alloc) {
        size += GET_SIZE(HDRP(NEXT_BLKP(curr_ptr)));
        remove_free_block(NEXT_BLKP(curr_ptr));
        PUT(HDRP(curr_ptr), PACK(size,0));
        PUT(FTRP(curr_ptr), PACK(size,0));
    // case 3 : prev block is free & next block is allocated (i.e. coalesce prev block)
    } else if(!prev_alloc && next_alloc) {
        size += GET_SIZE(HDRP(PREV_BLKP(curr_ptr)));
        remove_free_block(curr_ptr);
        PUT(FTRP(curr_ptr), PACK(size,0));
        PUT(HDRP(PREV_BLKP(curr_ptr)), PACK(size,0));
        curr_ptr = PREV_BLKP(curr_ptr);
    // case 4 : previous & next block are both free (i.e. coalesce next & prev block)
    } else if(!prev_alloc && !next_alloc) {
        size += GET_SIZE(HDRP(PREV_BLKP(curr_ptr))) + GET_SIZE(FTRP(NEXT_BLKP(curr_ptr)));
        remove_free_block(PREV_BLKP(curr_ptr));
        remove_free_block(NEXT_BLKP(curr_ptr));
        curr_ptr = PREV_BLKP(curr_ptr);
        PUT(HDRP(curr_ptr), PACK(size, 0));
        PUT(FTRP(curr_ptr), PACK(size, 0)); 
    }

    // insert the coalesced block at the front of the free list
    NEXT_FREE(curr_ptr) = free_list_ptr;
    PREV_FREE(free_list_ptr) = curr_ptr;
    PREV_FREE(curr_ptr) = NULL;
    free_list_ptr = curr_ptr;

    return curr_ptr;
}

// CS:APP - diagram 9.45
// [MOD] increase the size heap if not enough free list
static void *extend_heap(size_t size) {
    char *brk_ptr;
    size_t alloc_size;

    // adjust the size so the alignment and min block size req are satisfied
    alloc_size = (size % 2) ? (size + 1) * SIZE4 : size * SIZE4;
    if(alloc_size < BLOCKSIZE)
        alloc_size = BLOCKSIZE;

    // attempt to grow the heap by the adjusted size
    if((brk_ptr = mem_sbrk(alloc_size)) == (void *) - 1)
        return NULL;

    // set the hdr and ftr of the newly created free block
    PUT(HDRP(brk_ptr), PACK(alloc_size,0));
    PUT(FTRP(brk_ptr), PACK(alloc_size,0));
    // move the epilogue (tail) to the end
    PUT(HDRP(NEXT_BLKP(brk_ptr)), PACK(0,1));

    return coalesce(brk_ptr);
}

// TODO
static void *find_first_fit(size_t size) {
    void *ff_ptr;

    for(ff_ptr = free_list_ptr; GET_ALLOC(HDRP(ff_ptr)) == 0; ff_ptr = NEXT_FREE(ff_ptr)) {
        if(size <= GET_SIZE(HDRP(ff_ptr)))
            return ff_ptr;
    }

    return NULL;
}

// TODO
static void place(void *curr_ptr, size_t alloc_size) {
    // Gets the total size of the free block 
    size_t free_size = GET_SIZE(HDRP(curr_ptr));

    if((free_size - alloc_size ) >= BLOCKSIZE) {
        PUT(HDRP(curr_ptr), PACK(alloc_size,1));
        PUT(FTRP(curr_ptr), PACK(alloc_size,1));

        remove_free_block(curr_ptr);
        curr_ptr = NEXT_BLKP(curr_ptr);

        PUT(HDRP(curr_ptr), PACK(free_size - alloc_size, 0));
        PUT(FTRP(curr_ptr), PACK(free_size - alloc_size, 0));

        coalesce(curr_ptr);
    } else {
        PUT(HDRP(curr_ptr), PACK(free_size, 1));
        PUT(FTRP(curr_ptr), PACK(free_size, 1));

        remove_free_block(curr_ptr);
    }
}

// TODO
static void remove_free_block(void *curr_ptr) {
    if(curr_ptr) {
        if(PREV_FREE(curr_ptr))
            NEXT_FREE(PREV_FREE(curr_ptr)) = NEXT_FREE(curr_ptr);
        else
            free_list_ptr = NEXT_FREE(curr_ptr);

        if(NEXT_FREE(curr_ptr) != NULL)
            PREV_FREE(NEXT_FREE(curr_ptr)) = PREV_FREE(curr_ptr);
    }
}













