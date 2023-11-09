#include "monitor.h"
#include "time.h"
#include "assert.h"
#include "tfork.h"
#include "cvm/init.h"
#include "cvm/log.h"

struct s_box	cvms[MAX_CVMS];
int send_req, receive_resp;

//default config
int timers = 0;
int debug_calls = 0;
//
pthread_mutex_t print_lock;

extern uint64_t starttime;
// extern host_syscall_handler_adv(char *, void * __capability pcc, void * __capability ddc, void * __capability pcc2);
// extern host_syscall_handler_prb(char *name, void *, void *, void *);
extern void tp_write();
extern void ret_from_cinv2();

extern const int TFORK_FAILED;

#if SIM
extern void __inline__ cinv(void *, void *, void *, void *, void *, void *, void *, void *);
#else
extern void cinv(void *, void *);
#endif



void sig_handler(int j, siginfo_t *si, void *uap) {
	mcontext_t *mctx = &((ucontext_t *)uap)->uc_mcontext;
	printf("trap %d\n", j);
	printf("SI_ADDR: 0x%lx\n", si->si_addr);
	printf("SI_PC_ADDR: 0x%lx\n", mctx->mc_gpregs.gp_sepc);

#ifdef SIM 
	printf("not implemented, linux has different mcontext\n");
#else
	__register_t ra = mctx->mc_gpregs.gp_ra;
	__register_t sp = mctx->mc_gpregs.gp_sp;
	__register_t gp = mctx->mc_gpregs.gp_sp;
	__register_t tp = mctx->mc_gpregs.gp_tp;
	__register_t *a = &mctx->mc_gpregs.gp_a[0];
	__register_t *t = &mctx->mc_gpregs.gp_t[0];
	__register_t *s = &mctx->mc_gpregs.gp_s[0];
	__register_t gp_sepc = mctx->mc_gpregs.gp_sepc;
	__register_t gp_sstatus = mctx->mc_gpregs.gp_sstatus;

	printf("ra = 0x%lx, sp = 0x%lx, gp = 0x%lx, tp = 0x%lx, gp_sepc = %p, gp_status = %p\n", ra, sp, gp, tp, gp_sepc, gp_sstatus);
	for(int i = 0; i < 7; i++) {
		printf("gp_t[%d]\t0x%lx\n", i, t[i]);
	}

	for(int i = 0; i < 12; i++) {
		printf("gp_s[%d]\t0x%lx\n", i, s[i]);
	}

	for(int i = 0; i < 8; i++) {
		printf("gp_a[%d]\t0x%lx\n", i, a[i]);
	}

	int sepcc_offset = (4+7+12+8)*16;
	int ddc_offset = (4+7+12+8+1)*16;

	log("sepcc:\n");
	CHERI_CAP_PRINT(*(void* __capability*)(mctx->mc_capregs+sepcc_offset));
	log("ddc:\n");
	CHERI_CAP_PRINT(*(void* __capability*)(mctx->mc_capregs+ddc_offset));

#endif
	printf("program receive trap signal, press Ctrl+c to exit.\n");
	while(1);
}

void setup_segv_sig() {
	stack_t sigstack;
	struct sigaction sa;
#if 1
	int stsize = SIGSTKSZ*100; // 900 * pagesize

	sigstack.ss_sp = malloc(stsize);
	if(sigstack.ss_sp == NULL) {
		perror("malloc");
	}

	sigstack.ss_size = stsize;
	sigstack.ss_flags = 0;
	if(sigaltstack(&sigstack, NULL) == -1) {
		perror("sigstack");
		exit(1);
	}
#endif

	dlog("%d Alternate stack is at %10p-%p\n", stsize, sigstack.ss_sp,sigstack.ss_sp+stsize);

//	sa.sa_handler = sig_handler;
	sa.sa_sigaction = sig_handler;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_ONSTACK | SA_SIGINFO;

	if(sigaction(SIGSEGV, &sa, NULL) == -1) {
		perror("sigaction");
		exit(1);
	}
}

void setup_sig() {
	setup_segv_sig();
	// setup_save_sig();
}

void parse_cmdline(char *argv[], const char *disk_img, const char *runtime_so, char **yaml_cfg, int *skip_argc) {
	for (++argv; *argv; ++argv)
	{
		if (strcmp("-h", *argv) == 0 || strcmp("--help", *argv) == 0)
		{
			printf("CARRIE -- a virtualisation platform for CHERI\n\t <monitor> [-hdt] --args /path/to/app [app args] \n");
			printf("\t-h --help\tshow this help and exit\n");
			printf("\t-d --disk\tpath to disk image. Default is %s\n", disk_img);
			printf("\t-r --runtime\tpath to runtime so. Default is %s\n", runtime_so);
			printf("\t-y --yaml\tpath to yaml config. Default is %s\n", yaml_cfg);
			printf("\t-c --debug_calls\t trace hostcalls at the host side, default is %d\n", debug_calls);
			printf("\t-t --timer\tenable oneshot timer threads, default: %d\n", timers);
			exit(0);
		}
		else if (strcmp("-y", *argv) == 0 || strcmp("--yaml", *argv) == 0)
		{
			*yaml_cfg = *++argv;
			dlog("Using yaml.cfg = %s\n", *yaml_cfg);

			break;
		}
		else if (strcmp("-d", *argv) == 0 || strcmp("--disk", *argv) == 0)
		{
		  skip_argc+=2;
			disk_img = *++argv;
		}
		else if (strcmp("-t", *argv) == 0 || strcmp("--timer", *argv) == 0)
		{
		  skip_argc+=2;
			timers = atoi(*++argv);
		}
		else if (strcmp("-c", *argv) == 0 || strcmp("--debug_calls", *argv) == 0)
		{
		  skip_argc+=2;
			debug_calls = atoi(*++argv);
		}
		else if (strcmp("-a", *argv) == 0 || strcmp("--args", *argv) == 0)
		{

			  break; //argv now points to the beginning of args
		}
	}
}

