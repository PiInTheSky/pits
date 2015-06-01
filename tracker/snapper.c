#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <errno.h>
#include <wiringPiSPI.h>
#include <gertboard.h>

#include "gps.h"
#include "misc.h"

void *CameraLoop(void *some_void_ptr)
{
	int width, height, new_mode;
	struct TGPS *GPS;
	char *filename = "/home/pi/pits/tracker/take_pic";
	int old_mode=0;
	FILE *fp;

	GPS = (struct TGPS *)some_void_ptr;
	
	while (1)
	{
		if (GPS->Altitude >= Config.high)
		{
			width = Config.high_width;
			height = Config.high_height;
			new_mode = 2;
		}
		else
		{
			width = Config.low_width;
			height = Config.low_height;
			new_mode = 1;
		}
		
		width = (width / 16)* 16;
		height = (height / 16) * 16;
		
		if (new_mode |= old_mode)
		{
			if ((fp = fopen(filename, "wt")) != NULL)
			{
				fprintf(fp, "raspistill -w %d -h %d -t 3000 -ex auto -mm matrix -o /home/pi/pits/tracker/download/$1.jpg\n", width, height);
				fclose(fp);
				chmod(filename, S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IWGRP | S_IXGRP | S_IROTH | S_IWOTH | S_IXOTH); 
				new_mode = old_mode;
			}
		}
		
		sleep(5);
	}
}


