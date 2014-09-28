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
#include "adc.h"

int AnalogRead (int chan)
{
  unsigned char spiData [3] ;
  unsigned char chanBits ;

  chanBits = 0xC0 | ((chan & 7) << 3);

  spiData[0] = chanBits ;
  spiData[1] = 0;
  spiData[2] = 0;

  wiringPiSPIDataRW (0, spiData, 3) ;

  return ((spiData [0] << 7) | (spiData [1] >> 1)) & 0x3FF ;
}

double GetVoltage(int chan, double FullScale)
{
	int RawValue;
    double Voltage;

   	RawValue = AnalogRead(chan);
	Voltage = (double)RawValue * FullScale / 1024.0;

	return Voltage;
}


void *ADCLoop(void *some_void_ptr)
{
	double BatteryVoltage;
	FILE *fp;
	struct TGPS *GPS;

	GPS = (struct TGPS *)some_void_ptr;

    // printf("Opening SPI ...\n");

    if (gertboardSPISetup () < 0)
    {
        printf("Failed to setup SPI\n");
    }
		
	while (1)
	{
		BatteryVoltage = GetVoltage(2, 6.67);

		GPS->BatteryVoltage = BatteryVoltage;

		sleep(10);
	}

	return 0;
}
