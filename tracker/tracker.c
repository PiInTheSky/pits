/*------------------------------------------------------------------\
|                                                                   |
|                PI IN THE SKY TRACKER PROGRAM                      |
|                                                                   |
| This program is written for the Pi Telemetry Board                |
| produced by HAB Supplies Ltd.  No support is provided             |
| for use on other hardware. It does the following:                 |
|                                                                   |
| 1 - Sets up the hardware including putting the GPS in flight mode |
| 2 - Reads the current temperature                                 |
| 3 - Reads the current battery voltage                             |
| 4 - Reads the current GPS position                                |
| 5 - Builds a telemetry sentence to transmit                       |
| 6 - Sends it to the NTX2B radio transmitteR                       |
| 7 - repeats steps 2-6                                             |
|                                                                   |
| Usual disclaimer bullshit goes here                               |
|                                                                   |
\------------------------------------------------------------------*/

#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <ctype.h>
#include <stdio.h>   	// Standard input/output definitions
#include <string.h>  	// String function definitions
#include <unistd.h>  	// UNIX standard function definitions
#include <fcntl.h>   	// File control definitions
#include <errno.h>   	// Error number definitions
#include <termios.h> 	// POSIX terminal control definitions
#include <stdint.h>
#include <stdlib.h>
#include <dirent.h>
#include <math.h>
#include <pthread.h>
#include <wiringPi.h>
#include "gps.h"
#include "DS18B20.h"
#include "adc.h"
#include "misc.h"
#include "snapper.h"
#include "led.h"

struct TConfig Config;

// Pin allocations.  Do not change unless you're using your own hardware
#define NTX2B_ENABLE	0
#define UBLOX_ENABLE	2

FILE *ImageFP;
int Records, FileNumber;
struct termios options;
char *SSDVFolder="/home/pi/pits/tracker/download";
char *SavedImageFolder="/home/pi/pits/tracker/download/keep";
 
void BuildSentence(char *TxLine, int SentenceCounter, struct TGPS *GPS)
{
    int Count, i, j;
    unsigned char c;
    unsigned int CRC, xPolynomial;
	char TimeBuffer1[12], TimeBuffer2[10];
	
	sprintf(TimeBuffer1, "%06.0f", GPS->Time);
	TimeBuffer2[0] = TimeBuffer1[0];
	TimeBuffer2[1] = TimeBuffer1[1];
	TimeBuffer2[2] = ':';
	TimeBuffer2[3] = TimeBuffer1[2];
	TimeBuffer2[4] = TimeBuffer1[3];
	TimeBuffer2[5] = ':';
	TimeBuffer2[6] = TimeBuffer1[4];
	TimeBuffer2[7] = TimeBuffer1[5];
	TimeBuffer2[8] = '\0';
	
    sprintf(TxLine, "$$%s,%d,%s,%7.5lf,%7.5lf,%05.5u,%d,%d,%d,%3.1f,%3.1f",
            Config.PayloadID,
            SentenceCounter,
			TimeBuffer2,
            GPS->Latitude,
            GPS->Longitude,
            GPS->Altitude,
			(GPS->Speed * 13) / 7,
			GPS->Direction,
			GPS->Satellites,            
            GPS->Temperature,
            GPS->BatteryVoltage);

    Count = strlen(TxLine);

    CRC = 0xffff;           // Seed
    xPolynomial = 0x1021;
   
     for (i = 2; i < Count; i++)
     {   // For speed, repeat calculation instead of looping for each bit
        CRC ^= (((unsigned int)TxLine[i]) << 8);
        for (j=0; j<8; j++)
        {
            if (CRC & 0x8000)
                CRC = (CRC << 1) ^ 0x1021;
            else
                CRC <<= 1;
        }
     }

    TxLine[Count++] = '*';
    TxLine[Count++] = Hex((CRC >> 12) & 15);
    TxLine[Count++] = Hex((CRC >> 8) & 15);
    TxLine[Count++] = Hex((CRC >> 4) & 15);
    TxLine[Count++] = Hex(CRC & 15);
	TxLine[Count++] = '\n';  
	TxLine[Count++] = '\0';  

    printf("\n%s\n", TxLine);
}


void ReadString(FILE *fp, char *keyword, char *Result, int Length, int NeedValue)
{
	char line[100], *token, *value;
 
	fseek(fp, 0, SEEK_SET);
	*Result = '\0';

	while (fgets(line, sizeof(line), fp) != NULL)
	{
		token = strtok(line, "=");
		if (strcmp(keyword, token) == 0)
		{
			value = strtok(NULL, "\n");
			strcpy(Result, value);
			return;
		}
	}

	if (NeedValue)
	{
		printf("Missing value for '%s' in configuration file\n", keyword);
		exit(1);
	}
}

int ReadInteger(FILE *fp, char *keyword, int NeedValue)
{
	char Temp[32];

	ReadString(fp, keyword, Temp, sizeof(Temp), NeedValue);

	return atoi(Temp);
}

