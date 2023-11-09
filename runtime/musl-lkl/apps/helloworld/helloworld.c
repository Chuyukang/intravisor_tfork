#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#define HW_FILE "/app/helloworld.txt"

static __inline__ void * getSP(void) {
    register void * sp asm("sp");
    asm ("" : "=r"(sp));
    return sp;
}

unsigned long gettime() {
	struct timeval t;
	gettimeofday(&t, 0);
	return ((unsigned long)t.tv_sec) * 1000 + t.tv_usec / 1000 ;
}

int main(int argc, char **argv)
{
    char buf[100];
    FILE *f = fopen(HW_FILE, "r");
    if (!f) {
        fprintf(stderr, "Could not open file %s: %s\n", HW_FILE, strerror(errno));
        exit(1);
    }

    printf("[%d] getSP = %p, &buf = %x\n", gettime(), getSP, buf);

    // Prints first line of file /app/helloworld.txt (max 100 characters)
    if (fgets(buf, sizeof(buf), f) == buf) {
        printf("%s", buf);
    } else {
        fprintf(stderr, "Could not read first line of file %s: %s\n", HW_FILE, strerror(errno));
        exit(1);
    }

    return 0;
}

