#include <stdio.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <time.h>
#include <math.h>
#include <unistd.h>
#include <wiringPi.h>

#include "gps.h"
#include "misc.h"
#include "pin.h"

int ControlPinNumber=0;
int ControlPeriod=0;
int ControlPinValue=0;
int UsePWM=0;
	
void ControlPin(int Pin, int Period)
{
	ControlPinNumber = Pin;
	ControlPeriod = Period;
	UsePWM = 0;
}

void ControlServo(int Pin, int Period, int Value)
{
	ControlPinNumber = Pin;
	ControlPeriod = Period;
	ControlPinValue = Value;
	UsePWM = 1;
}

void *PinLoop(void *some_void_ptr)
{
	while (1)
	{
		if ((ControlPinNumber > 0) && (ControlPeriod >= 0) && (ControlPeriod <= 60))
		{
			if (UsePWM)
			{
				pinMode (ControlPinNumber, PWM_OUTPUT);
				pwmWrite (ControlPinNumber, ControlPinValue);
				if (ControlPeriod > 0)
				{
					sleep(ControlPeriod);
					pwmWrite (ControlPinNumber, 0);
				}
			}
			else
			{
				pinMode (ControlPinNumber, OUTPUT);
				digitalWrite (ControlPinNumber, 1);
				if (ControlPeriod > 0)
				{
					sleep(ControlPeriod);
					digitalWrite (ControlPinNumber, 0);
				}
			}
			
			ControlPinNumber = 0;
		}
		
		sleep(1);
	}

	return 0;
}