int ReadBoolean(FILE *fp, char *keyword, int NeedValue)
{
	char Temp[32];

	ReadString(fp, keyword, Temp, sizeof(Temp), NeedValue);

	return (*Temp == '1') || (*Temp == 'Y') || (*Temp == 'y');
}

speed_t BaudToSpeed(int baud)
{
	switch (baud)
	{
		case 50: return B50;
		case 75: return B75;
		case 150: return B150;
		case 200: return B200;
		case 300: return B300;
		case 600: return B600;
		case 1200: return B1200;
	}

	return 0;
}

void LoadConfigFile(struct TConfig *Config)
{
	FILE *fp;
	int BaudRate;
	char *filename = "/boot/pisky.txt";

	if ((fp = fopen(filename, "r")) == NULL)
	{
		printf("\nFailed to open config file %s (error %d - %s).\nPlease check that it exists and has read permission.\n", filename, errno, strerror(errno));
		exit(1);
	}

	ReadString(fp, "payload", Config->PayloadID, sizeof(Config->PayloadID), 1);
	printf ("Payload ID = '%s'\n", Config->PayloadID);

	Config->Frequency[0] = '\0';
	ReadString(fp, "frequency", Config->Frequency, sizeof(Config->Frequency), 0);
	if (*Config->Frequency)
	{
		printf("Frequency set to channel %s\n", Config->Frequency);
	}

	BaudRate = ReadInteger(fp, "baud", 1);
	Config->TxSpeed = BaudToSpeed(BaudRate);
	if (Config->TxSpeed == B0)
	{
		printf ("Unknown baud rate %d\nPlease edit in configuration file\n", BaudRate);
		exit(1);
	}
	printf ("Radio baud rate = %d\n", BaudRate);

	Config->Camera = ReadBoolean(fp, "camera", 0);
	printf ("Camera %s\n", Config->Camera ? "Enabled" : "Disabled");
	if (Config->Camera)
	{
		Config->high = ReadInteger(fp, "high", 0);
		printf ("Image size changes at %dm\n", Config->high);
	
		Config->low_width = ReadInteger(fp, "low_width", 0);
		Config->low_height = ReadInteger(fp, "low_height", 0);
		printf ("Low image size %d x %d pixels\n", Config->low_width, Config->low_height);
	
		Config->high_width = ReadInteger(fp, "high_width", 0);
		Config->high_height = ReadInteger(fp, "high_height", 0);
		printf ("High image size %d x %d pixels\n", Config->high_width, Config->high_height);

		Config->image_packets = ReadInteger(fp, "image_packets", 0);
		printf ("1 Telemetry packet every %d image packets\n", Config->image_packets);
	}
	
	fclose(fp);
}

void SetFrequency(char *Frequency)
{
        int fd;
	char Command[16];

        fd = open("/dev/ttyAMA0", O_RDWR | O_NOCTTY | O_NDELAY);
        if (fd >= 0)
        {
                tcgetattr(fd, &options);

                options.c_cflag &= ~CSTOPB;
                cfsetispeed(&options, B4800);
                cfsetospeed(&options, B4800);
                options.c_cflag |= CS8;
                options.c_oflag &= ~ONLCR;
                options.c_oflag &= ~OPOST;

                tcsetattr(fd, TCSANOW, &options);

		pinMode (NTX2B_ENABLE, INPUT);
		pullUpDnControl(NTX2B_ENABLE, PUD_OFF);

		sprintf(Command, "%cch%s\r", 0x80, Config.Frequency);
		write(fd, Command, strlen(Command)); 

		printf("NTX2B Now set to channel %s\n", Config.Frequency);

		close(fd);
        }
}

int OpenSerialPort(void)
{
	int fd;

	fd = open("/dev/ttyAMA0", O_RDWR | O_NOCTTY | O_NDELAY);
	if (fd >= 0)
	{
		/* get the current options */
		tcgetattr(fd, &options);

		/* set raw input */
		options.c_lflag &= ~ECHO;
		options.c_cc[VMIN]  = 0;
		options.c_cc[VTIME] = 10;

		options.c_cflag |= CSTOPB;
		cfsetispeed(&options, B300);
		cfsetospeed(&options, B300);
		options.c_cflag |= CS8;
		options.c_oflag &= ~ONLCR;
		options.c_oflag &= ~OPOST;
	
		tcsetattr(fd, TCSANOW, &options);
	}

	return fd;
}

void SendSentence(char *TxLine)
{
	int fd;

	
	if ((fd = OpenSerialPort()) >= 0)
	{
		printf("Sending ...\n");
		write(fd, TxLine, strlen(TxLine));
		
		if (close(fd) < 0)
		{
			printf("NOT Sent - error %d\n", errno);
		}
		else
		{
			printf("Sent\n");
		}
	}
	else
	{
		printf("Failed to open serial port\n");
	}
	
}

