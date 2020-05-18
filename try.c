#include "types.h"
#include "user.h"
/* 
 * note that the print statements allow ONLY the parent
 * process to be traced. See below comments for the amount
 * of system calls, and the print statements will print
 * out the correct number during run time.
 *
 * Note that for a printf() call, sysCalls will be
 * incremented once for each char printed.
 *
 * using trace(0) as a parameter for printf WILL NOT
 * count the characters in that printf call. only
 * after the printf will each char increment sysCalls
 */
int main()
{
	// 2 sys calls sbrk and exec

	// 1 sys call
	int pid = getpid();
	
	// 1 sys call
	trace(1);
	
	// 4 sys calls
	fork();
	fork();
	wait();
	wait();
	
	// 1 sys call
	if(getpid() == pid){
		// should be 10 (include trace(0))	
		printf(1, "total syscalls so far: %d\n", trace(0)); 
		// 26 more from print (2 digits for num)
	}

	// 2 sys calls
	fork();
	wait();
	
	// 1 sys call
	if(getpid() == pid){
		// should be 40 (39 + trace(0))
		printf(1, "totalsys calls so far: %d\n", trace(0)); 
	}
	exit();
}