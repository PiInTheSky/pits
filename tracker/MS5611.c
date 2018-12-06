#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
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
#include <wiringPi.h>
#include <wiringPiI2C.h>
#include <inttypes.h>

#include "gps.h"
#include "misc.h"
#include "MS5611.h"

#define MS5611_ADDRESS			0x77

#define MS5611_CMD_ADC_READ     0x00
#define MS5611_CMD_RESET        0x1E
#define MS5611_CMD_CONV_D1      0x40
#define MS5611_CMD_CONV_D2      0x50
#define MS5611_CMD_READ_PROM    0xA2

#define MS5611_ULTRA_HIGH_RES   0x08
#define MS5611_HIGH_RES         0x06
#define MS5611_STANDARD         0x04
#define MS5611_LOW_POWER        0x02
#define MS5611_ULTRA_LOW_POWER  0x00

void ms5611WriteRegister(int fd, int Value)
{
	unsigned char buf[1];
  
	buf[0] = Value;
	
	if ((write(fd, buf, 1)) != 1)
	{
		printf("Error writing to MS5611\n");
	}
}

uint16_t ms5611ReadRegister16(int fd, uint8_t address)
{
	unsigned char buf[2];

	buf[0] = address;

	if ((write(fd, buf, 1)) != 1) {								// Send register we want to read from	
		printf("Error writing to i2c slave\n");
		return -1;
	}
	
	if (read(fd, buf, 2) != 2) {								// Read back data into buf[]
		printf("Unable to read from slave\n");
		return -1;
	}

	return (uint16_t) buf[0]<<8 | buf[1];
}

int32_t ms5611ReadRegister24(int fd, uint8_t address)
{
	unsigned char buf[3];

	buf[0] = address;

	// Send register we want to read from	
	if ((write(fd, buf, 1)) != 1)
	{
		printf("Error writing to i2c slave\n");
		return -1;
	}
	
	// Read back data into buf[]
	if (read(fd, buf, 3) != 3)
	{
		printf("Unable to read from slave\n");
		return -1;
	}

	return (int32_t) buf[0]<<16 | buf[1]<<8 | buf[2];
}

void ms5611ReadPROM(int fd, uint16_t *fc)
{
	uint8_t offset;
	
    for (offset = 0; offset < 6; offset++)
    {
		fc[offset] = ms5611ReadRegister16(fd, MS5611_CMD_READ_PROM + (offset * 2));
    }
}

uint32_t ms5611ReadRawTemperature(int fd)
{
	ms5611WriteRegister(fd, MS5611_CMD_CONV_D2 + MS5611_ULTRA_HIGH_RES);

    delay(10);

    return ms5611ReadRegister24(fd, MS5611_CMD_ADC_READ);
}

uint32_t ms5611ReadRawPressure(int fd)
{
	ms5611WriteRegister(fd, MS5611_CMD_CONV_D1 + MS5611_ULTRA_HIGH_RES);

    delay(10);

    return ms5611ReadRegister24(fd, MS5611_CMD_ADC_READ);
}

double ms5611ReadTemperature(int fd, uint16_t *fc, int compensation)
{
    uint32_t D2;
    int32_t dT, TEMP, TEMP2;

    D2 = ms5611ReadRawTemperature(fd);
    dT = D2 - (uint32_t)fc[4] * 256;
	TEMP = 2000 + ((int64_t) dT * fc[5]) / 8388608;
    TEMP2 = 0;

    if (compensation)
    {
		if (TEMP < 2000)
		{
			TEMP2 = (dT * dT) / (2 << 30);
		}
    }

    TEMP = TEMP - TEMP2;

    return ((double)TEMP/100);
}


double ms5611ReadPressure(int fd, uint16_t *fc, int compensation)
{
    uint32_t D1, D2;
    int32_t dT;
    int64_t OFF, SENS, TEMP3;

    D1 = ms5611ReadRawPressure(fd);

    D2 = ms5611ReadRawTemperature(fd);
    
	dT = D2 - (uint32_t)fc[4] * 256;

    OFF = (int64_t)fc[1] * 65536 + (int64_t)fc[3] * dT / 128;
    
	SENS = (int64_t)fc[0] * 32768 + (int64_t)fc[2] * dT / 256;

    if (compensation)
    {
		int32_t TEMP;
		int64_t OFF2, SENS2;
		
		TEMP = 2000 + ((int64_t) dT * fc[5]) / 8388608;

		OFF2 = 0;
		SENS2 = 0;

		if (TEMP < 2000)
		{
			OFF2 = 5 * ((TEMP - 2000) * (TEMP - 2000)) / 2;
			SENS2 = 5 * ((TEMP - 2000) * (TEMP - 2000)) / 4;
		}

		if (TEMP < -1500)
		{
			OFF2 = OFF2 + 7 * ((TEMP + 1500) * (TEMP + 1500));
			SENS2 = SENS2 + 11 * ((TEMP + 1500) * (TEMP + 1500)) / 2;
		}

		OFF = OFF - OFF2;
		SENS = SENS - SENS2;
    }

	TEMP3 = (D1 * SENS) / 2097152 - OFF;
	
    return  (double)TEMP3 / 327680.0;
}

void *MS5611Loop(void *some_void_ptr)
{
	struct TGPS *GPS;
	int fd;
	uint16_t fc[6];

	GPS = (struct TGPS *)some_void_ptr;

	// Initialise BMP085
	if ((fd = open_i2c(MS5611_ADDRESS)) >= 0)
	{
		printf("MS6511 found OK\n");
				
		// Reset device
		ms5611WriteRegister(fd, MS5611_CMD_RESET);
		
		delay(100);

		ms5611ReadPROM(fd, fc);
		
		close(fd);
		
		while (1)
		{
			if ((fd = open_i2c(MS5611_ADDRESS)) >= 0)
			{
				GPS->BMP180Temperature = ms5611ReadTemperature(fd, fc, 1);
				GPS->Pressure = ms5611ReadPressure(fd, fc, GPS->BMP180Temperature);

				printf("**** Temperature is %5.2lf\n", GPS->BMP180Temperature);
				printf("**** Pressure is %5.2lf\n", GPS->Pressure);

				close(fd);
			}

			sleep(10);
		}
	}
	return 0;
}

