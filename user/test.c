#include"kernel/types.h"
#include"kernel/stat.h"
#include"kernel/fcntl.h"
#include"user/user.h"

int main()
{	
	int testTime;
	int n = 100;
	char path[] = "/dir/00";
	char data[10];
	memset(data, 'a', sizeof(data)); 
	mkdir("/dir");
	testTime = uptime();
	for(int i = 0 ; i < n; i++)
	{
		int p = 100;
		path[5] = '0' + i/10; 
		path[6] = '0' + i%10;
		int  fd = open(path, O_CREATE | O_RDWR);
		while(p>0)
		{
			write(fd,data, sizeof(data));
			p--;
		}
		//write(fd, data, sizeof(data));
  		close(fd);
	}
	testTime = uptime() - testTime;
	printf("\n%d\n",testTime);
	exit(0);
}
