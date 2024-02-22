#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>

#include "mm.h"
#include "memlib.h"

team_t team = {
    ".",
    "Jinwooooo",
    ".",
    "",
    ""
};

/***** [MOD] FUNCTION CALL *****/
static void *coalesce(void *curr_ptr);
static void *extend_heap(size_t size);
static void *find_first_fit(size_t size);
static void place(void *curr_ptr, size_t a_size);
static void remove_free_block(void *curr_ptr);

// skeletal code from CS:APP - diagram 9.43
/***** DECLARING CONSTANTS *****/
#define ALIGNMENT           8 
#define SIZE4               4       // word and hdr/ftr size (4 bytes)
#define SIZE8               8       // double word size (8 bytes)
#define DEFAULTBLOCKSIZE    16      // default block size  

/***** DECLARING MACRO *****/
#define ALIGN(size)         (((size) + (ALIGNMENT-1)) & ~0x7) 
#define MAX(x,y)            ((x) > (y) ? (x) : (y))
#define PACK(size, alloc)   ((size) | (alloc))
#define GET(curr_ptr)       (*(size_t *)(curr_ptr))
#define PUT(curr_ptr,val)   (*(size_t *)(curr_ptr) = (val))
#define GET_SIZE(curr_ptr)  (GET(curr_ptr) & ~0x1)  // ~0x1 = 11111110 = masks out one LSB, used for memory alignment
#define GET_ALLOC(curr_ptr) (GET(curr_ptr) & 0x1)   // 0x1 = 00000001 = isolates LSB => LSB = 1 means memory is considered alloated 0 otherwise 
#define HDRP(curr_ptr)      ((void *)(curr_ptr) - SIZE4)                             // HeaDeR Pointer
#define FTRP(curr_ptr)      ((void *)(curr_ptr) + GET_SIZE(HDRP(curr_ptr)) - SIZE8)  // FooTeR Pointer
#define NEXT_BLKP(curr_ptr) ((void *)(curr_ptr) + GET_SIZE(HDRP(curr_ptr)))          // NEXT BLocK
#define PREV_BLKP(curr_ptr) ((void *)(curr_ptr) - GET_SIZE(HDRP(curr_ptr) - SIZE4))  // PREV BLocK

// [MOD] to traverse free list
#define NEXT_FREE(free_ptr)  (*(void **)(free_ptr))
#define PREV_FREE(free_ptr)  (*(void **)(free_ptr + SIZE4))

static char *heap_head_ptr = 0;
static char *free_list_ptr = 0; // [MOD] to keep track of explicit Doubly Linked List

/* 
---------------------------------------------
basic heap structure visualized

    4 bytes        4 bytes               8+ bytes               4 byte          4 byte
|--------------|--------------|--------------|--------------|--------------|--------------|
|   PROLOGUE   |    HEADER    |           PAYLOAD           |    FOOTER    |   EPILOGUE   |
|--------------|--------------|--------------|--------------|--------------|--------------|
^                             ^       
heap_head_ptr                 curr_ptr 

---------------------------------------------
free list structure visualized

    4 bytes        4 bytes        4 bytes           1+ bytes          4 byte
|--------------|--------------|--------------|-------------------|--------------|
|    HEADER    |   PREV_BLK   |   NEXT_BLK   |   TRASH_PAYLOAD   |    FOOTER    |
|--------------|--------------|--------------|-------------------|--------------|

Besides the free list structure, this application will keep the free block in front of the
heap (logically, not physically). This produces extra steps in coalescing and realloc.
---------------------------------------------
*/


/*
mm_init visualized

    4 bytes        4 bytes         4 byte         4 byte        4 byte          4 byte           8 byte
|--------------|--------------|--------------|--------------|--------------|--------------|------------------|
|   PROLOGUE   |    HEADER    |  PREV & NEXT BLK or PAYLOAD |    FOOTER    |   EPILOGUE   |      EMPTY       |
|--------------|--------------|--------------|--------------|--------------|--------------|------------------|
^              ^                     
heap_head_ptr  free_list_ptr
*/

