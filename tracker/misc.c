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
#include "misc.h"
#include <sys/types.h>
#include <dirent.h>
#include <string.h>
#include <stdarg.h>

char Hex(char Character)
{
	char HexTable[] = "0123456789ABCDEF";
	
	return HexTable[Character];
}

void WriteLog(char *FileName, char *Buffer)
{
	FILE *fp;
	
	if ((fp = fopen(FileName, "at")) != NULL)
	{
		fputs(Buffer, fp);
		fclose(fp);
	}
}

int GetBoardType(void)
{
	FILE *cpuFd ;
	char line [120] ;
	char *c ;
	static int  boardRev = -1 ;

	if (boardRev < 0)
	{
		if ((cpuFd = fopen ("/proc/cpuinfo", "r")) != NULL)
		{
			while (fgets (line, 120, cpuFd) != NULL)
			{
				/*
				if (strncmp (line, "Hardware", 8) == 0)
				{
					printf ("RPi %s", line);
					if (strstr (line, "BCM2709") != NULL)
					{
						boardRev = 2;
					}
				}
				*/
				
				if (strncmp (line, "Revision", 8) == 0)
				{
					if (boardRev < 0)
					{
						printf ("RPi %s", line);
						
						if ((strstr(line, "0002") != NULL) ||
							(strstr(line, "0003") != NULL) ||
							(strstr(line, "0004") != NULL) ||
							(strstr(line, "0005") != NULL) ||
							(strstr(line, "0006") != NULL) ||
							(strstr(line, "000d") != NULL) ||
							(strstr(line, "000e") != NULL) ||
							(strstr(line, "000f") != NULL))
						{
							printf("Model B\n");
							boardRev = 0;
						}
						else if ((strstr(line, "0007") != NULL) ||
							(strstr(line, "0008") != NULL) ||
							(strstr(line, "0009") != NULL))
						{
							printf("Model A\n");
							boardRev = 0;
						}
						else if (strstr(line, "0010") != NULL)
						{
							printf("Model B+\n");
							boardRev = 1;
						}
						else if (strstr(line, "0011") != NULL)
						{
							printf("Compute Module\n");
							boardRev = 1;
						}
						else if (strstr(line, "0012") != NULL)
						{
							printf("Model A+\n");
							boardRev = 1;
						}
						else if ((strstr(line, "a01041") != NULL) ||
							(strstr(line, "a21041") != NULL))
						{
							printf("Pi 2 Model B\n");
							boardRev = 1;
						}					
						else if (strstr(line, "900092") != NULL)
						{
							printf("Pi Zero\n");
							boardRev = 1;
						}
						else if (strstr(line, "a02082") != NULL)
						{
							printf("Pi 3 Model B\n");
							boardRev = 1;
						}
						else
						{
							printf("Unknown Pi Revision\n");
							boardRev = 1;
						}
					}
				}
			}

			fclose (cpuFd) ;
		}
	}
	
	return boardRev;
}

short open_i2c(int address)
{
	short fd;
	char i2c_dev[16];

	sprintf(i2c_dev, "/dev/i2c-%d", piBoardRev()-1);

	if ((fd = open(i2c_dev, O_RDWR)) < 0)
	{                                        // Open port for reading and writing
		printf("Failed to open i2c port\n");
		return 0;
	}

	if (ioctl(fd, I2C_SLAVE, address) < 0)                                 // Set the port options and set the address of the device we wish to speak to
	{
		printf("Unable to get bus access to talk to slave on address %02Xh\n", address);
		return 0;
	}

	return fd;
}

int FileExists(char *filename)
{
	struct stat st;

	return stat(filename, &st) == 0;
}

void StartNewFileIfNeeded(int Channel)
{
    if (Config.Channels[Channel].ImageFP == NULL)
    {
		// Not currently sending a file.  Test to see if SSDV file has been marked as complete
		if (FileExists(Config.Channels[Channel].ssdv_done))
		{
			// New file should be ready now
			
			// Zap the "done" file and the existing SSDV file
			remove(Config.Channels[Channel].ssdv_done);
			remove(Config.Channels[Channel].current_ssdv);
			
			// Rename new file to replace that one
			rename(Config.Channels[Channel].next_ssdv, Config.Channels[Channel].current_ssdv);
			
			if ((Config.Channels[Channel].ImageFP = fopen(Config.Channels[Channel].current_ssdv, "rb")) != NULL)
			{
				char filename[100];
				
				// That worked so let's get the file size so we can monitor progress
				fseek(Config.Channels[Channel].ImageFP, 0L, SEEK_END);
				// printf("%lu bytes\n", ftell(Config.Channels[Channel].ImageFP));
				Config.Channels[Channel].SSDVTotalRecords = ftell(Config.Channels[Channel].ImageFP) / 256;		// SSDV records are 256 bytes
				fseek(Config.Channels[Channel].ImageFP, 0L, SEEK_SET);				
				
				// Set record counter back to zero
				Config.Channels[Channel].SSDVRecordNumber = 0;
				
				// And clear the flag so that the script can be recreated later
				// Config.Channels[Channel].NextSSDVFileReady = 0;
				sprintf(filename, "ssdv_done_%d", Channel);
				remove(filename);
			}
		}
	}
}

