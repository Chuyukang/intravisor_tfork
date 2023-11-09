#include "../monitor.h"

int fork_cvm(int cid, int t_cid, struct cmp_s *cmp, int argc, char *argv[])
{
    struct s_box *t_cvm = &cvms[t_cid];
    struct s_box *cvm = &cvms[cid];

    cvm->base = cmp->base;
    cvm->top = (void *)((unsigned long)cmp->base + cmp->size);
    // memcpy(&cvm->box_caps, &t_cvm->box_caps, sizeof(struct box_caps_s));
    cvm->entry = t_cvm->entry;
    cvm->ret_from_mon = t_cvm->ret_from_mon;
    strcpy(cvm->disk_image, t_cvm->disk_image);
    cvm->fd = t_cvm->fd;
    cvm->pure = t_cvm->pure;
    cvm->syscall_handler = t_cvm->syscall_handler;
    strcpy(cvm->libos, t_cvm->libos);

    if (pthread_mutex_init(&cvms[cid].ct_lock, NULL) != 0)
    {
        printf("\n mutex init failed\n");
        return 1;
    }

    // todo: cvm->disk_image, when running baremetal, disk_image is NULL;
    struct c_thread *ct = cvm->threads;
    pthread_t cur_cid = pthread_self();
    // printf("cur_cid = pthread_self(), tid=0x%x\n", cur_cid);
    memcpy(ct, t_cvm->threads, sizeof(struct c_thread));
    ct->tid = cur_cid;
    for (int i = 0; i < MAX_THREADS; i++)
    {
        ct[i].id = -1;
        ct[i].sbox = cvm;
    }

    ct[0].stack = (void *)((unsigned long)cvm->top - STACK_SIZE);
    ct[0].argc = argc;
    ct[0].argv = argv;

    ct->m_tp = getTP();
    ct->c_tp = (void *)(ct->stack + PAGE_SIZE);

#ifdef __linux__
    //	int from = (cid - 2) * 2;
    //	int to  = ((cid - 2) + 1) * 2;
    int from = 0;
    int to = 4;
    CPU_ZERO(&cvms[cid].cpuset);
    for (int j = from; j < to; j++)
        CPU_SET(j, &cvms[cid].cpuset);

    int ret = pthread_attr_setaffinity_np(&ct[0].tattr, sizeof(cvms[cid].cpuset), &cvms[cid].cpuset);
    if (ret != 0)
    {
        perror("pthread set affinity");
        printf("ret = %d\n", ret);
    }

#endif
    // fork_cvm shouldn't call gen_caps because caps will be generated in functon: load.
    // gen_caps(cvm, ct);
}