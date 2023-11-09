#include <stdio.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <assert.h>
#include <ucontext.h>
#include <machine/ucontext.h>
#include <machine/reg.h>

#include "monitor.h"
#include "tfork.h"
#include "cvm/log.h"
#include "daemon.h"

#define SIGSAVE SIGUSR1
#define RET_COMP_PPC (16 * 11)
#define RET_COMP_DDC (16 * 12)

#define offsetof(type, field) __offsetof(type, field)
// borrowed from CheriBSD freebsd64_machdep.c
#define	CONTEXT64_GPREGS	(offsetof(struct gpregs, gp_sepc) / sizeof(register_t))

const int TFORK_FAILED = MAP_FAILED;
const static int tfork_syscall_num = 577;
extern struct c_thread *get_cur_thread();

map_entry *cvm_map_entry_list[MAX_CVMS];
int cvm_snapshot_fd[MAX_CVMS];

int tfork(void *src_addr, void *dst_addr, int len)
{
    return syscall(tfork_syscall_num, src_addr, dst_addr, len);
}

unsigned int parse_permstr(char *perms)
{
    unsigned int res = 0;
    if (perms[0] == 'r')
        res |= PROT_READ;
    if (perms[1] == 'w')
        res |= PROT_WRITE;
    if (perms[2] == 'x')
        res |= PROT_EXEC;
    return res;
}

map_entry *get_map_entry_list(int cid)
{
    FILE *map_file = fopen("/proc/curproc/map", "r");
    assert(map_file != NULL);

    map_entry *map_entry_list = NULL, *rear = NULL;

    unsigned long range_low, range_high;
    range_low = cvms[cid].cmp_begin;
    range_high = cvms[cid].cmp_end;

    unsigned long start, end;
    int resident, privateresident;
    void *obj;
    char permstr[32] = "";

    char map_buf[256];
    while (fgets(map_buf, sizeof(map_buf), map_file))
    {
        int num = sscanf(map_buf, "0x%lx 0x%lx %d %d %p %31s",
                         &start, &end,
                         &resident, &privateresident,
                         &obj, permstr);
        assert(num == 6);

        if (end - 1 < range_low)
            continue;
        if (start >= range_high)
            break;

        int prot = parse_permstr(permstr);
        map_entry *entry = (map_entry *)malloc(sizeof(map_entry));
        entry->start = start;
        entry->end = end;
        entry->prot = prot;
        entry->next = NULL;

        if (map_entry_list == NULL)
        {
            map_entry_list = entry;
        }
        else
        {
            rear->next = entry;
        }
        rear = entry;
    }
    fclose(map_file);
    return map_entry_list;
}

