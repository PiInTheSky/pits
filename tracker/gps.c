/* ========================================================================== */
/*   gps.c                                                                    */
/*                                                                            */
/*   i2c bit-banging code for ublox on Pi A/A+/B/B+                           */
/*                                                                            */
/*   Description                                                              */
/*                                                                            */
/*   12/10/14: Modified for the UBlox Max8 on the B+ board                    */
/*   19/12/14: Rewritten to use wiringPi library                              */
/*                                                                            */
/*                                                                            */
/* ========================================================================== */
// Version 0.1 7/9/2012
// * removed a line of debug code

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
#include <sys/mman.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <wiringPi.h>
#include "misc.h"
#include "gps.h"

struct i2c_info {
    uint8_t address; // 7 bit address
    uint8_t sda; // pin used for sda coresponds to gpio
    uint8_t scl; // clock
    uint32_t clock_delay; // proporional to bus speed
    uint32_t timeout; //
	int Failed;
};

void delayMilliseconds (unsigned int millis)
{
  struct timespec sleeper, dummy ;

  sleeper.tv_sec  = (time_t)(millis / 1000) ;
  sleeper.tv_nsec = (long)(millis % 1000) * 1000000 ;
  nanosleep (&sleeper, &dummy) ;
}


// *****************************************************************************
// open bus, sets structure and initialises GPIO
// The scl and sda line are set to be always 0 (low) output, when a high is
// required they are set to be an input.
// *****************************************************************************
int OpenI2C(struct i2c_info *bb,
			uint8_t adr, // 7 bit address
			uint8_t data,   // GPIO pin for data 
			uint8_t clock,  // GPIO pin for clock
			uint32_t delay, // clock delay us
			uint32_t timeout) // clock stretch & timeout
{
    bb->address = adr;
    bb->sda = data;
    bb->scl = clock;
    bb->clock_delay = delay;
    bb->timeout = timeout;
	bb->Failed = 0;

	
    // also they should be set low, input - output determines level
	pinMode(bb->sda, INPUT);
	pinMode(bb->scl, INPUT);

	digitalWrite(bb->sda, LOW);
	digitalWrite(bb->scl, LOW);

	pullUpDnControl(bb->sda, PUD_UP);
	pullUpDnControl(bb->scl, PUD_UP);

    return 0;
}

void ResetI2C(struct i2c_info *bb)
{
	int i;
	
	pinMode(bb->sda, INPUT);
	digitalWrite(bb->scl, LOW);
	
	for (i=0; i<16; i++)
	{
		pinMode(bb->scl, OUTPUT);
		usleep(bb->clock_delay);
		pinMode(bb->scl, INPUT);
		usleep(bb->clock_delay);
	}
}

// *****************************************************************************
// bit delay, determins bus speed. nanosleep does not give the required delay
// its too much, by about a factor of 100
// This simple delay using the current Aug 2012 board gives aproximately:
// 500 = 50kHz. Obviously a better method of delay is needed.
// *****************************************************************************
void BitDelay(uint32_t del)
{
	usleep(del);
}

// *****************************************************************************
// clock with stretch - bit level
// puts clock line high and checks that it does go high. When bit level
// stretching is used the clock needs checking at each transition
// *****************************************************************************
void I2CClockHigh(struct i2c_info *bb)
{
    uint32_t to = bb->timeout;
	
	pinMode(bb->scl, INPUT);
	
    // check that it is high
	while (!digitalRead(bb->scl))
	{
		usleep(1000);
        if(!to--)
		{
            fprintf(stderr, "i2c_info: Clock line held by slave\n");
			bb->Failed = 1;
			return;
        }
    }
}

void I2CClockLow(struct i2c_info *bb)
{
	pinMode(bb->scl, OUTPUT);
}

void I2CDataLow(struct i2c_info *bb)
{
	pinMode(bb->sda, OUTPUT);
}

void I2CDataHigh(struct i2c_info *bb)
{
	pinMode(bb->sda, INPUT);
}

	
// *****************************************************************************
// Returns 1 if bus is free, i.e. both sda and scl high
// *****************************************************************************
int BusIsFree(struct i2c_info *bb)
{
	return digitalRead(bb->sda) && digitalRead(bb->scl);
		
}

// *****************************************************************************
// Start condition
// This is when sda is pulled low when clock is high. This also puls the clock
// low ready to send or receive data so both sda and scl end up low after this.
// *****************************************************************************
void I2CStart(struct i2c_info *bb)
{
    uint32_t to = bb->timeout;
    // bus must be free for start condition
    while(to-- && !BusIsFree(bb))
	{
		usleep(1000);
	}

    if (!BusIsFree(bb))
	{
        fprintf(stderr, "i2c_info: Cannot set start condition\n");
		bb->Failed = 1;
        return;
    }

    // start condition is when data linegoes low when clock is high
	I2CDataLow(bb);
    BitDelay((bb->clock_delay)/2);
	I2CClockLow(bb);
    BitDelay(bb->clock_delay);
}


