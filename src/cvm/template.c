#include "monitor.h"
#include "cvm/log.h"

// When init template cvm, we must make sure stack memory is accessable by using mmap.
int init_pthread_stack(struct s_box *cvm)
{
    struct c_thread *ct = &cvm->threads[0];
    int ret = mmap(ct->stack, ct->stack_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_FIXED | MAP_ANONYMOUS, -1, 0);
    if (ret == MAP_FAILED) {
        perror("mmap");
        return 1;
    }
    else {
        dlog("[cVM STACKs] = [%p -- %lx]\n", ct->stack, (unsigned long)ct->stack + ct->stack_size);
    }

    /* Remove temporarily.The anonymous region is zero-filled*/
    // memset(ct->stack, 0, ct->stack_size);

    place_canaries(ct->stack, ct->stack_size, 0xabbacaca);
    check_canaries(ct->stack, ct->stack_size, 0xabbacaca);
    return 0;
}

int build_cvm(int cid, struct cvm *f, 
// struct cmp_s *comp, char *libos, char *disk, 
int argc, char *argv[] 
// , char *cb_out, char *cb_in
) {
    struct encl_map_info encl_map;
    void *base = f->isol.base;
    unsigned long size = f->isol.size;
    unsigned long cmp_begin = f->isol.begin;
    unsigned long cmp_end = f->isol.end;

    memset(&encl_map, 0, sizeof(struct encl_map_info));

    load_elf(f->runtime, base, &encl_map);
    if (encl_map.base < 0)
    {
        printf("Could not load '%s', die\n", f->runtime);
        while (1)
            ;
    }

    if (encl_map.base != (unsigned long)base)
    {
        printf("mapped at wrong addres [%p]:[%p], die\n", encl_map.base, base);
        while (1)
            ;
    }

    dlog("ELF BASE = %p, MAP SIZE = %lx, ENTRY = %p\n", encl_map.base, encl_map.size, encl_map.entry_point);

    int ret = 0;

    if (encl_map.entry_point == 0)
    {
        printf("entry_point is 0, runtime image is wrong/corrupted\n");
        while (1)
            ;
    }
    else {
        dlog("encl_map.entry = %p\n", encl_map.entry_point);
    }

    if (encl_map.ret_point == 0)
    {
        printf("ret_from_monitor is 0, runtime image is wrong/corrupted\n");
        while (1)
            ;
    }
    else {
        dlog("encl_map.ret = %p\n", encl_map.ret_point);
    }

    cvms[cid].host_exit_addr = (uint64_t)encl_map.host_exit;
    if (encl_map.cap_relocs)
    {
        printf("we have __cap_relocs, it is a purecap binary\n");
        cvms[cid].pure = 1;
        struct cap_relocs_s *cr = (struct cap_relocs_s *)encl_map.cap_relocs;
        for (int j = 0; j < encl_map.cap_relocs_size / sizeof(struct cap_relocs_s); j++)
        {
            printf("TODO: create cap: %p Base: %p Length: %ld Perms: %lx Unk = %ld\n", f->isol.base + cr[j].dst, cr[j].addr, cr[j].len, cr[j].perms, cr[j].unknown);
            void *__capability rel_cap;
            if (cr[j].perms == 0x8000000000000000ll)
            {
#if 0
//there is a problem when I call an asm function. the caller cap doesn't have a proper size.
//so I just make all call caps PCC-size (see the else)
				rel_cap = pure_codecap_create((void *) comp->base,(void *)  comp->base + cr[j].addr + cr[j].len);
				rel_cap = cheri_setaddress(rel_cap, comp->base + cr[j].addr);
#else
                rel_cap = pure_codecap_create((void *)f->isol.base, (void *)f->isol.base + f->isol.size);
                rel_cap = cheri_setaddress(rel_cap, f->isol.base + cr[j].addr);
#endif
            }
            else
            {
                // TODO: we need something better
                if (cr[j].len == 0xabba)
                {
                    printf("replace cap for caps\n");
                    rel_cap = datacap_create((void *)f->isol.base + 0xe001000, f->isol.base + 0xe002000);
                }
                else
                    rel_cap = datacap_create((void *)f->isol.base + cr[j].addr, (void *)f->isol.base + cr[j].addr + cr[j].len);
            }
            printf("store REL_CAP\n");
            CHERI_CAP_PRINT(rel_cap);
            st_cap(cr[j].dst + f->isol.base, rel_cap);
        }
    }

    cvms[cid].base = encl_map.base;
    cvms[cid].top = (void *)((unsigned long)base + size);
    cvms[cid].box_size = encl_map.size;
    cvms[cid].entry = encl_map.entry_point;
    cvms[cid].stack_size = (MAX_THREADS + 1) * STACK_SIZE; // last thread -- store for caps

    cvms[cid].ret_from_mon = encl_map.ret_point;
    cvms[cid].syscall_handler = encl_map.syscall_handler;

    memset(cvms[cid].libos, 0, MAX_LIBOS_PATH);
    strcpy(cvms[cid].libos, f->runtime);

    //	printf("cvms.base = %p, cvms.box_size = %lx\n", cvms[cid].base, cvms[cid].box_size);

    cvms[cid].cmp_begin = cmp_begin;
    cvms[cid].cmp_end = cmp_end;

#if 0
	cvms[cid].fd = create_console(cid);
#else
    cvms[cid].fd = STDOUT_FILENO;
#endif
    if (f->disk)
        strncpy(cvms[cid].disk_image, f->disk, sizeof(cvms[cid].disk_image));

    struct c_thread *ct = cvms[cid].threads;
    ////////////////////
    for (int i = 0; i < MAX_THREADS; i++)
    {
        ct[i].id = -1;
        ct[i].sbox = &cvms[cid];
    }

    ct[0].id = 0;
    ct[0].func = encl_map.entry_point;
    ct[0].cb_in = f->cb_in;
    ct[0].cb_out = f->cb_out;
    ct[0].stack_size = STACK_SIZE;
    ct[0].stack = (void *)((unsigned long)cvms[cid].top - STACK_SIZE);
    ct[0].arg = NULL;
    ct[0].sbox = &cvms[cid];

    ct[0].argc = argc;
    ct[0].argv = argv;

    /*** gen caps ***/
    // do we really need to save the sealcap?
    gen_caps(&cvms[cid], &ct[0]);
    return 0;
}

