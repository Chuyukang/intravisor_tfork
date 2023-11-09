#include <stdio.h>
#include "crt.h"
#include "hostcalls.h"

#define MSG "checkpoint test with multi-thread... \n"

void sig_handler() {
	host_save();
}

void print_a()
{
	for (int i = 0; i < 1500; i++)
	{
		host_write("a", 1);
	}

	host_save();

	// note: loop for waiting sub-threads
	// currently no pthread_join
	while(1);
}

void print_b()
{
	for (;;) {
    }
	host_exit();
}

void print_c()
{
	for (;;) {
	}
	host_exit();
}

void hello_c()
{
	long tid_1 = host_pthread_create(print_b, NULL);
	long tid_2 = host_pthread_create(print_c, NULL);

	print_a();

	host_exit();
}
