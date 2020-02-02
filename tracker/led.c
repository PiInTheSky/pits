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
	int peak_alt=0;
	int servo_open=0;

	GPS = (struct TGPS *)some_void_ptr;

	// We have 2 LED outputs
	pinMode (Config.LED_Warn, OUTPUT);
	pinMode (Config.LED_OK, OUTPUT);
	
	pinMode (26, PWM_OUTPUT); //GPIO 26 for cutdown servo
	pwmSetMode(PWM_MODE_MS);
	pwmSetRange(2000);
	pwmSetClock(192);
	pwmWrite (26, 150); //Set servo PWM for 'closed' position

	digitalWrite (Config.PiezoPin, 1);
	sleep(1);
	digitalWrite (Config.PiezoPin, 0);
	
	while (1)
	{
		digitalWrite (Config.LED_Warn, (GPS->Satellites < 4) && (GPS->Altitude < 2000) && (GPS->MessageCount & 1));	
		digitalWrite (Config.LED_OK, (GPS->Satellites >= 4) && (GPS->Altitude < 2000) && (Flash ^= 1));	
		
		if ((servo_open == 0) && (GPS->Satellites >= 5))
			{
				if (GPS->Altitude > peak_alt)
					peak_alt = GPS->Altitude;
				
				if ((GPS->Altitude > 20000) || (peak_alt > (GPS->Altitude + 300)) || (GPS->Latitude > 45.1) || (GPS->Longitude > 26.8))
				{
					pwmWrite (26, 225); //Set servo PWM for 'open' position
					servo_open=1;
				}
			}
		
		sleep(1);
	}

	return 0;
}