void ReadString(FILE *fp, char *keyword, int Channel, char *Result, int Length, int NeedValue)
{
	char line[100], FullKeyWord[64], *token, *value;
 
	if (Channel >= 0)
	{
		sprintf(FullKeyWord, "%s_%d", keyword, Channel);
	}
	else
	{
		strcpy(FullKeyWord, keyword);
	}
 
	fseek(fp, 0, SEEK_SET);
	*Result = '\0';

	while (fgets(line, sizeof(line), fp) != NULL)
	{
		line[strcspn(line, "\r")] = '\0';			// Ignore any CR (in case someone has edited the file from Windows with notepad)
		
		token = strtok(line, "=");
		if (strcasecmp(FullKeyWord, token) == 0)
		{
			value = strtok(NULL, "\n");
			strncpy(Result, value, Length);
			if (Length) Result[Length-1] = '\0';
			return;
		}
	}

	if (NeedValue)
	{
		printf("Missing value for '%s' in configuration file\n", keyword);
		exit(1);
	}
}

double ReadFloat(FILE *fp, char *keyword, int Channel, int NeedValue, double DefaultValue)
{
	char Temp[64];
	
	ReadString(fp, keyword, Channel, Temp, sizeof(Temp), NeedValue);

	if (Temp[0])
	{
		return atof(Temp);
	}
	
	return DefaultValue;
}

int ReadInteger(FILE *fp, char *keyword, int Channel, int NeedValue, int DefaultValue)
{
	char Temp[64];
	
	ReadString(fp, keyword, Channel, Temp, sizeof(Temp), NeedValue);

	if (Temp[0])
	{
		return atoi(Temp);
	}
	
	return DefaultValue;
}

int ReadCameraType(FILE *fp, char *keyword)
{
	char Temp[64];
	
	ReadString(fp, keyword, -1, Temp, sizeof(Temp), 0);

	if ((*Temp == '1') || (*Temp == 'Y') || (*Temp == 'y') || (*Temp == 't') || (*Temp == 'T'))
	{
		return 1;		// CSI (raspistill) Camera
	}
	
	if ((*Temp == 'F') || (*Temp == 'f') || (*Temp == 'U') || (*Temp == 'u'))
	{
		return 2;		// USB (fswebcam) Camera
	}
	
	return 0;
}

int ReadBoolean(FILE *fp, char *keyword, int Channel, int NeedValue, int *Result)
{
	char Temp[32];

	ReadString(fp, keyword, Channel, Temp, sizeof(Temp), NeedValue);

	if (*Temp)
	{
		*Result = (*Temp == '1') || (*Temp == 'Y') || (*Temp == 'y') || (*Temp == 't') || (*Temp == 'T');
	}
	else
	{
		*Result = 0;
	}
	
	return *Temp;
}

int ReadBooleanFromString(FILE *fp, char *keyword, char *searchword)
{
	char Temp[100];

	ReadString(fp, keyword, -1, Temp, sizeof(Temp), 0);

	if (strcasestr(Temp, searchword)) return 1; else return 0;
}

void AppendCRC(char *Temp)
{
	int i, j, Count;
	unsigned int CRC, xPolynomial;
	
    Count = strlen(Temp);
	
	// Config->PredictionID	

    CRC = 0xffff;           // Seed
    xPolynomial = 0x1021;
   
     for (i = 2; i < Count; i++)
     {   // For speed, repeat calculation instead of looping for each bit
        CRC ^= (((unsigned int)Temp[i]) << 8);
        for (j=0; j<8; j++)
        {
            if (CRC & 0x8000)
                CRC = (CRC << 1) ^ 0x1021;
            else
                CRC <<= 1;
        }
     }

    Temp[Count++] = '*';
    Temp[Count++] = Hex((CRC >> 12) & 15);
    Temp[Count++] = Hex((CRC >> 8) & 15);
    Temp[Count++] = Hex((CRC >> 4) & 15);
    Temp[Count++] = Hex(CRC & 15);
	Temp[Count++] = '\n';  
	Temp[Count++] = '\0';
}

int prog_count(char* name)
{
    DIR* dir;
    struct dirent* ent;
    char buf[512];
    long  pid;
    char pname[100] = {0,};
    char state;
    FILE *fp=NULL; 
	int Count=0;

    if (!(dir = opendir("/proc")))
	{
        perror("can't open /proc");
        return 0;
    }

    while((ent = readdir(dir)) != NULL)
	{
        long lpid = atol(ent->d_name);
        if (lpid < 0)
            continue;
        snprintf(buf, sizeof(buf), "/proc/%ld/stat", lpid);
        fp = fopen(buf, "r");

        if (fp)
		{
            if ((fscanf(fp, "%ld (%[^)]) %c", &pid, pname, &state)) != 3 )
			{
                printf("fscanf failed \n");
                fclose(fp);
                closedir(dir);
                return 0;
            }
			
            if (!strcmp(pname, name))
			{
                Count++;
            }
            fclose(fp);
        }
    }

	closedir(dir);
	
	return Count;
}

void LogMessage(const char *format, ...)
{
	char Buffer[200];
	
    va_list args;
    va_start(args, format);

    vsprintf(Buffer, format, args);

    va_end(args);

	if (strlen(Buffer) > 79)
	{
		Buffer[77] = '.';
		Buffer[78] = '.';
		Buffer[79] = 0;
	}

	if (Buffer[strlen(Buffer)-1] == '\n')
	{
		Buffer[strlen(Buffer)-1] = '\0';
	}

	puts(Buffer);
}
