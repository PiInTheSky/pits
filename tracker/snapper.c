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
	// int i;
	struct TGPS *GPS;
	char *prefix;
	//char Command[200];
	char *filename = "/home/pi/tracker/take_pic";
	int old_mode=0;
	FILE *fp;

	GPS = (struct TGPS *)some_void_ptr;
	
	// for (i=1; 1; i++)
	while (1)
	{
		if (GPS->Altitude >= Config.high)
		{
			width = Config.high_width;
			height = Config.high_height;
			prefix = "medium";
			new_mode = 2;
		}
		else
		{
			width = Config.low_width;
			height = Config.low_height;
			prefix = "small";
			new_mode = 1;
		}
		
		if (new_mode |= old_mode)
		{
			if ((fp = fopen(filename, "wt")) != NULL)
			{
				fprintf(fp, "raspistill -w %d -h %d -t 2000 -ex auto -mm matrix -o /home/pi/tracker/download/%s_$1.jpg", width, height, prefix);
				fclose(fp);
				chmod(filename, S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH); 
				new_mode = old_mode;
			}
		}
		
		sleep(5);

		/*
		
		sprintf(Command, "raspistill -w %d -h %d -t 2000 -ex auto -mm matrix -o /home/pi/tracker/download/%s%d.jpg > /dev/null", width, height, prefix, i);
		system(Command);
		
		sleep(5);
		*/
	}
}


