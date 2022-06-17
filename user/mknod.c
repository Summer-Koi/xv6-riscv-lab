#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main(int argc, char *argv[]) {
  if(argc != 4){
    fprintf(2, "Usage: mknod devicefile minor major\n");
    exit(1);
  }

 if(mknod(argv[1], atoi(argv[2]), atoi(argv[3])) < 0) {
   fprintf(2, "unable to create device file %s\n",argv[1]);
   exit(1);
 }
  exit(0);
}
