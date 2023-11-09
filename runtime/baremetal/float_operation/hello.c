#include "crt.h"
#include "hostcalls.h"

void hello_c() {
	double a = 4;
	double result = a/3.14;
	if (result < 2 && result > 1) {
		host_write("float ok1\n", 11);
	}
	host_save();
	for (int i=0; i<1000; ++i) {
		math_sin(result);
	}
	host_write("float ok2\n", 11);
	host_exit();
}

