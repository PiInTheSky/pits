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
#include <wiringPi.h>

#include "gps.h"
#include "misc.h"


void *LogLoop(void *some_void_ptr)
{
	struct TGPS *GPS;

	GPS = (struct TGPS *)some_void_ptr;
	
	while (1)
	{		
		FILE *fp;
		
		if ((fp = fopen("latest.txt", "wt")) != NULL)
		{
			if (GPS->Altitude < 1000)
			{
				fprintf(fp, "Sats %d, Alt %dm, temp %.1lfC\n", GPS->Satellites, GPS->Altitude, GPS->DS18B20Temperature[Config.ExternalDS18B20]);
			}
			else
			{
				fprintf(fp, "Altitude %d metres, temperature %.1lf degC\n", GPS->Altitude, GPS->DS18B20Temperature[Config.ExternalDS18B20]);
			}
			fclose(fp);
		}
		
		sleep(Config.TelemetryFileUpdate);
	}

	return 0;
}