int FindAndConvertImage(void)
{
	size_t LargestFileSize;
	char LargestFileName[100], FileName[100], CommandLine[2000];
	DIR *dp;
	struct dirent *ep;
	struct stat st;
	int Done;
	
	LargestFileSize = 0;
	Done = 0;
	
	dp = opendir(SSDVFolder);
	if (dp != NULL)
	{
		while (ep = readdir (dp))
		{
			if (strstr(ep->d_name, ".jpg") != NULL)
			{
				sprintf(FileName, "%s/%s", SSDVFolder, ep->d_name);
				stat(FileName, &st);
				if (st.st_size > LargestFileSize)
				{
					LargestFileSize = st.st_size;
					strcpy(LargestFileName, FileName);
				}
			}
		}
		(void) closedir (dp);
	}

	if (LargestFileSize > 0)
	{
		printf("Found file %s to convert\n", LargestFileName);
		
		// Now convert the file
		FileNumber++;
		FileNumber = FileNumber & 255;
		sprintf(CommandLine, "ssdv -e -c %s -i %d %s /home/pi/pits/tracker/snap.bin", Config.PayloadID, FileNumber, LargestFileName);
		system(CommandLine);
		
		// And move those pesky image files
		sprintf(CommandLine, "mv %s/*.jpg %s", SSDVFolder, SavedImageFolder);
		system(CommandLine);

		Done = 1;
	}
	
	return (LargestFileSize > 0);
}

int SendImage()
{
    unsigned char Buffer[256];
    size_t Count;
    int SentSomething = 0;
	int fd;

    if (ImageFP == NULL)
    {
		if (FindAndConvertImage())
		{
			ImageFP = fopen("/home/pi/pits/tracker/snap.bin", "r");
		}
        Records = 0;
    }

    if (ImageFP != NULL)
    {
        Count = fread(Buffer, 1, 256, ImageFP);
        if (Count > 0)
        {
            printf("Record %d, %d bytes\r\n", ++Records, Count);

			if ((fd = OpenSerialPort()) >= 0)
			{
				write(fd, Buffer, Count);
				close(fd);
			}

            SentSomething = 1;
        }
        else
        {
            fclose(ImageFP);
            ImageFP = NULL;
        }
    }

    return SentSomething;
}


int main(void)
{
	int fd, ReturnCode, i;
	unsigned long Sentence_Counter = 0;
	char Sentence[100], Command[100];
	struct stat st = {0};
	struct TGPS GPS;
	pthread_t GPSThread, DS18B20Thread, ADCThread, CameraThread, LEDThread;

	printf("\n\nRASPBERRY PI-IN-THE-SKY FLIGHT COMPUTER\n");
	printf("=======================================\n\n");

	LoadConfigFile(&Config);

	GPS.Time = 0.0;
	GPS.Longitude = 0.0;
	GPS.Latitude = 0.0;
	GPS.Altitude = 0;
	GPS.Satellites = 0;
	GPS.Speed = 0.0;
	GPS.Direction = 0.0;

	// Set up I/O
	if (wiringPiSetup() == -1)
	{
		exit (1);
	}

	if (*Config.Frequency)
	{
		SetFrequency(Config.Frequency);
	}

	// We have 2 enable outputs
	pinMode (NTX2B_ENABLE, OUTPUT);
	pinMode (UBLOX_ENABLE, OUTPUT);
	
	// Switch on the GPS
	digitalWrite (UBLOX_ENABLE, 0);
	
	// Switch on the radio
	digitalWrite (NTX2B_ENABLE, 1);
	
	if ((fd = OpenSerialPort()) < 0)
	{
		printf("Cannot open serial port - check documentation!\n");
		exit(1);
	}
	close(fd);

	// Set up DS18B20
	system("sudo modprobe w1-gpio");
	system("sudo modprobe w1-therm");
	
	// SPI for ADC
	system("gpio load spi");

	/*
	if (stat(SSDVFolder, &st) == -1)
	{
		mkdir(SSDVFolder, 0700);
	}	

	if (stat(SavedImageFolder, &st) == -1)
	{
		mkdir(SavedImageFolder, 0700);
	}
	*/

	if (pthread_create(&GPSThread, NULL, GPSLoop, &GPS))
	{
		fprintf(stderr, "Error creating GPS thread\n");
		return 1;
	}
	
	if (pthread_create(&DS18B20Thread, NULL, DS18B20Loop, &GPS))
	{
		fprintf(stderr, "Error creating DS18B20s thread\n");
		return 1;
	}

	if (pthread_create(&ADCThread, NULL, ADCLoop, &GPS))
	{
		fprintf(stderr, "Error creating ADC thread\n");
		return 1;
	}

	if (Config.Camera)
	{
		if (pthread_create(&CameraThread, NULL, CameraLoop, &GPS))
		{
			fprintf(stderr, "Error creating camera thread\n");
			return 1;
		}
	}

	if (pthread_create(&LEDThread, NULL, LEDLoop, &GPS))
	{
		fprintf(stderr, "Error creating LED thread\n");
		return 1;
	}
	
	while (1)
	{	
		BuildSentence(Sentence, ++Sentence_Counter, &GPS);
		
		SendSentence(Sentence);
		
		for (i=0; i<Config.image_packets; i++)
		{
			SendImage();
		}
		
	}
}
