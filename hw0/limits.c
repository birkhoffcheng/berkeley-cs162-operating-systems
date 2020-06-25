#include <stdio.h>
#include <sys/resource.h>
#include <unistd.h>

int main() {
	struct rlimit lim;
	getrlimit(RLIMIT_STACK, &lim);
	printf("stack size: %ld\n", lim.rlim_cur);
	printf("process limit: %ld\n", sysconf(_SC_CHILD_MAX));
	printf("max file descriptors: %ld\n", sysconf(_SC_OPEN_MAX));
	return 0;
}
