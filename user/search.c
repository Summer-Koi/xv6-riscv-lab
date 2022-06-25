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

char* strcat(char* s1, char* s2)
{
  static char buf[50];
  int m = strlen(s1), n = strlen(s2);
  for (int i = 0;i != m;++i)
  {
    buf[i] = s1[i];
  }
  for (int j = 0;j != n;++j)
  {
    buf[m + j] = s2[j];
  }
  return buf;
}
int 
strcmp2(const char* p, const char* q)
{
  int n = strlen(p);
  if(n != strlen(q)) return 0;
  for(int i=0;i!=n;i++)
  {
    if(p[i] != q[i]) return 0;
  }
  return 1;
}

void
search(char* target, char* path)
{
  // fprintf(2, "call : remove %s\n", path);
  char buf[512], * p;
  int fd;
  struct dirent de;
  struct stat st;

  if ((fd = open(path, 0)) < 0) {
    fprintf(2, "search: cannot open %s\n", path);
    fprintf(2, "Maybe you can try again.\n", path);
    return;
  }

  if (fstat(fd, &st) < 0) {
    fprintf(2, "search: cannot stat %s\n", path);
    fprintf(2, "Maybe you can try again.\n", path);
    close(fd);
    return;
  }

  switch (st.type) {

  case T_FILE:
  // path 是文件，递归的叶结点
  if(strcmp2(target, fmtname(path)))
  {
    // fprintf(2,"%s %s\n",target,path);
    fprintf(2, "target in : %s\n", path);
  }
  break;

  case T_DIR:
  // path 是文件夹
  strcpy(buf, path);
  p = buf + strlen(buf);
  *p++ = '/';

  int count = 0;

  while (read(fd, &de, sizeof(de)) == sizeof(de)) {
    count++;
    if (count <= 2) continue;
    // fprintf(2, "in while\n");
    // if (de.inum == 0)
    //   continue;
    memmove(p, de.name, DIRSIZ);
    p[DIRSIZ] = 0;
    // if (stat(buf, &st) < 0) {
    //   printf("ls: cannot stat %s\n", buf);
    //   continue;
    // }
    // if (strcmp(fmtname(buf), ".") || strcmp(fmtname(buf), ".."))
    //   continue;
    // fprintf(2, buf);
    // fprintf(2, "\n");
    // fprintf(2, fmtname(buf));
    // fprintf(2, "\n");

    // fprintf(2, "recur call\n");
    search(target,buf);
  }
  break;
  }
  if (strcmp2(target, fmtname(path)))
  {
    // fprintf(2, "%s %s\n", target, path);
    fprintf(2, "target in : %s\n", path);
  }
  close(fd);
}

int
main(int argc, char* argv[])
{

  if (argc < 3) {
    // failed
    fprintf(2 ,"Usage: search [target] [path]\n");
    fprintf(2, "Note that [path] cannot be the root path.\n");
    exit(1);
  }
  if(argc == 3)
    search(argv[1], argv[2]);
  exit(0);
}