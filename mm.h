#include <stdio.h>

extern int mm_init (void);
extern void *mm_malloc (size_t size);
extern void mm_free (void *ptr);
extern void *mm_realloc(void *ptr, size_t size);

typedef struct {
    char *teamname;
    char *name1;
    char *id1;
    char *name2;
    char *id2;
} team_t;

extern team_t team;