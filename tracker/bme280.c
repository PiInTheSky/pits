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
#include <wiringPiI2C.h>
#include <inttypes.h>

#include "gps.h"
#include "misc.h"

// Modes
#define BME280_OSAMPLE_1  1
#define BME280_OSAMPLE_2  2
#define BME280_OSAMPLE_4  3
#define BME280_OSAMPLE_8  4
#define BME280_OSAMPLE_16 5

// BME280 Registers
#define BME280_REGISTER_DIG_T1 0x88
#define BME280_REGISTER_DIG_T2 0x8A
#define BME280_REGISTER_DIG_T3 0x8C

#define BME280_REGISTER_DIG_P1 0x8E
#define BME280_REGISTER_DIG_P2 0x90
#define BME280_REGISTER_DIG_P3 0x92
#define BME280_REGISTER_DIG_P4 0x94
#define BME280_REGISTER_DIG_P5 0x96
#define BME280_REGISTER_DIG_P6 0x98
#define BME280_REGISTER_DIG_P7 0x9A
#define BME280_REGISTER_DIG_P8 0x9C
#define BME280_REGISTER_DIG_P9 0x9E

#define BME280_REGISTER_DIG_H1 0xA1
#define BME280_REGISTER_DIG_H2 0xE1
#define BME280_REGISTER_DIG_H3 0xE3
#define BME280_REGISTER_DIG_H4 0xE4
#define BME280_REGISTER_DIG_H5 0xE5
#define BME280_REGISTER_DIG_H6 0xE6
#define BME280_REGISTER_DIG_H7 0xE7

#define BME280_REGISTER_CHIPID 0xD0
#define BME280_REGISTER_VERSION 0xD1
#define BME280_REGISTER_SOFTRESET 0xE0

#define BME280_REGISTER_CONTROL_HUM 0xF2
#define BME280_REGISTER_CONTROL 0xF4
#define BME280_REGISTER_CONFIG 0xF5
#define BME280_REGISTER_PRESSURE_DATA 0xF7
#define BME280_REGISTER_TEMP_DATA 0xFA
#define BME280_REGISTER_HUMIDITY_DATA 0xFD

struct TBME
{
	// i2c file handle
	int fd;
		
	// Data registers containing raw readings
	unsigned char Registers[8];
	
	// Calibration Constants
	uint32_t T1;
	int32_t T2, T3;
	uint32_t P1;
	int32_t P2, P3, P4, P5, P6, P7, P8, P9;
	uint8_t H1, H3;
	int16_t H2, H4, H5;
	int8_t H6;
	
	// Raw values from registers
	double RawTemperature;
	double RawPressure;
	double RawHumidity;
	double RawTempFine;
};

void bme280Calibration(struct TBME *bme);
int bme280ReadInt(struct TBME *bme, unsigned char address);
// double bme280ReadUP(short fd);
int BMEAddress;

#define BME280_ADDRESS 0x76  		// Possible I2C address of BME280 pressure sensor (could also be on ox77)


int BMEPresent(struct TBME *bme, int Address)
{
	int IsPresent;
	
	IsPresent = 0;
	
	if ((bme->fd = open_i2c(Address)) >= 0)
	{
		if (wiringPiI2CRead(bme->fd) != -1)
		{
			// Device present
			IsPresent = 1;
			bme280Calibration(bme);
		}
		
		close(bme->fd);
	}
	
	return IsPresent;
}

void bme280WriteRegister(struct TBME *bme, int Register, int Value)
{
	unsigned char buf[2];
  
	buf[0] = Register;
	buf[1] = Value;
	
	if ((write(bme->fd, buf, 2)) != 2)
	{
		printf("Error writing to BME280\n");
	}
}

uint16_t bme280ReadUInt16(struct TBME *bme, int Register)
{
	unsigned char buf[2];
  
	buf[0] = Register;
	
	if ((write(bme->fd, buf, 1)) != 1)
	{
		printf("Error writing to BME280\n");
		return 0;
	}
	
	if (read(bme->fd, buf, 2) != 2)
	{
		printf("Unable to read from BME280\n");
		return 0;
	}
	
	return (uint16_t)buf[1]<<8 | (uint16_t)buf[0];
}

uint8_t bme280ReadUInt8(struct TBME *bme, int Register)
{
	unsigned char buf[1];
  
	buf[0] = Register;
	
	if ((write(bme->fd, buf, 1)) != 1)
	{
		printf("Error writing to BME280\n");
		return 0;
	}
	
	if (read(bme->fd, buf, 1) != 1)
	{
		printf("Unable to read from BME280\n");
		return 0;
	}
	
	return (uint8_t)buf[0];
}