void save_cur_thread_and_exit(int cid, struct c_thread *cur_thread)
{
    register void *cur_sp asm("sp");
    register void *cur_ra asm("ra");
    register void *cur_s0 asm("s0");
    asm(""
        : "=r"(cur_sp), "=r"(cur_ra), "=r"(cur_s0));

#ifndef TFORK
    // get memory layout of template memory region
    map_entry *map_entry_list = get_map_entry_list(cid);
    cvm_map_entry_list[cid] = map_entry_list;

    // print_map_entry_list(map_entry_list);

    // save memory memory content
    int fd = memfd_create(cvms[cid].libos, 0);
    // change file size according to cmp size
    unsigned long cmp_begin = cvms[cid].cmp_begin;
    unsigned long cmp_end = cvms[cid].cmp_end;
    size_t cmp_size = cmp_end - cmp_begin;
    // set the memfd size
    ftruncate(fd, cmp_size);
    cvm_snapshot_fd[cid] = fd;

    // create mmap shared region
    void *res = mmap(NULL, cmp_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    assert(res != MAP_FAILED);

    dlog("cmp_begin = 0x%lx, cmp_end = 0x%lx\n", cmp_begin, cmp_end);
    dlog("snapshot data memory region: %p - %p\n", res, res + cmp_size);

    uint8_t *snapshot_data = res;
    unsigned long offset = 0;
    map_entry *p = map_entry_list;
    while (p != NULL)
    {
        size_t size = p->end - p->start;

        memcpy(snapshot_data + offset, p->start, size);

        offset += size;
        p = p->next;
    }
    assert(munmap(snapshot_data, cmp_size) == 0);
#endif
    // printf("save status = %d\n", status);
    // __asm__ __volatile__("sd sp, %0" :"=m" (cur_sp) :: "memory");

    dlog("sp is %p; ra is %p;\n", cur_sp, cur_ra);

    // cur_thread->ctx.s0 = cur_sp;
    // cur_thread->ctx.ra = cur_ra;
    cur_thread->ctx.s0 = cur_s0;

    destroy_carrie_thread(cur_thread->sbox->threads);
}

// note: restore main thread of cvm (from half of tp_write function)
// TODO: restore threads in two mode
void gen_caps_restored(struct c_thread *target_thread)
{
    target_thread->m_tp = getTP();
    target_thread->c_tp = (void *)(target_thread->stack + 4096);

    struct s_box *cvm = target_thread->sbox;
    struct cvm_tmplt_ctx *ctx = &target_thread->ctx;
    struct s_box *t_cvm = &cvms[cvm->t_cid];

    void *prev_s0 = (void *)(*(uint64_t *)(ctx->s0 - 16) + 112);
    void *__capability *caps = prev_s0 - 3 * sizeof(void *__capability);
    
    void *ret_comp_pc = cheri_getoffset(caps[2]);
    printf("thread[tid=%x], ret_from_mon's address is 0x%x\n", target_thread->tid, ret_comp_pc);
    void *__capability ret_comp_pcc = codecap_create(cvm->cmp_begin, cvm->cmp_end);
    ret_comp_pcc = cheri_setaddress(ret_comp_pcc, comp_to_mon((unsigned long long)ret_comp_pc, cvm));

    void *__capability ret_comp_dcap = datacap_create((void *)cvm->cmp_begin, (void *)cvm->cmp_end);

    void *__capability sealcap;
    size_t sealcap_size = sizeof(sealcap);
    if (sysctlbyname("security.cheri.sealcap", &sealcap, &sealcap_size, NULL, 0) < 0)
    {
        printf("sysctlbyname(security.cheri.sealcap)\n");
        while (1)
            ;
    }

    // note: write caps to local_cap_store region
    void *__capability *local_cap_store = comp_to_mon(0xe001000, cvm);
    void *__capability *comp_ddc = ((uint64_t)local_cap_store) + 2 * sealcap_size;
    void *__capability *sealed_pcc = ((uint64_t)local_cap_store) + 11 * sealcap_size;
    void *__capability *sealed_ddc = ((uint64_t)local_cap_store) + 12 * sealcap_size;
    *sealed_pcc = cheri_seal(ret_comp_pcc, sealcap);
    *sealed_ddc = cheri_seal(ret_comp_dcap, sealcap);
    *comp_ddc = datacap_create((void *)cvm->cmp_begin, (void *)cvm->cmp_end);

    dlog("gen_caps_restored: sealed_pcc \n");
    CHERI_CAP_PRINT(*sealed_pcc);
    dlog("gen_caps_restored: sealed_ddc \n");
    CHERI_CAP_PRINT(*sealed_ddc);
    dlog("gen_caps_restored: comp_ddc \n");
    CHERI_CAP_PRINT(*comp_ddc);

    // note: initialize the sp
    void *sp = mon_to_comp(prev_s0, t_cvm);

    // note: restore sp register and cinvoke to the ret_from_monitor
    __asm__ __volatile__(
        "ld sp, %0;"
        "lc ct0, %1;"
        "lc ct1, %2;"
        "lc ct2, %3;"
        "cspecialw ddc, ct2;"
        "CInvoke ct0, ct1;" ::"m"(sp),
        "m"(*sealed_pcc), "m"(*sealed_ddc), "m"(*comp_ddc));
}

// note: start a new thread from template ucontext
long load_ucontext(struct c_thread *target_thread)
{
    ucontext_t uctx;
    struct s_box *cvm, *t_cvm;
    struct capregs mc_capregs; // ucontext cap_regs
    size_t sealcap_size;

    cvm = target_thread->sbox;
    t_cvm = &cvms[cvm->t_cid];

    target_thread->m_tp = getTP();
    target_thread->c_tp = (void *)(target_thread->stack + 4096);

    memset(&uctx, 0, sizeof(uctx));
    memset(&mc_capregs, 0, sizeof(mc_capregs));
    // use gp_regs and cp_regs to initialize uctx.uc_mcontext ;
    memcpy(&(uctx.uc_mcontext.mc_gpregs), &(target_thread->gp_regs), sizeof(struct reg));
    memcpy(&mc_capregs, &(target_thread->cap_regs), sizeof(struct capreg));
    mc_capregs.cp_sstatus = target_thread->gp_regs.sstatus;

    // TODO: consider the monitor mode(ddc.base=0) thread
    void* __capability ddc=target_thread->cap_regs.ddc;
    unsigned long base = cheri_getbase(ddc);
    if (base==0x0) { // monitor
        // change tp (gpregs, capregs)
        uctx.uc_mcontext.mc_gpregs.gp_tp = target_thread->m_tp;
        mc_capregs.cp_ctp = (uintptr_t)target_thread->m_tp;
        // change sp (gpregs, capregs)
        void* sp = uctx.uc_mcontext.mc_gpregs.gp_sp;
        sp = sp - t_cvm->base + cvm->base;
        uctx.uc_mcontext.mc_gpregs.gp_sp = sp;
        mc_capregs.cp_csp = (uintptr_t)sp;

        uctx.uc_mcontext.mc_capregs = &mc_capregs;
        uctx.uc_mcontext.mc_flags = 0x0;
    } else { // compartment
        // change tp (gpregs, capregs)
        uctx.uc_mcontext.mc_gpregs.gp_tp = target_thread->c_tp;
        mc_capregs.cp_ctp = (uintptr_t)target_thread->c_tp;
        // change sepc (gpregs)
        register_t sepc = uctx.uc_mcontext.mc_gpregs.gp_sepc; // absolute
        sepc = sepc - t_cvm->cmp_begin; // cap-relative
        uctx.uc_mcontext.mc_gpregs.gp_sepc = sepc;
        // change sepcc (capregs)
        // cooperate with sepc, the final sepcc = cheri_setoffset(cp_sepcc, sepc)
        void *__capability cp_sepcc = codecap_create(cvm->cmp_begin, cvm->cmp_end);
        // change ddc (capregs)
        void *__capability cp_ddc = datacap_create(cvm->cmp_begin, (void *)cvm->cmp_end);
        mc_capregs.cp_sepcc = cp_sepcc;
        mc_capregs.cp_ddc = cp_ddc;

        // note: set mc_capregs and set flag
        uctx.uc_mcontext.mc_capregs = &mc_capregs;
        // note: would use mc_capregs(add sepc) to set new context
        uctx.uc_mcontext.mc_flags = _MC_CAP_VALID;
    }

    dlog("monitor: load_ucontext, &ucontext=%p, &mc_capregs=%p\n", &uctx, &mc_capregs);
    dlog("sizeof(uctx)=%lu, sizeof(mc_capregs)=%lu\n", sizeof(ucontext_t), sizeof(struct capregs));

    setcontext(&uctx);
}

long load_sub_thread(struct c_thread *ct, struct c_thread *t_ct)
{
    int ret = pthread_attr_init(&ct->tattr);
    if (ret != 0)
    {
        perror("attr init");
        printf("ret = %d\n", ret);
        while (1)
            ;
    }

    // TODO: change thread stack here? Does the fini execution correctly?
    void* res=mmap(NULL, TEMP_STACK_SIZE, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANON, -1, 0);
    assert(res!=MAP_FAILED);
    ct->temp_stack = res;
    ret = pthread_attr_setstack(&ct->tattr, ct->temp_stack, TEMP_STACK_SIZE);
    if (ret != 0)
    {
        perror("pthread attr setstack");
        printf("ret = %d\n", ret);
    }

#ifdef __linux__
    ret = pthread_attr_setaffinity_np(&ct->tattr, sizeof(ct->sbox->cpuset), &ct->sbox->cpuset);
    if (ret != 0)
    {
        perror("pthread set affinity");
        printf("ret = %d\n", ret);
    }
#endif
    ret = pthread_create(&ct->tid, &ct->tattr, load_ucontext, ct);
    if (ret != 0)
    {
        perror("pthread create");
        printf("ret = %d\n", ret);
        while (1)
            ;
    }

    return (long)ct->tid;
}

void load_all_thread(int cid)
{
    struct s_box *cvm = &cvms[cid];
    struct c_thread *me = cvm->threads;
    int t_cid = me->sbox->t_cid;
    struct c_thread *t_me = cvms[t_cid].threads;

    // note: initialize all sub-threads
    for (int i = 1; i < 63; i++)
    {
        // note:global is initalize as zero
        if (t_me[i].gp_regs.sp == NULL)
        {
            break;
        }
        dlog("monitor: load_all_thread, t_me[%d]\n", i);

        memcpy(&me[i], &t_me[i], sizeof(struct c_thread));
        me[i].sbox = cvm;
        // change stack base addr
        me[i].stack = t_me[i].stack - t_me->sbox->base + cvm->base;
        // change func
        me[i].func = t_me[i].func - t_me->sbox->base + cvm->base;
        dlog("derived cvm has sub-thread, i=%d\n", i);
        load_sub_thread(&me[i], &t_me[i]);
    }

    // note: restore main thread of cvm
    gen_caps_restored(me);
}

void notify_other_thread_save(struct c_thread *cur_thread)
{
    int i, capreg_size;
    void *sp;
    extern int send_req, receive_resp;
    struct c_thread *threads;
    snapshot_req_t req;
    snapshot_resp_t resp;

    ucontext_t uctx;
    struct capregs *stack_capregs;

    threads = cur_thread->sbox->threads;
    assert(cur_thread == threads);

    memset(&req, 0, sizeof(req));
    req.main_thread_id = threads[0].task_id;
    req.host_exit_addr = cur_thread->sbox->base + cur_thread->sbox->host_exit_addr;
    dlog("monitor: main_thread_id=%d\n", req.main_thread_id);
    for (i = 1; i < 62; ++i)
    {
        if (threads[i].task_id == NULL)
        {
            break;
        }
        dlog("monitor: threads[%d].tid=%d\n", i, threads[i].task_id);
        req.sub_threads[i - 1].task_id = threads[i].task_id;
        req.sub_threads[i - 1].pthread_id = threads[i].tid;
        req.sub_threads[i - 1].ct = &(threads[i]);
    }
    if (i == 1)
    {
        return;
    }

    dlog("monitor: ready to send snapshot req. fd=%d\n", send_req);
    write(send_req, &req, sizeof(req));

    dlog("monitor: ready to read snapshot response. fd=%d\n", receive_resp);
    read(receive_resp, &resp, sizeof(resp));
    dlog("monitor: receive snapshot response.\n");

    // save ptrace result
    for (i = 0; i < MAX_THREADS; i++)
    {
        sp = (void*)resp.contexts[i].gp_regs.sp;
        if (sp == NULL)
        {
            break;
        }

        threads[i+1].gp_regs = resp.contexts[i].gp_regs;
        threads[i+1].cap_regs = resp.contexts[i].cap_regs;
    };
    dlog("monitor: notify_other_thread_save finish.\n");
}
