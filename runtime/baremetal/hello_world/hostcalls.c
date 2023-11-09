int host_write(char *ptr, int size) {
	int tmp = 1;
	register long t5 __asm__("t5") = tmp;
	register long a0 __asm__("a0") = (long) ptr;
	register long a1 __asm__("a1") = (long) size;

	__asm__ __volatile__(
		"jal c_out"
		: "=r"(a0) // 可写 通用寄存器, 输出
		: "r"(t5), "r"(a0), "r"(a1) // 只读操作数, 输入
		: "memory" );

	return a0;
}

int host_inspect() {
	int tmp = 114;
	register long t5 __asm__("t5") = tmp;
	register int a0 __asm__("a0") = 0;

	__asm__ __volatile__(
		"jal c_out"
		: "=r"(a0)
		: "r"(t5)
	);

	return a0;
}

void host_exit() {
	int tmp = 13;
	register long a0 __asm__("a0");
	register long t5 __asm__("t5") = tmp;

	__asm__ __volatile__("jal c_out" : "=r"(a0) : "r"(t5) : "memory" );
}

int host_cap_prb(char *key, void *location, long *size) {
	int tmp = 406;
	register long a0 __asm__("a0") = (long) key;
	register long a1 __asm__("a1") = (long) location;
	register long a2 __asm__("a2") = (long) size;
	register long t5 __asm__("t5") = tmp;
	__asm__ __volatile__("jal c_out" : "=r"(a0) : "r"(t5), "r"(a0), "r"(a1), "r"(a2) : "memory" );
	return (int) a0; 
}

void host_save() {
	// int status; // 假设默认是 0 
	// if (status == 0) {
		// status = 1;
		int tmp = 115;
		register long a0 __asm__("a0") = (void *)host_save;
		register long t5 __asm__("t5") = tmp;
		
		__asm__ __volatile__("jal c_out" : "=r"(a0) : "r"(t5), "r"(a0) : "memory" );
	// }
}