// *****************************************************************************
// stop condition
// when the clock is high, sda goes from low to high
// *****************************************************************************
void I2CStop(struct i2c_info *bb)
{
	I2CDataLow(bb);

    BitDelay(bb->clock_delay);

    I2CClockHigh(bb); // clock will be low from read/write, put high

    BitDelay(bb->clock_delay);

	I2CDataHigh(bb);
}

// *****************************************************************************
// sends a byte to the bus, this is an 8 bit unit so could be address or data
// msb first
// returns 1 for NACK and 0 for ACK (0 is good)
// *****************************************************************************
int I2CSend(struct i2c_info *bb, uint8_t value)
{
    uint32_t rv;
    uint8_t j, mask=0x80;

    // clock is already low from start condition
    for(j=0;j<8;j++)
	{
        BitDelay(bb->clock_delay);
        if (value & mask)
		{
			I2CDataHigh(bb);
		}
		else
		{
			I2CDataLow(bb);
		}
        // clock out data
        I2CClockHigh(bb);  // clock it out
        BitDelay(bb->clock_delay);
        I2CClockLow(bb);      // back to low so data can change
        mask>>= 1;      // next bit along
    }
    // release bus for slave ack or nack
	I2CDataHigh(bb);
    BitDelay(bb->clock_delay);
    I2CClockHigh(bb);     // and clock high tels slave to NACK/ACK
    BitDelay(bb->clock_delay); // delay for slave to act
    rv = digitalRead(bb->sda);     // get ACK, NACK from slave
	
    I2CClockLow(bb);
	BitDelay(bb->clock_delay);
    return rv;
}

// *****************************************************************************
// receive 1 char from bus
// Input
// send: 1=nack, (last byte) 0 = ack (get another)
// *****************************************************************************
uint8_t I2CRead(struct i2c_info *bb, uint8_t ack)
{
    uint8_t j, data=0;

    for (j=0;j<8;j++)
	{
        data<<= 1;      // shift in
        BitDelay(bb->clock_delay);
        I2CClockHigh(bb);      // set clock high to get data
        BitDelay(bb->clock_delay); // delay for slave
		
		if (digitalRead(bb->sda)) data++;   // get data
		
		I2CClockLow(bb);
	}

	// clock has been left low at this point
	// send ack or nack
	BitDelay(bb->clock_delay);
   
	if (ack)
	{
		I2CDataHigh(bb);
	}
	else
	{
		I2CDataLow(bb);
	}
   
	BitDelay(bb->clock_delay);
	I2CClockHigh(bb);    // clock it in
	BitDelay(bb->clock_delay);
	I2CClockLow(bb);
	I2CDataHigh(bb);

	return data;
}

// *****************************************************************************
// writes buffer
// *****************************************************************************
void I2Cputs(struct i2c_info *bb, uint8_t *s, uint32_t len)
{
    I2CStart(bb);
    I2CSend(bb, bb->address * 2); // address
    while(len) {
        I2CSend(bb, *(s++));
        len--;
    }
    I2CStop(bb); // stop    
}
// *****************************************************************************
// read one byte
// *****************************************************************************
uint8_t I2CGetc(struct i2c_info *bb)
{
    uint8_t rv;
    I2CStart(bb);
    I2CSend(bb, (bb->address * 2)+1); // address
    rv = I2CRead(bb, 1);
    I2CStop(bb); // stop
    return rv;    
}

int GPSChecksumOK(unsigned char *Buffer, int Count)
{
  unsigned char XOR, i, c;

  XOR = 0;
  for (i = 1; i < (Count-4); i++)
  {
    c = Buffer[i];
    XOR ^= c;
  }

  return (Buffer[Count-4] == '*') && (Buffer[Count-3] == Hex(XOR >> 4)) && (Buffer[Count-2] == Hex(XOR & 15));
}


void SendUBX(struct i2c_info *bb, unsigned char *MSG, int len)
{
	I2Cputs(bb, MSG, len);
}

void SetFlightMode(struct i2c_info *bb)
{
    // Send navigation configuration command
    unsigned char setNav[] = {0xB5, 0x62, 0x06, 0x24, 0x24, 0x00, 0xFF, 0xFF, 0x06, 0x03, 0x00, 0x00, 0x00, 0x00, 0x10, 0x27, 0x00, 0x00, 0x05, 0x00, 0xFA, 0x00, 0xFA, 0x00, 0x64, 0x00, 0x2C, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x16, 0xDC};
    SendUBX(bb, setNav, sizeof(setNav));
	printf ("Setting flight mode\n");
}

float FixPosition(float Position)
{
	float Minutes, Seconds;
	
	Position = Position / 100;
	
	Minutes = trunc(Position);
	Seconds = fmod(Position, 1);

	return Minutes + Seconds * 5 / 3;
}

