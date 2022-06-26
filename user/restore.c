#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(int argc, char* argv[])
{
    if (argc < 2) {
        fprintf(2, "Usage: restore file [path]\n");
        exit(1);
    }

    char* filename = argv[1];
    char rb[] = "/recyclebin/";
    char rs[128];
    memset(rs,0,sizeof(rs));
    strcpy(rs,"/");
    if(argc == 3)
      strcpy(rs,argv[2]);
    fprintf(2,"rs : %s\n",rs);


    int m = strlen(filename);
    int n = strlen(rb);
    int p = strlen(rs);

    char source[m + n];
    for (int j = 0; j != n;j++)
    {
        source[j] = rb[j];
    }
    for (int i = 0; i != m; i++)
    {
        source[n + i] = filename[i];
    }

    char dest[p + m];
    for (int i = 0;i != p;i++)
    {
        dest[i] = rs[i];
    }
    for (int j = 0;j != m;j++)
    {
        dest[p + j] = filename[j];
    }

    fprintf(2, "source : %s\n", source);
    fprintf(2, "dest : %s\n", dest);

    if (link(source, dest) < 0)
        fprintf(2, "can't restore %s: failed\n", argv[1]);
    unlink(source);
    // delete[] dest;
    exit(0);
}
