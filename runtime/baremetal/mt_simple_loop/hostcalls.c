int host_write(char *ptr, int size)
{
	int tmp = 1;
	register long t5 __asm__("t5") = tmp;
	register long a0 __asm__("a0") = (long)ptr;
	register long a1 __asm__("a1") = (long)size;

	__asm__ __volatile__(
		"jal c_out"
		: "=r"(a0)					// 可写 通用寄存器, 输出
		: "r"(t5), "r"(a0), "r"(a1) // 只读操作数, 输入
		: "memory");

	return a0;
}

int host_inspect()
{
	int tmp = 114;
	register long t5 __asm__("t5") = tmp;
	register int a0 __asm__("a0") = 0;

	__asm__ __volatile__(
		"jal c_out"
		: "=r"(a0)
		: "r"(t5));

	return a0;
}

void host_exit()
{
	int tmp = 13;
	register long a0 __asm__("a0");
	register long t5 __asm__("t5") = tmp;

	__asm__ __volatile__("jal c_out"
						 : "=r"(a0)
						 : "r"(t5)
						 : "memory");
}

int host_cap_prb(char *key, void *location, long *size)
{
	int tmp = 406;
	register long a0 __asm__("a0") = (long)key;
	register long a1 __asm__("a1") = (long)location;
	register long a2 __asm__("a2") = (long)size;
	register long t5 __asm__("t5") = tmp;
	__asm__ __volatile__("jal c_out"
						 : "=r"(a0)
						 : "r"(t5), "r"(a0), "r"(a1), "r"(a2)
						 : "memory");
	return (int)a0;
}

void host_save()
{
	int tmp = 115;
	register long a0 __asm__("a0") = (void *)host_save;
	register long t5 __asm__("t5") = tmp;

	__asm__ __volatile__("jal c_out"
						 : "=r"(a0)
						 : "r"(t5), "r"(a0)
						 : "memory");
}

// long pthread_target_wrapper(void *args[2]) {
// 	host_write("multi_thread cvm, wrap target function, target=%p, ");
// 	host_write("")
// 	void (*f)(void *) = args[0];
// 	void *arg = args[1];
// 	f(arg);
// 	host_exit();
// }

long host_pthread_create(void *f, void *arg)
{
	int tmp = 11;
	// void *args[2] = {f, arg};
	register long t5 __asm__("t5") = tmp;
	register long a0 __asm__("a0") = f;
	register long a1 __asm__("a1") = arg;

	__asm__ __volatile__("jal c_out"
						 : "=r"(a0)
						 : "r"(t5), "r"(a0), "r"(a1)
						 : "memory");

	return (long)a0;
}

int host_thread_join(long tid)
{
	int tmp = 14;
	register long t5 __asm__("t5") = tmp;
	register long a0 __asm__("a0") = tid;

	__asm__ __volatile__("jal c_out"
						 : "=r"(a0)
						 : "r"(t5), "r"(a0)
						 : "memory");

	return (int)a0;
}

void host_signal(void (*handler)(int)) {
	// int tmp = 117;
	register long t5 __asm__("t5") = 117;
	register long a0 __asm__("a0") = handler;

	__asm__ __volatile__("jal c_out"
						 : "=r"(a0)
						 : "r"(t5), "r"(a0)
						 : "memory");
}