int gen_caps(struct s_box *cvm, struct c_thread *ct)
{
    ct->sbox->box_caps.sealcap_size = sizeof(ct->sbox->box_caps.sealcap);
    if (sysctlbyname("security.cheri.sealcap", &ct->sbox->box_caps.sealcap, &ct->sbox->box_caps.sealcap_size, NULL, 0) < 0)
    {
        printf("sysctlbyname(security.cheri.sealcap)\n");
        while (1)
            ;
    }
    // assert(ct->sbox == cvm);
    void *__capability ccap;
    if (cvm->pure)
        ccap = pure_codecap_create((void *)ct->sbox->cmp_begin, (void *)ct->sbox->cmp_end);
    else
        //                                  0x20000000                 0x30000000
        ccap = codecap_create((void *)ct->sbox->cmp_begin, (void *)ct->sbox->cmp_end);
    // ccap = 0x20000000

    void *__capability dcap = datacap_create((void *)ct->sbox->cmp_begin, (void *)ct->sbox->cmp_end);
    // dcap = 0x20000000
    ct->sbox->box_caps.dcap = dcap;

    ccap = cheri_setaddress(ccap, (unsigned long)(ct->func) + (unsigned long)(ct->sbox->base));
    // ccap = 0x200012c4
    ct->sbox->box_caps.sealed_datacap = cheri_seal(dcap, ct->sbox->box_caps.sealcap);
    // ddc = 0x53d20
    ct->sbox->box_caps.sealed_codecap = cheri_seal(ccap, ct->sbox->box_caps.sealcap);
    // ppc = 0x53d00
    // assert(ct->sbox == cvm);
    // probe capabilitites for syscall/hostcall.
    if (ct->cb_out == NULL)
    {
        printf("callback_out is empty, use default 'monitor'\n");
        ct->cb_out = "monitor";
    }
    host_syscall_handler_prb(ct->cb_out, &ct->sbox->box_caps.sealed_codecapt, &ct->sbox->box_caps.sealed_datacapt, &ct->sbox->box_caps.sealed_codecapt2);

    // generate capabilitites for ret_from_mon. TODO: we should make them public and our syscall/hostcall should fetch them
    // todo: we need something better than comp_to_mon_force
    ccap = cheri_setaddress(ccap, comp_to_mon_force(ct->sbox->ret_from_mon + ct->sbox->base - ct->sbox->cmp_begin, (unsigned long)ct->sbox)); // here should be base but not cmp_begin.
    ct->sbox->box_caps.sealed_ret_from_mon = cheri_seal(ccap, ct->sbox->box_caps.sealcap);

    // if we have syscall handler, we should publish it. TODO: let's init thread pubs this handler?
    if (cvm->syscall_handler != 0)
    {
        printf("ACHTUNG: '%s' has syscall handler 'syscall_handler' at %p\n", cvm->libos, cvm->syscall_handler);
        void *__capability syscall_pcc_cap = cheri_setaddress(ccap, (unsigned long)comp_to_mon_force(cvm->syscall_handler + ct->sbox->base - ct->sbox->cmp_begin, (unsigned long)ct->sbox));
        void *__capability sealed_syscall_pcc_cap = cheri_seal(syscall_pcc_cap, ct->sbox->box_caps.sealcap);

        host_syscall_handler_adv(cvm->libos, sealed_syscall_pcc_cap, ct->sbox->box_caps.sealed_datacap);
    }
    // assert(ct->sbox == cvm);
}

