#include <stdlib.h>
#include <signal.h>
#include <pthread.h>
#include <string.h>

#include <sys/types.h>
#include <sys/wait.h>
#include <sys/ptrace.h>
#include <machine/reg.h>

#include "cvm/log.h"
#include "monitor.h"
#include "daemon.h"

int monitor_pid;

// hostcall.c
extern void destroy_carrie_thread(struct c_thread *ct);

int handler(snapshot_req_t *req, snapshot_resp_t *resp)
{
    int ret, i, val;
    pthread_t tid;
    struct reg new_regs;

    ret = ptrace(PT_ATTACH, monitor_pid, 0, 0);
    if (ret != 0)
    {
        printf("ptrace PT_ATTACH failed\n");
    }

    wait(&val);
    dlog("daemon: receive wait val=%d.\n", val);
    for (i = 0; i < MAX_THREADS; i++)
    {
        tid = req->sub_threads[i].task_id;
        if (tid == 0)
        {
            break;
        }
        dlog("daemon: ptrace to get regs of tid=%d.\n", tid);
        if (ptrace(PT_GETREGS, tid, &resp->contexts[i].gp_regs, 0) != 0)
        {
            dlog("daemon: PT_GETREGS failed.\n");
        }
        dlog("daemon: sub_threads[%d] pc=%p\n", i, resp->contexts[i].gp_regs.sepc);
        dlog("daemon: sub_threads[%d] sp=%p\n", i, resp->contexts[i].gp_regs.sp);

        if (ptrace(PT_GETCAPREGS, tid, &resp->contexts[i].cap_regs, 0) != 0)
        {
            dlog("daemon: PT_GETCAPREGS failed.\n");
        }
        dlog("daemon: sub_threads[%d] ddc:", i);
        CHERI_CAP_PRINT(resp->contexts[i].cap_regs.ddc);
        dlog("daemon: sub_threads[%d] sepcc:", i);
        CHERI_CAP_PRINT(resp->contexts[i].cap_regs.sepcc);
        
        memcpy(&new_regs, &resp->contexts[i].gp_regs, sizeof(struct reg));
        // terminate the thread according to the ddc mode 
        // (host_exit or destroy_carrie_thread)
        unsigned long ddc_base = cheri_getbase(resp->contexts[i].cap_regs.ddc);
        if(ddc_base>0) { // compart mode
            new_regs.sepc = req->host_exit_addr;
        } else { // monitor mode 
            // pass parameter here
            // get struct c_thread* ct as parameter
            new_regs.a[0] = req->sub_threads[i].ct;
            new_regs.sepc = destroy_carrie_thread;
        }
        ptrace(PT_SETREGS, tid, &new_regs, 0);
        // kill(tid, 9);
    }

    ptrace(PT_DETACH, monitor_pid, 0, 0);
    return 0;
}

int daemon_main(int child_pid, int req_pipe, int resp_pipe)
{
    snapshot_req_t req;
    snapshot_resp_t resp;
    siginfo_t info;

    dlog("daemon: daemon_main hello, child_pid=%d\n", child_pid);
    monitor_pid = child_pid;

    while (1)
    {
        memset(&req, 0, sizeof(req));
        memset(&resp, 0, sizeof(resp));
        read(req_pipe, &req, sizeof(req));
        dlog("daemon: receive snapshot request, size=%d! main_thread_id=%d, req.sub_thread_ids[0]=%d, host_exit_addr=%p\n", sizeof(req), req.main_thread_id, req.sub_threads[0].task_id, req.host_exit_addr);
        handler(&req, &resp);
        dlog("daemon: ready to send snapshot response! fd=%d\n", resp_pipe);
        write(resp_pipe, &resp, sizeof(resp));

        waitid(P_PID, monitor_pid, &info, WSTOPPED | WEXITED);
        dlog("daemon: monitor crash! receive wait val\n");
        dlog("si_signo=%p, si_code=%p, si_addr=%p, si_status=%d\n", info.si_signo, info.si_code, info.si_addr, info.si_status);
        if (WIFEXITED(info.si_status))
        {
            dlog("daemon: monitor exit normally. daemon exit.\n");
            exit(0);
        }
        else if (WIFSIGNALED(info.si_status))
        {
            dlog("WTERMSIG=%d\n", WTERMSIG(info.si_status));
        }
        else if (WIFSTOPPED(info.si_status))
        {
            dlog("WSTOPPED=%d\n", WSTOPSIG(info.si_status));
        } else {
            dlog("unknown si_status=%d.\n", info.si_status);
            exit(0);
        }
    }
}