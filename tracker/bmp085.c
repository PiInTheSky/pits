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
#include <wiringPiSPI.h>

#include "gps.h"
#include "misc.h"

struct TBMP
{
	short fd;
	short ac1;
	short ac2; 
	short ac3; 
	unsigned short ac4;
	unsigned short ac5;
	unsigned short ac6;
	short B1; 
	short B2;
	short Mb;
	short Mc;
	short Md;
};

int bmp085Calibration(struct TBMP *bmp);
double bmp085GetTemperature(struct TBMP *bmp);
double bmp085GetPressure(struct TBMP *bmp, double Temperature);
int bmp085ReadInt(short fd, unsigned char address);
unsigned short bmp085ReadUT(short fd);
double bmp085ReadUP(short fd);

#define BMP085_ADDRESS 0x77  		// I2C address of BMP085 pressure sensor


void *BMP085Loop(void *some_void_ptr)
{
	struct TBMP bmp;
	struct TGPS *GPS;

	GPS = (struct TGPS *)some_void_ptr;

	// Initialise BMP085
	if ((bmp.fd = open_i2c(BMP085_ADDRESS)) >= 0)
	{
		int NoBMP;
		
		NoBMP = bmp085Calibration(&bmp);
		close(bmp.fd);
		
		if (NoBMP)
		{
			return 0;
		}
	}
	else
	{
		return 0;
	}
	
	while (1)
	{
		if ((bmp.fd = open_i2c(BMP085_ADDRESS)) >= 0)
		{
			GPS->BMP180Temperature = bmp085GetTemperature(&bmp);
			GPS->Pressure = bmp085GetPressure(&bmp, GPS->BMP180Temperature);

			// printf("Temperature is %5.2lf\n", GPS->BMP180Temperature);
			// printf("Pressure is %5.2lf\n", GPS->Pressure);

			close(bmp.fd);
		}

		sleep(10);
	}

    return 0;
}

int bmp085Calibration(struct TBMP *bmp)
{
	if (bmp085ReadInt(bmp->fd, 0xAA) < 0)
	{
		return 1;
	}
	
	bmp->ac1 = bmp085ReadInt(bmp->fd, 0xAA);
	bmp->ac2 = bmp085ReadInt(bmp->fd, 0xAC);
	bmp->ac3 = bmp085ReadInt(bmp->fd, 0xAE);
	bmp->ac4 = bmp085ReadInt(bmp->fd, 0xB0);
	bmp->ac5 = bmp085ReadInt(bmp->fd, 0xB2);
	bmp->ac6 = bmp085ReadInt(bmp->fd, 0xB4);
	bmp->B1 = bmp085ReadInt(bmp->fd, 0xB6);
	bmp->B2 = bmp085ReadInt(bmp->fd, 0xB8);
	bmp->Mb = bmp085ReadInt(bmp->fd, 0xBA);
	bmp->Mc = bmp085ReadInt(bmp->fd, 0xBC);
	bmp->Md = bmp085ReadInt(bmp->fd, 0xBE);
	
	return 0;

	// printf ("Values are %d %d %d %u %u %u %d %d %d %d %d\n", ac1, ac2, ac3, ac4, ac5, ac6, B1, B2, Mb, Mc, Md);
}

// Calculate temperature given ut.
// Value returned will be in units of 0.1 deg C
double bmp085GetTemperature(struct TBMP *bmp)
{
	double c5, alpha, mc, md;
	unsigned short ut;

	ut = bmp085ReadUT(bmp->fd);

	c5 = (double)bmp->ac5 / (32768 * 160);

	alpha = c5 * (double)(ut - bmp->ac6);

	mc = 2048 * bmp->Mc / (160 * 160);

	md = (double)bmp->Md / 160;

	return alpha + mc / (alpha + md);
}

// Calculate pressure given up
// calibration values must be known
// b5 is also required so bmp085GetTemperature(...) must be called first.
// Value returned will be pressure in units of Pa.
double bmp085GetPressure(struct TBMP *bmp, double Temperature)
{
	double Pu, s, x0, x1, x2, c3, c4, b1, y0, y1, y2, p0, p1, p2, x, y, z, P;

	Pu = bmp085ReadUP(bmp->fd);
	// printf("Pu = %lf\n", Pu);

	s = Temperature - 25;
	x0 = bmp->ac1;
	x1 = (double)bmp->ac2 * 160 / 8192;
	x2 = (double)bmp->B2 * 160 * 160 / 33554432;
	c3 = (double)bmp->ac3 * 160 / 32768;
	c4 = (double)bmp->ac4 / 32768000;
	b1 = (double)bmp->B1 * 160 * 160 /1073741824;
	y0 = c4 * 32768;
	y1 = c4 * c3;
	y2 = c4 * b1;
	p0 = (3791.0 - 8.0) / 1600.0;
	p1 = 1.0 - 7357.0 / 1048576.0;
	p2 = 303800.0 / 68719476740.0;
	x = x2 * s * s + x1 * s + x0;
	y = y2 * s * s + y1 * s + y0;
	z = (Pu - x) / y;
	P = p2 * z * z + p1 * z + p0;


	return P;
}

// Read 2 bytes from the BMP085
// First byte will be from 'address'
// Second byte will be from 'address'+1
int bmp085ReadInt(short fd, unsigned char address)
{
	unsigned char buf[10];

	buf[0] = address;

	if ((write(fd, buf, 1)) != 1) {								// Send register we want to read from	
		printf("Error writing to i2c slave\n");
		return -1;
	}
	
	if (read(fd, buf, 2) != 2) {								// Read back data into buf[]
		printf("Unable to read from slave\n");
		return -1;
	}

	return (short) buf[0]<<8 | buf[1];
}

// Read the uncompensated temperature value
unsigned short bmp085ReadUT(short fd)
{
 	unsigned short ut;
	unsigned char buf[10];
  
  // Write 0x2E into Register 0xF4
  // This requests a temperature reading

	buf[0] = 0xF4;
	buf[1] = 0x2E;

	if ((write(fd, buf, 2)) != 2)
	{
		printf("Error writing to i2c slave\n");
		return 0;
	}

	usleep(5000);
	
	ut = bmp085ReadInt(fd, 0xF6);

	// printf("ut = %u\n", ut);

	return ut;
}

// Read the uncompensated pressure value
double bmp085ReadUP(short fd)
{
  unsigned char msb, lsb, xlsb;
  double up;
	unsigned char buf[10];
  
  // Write 0x34 into register 0xF4
  // Request a pressure reading w/ oversampling setting

	buf[0] = 0xF4;
	buf[1] = 0x34;

	if ((write(fd, buf, 2)) != 2)
	{
		printf("Error writing to i2c slave\n");
		return 0;
	}

	usleep(3000);

	buf[0] = 0xF6;

	if ((write(fd, buf, 1)) != 1)
	{
		printf("Error writing to i2c slave\n");
		return 0;
	}
	
	if (read(fd, buf, 3) != 3)
	{
		printf("Unable to read from slave\n");
		return 0;
	}

	msb = buf[0];
	lsb = buf[1];
	xlsb = buf[2];

	// printf("Pressure values are %02X %02X %02X\n", msb, lsb, xlsb);

	up = (double)msb * 256 + (double)lsb + (double)xlsb / 256;


	return up;
}

