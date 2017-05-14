#include <stdio.h>
#include <stdlib.h>
#include <linux/i2c-dev.h>
#include <fcntl.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <errno.h>
#include <wiringPiI2C.h>
#include <wiringPi.h>


#include "gps.h"
#include "misc.h"

#define MCP3426_ADDRESS 0x68  		// I2C address of MCP3426 ADC

int I2CADCExists(void)
{
	int fd, result;
	
	result = 0;
	
	if ((fd = open_i2c(MCP3426_ADDRESS)) >= 0)
	{
		if (wiringPiI2CRead(fd) != -1)
		{
			result = 1;
		}
		close(fd);
	}
	
	return result;
}

unsigned int I2CAnalogRead (int fd, int chan)
{
	unsigned char buffer [4] ;
	unsigned int value;
	
	wiringPiI2CWrite(fd, 0x80 +			// bit  7   -  1 = start single-shot conversion
						 (chan << 6) +	// bits 6,5 - 00 = channel 1, 01 = channel 2
						 0x00 +			// bit 4    -  0 = single shot mode
						 0x04 +			// bits 3,2 - 10 = 16-bit data (14 measurements per second)
						 0x00);			// bits 1,0 - 00 = Gain x1
	
	delay (70) ;
    read (fd, buffer, 3) ;

	value = buffer[0];
	value <<= 8;
	value += buffer[1];
	
	return value;
}


double ReadI2CADC(int fd, int chan, double FullScale)
{
	unsigned int RawValue;
    double Value;
	int i;

	Value = 0;
	for (i=0; i<10; i++)
	{
		RawValue = I2CAnalogRead(fd, chan);
		Value += (double)RawValue * FullScale / 65536;
	}
	
	return Value / 10;
}

void *I2CADCLoop(void *some_void_ptr)
{
	struct TGPS *GPS;
	int fd;

	GPS = (struct TGPS *)some_void_ptr;

	// Initialise MCP3426
	
	while (1)
	{
		if ((fd = open_i2c(MCP3426_ADDRESS)) >= 0)
		{
			double BatteryVoltage, BoardCurrent;
			
			BatteryVoltage = ReadI2CADC(fd, 0, Config.MaxADCVoltage);
			GPS->BatteryVoltage = BatteryVoltage;
			// printf("Battery Voltage = %lf\n", BatteryVoltage);
			
			BoardCurrent = ReadI2CADC(fd, 1, 1.4);
			GPS->BoardCurrent = BoardCurrent;
			// printf("Board Current = %lf\n", BoardCurrent);

			close(fd);
		}

		sleep(10);
	}

    return 0;
}