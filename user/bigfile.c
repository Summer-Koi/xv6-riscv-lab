#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"
#include "kernel/fcntl.h"

int
main(int argc, char *argv[])
{
  int fd, i;
  char path[] = "bigfile_test";
  char data[1024]; // 1KB

  memset(data, 'a', sizeof(data));

  fd = open(path, O_CREATE | O_RDWR);
  for(i = 0; i < 10000; i++)  // 10MB
    write(fd, data, sizeof(data));
  close(fd);

  exit(0);
}