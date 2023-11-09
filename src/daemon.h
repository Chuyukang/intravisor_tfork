#ifndef DAEMON_H
#define DAEMON_H

#include <machine/ucontext.h>
#include <sys/ptrace.h>

#ifndef MAX_THREADS
#define MAX_THREADS 63
#endif

typedef struct snapshot_req
{
    struct snapshot_thr
    {
        int pthread_id;
        int task_id;
        void* ct;
    } sub_threads[MAX_THREADS];
    pthread_t main_thread_id;
    uint64_t host_exit_addr;
} snapshot_req_t;

typedef struct snapshot_resp
{
    struct snapshot_ctx
    {
        struct reg gp_regs;
        struct capreg cap_regs;
    } contexts[MAX_THREADS];
} snapshot_resp_t;

#endif
