#include "types.h"
#include "user.h"

/*
 * sbrk();
 * fork();
 * trace();
 * trace();
 * 4 system calls
 */
int main()
{
	trace(1);
	printf(1, "Total system calls so far: %d\n", trace(0));
	exit();
	
}