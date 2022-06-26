#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"

char*
fmtname(char* path) // 返回path斜杠后的所有内容
{
  static char buf[DIRSIZ + 1];
  char* p;

  // Find first character after last slash.
  for (p = path + strlen(path); p >= path && *p != '/'; p--)
    ;
  p++;

  // Return blank-padded name.
  if (strlen(p) >= DIRSIZ)
    return p;
  memmove(buf, p, strlen(p));
  // memset(buf + strlen(p), ' ', DIRSIZ - strlen(p));
  return buf;
}

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

  char *filenamenp = fmtname(filename);
  int p = strlen(filenamenp);

  for (int i = 0; i != p; i++)
  {
    dest[n + i] = filenamenp[i];
  }
  // fprintf(2,"filename : ");
  // fprintf(2, filename);
  // fprintf(2, "\n");
  // fprintf(2, dest);
  // fprintf(2, "\n");

  fprintf(2,"filemane : %s\n",filename);
  fprintf(2, "dest : %s\n", dest);
  if (link(filename, dest) < 0)
    fprintf(2, "can't del %s: failed\n", argv[1]);
  unlink(argv[1]);
  
  exit(0);
}