/***** ASGN MAIN FUNCTIONS *****/
int mm_init(void) {
  // 4 (prologue) + 4 (blk header) + 4 (free header) + 4 (free footer) + 4 (blk footer) + 4 (epilogue) = 24 byte
  if ((heap_head_ptr = mem_sbrk(16 + DEFAULTBLOCKSIZE)) == (void *) - 1)
      return -1; 

    PUT(heap_head_ptr + (0 * SIZE4), PACK(DEFAULTBLOCKSIZE,1));    // Prologue
    PUT(heap_head_ptr + (1 * SIZE4), PACK(DEFAULTBLOCKSIZE,0));    // Header

    PUT(heap_head_ptr + (2 * SIZE4), PACK(0,0));                   // Prev Blk
    PUT(heap_head_ptr + (3 * SIZE4), PACK(0,0));                   // Next Blk 
  
    PUT(heap_head_ptr + (4 * SIZE4), PACK(DEFAULTBLOCKSIZE,0));    // Footer
    PUT(heap_head_ptr + (5 * SIZE4), PACK(0,1));                   // Eplilogue

    // Prologue is for heap, move 4 bytes to point the header
    free_list_ptr = heap_head_ptr + (SIZE4);

    return 0;
}

void *mm_malloc(size_t curr_size) {
    // base case
    if(curr_size == 0)
        return NULL;

    // minimum size = 16 bytes
    size_t alloc_size = MAX(ALIGN(curr_size) + SIZE8, DEFAULTBLOCKSIZE);
    size_t extend_size;
    char *curr_ptr;
    
    // search free list to fit the block
    if((curr_ptr = find_first_fit(alloc_size))) {
        place(curr_ptr, alloc_size);
        return curr_ptr;
    }

    // if free list cannot allocate, get more memory and place block
    extend_size = MAX(alloc_size, DEFAULTBLOCKSIZE);
    if ((curr_ptr = extend_heap(extend_size / SIZE4)) == NULL)
        return NULL;

    // Place the newly allocated block
    place(curr_ptr, alloc_size);

    return curr_ptr;
}

// CS:APP - diagram 9.46
void mm_free(void *curr_ptr) { 
    size_t size = GET_SIZE(HDRP(curr_ptr));

    PUT(HDRP(curr_ptr), PACK(size,0));
    PUT(FTRP(curr_ptr), PACK(size,0));

    coalesce(curr_ptr);
}

// [MOD] because freed block will be moved to the front (not physically, only logically), extra procedures are required
void *mm_realloc(void *curr_ptr, size_t size) {
    if (curr_ptr == NULL)
        return mm_malloc(size);

    if (size == 0) {
        mm_free(curr_ptr);
        return NULL;
    }
    
    size_t alloc_size = MAX(ALIGN(size) + SIZE8, DEFAULTBLOCKSIZE);
    size_t curr_size = GET_SIZE(HDRP(curr_ptr));

    void *next_ptr;
    char *next_blk = HDRP(NEXT_BLKP(curr_ptr));
    size_t new_size = curr_size + GET_SIZE(next_blk);

    // base case
    if(alloc_size == curr_size)
        return curr_ptr;

    // size is less than the curr payload
    if(alloc_size <= curr_size) {
        if(alloc_size > DEFAULTBLOCKSIZE && (curr_size - alloc_size) > DEFAULTBLOCKSIZE) {  
            PUT(HDRP(curr_ptr), PACK(alloc_size, 1));
            PUT(FTRP(curr_ptr), PACK(alloc_size, 1));
            next_ptr = NEXT_BLKP(curr_ptr);
            PUT(HDRP(next_ptr), PACK(curr_size - alloc_size, 1));
            PUT(FTRP(next_ptr), PACK(curr_size - alloc_size, 1));
            mm_free(next_ptr);

            return curr_ptr;
        }

        next_ptr = mm_malloc(alloc_size);
        memcpy(next_ptr, curr_ptr, alloc_size);
        mm_free(curr_ptr);

        return next_ptr;
    // size is greater than the curr payload
    } else {
        // next block is free and is able to fit -> merge block to the required size
        if(!GET_ALLOC(next_blk) && new_size >= alloc_size ) {
            remove_free_block(NEXT_BLKP(curr_ptr));
            PUT(HDRP(curr_ptr), PACK(alloc_size, 1));
            PUT(FTRP(curr_ptr), PACK(alloc_size, 1));
            next_ptr = NEXT_BLKP(curr_ptr);
            PUT(HDRP(next_ptr), PACK(new_size - alloc_size, 1));
            PUT(FTRP(next_ptr), PACK(new_size - alloc_size, 1));
            mm_free(next_ptr);

            return curr_ptr;
        }  
        // not able to fit -> allocate a new block and free the current block
        next_ptr = mm_malloc(alloc_size); 
        memcpy(next_ptr, curr_ptr, curr_size);
        mm_free(curr_ptr);
        return next_ptr;
    }

}