void ProcessLine(struct i2c_info *bb, struct TGPS *GPS, char *Buffer, int Count)
{
    float utc_time, latitude, longitude, hdop, altitude, speed, course;
	int lock, satellites, date;
	char active, ns, ew, units, speedstring[16], coursestring[16];
	long Hours, Minutes, Seconds;
	
    if (GPSChecksumOK(Buffer, Count))
	{
		satellites = 0;
	
		if (strncmp(Buffer+3, "GGA", 3) == 0)
		{
			if (sscanf(Buffer+7, "%f,%f,%c,%f,%c,%d,%d,%f,%f,%c", &utc_time, &latitude, &ns, &longitude, &ew, &lock, &satellites, &hdop, &altitude, &units) >= 1)
			{	
				// $GPGGA,124943.00,5157.01557,N,00232.66381,W,1,09,1.01,149.3,M,48.6,M,,*42
				if (satellites >= 4)
				{
					GPS->Time = utc_time;
					GPS->Latitude = FixPosition(latitude);
					if (ns == 'S') GPS->Latitude = -GPS->Latitude;
					GPS->Longitude = FixPosition(longitude);
					if (ew == 'W') GPS->Longitude = -GPS->Longitude;
					GPS->Altitude = altitude;
				}
				GPS->Satellites = satellites;
			}
			if (Config.EnableGPSLogging)
			{
				WriteLog("gps.txt", Buffer);
			}
		}
		else if (strncmp(Buffer+3, "RMC", 3) == 0)
		{
			speedstring[0] = '\0';
			coursestring[0] = '\0';
			if (sscanf(Buffer+7, "%f,%c,%f,%c,%f,%c,%[^','],%[^','],%d", &utc_time, &active, &latitude, &ns, &longitude, &ew, speedstring, coursestring, &date) >= 7)
			{
				// $GPRMC,124943.00,A,5157.01557,N,00232.66381,W,0.039,,200314,,,A*6C

				speed = atof(speedstring);
				course = atof(coursestring);
				
				GPS->Speed = (int)speed;
				GPS->Direction = (int)course;
			}

			if (Config.EnableGPSLogging)
			{
				WriteLog("gps.txt", Buffer);
			}
		}
		else if (strncmp(Buffer+3, "GSV", 3) == 0)
        {
            // Disable GSV
            printf("Disabling GSV\r\n");
            unsigned char setGSV[] = { 0xB5, 0x62, 0x06, 0x01, 0x08, 0x00, 0xF0, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x03, 0x39 };
            SendUBX(bb, setGSV, sizeof(setGSV));
        }
		else if (strncmp(Buffer+3, "GLL", 3) == 0)
        {
            // Disable GLL
            printf("Disabling GLL\r\n");
            unsigned char setGLL[] = { 0xB5, 0x62, 0x06, 0x01, 0x08, 0x00, 0xF0, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x2B };
            SendUBX(bb, setGLL, sizeof(setGLL));
        }
		else if (strncmp(Buffer+3, "GSA", 3) == 0)
        {
            // Disable GSA
            printf("Disabling GSA\r\n");
            unsigned char setGSA[] = { 0xB5, 0x62, 0x06, 0x01, 0x08, 0x00, 0xF0, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x02, 0x32 };
            SendUBX(bb, setGSA, sizeof(setGSA));
        }
		else if (strncmp(Buffer+3, "VTG", 3) == 0)
        {
            // Disable VTG
            printf("Disabling VTG\r\n");
            unsigned char setVTG[] = {0xB5, 0x62, 0x06, 0x01, 0x08, 0x00, 0xF0, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x05, 0x47};
            SendUBX(bb, setVTG, sizeof(setVTG));
        }
        else
        {
            printf("Unknown NMEA sentence: %s\n", Buffer);
        }
    }
    else
    {
       printf("Bad checksum\r\n");
	}
}


void *GPSLoop(void *some_void_ptr)
{
	unsigned char Line[100];
	int id, Length;
	struct i2c_info bb;
	struct TGPS *GPS;

	GPS = (struct TGPS *)some_void_ptr;
	
	Length = 0;

    while (1)
    {
        int i;
		unsigned char Character;

		printf ("SDA/SCL = %d/%d\n", Config.SDA, Config.SCL);
		
		if (OpenI2C(&bb, 0x42, Config.SDA, Config.SCL, 10, 100))		// struct, i2c address, SDA, SCL, us clock delay, timeout ms
		{
			printf("Failed to open I2C\n");
			exit(1);
		}
	
		SetFlightMode(&bb);

        while (!bb.Failed)
        {
            Character = I2CGetc(&bb);

			if (Character == 0xFF)
			{
				delayMilliseconds (100);
			}
            else if (Character == '$')
			{
				Line[0] = Character;
				Length = 1;
			}
            else if (Length > 90)
			{
				Length = 0;
            }
            else if ((Length > 0) && (Character != '\r'))
            {
               	Line[Length++] = Character;
               	if (Character == '\n')
               	{
               		Line[Length] = '\0';
					// puts(Line);
               		ProcessLine(&bb, GPS, Line, Length);
					delayMilliseconds (100);
               		Length = 0;
               	}
            }
		}
		
		ResetI2C(&bb);
	}
}


