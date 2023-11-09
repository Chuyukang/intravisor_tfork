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
	for (int i = 0; i < 100; i++)
	{
		host_write("a", 1);
	}
}

void print_b()
{
	// host_signal(sig_handler);
	// for (int i = 0; i < 200; i++)
	for (;;)
	{
		// host_write("b", 1);
	}
	host_exit();
}

void print_c()
{
	// host_signal(sig_handler);
	for (;;)
	{
		// host_write("c", 1);
	}
	host_exit();
}

void hello_c()
{
	// host_signal(sig_handler);
	long tid_1 = host_pthread_create(print_b, NULL);
	long tid_2 = host_pthread_create(print_c, NULL);

	print_a();

	host_exit();
}
