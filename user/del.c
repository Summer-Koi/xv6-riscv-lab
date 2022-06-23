#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(int argc, char* argv[])
{
  if (argc != 2) {
    fprintf(2, "Usage: del source\n");
    exit(1);
  }

  if (mkdir("recyclebin") < 0)
    fprintf(2, "/recyclebin is already exist\n");
  else
    fprintf(2, "Created /recyclebin\n");

  char* filename = argv[1];
  char rb[] = "/recyclebin/";

  int m = strlen(filename);
  int n = strlen(rb);
  char dest[m + n];

  for (int j = 0; j != n;j++)
  {
    dest[j] = rb[j];
  }
  for (int i = 0; i != m; i++)
  {
    dest[n + i] = filename[i];
  }
  // fprintf(2,"filename : ");
  // fprintf(2, filename);
  // fprintf(2, "\n");
  // fprintf(2, dest);
  // fprintf(2, "\n");

  if (link(filename, dest) < 0)
    fprintf(2, "can't del %s: failed\n", argv[1]);
  unlink(argv[1]);
  // delete[] dest;
  exit(0);
}
