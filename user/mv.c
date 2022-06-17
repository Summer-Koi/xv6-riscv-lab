#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
  if(argc != 3){
    fprintf(2, "Usage: mv source dest\n");
    exit(1);
  }
  if(link(argv[1], argv[2]) < 0)
    fprintf(2, "can't move %s: failed\n", argv[1]);
  unlink(argv[1]);
  exit(0);
}