void *init_thread(int cid)
{
    struct c_thread *me = &cvms[cid].threads[0];
    thr_self(&me->task_id);
    // assert(me->sbox != 0);
    void *sp_read = me->stack + me->stack_size; // getSP(); = 0x2ff80000 + 524288 = 0x3000 0000 = cmp_end
    char argv1[128];
    char lc1[128];
    char env1[128];
    char env2[128];
    char env3[128];
    char env4[128];
    char env5[128];

    snprintf(argv1, 128, "/ld.so");
    snprintf(lc1, 128, "LC_ALL=C.UTF-8");

    snprintf(env1, 128, "PYTHONHOME=/usr");
    snprintf(env2, 128, "PYTHONPATH=/usr");
    snprintf(env3, 128, "PYTHONUSERBASE=site-packages");
    snprintf(env4, 128, "TMPDIR=/tmp");
    snprintf(env5, 128, "PYTHONDEBUG=3");
    //	snprintf(env5, 128, "_PYTHON_SYSCONFIGDATA_NAME=_sysconfigdata");

    me->m_tp = getTP();
    me->c_tp = (void *)(me->stack + 4096);

    char *cenv = (char *)(sp_read - 4096 * 3);         // originally, here was *2, but networking corrupts this memory
    volatile unsigned long *sp = (sp_read - 4096 * 4); // I don't know why, but without volatile sp gets some wrong value after initing CENV in -O2

    dlog("target SP = %lx, old TP = %lx sp_read = %p, me->stacl = %p, getSP()=%p, me->c_tp = %p\n", sp, getTP(), sp_read, me->stack, getSP(), me->c_tp);

    int cenv_size = 0;
    // sp 是栈顶指针(位于低地址), 初始化栈按地址增长方向, 依次存放 argc, argv, envs
    sp[0] = me->argc;
    sp[1] = (unsigned long)(mon_to_comp(argv1, me->sbox));
    int i;
    for (i = 1; i < me->argc; i++)
    {
        printf("[%d] '%s'\n", i, me->argv[i]);

        int tmp_add = snprintf(&cenv[cenv_size], 128, "%s\0", me->argv[i]);
        if (cenv_size + tmp_add > 4096)
        {
            printf("need more space for args on the stack, die\n");
            while (1)
                ;
        }
        sp[i + 1] = (unsigned long)(mon_to_comp(&cenv[cenv_size], me->sbox));
        printf("sp[i+1] = '%s'\n", (char *)(comp_to_mon(sp[i + 1], me->sbox)));
        cenv_size += tmp_add + 1;
    }
    sp[i + 1] = 0; // terminator
    int ienv = i + 2;

    dlog("&env0 = %p, &env1=%p\n", &sp[ienv], &sp[ienv + 1]);

    sp[ienv++] = mon_to_comp(lc1, me->sbox);
    sp[ienv++] = mon_to_comp(env1, me->sbox);
    sp[ienv++] = mon_to_comp(env2, me->sbox);
    sp[ienv++] = mon_to_comp(env3, me->sbox);
    sp[ienv++] = mon_to_comp(env4, me->sbox);
    sp[ienv++] = mon_to_comp(env5, me->sbox);
    sp[ienv++] = 0;

    size_t *auxv = &sp[ienv];
    dlog("%d sp = %p\n", __LINE__, sp);

    if (strlen(me->sbox->disk_image))
    {
        me->sbox->lkl_disk.fd = open(me->sbox->disk_image, O_RDWR);
        if (me->sbox->lkl_disk.fd < 0)
        {
            printf("cannot open disk '%s'\n", me->sbox->disk_image);
            while (1)
                ;
        }
    }
    else
        me->sbox->lkl_disk.fd = -1;

    me->sbox->lkl_disk.ops = &lkl_dev_blk_ops;

    //	printf("LOADER: argv = %lx, envp = %lx(expected %lx), auxv = %lx \n", &sp[1], &sp[4], &sp[1 + 1 + sp[0]],auxv);
    //	printf("LOADER: argv = %s, envp = %s, \n", sp[1], sp[4]);
    auxv[0] = AT_BASE;
    auxv[1] = (unsigned long)me->sbox->base;
    auxv[2] = AT_ENTRY;
    auxv[3] = (unsigned long)me->func;
    auxv[4] = AT_PHDR;
    auxv[5] = mon_to_comp(me->sbox->base, me->sbox) + 0x40;
    auxv[6] = AT_PAGESZ;
    auxv[7] = 4096;
    auxv[8] = AT_IGNORE;
    auxv[9] = -1;

    int aid = 10;
    auxv[aid++] = AT_CLKTCK;
    auxv[aid++] = 100;
    auxv[aid++] = AT_HWCAP;
    auxv[aid++] = 0;
    auxv[aid++] = AT_EGID;
    auxv[aid++] = 0;
    auxv[aid++] = AT_EUID;
    auxv[aid++] = 0;
    auxv[aid++] = AT_GID;
    auxv[aid++] = 0;
    auxv[aid++] = AT_SECURE;
    auxv[aid++] = 0;
    auxv[aid++] = AT_UID;
    auxv[aid++] = -1;
    auxv[aid++] = AT_RANDOM;
    auxv[aid++] = 0;
    auxv[aid++] = AT_NULL;
    auxv[aid++] = 0;

    //    auxv[12]  = AT_EXECFN;	auxv[13]  = (size_t) "";
    //    auxv[22] = AT_PLATFORM;	auxv[23] = (size_t) "x86_64";
    //    auxv[28] = AT_RANDOM;	auxv[29] = getauxval(AT_RANDOM);
    //	auxv[aid++] = AT_HWCAP;		auxv[aid++] = getauxval(AT_HWCAP);

    //	if(mprotect(0x2fffd000, 4096, PROT_READ) == -1) {
    //		perror("mprotect");while(1);
    //	  }

#if SIM
#define CRTJMP(pc, sp) __asm__ __volatile__( \
    "mv sp, %1 ; jr %0"                      \
    :                                        \
    : "r"(pc), "r"(sp)                       \
    : "memory")

    printf("SIM: sp = %p, tp = %p\n", sp, me->c_tp);
    printf("-----------------------------------------------\n");
    __asm__ __volatile__("mv sp, %0; mv tp, %1;" ::"r"(sp), "r"(me->c_tp)
                         : "memory");
    cinv(
        me->func,     // entrance
        NULL,         // entrance
        NULL,         // compartment data cap
        me->hostcall, // cap for exit
        NULL,         // cap for example
        NULL,         // default data cap after exit, must be changed
        me->sbox->ret_from_mon + me->sbox->base,
        auxv[1] /* AT_BASE */ + 0x0e000000 + 0x1000 // local_cap_store
    );

    printf("%s:%d\tBUG, die\n", __func__, __LINE__);
    while (1)
        ;