int8_t bme280ReadInt8(struct TBME *bme, int Register)
{
	unsigned char buf[1];
  
	buf[0] = Register;
	
	if ((write(bme->fd, buf, 1)) != 1)
	{
		printf("Error writing to BME280\n");
		return 0;
	}
	
	if (read(bme->fd, buf, 1) != 1)
	{
		printf("Unable to read from BME280\n");
		return 0;
	}
	
	return (int8_t)buf[0];
}

int16_t bme280ReadInt16(struct TBME *bme, int Register)
{
	unsigned char buf[2];
  
	buf[0] = Register;
	
	if ((write(bme->fd, buf, 1)) != 1)
	{
		printf("Error writing to BME280\n");
		return 0;
	}
	
	if (read(bme->fd, buf, 2) != 2)
	{
		printf("Unable to read from BME280\n");
		return 0;
	}

	return (int16_t)buf[1]<<8 | (int16_t)buf[0];
}

void bme280Calibration(struct TBME *bme)
{
    bme->T1 = bme280ReadUInt16(bme, BME280_REGISTER_DIG_T1);
	bme->T2 = bme280ReadInt16(bme, BME280_REGISTER_DIG_T2);
	bme->T3 = bme280ReadInt16(bme, BME280_REGISTER_DIG_T3);
	
	printf("T1=%" PRId32 ", T2=%" PRId32 ", T3=%" PRId32 "\n", bme->T1, bme->T2, bme->T3);

    bme->P1 = bme280ReadUInt16(bme, BME280_REGISTER_DIG_P1);
    bme->P2 = bme280ReadInt16(bme, BME280_REGISTER_DIG_P2);
    bme->P3 = bme280ReadInt16(bme, BME280_REGISTER_DIG_P3);
    bme->P4 = bme280ReadInt16(bme, BME280_REGISTER_DIG_P4);
    bme->P5 = bme280ReadInt16(bme, BME280_REGISTER_DIG_P5);
    bme->P6 = bme280ReadInt16(bme, BME280_REGISTER_DIG_P6);
    bme->P7 = bme280ReadInt16(bme, BME280_REGISTER_DIG_P7);
    bme->P8 = bme280ReadInt16(bme, BME280_REGISTER_DIG_P8);
    bme->P9 = bme280ReadInt16(bme, BME280_REGISTER_DIG_P9);
	
    bme->H1 = bme280ReadUInt8(bme, BME280_REGISTER_DIG_H1);
    bme->H2 = bme280ReadInt16(bme, BME280_REGISTER_DIG_H2);
    bme->H3 = bme280ReadUInt8(bme, BME280_REGISTER_DIG_H3);
	
    bme->H4 = bme280ReadInt8(bme, BME280_REGISTER_DIG_H4);
	bme->H4 *= 16;	// <<= 4;
    bme->H4 |= (bme280ReadUInt8(bme, BME280_REGISTER_DIG_H5) & 0x0F);
	// printf("Registers %d %u, Value %d\n",
			// bme280ReadInt8(bme, BME280_REGISTER_DIG_H4),
			// bme280ReadUInt8(bme, BME280_REGISTER_DIG_H5),
			// bme->H4);
	
    bme->H5 = bme280ReadInt8(bme, BME280_REGISTER_DIG_H6);
	bme->H5 *= 16;	// <<= 4;
    bme->H5 |= (bme280ReadUInt8(bme, BME280_REGISTER_DIG_H5) >> 4);
	// printf("Registers %d %u, Value %d\n",
			// bme280ReadInt8(bme, BME280_REGISTER_DIG_H6),
			// bme280ReadUInt8(bme, BME280_REGISTER_DIG_H5),
			// bme->H5);

    bme->H6 = bme280ReadInt8(bme, BME280_REGISTER_DIG_H7);
	// printf("H6 = %d\n", bme->H6);
}

void bme280StartMeasurement(struct TBME *bme)
{
	int Mode = BME280_OSAMPLE_16;

	bme280WriteRegister(bme, BME280_REGISTER_CONTROL_HUM, Mode);
	
	bme280WriteRegister(bme, BME280_REGISTER_CONTROL, Mode << 5 | Mode << 2 | 1);
}

void bme280ReadDataRegisters(struct TBME *bme)
{
	// Read 8 data registers from F7 to FE
	unsigned char buf[1];
  
	buf[0] = 0xF7;

	if ((write(bme->fd, buf, 1)) != 1)								// Send register we want to read from	
	{
		printf("Error writing to BME280\n");
		return;
	}
	
	if (read(bme->fd, bme->Registers, 8) != 8)
	{
		printf("Unable to read from BME280\n");
		return;
	}
	
	// printf ("Registers are %02X, %02X ...\n", bme->Registers[0], bme->Registers[1]);
}
	