int monitor_init() {
	if (pthread_mutex_init(&print_lock, NULL) != 0) {
		printf("\n mutex init failed\n");
		return 1;
	}
	setup_sig();
	memset(cvms, 0, sizeof(cvms));
	init_cap_files_store();
	init_cbs(); // callbacks
/*** 		we generate and seal intravisor caps. cVMs use them later as hostcall/syscall handler ***/

	void *__capability ddc_cap = cheri_getdefault();
	void *__capability pcc_cap = cheri_getpcc();
	void *__capability pcc_cap2 = cheri_getpcc();
	pcc_cap = cheri_setaddress(pcc_cap, (unsigned long) tp_write);

//	printf("ret_from_cinv2 = %ld\n", ret_from_cinv2);
	pcc_cap2 = cheri_setaddress(pcc_cap2, (unsigned long) ret_from_cinv2);

	void * __capability sealcap;
	size_t sealcap_size;

	sealcap_size = sizeof(sealcap);
	if (sysctlbyname("security.cheri.sealcap", &sealcap, &sealcap_size, NULL, 0) < 0) {
		printf("sysctlbyname(security.cheri.sealcap)\n");while(1);
	}

	void * __capability sealed_pcc = cheri_seal(pcc_cap, sealcap);   // tp_write
	void * __capability sealed_pcc2 = cheri_seal(pcc_cap2, sealcap); // ret_from_cinv2
	void * __capability sealed_ddc = cheri_seal(ddc_cap, sealcap);   // default (?)

	host_syscall_handler_adv("monitor", sealed_pcc, sealed_ddc, sealed_pcc2);
}

int build_capfile(struct capfile *f) {
	if(f->addr) {
		printf("capfiles with pre-defined addresses are not supported\n");
	}

	void *ptr = malloc (f->size);
	if(!ptr) {
		printf("cannot alloc %d bytes for %s key\n", f->size, f->name);
		return 0;
	}

	memset(ptr, 0, f->size);

	//we support only text here
	if(f->data) {
		snprintf(ptr, f->size, "%s", f->data);
	}

	host_cap_file_adv(ptr, f->size, f->name);
}

int link_cvm(struct cvm *flist) {
	for (struct cvm *f = flist; f; f = f->next) {
		if(!f->cb_in) {
			continue;
		}

		for (struct cvm *n = flist; n; n = n->next) {
//todo: instead of runtime name we should use name. here in all other relevant places
			if (strcmp(f->cb_in, n->runtime) == 0) {
				cvms[f->isol.base / 0x10000000].inner=&cvms[n->isol.base / 0x10000000];
				printf("%s[%d] is inner for %s[%d]\n",  cvms[f->isol.base / 0x10000000].threads[0].cb_in, n->isol.base / 0x10000000, 
														f->runtime, f->isol.base / 0x10000000);
			}
		}
	}
}

int monitor_main(int argc, char *argv[]) {
//	printf("hello world %d %s\n", argc, argv[1]);
	starttime = get_ms_timestamp();
	char *disk_img = "./disk.img";
	char *yaml_cfg = 0;
	char *runtime_so = "libcarrie.so";
	int skip_argc = 1;

	if (monitor_init() != 0) {
		printf("\n monitor init failed\n");
		return 1;
	}

	parse_cmdline(argv, disk_img, runtime_so, &yaml_cfg, &skip_argc);

	if(yaml_cfg == 0) {
		// doesn't have -y *.yaml
		// default_cvm(runtime_so, disk_img, argc - skip_argc, argv);
		printf("usage: monitor -y config.yaml\n"); exit(1);
	}

	struct parser_state *state = run_yaml_scenario(yaml_cfg);
	if(state == 0) {
		printf("yaml is corrupted, die\n"); exit(1);
	}

	dlog("[%3d ms]: finish parse yaml\n", gettime());

	for (struct capfile *f = state->clist; f; f = f->next) {
		// printf("capfile: name=%s, data='%s', size=0x%lx, addr=0x%lx \n", f->name, f->data, f->size, f->addr);
		build_capfile(f);
	}

	// printf("***************** Link Inner<-->Outer ***************\n");
	// link_cvm(state->flist);
	
	for (struct cvm *f = state->flist; f; f = f->next) {
		create_and_start_cvm(f);
	}
	
	// wait completion
	for (int i = 0; i < MAX_CVMS; i++) {
		struct c_thread *ct = cvms[i].threads;
		void *cret;
		pthread_join(ct[0].tid, &cret);
	}
	printf("all cvm exit, monitor exit.\n");
	return 0;
}

int main(int argc, char *argv[]) { 
	pid_t pid;
	int req_pipe[2];
	int resp_pipe[2];

	if (pipe(req_pipe) != 0) {
		printf("pipe failed\n");
		exit(1);
	};

	if (pipe(resp_pipe) != 0) {
		printf("pipe failed\n");
		exit(1);
	};

	pid = fork();
	if (pid == 0) {
		send_req = req_pipe[1];
		receive_resp = resp_pipe[0];
		monitor_main(argc, argv);
		close(req_pipe[0]);
	} else {
		daemon_main(pid, req_pipe[0], resp_pipe[1]);
	}
}