#else

    /////////////////////////
    void *__capability sealed_codecap = me->sbox->box_caps.sealed_codecap;
    void *__capability sealed_datacap = me->sbox->box_caps.sealed_datacap;
    void *__capability dcap = me->sbox->box_caps.dcap;
    void *__capability sealed_codecapt = me->sbox->box_caps.sealed_codecapt;
    void *__capability sealed_codecapt2 = me->sbox->box_caps.sealed_codecapt2;
    void *__capability sealed_datacapt = me->sbox->box_caps.sealed_datacapt;
    void *__capability sealed_ret_from_mon = me->sbox->box_caps.sealed_ret_from_mon;

    struct cinv_s
    {
        void *__capability caps[10];
    } cinv_args;

    cinv_args.caps[0] = sealed_codecap;
    dlog("ca0: sealed COMP PPC\n");
    CHERI_CAP_PRINT(cinv_args.caps[0]);

    cinv_args.caps[1] = sealed_datacap;
    dlog("ca1: sealed COMP DDC\n");
    CHERI_CAP_PRINT(cinv_args.caps[1]);

    cinv_args.caps[2] = dcap;
    dlog("ca2: COMP DDC\n");
    CHERI_CAP_PRINT(cinv_args.caps[2]);

    cinv_args.caps[3] = sealed_codecapt;
    dlog("ca3: sealed HC PCC\n");
    CHERI_CAP_PRINT(cinv_args.caps[3]);

    cinv_args.caps[4] = sealed_datacapt;
    dlog("ca4: sealed HC DDC (mon.DDC)\n");
    CHERI_CAP_PRINT(cinv_args.caps[4]);

    cinv_args.caps[5] = sealed_codecapt2;
    dlog("ca5: sealed OCALL PCC \n");
    CHERI_CAP_PRINT(cinv_args.caps[5]);

    cinv_args.caps[6] = sealed_ret_from_mon;
    dlog("ca6: sealed ret from mon\n");
    CHERI_CAP_PRINT(cinv_args.caps[6]);


    if (me->sbox->pure)
    {

        // TOD: this is very unreliable. we need to use precise bottom of the stack here
        void *__capability sp_cap = datacap_create((void *)((unsigned long)sp - STACK_SIZE + 4096 * 4), (void *)sp);
        sp_cap = cheri_setaddress(sp_cap, sp);

        //
        cinv_args.caps[7] = sp_cap;
        dlog("ca7: SP cap for purecap cVMs\n");
        CHERI_CAP_PRINT(cinv_args.caps[7]);
        //
    }
    //           x2fffc000
    sp = mon_to_comp(sp, me->sbox);
    // sp = 0xfffc000
    //                      0x2ff81000
    me->c_tp = mon_to_comp(me->c_tp, me->sbox);
    // me->c_tp = 0xff81000

    dlog("[%3d ms]: finish init_thread\n", gettime());
    dlog("HW: sp = %p, tp = %p\n", sp, me->c_tp);
    dlog("-----------------------------------------------\n");

    __asm__ __volatile__("mv sp, %0;" ::"r"(sp)
                         : "memory");
    __asm__ __volatile__("mv tp, %0;" ::"r"(me->c_tp)
                         : "memory");
    // checkpoint(0x20000000, 0x10000000, "/tmp/checkpoint1");
    cinv(
#if 0
		  sealed_codecap,  	//ca0:	entrance
		  sealed_datacap,  	//ca1:	entrance
		  dcap, 			//ca2:	compartment data cap
///
		  sealed_codecapt,	//ca3:	cap for hostcalls
		  sealed_datacapt,	//ca4:	cap for hostcall, in fact -- sealed mon.DDC
///
		  sp_cap,
//		  sealed_codecapt2,	//ca5:	cap for ret from CINV2 (OCALL)
///
		  sealed_ret_from_mon, //ca6:	because compartment cannot create CAPS, this cap is created by MON prior calling
		  auxv[1] /* AT_BASE */ + 0x0e000000 + 0x1000 //local_cap_store
#else
        auxv[1] /* AT_BASE */ + 0x0e000000 + 0x1000, // local_cap_store  //-> 0x2e001000
        &cinv_args
#endif
    );

    while (1)
        ;
#endif
}