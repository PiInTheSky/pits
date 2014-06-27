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

#define LED_WARN		11
#define LED_OK			4


void *LEDLoop(void *some_void_ptr)
{
	struct TGPS *GPS;
	int Flash;

	GPS = (struct TGPS *)some_void_ptr;

	// We have 2 LED outputs
	pinMode (LED_WARN, OUTPUT);
	pinMode (LED_OK, OUTPUT);
	
	while (1)
	{
		digitalWrite (LED_WARN, GPS->Satellites < 4);	
		digitalWrite (LED_OK, (GPS->Altitude < 2000) && (Flash ^= 1));	

		sleep(1);
	}

	return 0;
}