void bme280GetRawValues(struct TBME *bme)
{
	uint32_t high, medium, low, Value;

	// Temperature
	high = bme->Registers[3];
	medium = bme->Registers[4];
	low = bme->Registers[5];
	
	Value = high<<16 | medium<<8 | low;

	bme->RawTemperature	= (double)Value / 16.0;
	
	// Pressure
	high = bme->Registers[0];
	medium = bme->Registers[1];
	low = bme->Registers[2];
	
	Value = high<<16 | medium<<8 | low;

	bme->RawPressure = (double)Value / 16.0;
	
	// Raw Humidity
	high = bme->Registers[6];
	low = bme->Registers[7];
	
	Value = high<<8 | low;

	bme->RawHumidity = (double)Value;
}

double bme280Temperature(struct TBME *bme)
{
	double var1, var2, T, T1, T2, T3;
		
	T = bme->RawTemperature;

	T1 = bme->T1;
	T2 = bme->T2;
	T3 = bme->T3;
	
	var1 = (T/16384.0 - T1/1024.0) * T2;
	
	var2 = ((T/131072.0 - T1/8192.0) * (T/131072.0 - T1/8192.0)) * T3;
	
	bme->RawTempFine = var1 + var2;

	return (var1 + var2) / 5120.0;
}

double bme280Pressure(struct TBME *bme)
{
	double var1, var2, p;

	var1 = (bme->RawTempFine/2.0) - 64000.0;
	var2 = var1 * var1 * ((double)bme->P6) / 32768.0;
	var2 = var2 + var1 * ((double)bme->P5) * 2.0;
	
	var2 = (var2/4.0)+(((double)bme->P4) * 65536.0);
	
	var1 = (((double)bme->P3) * var1 * var1 / 524288.0 + ((double)bme->P2) * var1) / 524288.0;
	
	var1 = (1.0 + var1 / 32768.0)*((double)bme->P1);
	if (var1 == 0.0) return 0;
		
	p = 1048576.0 - bme->RawPressure;
	p = (p - (var2 / 4096.0)) * 6250.0 / var1;
	
	var1 = ((double)bme->P9) * p * p / 2147483648.0;
	var2 = p * ((double)bme->P8) / 32768.0;
	
	p = p + (var1 + var2 + ((double)bme->P7)) / 16.0;
	
	return p / 100;
}

double bme280Humidity(struct TBME *bme)
{
	double H;
	
	H = bme->RawTempFine - 76800.0;
	
	H = (bme->RawHumidity - (((double)bme->H4) * 64.0 + ((double)bme->H5) / 16384.0 * H)) *
		(((double)bme->H2) / 65536.0 * (1.0 + ((double)bme->H6) / 67108864.0 * H *
		(1.0 + ((double)bme->H3) / 67108864.0 * H)));
		
	H = H * (1.0 - ((double)bme->H1) * H / 524288.0);
	
	if (H > 100.0) H = 100.0;
	if (H < 0.0) H = 0.0;

	return H;
}
// 

// Read 2 bytes from the bme280
// First byte will be from 'address'
// Second byte will be from 'address'+1
int bme280ReadInt(struct TBME *bme, unsigned char address)
{
	unsigned char buf[10];

	buf[0] = address;

	if ((write(bme->fd, buf, 1)) != 1) {								// Send register we want to read from	
		printf("Error writing to BME280\n");
		return -1;
	}
	
	if (read(bme->fd, buf, 2) != 2) {								// Read back data into buf[]
		printf("Unable to read from BME280\n");
		return -1;
	}

	return (short) buf[0]<<8 | buf[1];
}


void *BME280Loop(void *some_void_ptr)
{
	struct TBME bme;
	struct TGPS *GPS;

	GPS = (struct TGPS *)some_void_ptr;

	if (BMEPresent(&bme, BME280_ADDRESS))
	{
		BMEAddress = BME280_ADDRESS;
	}
	else if (BMEPresent(&bme, BME280_ADDRESS+1))
	{
		BMEAddress = BME280_ADDRESS+1;
	}
	else
	{
		BMEAddress = 0;
	}

	if (BMEAddress)
	{
		printf("BME280 Found At Address %02xh\n", BMEAddress);
	}
	else
	{
		printf("BME280 Not Found (nothing at addresses 76/77h)\n");
	}
	
	while (BMEAddress)
	{
		if ((bme.fd = open_i2c(BMEAddress)) >= 0)
		{
			bme280StartMeasurement(&bme);
		
			sleep(1);		// Wait (ample time) for measurement
		
			bme280ReadDataRegisters(&bme);
			
			bme280GetRawValues(&bme);
			
			GPS->BMP180Temperature = bme280Temperature(&bme);
			GPS->Pressure = bme280Pressure(&bme);
			GPS->Humidity = bme280Humidity(&bme);

			// printf("Temperature is %5.2lf\n", GPS->BMP180Temperature);
			// printf("Pressure is %5.2lf\n", GPS->Pressure);
			// printf("Humidity is %5.2lf\n", GPS->Humidity);

			close(bme.fd);
		}

		sleep(10);
	}
	
	return NULL;
}


