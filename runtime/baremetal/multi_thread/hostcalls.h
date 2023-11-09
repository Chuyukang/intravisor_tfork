//local store for capabilitites, relative address usualy provided via AUX
unsigned long local_cap_store = 0xe001000;


// hostcalls

int host_write(char *ptr, int size);
int host_cap_prb(char *key, void *location, long *size);
long host_pthread_create(void *f, void *arg);
int host_thread_join(long tid);
void host_exit();
void host_save();
void host_signal(void *handler);