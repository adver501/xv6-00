#include "types.h"
#include "stat.h"
#include "user.h"
#include "fcntl.h"

int
main(int argc, char const *argv[])
{
    int k, n, id;
    double x = 0, z , d;
    // int priority = 4;             /*  priority of this proc can define here.(Default is 3)  */
    // changepriority(pid, priority);

    if(argc < 2)
        n = 1;
    else
        n = atoi( argv[1] );
    if(n < 0 || n > 30)
        n = 2;
    
    if(argc < 3)
        d = 1.0;
    else
        d = atoi( argv[2] );
    
    x = 0;
    id = 0;
    // cps();
    
    for (k = 0; k < n; k++)
    {
        id = fork();
        if(id < 0)
            printf(1, "%d failed in fork\n", getpid());
        else if(id > 0){
            printf(1, "Parent %d creating child %d\n", getpid, id);
            wait();
        }
        else
        {
            printf(1, "child %d created\n", getpid());
            for (z = 0; z < 800000; z += d)
                x = x + 3.14 * 89.64;  // useless computation to consume cpu time
            break;
        }
        
            
    }
    
    
    exit();
}
