#ifndef TFORK_H
#define TFORK_H

#define MAX_CVMS	10

int tfork(void *, void *, int);

struct cvm_tmplt_ctx
{
    void *s0;
};

struct map_entry {
    struct map_entry *next;

    unsigned long start, end;
    int prot;
};
typedef struct map_entry map_entry;

extern struct cvm_tmplt_ctx cvm_ctx[MAX_CVMS];
extern map_entry* cvm_map_entry_list[MAX_CVMS];
extern int cvm_snapshot_fd[MAX_CVMS];

#endif
