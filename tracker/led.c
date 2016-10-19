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


void *LEDLoop(void *some_void_ptr)
{
	struct TGPS *GPS;
	int Flash=0;

	GPS = (struct TGPS *)some_void_ptr;

	// We have 2 LED outputs
	pinMode (Config.LED_Warn, OUTPUT);
	pinMode (Config.LED_OK, OUTPUT);
	
	while (1)
	{
		digitalWrite (Config.LED_Warn, (GPS->Satellites < 4) && (GPS->Altitude < 2000) && (GPS->MessageCount & 1));	
		digitalWrite (Config.LED_OK, (GPS->Satellites >= 4) && (GPS->Altitude < 2000) && (Flash ^= 1));	

		sleep(1);
	}

	return 0;
}