/***** MAIN ASSISTING FUNCTIONS *****/

// skeletal code from CS:APP - diagram 9.46
// [MOD] function to merge unused space
static void *coalesce(void *curr_ptr) {
    // determining the current allocation state of prev and next block
    size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(curr_ptr))) || PREV_BLKP(curr_ptr) == curr_ptr;
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(curr_ptr)));

    // get current block size to compound prev or next freed block
    size_t curr_size = GET_SIZE(HDRP(curr_ptr));

    // case 2 : prev block is allocated & next block is free (i.e. coalesce next block)
    if(prev_alloc && !next_alloc) {
        curr_size += GET_SIZE(HDRP(NEXT_BLKP(curr_ptr)));  
        remove_free_block(NEXT_BLKP(curr_ptr));
        PUT(HDRP(curr_ptr), PACK(curr_size, 0));
        PUT(FTRP(curr_ptr), PACK(curr_size, 0));
    // case 3 : prev block is free & next block is allocated (i.e. coalesce prev block)
    } else if(!prev_alloc && next_alloc) {
        curr_size += GET_SIZE(HDRP(PREV_BLKP(curr_ptr)));
        curr_ptr = PREV_BLKP(curr_ptr); 
        remove_free_block(curr_ptr);
        PUT(HDRP(curr_ptr), PACK(curr_size, 0));
        PUT(FTRP(curr_ptr), PACK(curr_size, 0));
    // case 4 : previous & next block are both free (i.e. coalesce next & prev block)
    } else if (!prev_alloc && !next_alloc) { 
        curr_size += GET_SIZE(HDRP(PREV_BLKP(curr_ptr))) + GET_SIZE(HDRP(NEXT_BLKP(curr_ptr)));
        remove_free_block(PREV_BLKP(curr_ptr));
        remove_free_block(NEXT_BLKP(curr_ptr));
        curr_ptr = PREV_BLKP(curr_ptr);
        PUT(HDRP(curr_ptr), PACK(curr_size, 0));
        PUT(FTRP(curr_ptr), PACK(curr_size, 0));
    }

    // insert the coalesced block at the front of the free list
    NEXT_FREE(curr_ptr) = free_list_ptr;
    PREV_FREE(free_list_ptr) = curr_ptr;
    PREV_FREE(curr_ptr) = NULL;
    free_list_ptr = curr_ptr;

    return curr_ptr;
}

// skeletal code from CS:APP - diagram 9.45
// [MOD] increase the size heap if free list cannot handle new payload
static void *extend_heap(size_t curr_size) {
      char *curr_ptr;
      size_t alloc_size;

      // adjust the size so the alignment and min block size req are satisfied
      alloc_size = (curr_size % 2) ? (curr_size + 1) * SIZE4 : curr_size * SIZE4;
      if (alloc_size < DEFAULTBLOCKSIZE)
            alloc_size = DEFAULTBLOCKSIZE;

      // attempt to grow the heap by the adjusted size 
      if ((curr_ptr = mem_sbrk(alloc_size)) == (void *)-1)
            return NULL;

      // set the hdr and ftr of the newly created free block
      PUT(HDRP(curr_ptr), PACK(alloc_size, 0));
      PUT(FTRP(curr_ptr), PACK(alloc_size, 0));
      // make the epilogue (tail) at the end
      PUT(HDRP(NEXT_BLKP(curr_ptr)), PACK(0, 1));

      return coalesce(curr_ptr); 
}

// [MOD] find the first fit in the free list
static void *find_first_fit(size_t size) {
  void *ff_ptr;

    for(ff_ptr = free_list_ptr; GET_ALLOC(HDRP(ff_ptr)) == 0; ff_ptr = NEXT_FREE(ff_ptr)) {
        if(size <= GET_SIZE(HDRP(ff_ptr)))
            return ff_ptr;
    }

    return NULL;
}

// [MOD] places payload into the curr_ptr position
static void place(void *curr_ptr, size_t alloc_size) {
    // Gets the total size of the free block 
    size_t free_size = GET_SIZE(HDRP(curr_ptr));

    if((free_size - alloc_size) >= DEFAULTBLOCKSIZE) {
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

// [MOD] Doubly Linked List node removal function
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
