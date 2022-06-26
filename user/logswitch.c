#include"kernel/types.h"
#include"kernel/stat.h"
#include"kernel/fcntl.h"
#include"user/user.h"

// 3 modes: complete, meta, none
int main(int argc, char *argv[])
{
        if(strcmp(argv[1],"complete") == 0)
        {
            logswitch(1);
            if (write(1, "complete\n", 9) != 9) {
                fprintf(2, "logswitch: write error\n");
                exit(1);
            }
            exit(0);
        }
        else if(strcmp(argv[1],"none") == 0)
        {
            logswitch(0);
            if (write(1, "none\n", 5) != 5) {
                fprintf(2, "logswitch: write error\n");
                exit(1);
            }
            exit(0);
        }
        else if(strcmp(argv[1],"meta") == 0)
        {
            logswitch(2);
            if (write(1, "meta\n", 5) != 5) {
                fprintf(2, "logswitch: write error\n");
                exit(1);
            }
            exit(0);
        }
    fprintf(2, "logswitch error\n");
    exit(1);
}
