#define _GNU_SOURCE

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
#include <wiringPi.h>
#include <wiringPiSPI.h>
#include <pigpio.h> 
#include <sys/types.h>
#include <dirent.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include <inttypes.h>

#include "gps.h"
#include "misc.h"
#ifdef EXTRAS_PRESENT
#	include "ex_misc.h"
#endif	


#define MAX_RECENT 10
struct TRecentPacket RecentPackets[4][MAX_RECENT];

char Hex(unsigned char Character)
{
	char HexTable[] = "0123456789ABCDEF";
	
	return HexTable[Character & 15];
}

void WriteGPSLog(char *Buffer)
{	
	if (Config.EnableGPSLogging)
	{
		FILE *fp = NULL;
		
		if ((fp = fopen("gps.txt", "at")) != NULL)
		{
			fputs(Buffer, fp);
			fclose(fp);
		}
	}
}

void WriteTelemetryLog(char *Buffer)
{
	if (Config.EnableTelemetryLogging)
	{
		FILE *fp = NULL;
		
		if ((fp = fopen("telemetry.txt", "at")) != NULL)
		{
			fputs(Buffer, fp);
			fclose(fp);
		}
	}
}

void WritePredictionLog(char *Buffer)
{
	FILE *fp = NULL;
	
	if ((fp = fopen("prediction.txt", "at")) != NULL)
	{
		fputs(Buffer, fp);
		fclose(fp);
	}
}

int GetBoardType(int *i2cChannel)
{
	FILE *cpuFd ;
	char line [120] ;
	static int  boardRev = -1;

	*i2cChannel = 1;
	
	if (boardRev < 0)
	{
		if ((cpuFd = fopen ("/proc/cpuinfo", "r")) != NULL)
		{
			while (fgets (line, 120, cpuFd) != NULL)
			{
				line[strcspn(line, "\n")] = '\0';			// Remove LF
				
				if (strncmp (line, "Hardware", 8) == 0)
				{
					printf ("RPi %s\n", line);
					if (strcmp (line, "BCM2709") == 0)
					{
						boardRev = 2;
					}
				}
				
				if (strncmp (line, "Revision", 8) == 0)
				{
					if (boardRev < 0)
					{	
						char *ptr;
						
						ptr = strchr(line, ':') + 2;
						
						if (strncmp(ptr, "1000", 4) == 0)
						{
							// Pi has been overvoltaged so skip the marker
							ptr += 4;
						}
						
						printf ("RPi %s\n", line);
						
						if (strcmp(ptr, "9000c1") == 0)
						{
							// Zero W
							boardRev = 4;
						}
						else if ((strcmp(ptr, "900092") == 0) ||
						    (strcmp(ptr, "900093") == 0) ||
						    (strcmp(ptr, "920092") == 0) ||
							(strcmp(ptr, "920093") == 0))
						{
							// Zero
							boardRev = 3;
						}
						else if ((strcmp(ptr, "0002") == 0) ||
								 (strcmp(ptr, "0003") == 0) ||
								 (strcmp(ptr, "0004") == 0) ||
								 (strcmp(ptr, "0005") == 0) ||
								 (strcmp(ptr, "0006") == 0) ||
								 (strcmp(ptr, "0007") == 0) ||
								 (strcmp(ptr, "0008") == 0) ||
								 (strcmp(ptr, "0009") == 0) ||
								 (strcmp(ptr, "000d") == 0) ||
								 (strcmp(ptr, "000e") == 0) ||
								 (strcmp(ptr, "000f") == 0))
						{
							// A or B
							boardRev = 0;
							if ((strcmp(ptr, "0002") == 0) || (strcmp(ptr, "0003") == 0))
							{
								// Really really old model A/B that uses I2C channel 0 on GPIO pins
								*i2cChannel = 0;
							}
						}
						else if ((strcmp(ptr, "0015") == 0) ||
								 (strcmp(ptr, "0010") == 0) ||
								 (strcmp(ptr, "0012") == 0) ||
								 (strcmp(ptr, "0013") == 0))
						{
							// B+ or A+
							boardRev = 1;
						}
						else
						{
							// Default something new but not zero
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

	sprintf(i2c_dev, "/dev/i2c-%d", Config.i2cChannel);

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
	if (NoMoreSSDVPacketsToSend(Channel))
	{
		if (Config.Channels[Channel].ImageFP != NULL)
		{
			fclose(Config.Channels[Channel].ImageFP);
			Config.Channels[Channel].ImageFP = NULL;
			printf("No more SSDV packets for channel %d\n", Channel);
		}
	}
		
    if (Config.Channels[Channel].ImageFP == NULL)
    {
		// Not currently sending a file.  Test to see if SSDV file has been marked as complete
		if (FileExists(Config.Channels[Channel].ssdv_done))
		{
			// New file should be ready now
			FILE *fp;
			
			printf("SSDV File %s for channel %d found\n", Config.Channels[Channel].ssdv_done, Channel);
			
			// Zap the "done" file and the previous SSDV file
			remove(Config.Channels[Channel].ssdv_done);
			
			if ((fp = fopen(Config.Channels[Channel].ssdv_filename, "rb")) != NULL)
			{
				char filename[100];
				int RecordCount, i;
								
				// That worked so let's get the file size so we can monitor progress
				fseek(fp, 0L, SEEK_END);
				RecordCount = ftell(fp) / 256;		// SSDV records are 256 bytes			
				fclose(fp);
				
				printf("File %s has %d records\n", Config.Channels[Channel].ssdv_filename, RecordCount);
				
				if (RecordCount > MAX_SSDV_PACKETS)
				{
					printf("SSDV IMAGE IS TOO LARGE AND WILL BE TRUNCATED\n");
					RecordCount = MAX_SSDV_PACKETS;
				}
				
				// FIFO for SSDV buffers
				memcpy(&(Config.Channels[Channel].SSDVPackets[2]), &(Config.Channels[Channel].SSDVPackets[1]), sizeof(Config.Channels[Channel].SSDVPackets[2]));
				memcpy(&(Config.Channels[Channel].SSDVPackets[1]), &(Config.Channels[Channel].SSDVPackets[0]), sizeof(Config.Channels[Channel].SSDVPackets[1]));
				
				// Now fill in list of un-sent packets
				for (i=0; i<RecordCount; i++)
				{
					// Config.Channels[Channel].SSDVPackets[0].Packets[i] = ((i & 63)<10) || ((i & 63)>30);
					Config.Channels[Channel].SSDVPackets[0].Packets[i] = 1;
				}
				Config.Channels[Channel].SSDVPackets[0].NumberOfPackets = RecordCount;
				Config.Channels[Channel].SSDVPackets[0].ImageNumber = Config.Channels[Channel].SSDVFileNumber;
				Config.Channels[Channel].SSDVPackets[0].InUse = 1;
				
				// Clear the flag so that the script can be recreated later
				sprintf(filename, "ssdv_done_%d", Channel);
				remove(filename);
			}
		}
	}
}

int FindNextUnsentImagePacket(int Channel, int *ImageNumber, int *PacketNumber, int *NumberOfPackets)
{
	int i, j, PacketType;

	// Cycle through the 3 packet arrays, starting with the last one (for oldest image, so we get that sent first)
	for (i=2; i>=0; i--)
	{
		if ((Config.Channels[Channel].SSDVPackets[i].ImageNumber >= 0) && Config.Channels[Channel].SSDVPackets[0].InUse)
		{
			// This packet has  an image
			for (j=0; j<Config.Channels[Channel].SSDVPackets[i].NumberOfPackets; j++)
			{
				PacketType = Config.Channels[Channel].SSDVPackets[i].Packets[j];
				
				if (PacketType)
				{			
					*ImageNumber = Config.Channels[Channel].SSDVPackets[i].ImageNumber;
					*PacketNumber = j;
					*NumberOfPackets = Config.Channels[Channel].SSDVPackets[i].NumberOfPackets;
					
					Config.Channels[Channel].SSDVPackets[i].Packets[j] = 0;
					
					// printf("Channel %d Image %d Packet %d of %d\n", Channel, *ImageNumber, *PacketNumber + 1, *NumberOfPackets);

					return (PacketType == 2);
				}
			}
			
			// This image array unused now
			Config.Channels[Channel].SSDVPackets[i].InUse = 0;
		}
	}
	
	// printf("Channel %d no packets\n", Channel);
	*ImageNumber = -1;
	*PacketNumber = -1;
	*NumberOfPackets = 0;
	
	return 0;
}

int NoMoreSSDVPacketsToSend(int Channel)
{
	int i, j;

	for (i=0; i<3; i++)
	{
		if (Config.Channels[Channel].SSDVPackets[i].ImageNumber >= 0)
		{
			// This image is in use
			for (j=0; j<Config.Channels[Channel].SSDVPackets[i].NumberOfPackets; j++)
			{
				if (Config.Channels[Channel].SSDVPackets[i].Packets[j])
				{
					return 0;
				}
			}		
		}
	}
	
	return 1;
}

int ChooseImagePacketToSend(int Channel)
{
	int ImageNumber, PacketNumber, NumberOfPackets, ResentPacket;

	ResentPacket = FindNextUnsentImagePacket(Channel, &ImageNumber, &PacketNumber, &NumberOfPackets);

	// Different image to existing ?
	if (ImageNumber != Config.Channels[Channel].SSDVImageNumber)
	{
		// No longer using the same file, so close if necessary
		if (Config.Channels[Channel].ImageFP)
		{
			fclose(Config.Channels[Channel].ImageFP);
			Config.Channels[Channel].ImageFP = NULL;
		}
		
		// Open file, if we have a packet to send
		if (ImageNumber >= 0)
		{
			// >=0 means there is a packet to send (-1 means there isn't)
			char FileName[100];
			
			sprintf(FileName, "ssdv_%d_%d.bin", Channel, ImageNumber);
			printf(">>>> Switching to SSDV file %s\n", FileName);
			Config.Channels[Channel].ImageFP = fopen(FileName, "rb");
		}

		// Note image and packet numbers for next call
		Config.Channels[Channel].SSDVImageNumber = ImageNumber;
		Config.Channels[Channel].SSDVPacketNumber = -1;
	}
		
	if (Config.Channels[Channel].ImageFP)
	{
		// So there was a packet, in existing or new file
		if (PacketNumber != (Config.Channels[Channel].SSDVPacketNumber+1))
		{
			// Not the next packet after the last one, in the same file
			// So we need to seek first
			// fseek(Config.Channels[Channel].ImageFP, (unsigned long)PacketNumber * 256L, SEEK_SET);
		}
		fseek(Config.Channels[Channel].ImageFP, (unsigned long)PacketNumber * 256L, SEEK_SET);
		
		// Remember packet number for next time
		Config.Channels[Channel].SSDVPacketNumber = PacketNumber;
	}
	
	Config.Channels[Channel].SSDVNumberOfPackets = NumberOfPackets;
	
	return ResentPacket;
}

int FindImageInList(int Channel, int ImageNumber)
{
	int i;

	for (i=0; i<3; i++)
	{
		if (Config.Channels[Channel].SSDVPackets[i].ImageNumber == ImageNumber)
		{
			return i;
		}
	}
	
	// Not found
	printf("FindImageInList - NOT FOUND\n");
	
	return -1;
}

void AddImagePacketToRecentList(int Channel, int ImageNumber, int PacketNumber)
{
	int i;
	
	// shift them along
	for (i=0; i<(MAX_RECENT-1); i++)
	{
		RecentPackets[Channel][i] = RecentPackets[Channel][i+1];
	}
	
	RecentPackets[Channel][MAX_RECENT-1].ImageNumber = ImageNumber;
	RecentPackets[Channel][MAX_RECENT-1].PacketNumber = PacketNumber;
	
	// printf("Added channel %d image %d packet %d to list\n", Channel, ImageNumber, PacketNumber);
}

int ImagePacketRecentlySent(int Channel, int ImageNumber, int PacketNumber)
{
	int i;
	
	for (i=0; i<MAX_RECENT; i++)
	{
		if ((RecentPackets[Channel][i].ImageNumber == ImageNumber) && (RecentPackets[Channel][i].PacketNumber == PacketNumber))
		{
			return 1;
		}
	}

	return 0;
}

void MarkMissingPacketsBeyond(int Channel, int ImageNumber, int HighestReceived)
{
	int Index;
	
	printf("MarkMissingPacketsBeyond(%d, %d, %d)\n", Channel, ImageNumber, HighestReceived);

	// Find image in our list of 3 recent images
	if ((Index = FindImageInList(Channel, ImageNumber)) >= 0)
	{
		int i;
		
		// Cycle through packets from highest received on ground(+1), to last packet we read from the SSDV file earlier
		for (i=HighestReceived+1; i<Config.Channels[Channel].SSDVPackets[Index].NumberOfPackets; i++)
		{
			if (!Config.Channels[Channel].SSDVPackets[Index].Packets[i])
			{
				// We've already sent the packet
				if (!ImagePacketRecentlySent(Channel, ImageNumber, i))
				{
					// But not recently (recent ones may be queued so don't resend just yet)
					// printf("Marking image %d index %d packet %d channel %d\n", ImageNumber, Index, i, Channel);
					Config.Channels[Channel].SSDVPackets[Index].Packets[i] = 2;
					Config.Channels[Channel].SSDVPackets[Index].InUse = 1;
				}
			}
		}
	}
}

void MarkMissingPackets(int Channel, int ImageNumber, int FirstMissingPacket, int LastMissingPacket)
{
	int Index, i;
	
	printf("MarkMissingPackets(%d, %d, %d, %d)\n", Channel, ImageNumber, FirstMissingPacket, LastMissingPacket);
	
	if ((Index = FindImageInList(Channel, ImageNumber)) >= 0)
	{
		printf("Index = %d\n", Index);
		for (i=FirstMissingPacket; i<=LastMissingPacket; i++)
		{
			if (!Config.Channels[Channel].SSDVPackets[Index].Packets[i])
			{
				if (!ImagePacketRecentlySent(Channel, ImageNumber, i))
				{
					// printf("Marking image %d index %d packet %d channel %d\n", ImageNumber, Index, i, Channel);
					Config.Channels[Channel].SSDVPackets[Index].Packets[i] = 2;
				}
			}
		}
	}
}

void ProcessSSDVUplinkMessage(int Channel, unsigned char *Message)
{
	int Value, Image, RangeStart;
	char Temp[8], *ptr;
	
	// !1:256=10-30,74-94,104,113,116,119,138-161,180,182,192,199,201-222,2:69=10-30

	printf("Uplink: %s\n", Message);
	
	ptr = Temp;
	Message++;		// Skip ! at start
	RangeStart = -1;
	Temp[0] = '\0';
	Image = 0;
		
	while (*Message)
	{
		if (isdigit((char)(*Message)))
		{
			*ptr++ = *Message;
			*ptr = '\0';
		}
		else
		{
			if (Temp[0])
			{
				Value = atoi(Temp);
				Temp[0] = '\0';
				ptr = Temp;
				
				if (*Message == ':')
				{
					// Image number
					Image = Value;
				}
				else if (*Message == '=')
				{
					// Highest received packet number
					MarkMissingPacketsBeyond(Channel, Image, Value);
				}
				else if (*Message == '-')
				{
					// Start of range of missing packets
					RangeStart = Value;
				}
				else
				{
					// Missing packet number
					if (RangeStart >= 0)
					{
						MarkMissingPackets(Channel, Image, RangeStart, Value);
						RangeStart = -1;
					}
					else
					{
						MarkMissingPackets(Channel, Image, Value, Value);
					}
				}
			}
		}
		Message++;
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
		
		if (*line)
		{
			token = strtok(line, "=");
			if (strcasecmp(FullKeyWord, token) == 0)
			{
				value = strtok(NULL, "\n");
				if (Length)
				{
					if (value != NULL)
					{
						strncpy(Result, value, Length);
					}
					Result[Length-1] = '\0';
				}

				return;
			}
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

int32_t ReadInteger(FILE *fp, char *keyword, int Channel, int NeedValue, int DefaultValue)
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
	
	if ((*Temp == 'G') || (*Temp == 'g'))
	{
		return 3;		// GPHOTO2
	}
	
	if ((*Temp == 'P') || (*Temp == 'p'))
	{
		return 4;		// Python script e.g. for GoPro
	}
	
	return 0;
}

char ReadCharacter(FILE *fp, char *keyword)
{
	char Temp[8];
	
	ReadString(fp, keyword, -1, Temp, sizeof(Temp), 0);
	
	return *Temp;
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
	unsigned int CRC;
	
    Count = strlen(Temp);
	
    CRC = 0xffff;           // Seed
   
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
	#define MAX_LEN 300
	char Buffer[MAX_LEN];
	
    va_list args;
    va_start(args, format);

    vsnprintf(Buffer, MAX_LEN, format, args);

    va_end(args);

	if (Buffer[strlen(Buffer)-1] == '\n')
	{
		Buffer[strlen(Buffer)-1] = '\0';
	}

	puts(Buffer);
}

int devicetree(void)
{
  struct stat statBuf ;

  return stat ("/proc/device-tree", &statBuf) == 0;
}

int BuildSentence(unsigned char *TxLine, int Channel, struct TGPS *GPS)
{	
	static char *Channels[6] = {"RTTY", "APRS", "LORA0", "LORA1", "FULL", "PIPE"};
	static char ExternalFields[100];
	static FILE *ExternalFile=NULL;
	static int FirstTime=1;
	int LoRaChannel;
	int ShowFields;
	char TimeBuffer[12], ExtraFields1[20], ExtraFields2[20], ExtraFields3[20], ExtraFields4[64], ExtraFields5[32], ExtraFields6[32], ExtraFields7[32], *ExtraFields8, Sentence[512];
	
	if (FirstTime)
	{
		FirstTime = 0;
		ExternalFields[0] = '\0';
	}
	
	Config.Channels[Channel].SentenceCounter++;
	ShowFields = Config.Channels[Channel].SentenceCounter == 1;
	
	sprintf(TimeBuffer, "%02d:%02d:%02d", GPS->Hours, GPS->Minutes, GPS->Seconds);
	
	ExtraFields1[0] = '\0';
	ExtraFields2[0] = '\0';
	ExtraFields3[0] = '\0';
	ExtraFields4[0] = '\0';
	ExtraFields5[0] = '\0';
	ExtraFields6[0] = '\0';
	ExtraFields7[0] = '\0';
	
	ExtraFields8 = "";
	
	if (ShowFields) printf("%s: ID,Ctr,Time,Lat,Lon,Alt,Speed,Head,Sats,Int.Temp", Channels[Channel]);
	
	
	// Battery voltage and current, if available
	if ((Config.BoardType == 3) || (Config.BoardType == 4) || (Config.DisableADC))
	{
			// Pi Zero - no ADC on the PITS Zero, or manually disabled ADC
	}
	else if (Config.BoardType == 0)
	{
		// Pi A or B.  Only Battery Voltage on the PITS
		
		sprintf(ExtraFields1, ",%.3f", GPS->BatteryVoltage);
		if (ShowFields) printf(",Volts");
	}
	else
	{
		// Pi A+ or B+ (V1 or V2 or V3).  Full ADC for voltage and current

		sprintf(ExtraFields1, ",%.1f,%.3f", GPS->BatteryVoltage, GPS->BoardCurrent);
		if (ShowFields) printf(",Volts,Current");
	}
	
	// BMP Pressure/Temperature/Humidity, if available
	if (Config.EnableBME280)
	{
		sprintf(ExtraFields2, ",%.1f,%.0f,%0.1f", GPS->BMP180Temperature, GPS->Pressure, GPS->Humidity);
		if (ShowFields) printf(",BME.Temp,Pressure,Humidity");
	}
	else if (Config.EnableMS5611)
	{
		sprintf(ExtraFields2, ",%.1f,%.1f", GPS->BMP180Temperature, GPS->Pressure);
		if (ShowFields) printf(",BMP.Temp,Pressure");
	}
	else if (Config.EnableBMP085)
	{
		sprintf(ExtraFields2, ",%.1f,%.0f", GPS->BMP180Temperature, GPS->Pressure);
		if (ShowFields) printf(",BMP.Temp,Pressure");
	}
	
	// Second DS18B20 Temperature Sensor, if available
	if (GPS->DS18B20Count > 1)
	{
		sprintf(ExtraFields3, ",%3.1f", GPS->DS18B20Temperature[Config.ExternalDS18B20]);
		if (ShowFields) printf(",Ext.Temp");
	}
	
	// Landing Prediction, if enabled
	if (Config.EnableLandingPrediction && (Config.PredictionID[0] == '\0'))
	{	
		// sprintf(ExtraFields4, ",%7.5lf,%7.5lf", GPS->PredictedLatitude, GPS->PredictedLongitude);
		sprintf(ExtraFields4, ",%.2lf,%7.5lf,%7.5lf,%3.1lf,%d", GPS->CDA,
																GPS->PredictedLatitude,
																GPS->PredictedLongitude,
																GPS->PredictedLandingSpeed,
																GPS->TimeTillLanding);
		if (ShowFields) printf(",CDA,Pred.Lat,Pred.Lon,Pred.Land,Pred.TTL");
	}
	
	// Specific to LoRa Uplink
	LoRaChannel = Channel - LORA_CHANNEL;
	if ((LoRaChannel == 0) || (LoRaChannel == 1))
	{
		// Fields for LoRa uplink
		if (Config.LoRaDevices[LoRaChannel].EnableRSSIStatus)
		{	
			sprintf(ExtraFields5, ",%d,%d,%d", Config.LoRaDevices[LoRaChannel].LastPacketRSSI,
											   Config.LoRaDevices[LoRaChannel].LastPacketSNR,
											   Config.LoRaDevices[LoRaChannel].PacketCount);
			if (ShowFields) printf(",RSSI,SNR,Packets");
		}

		if (Config.LoRaDevices[LoRaChannel].EnableMessageStatus)
		{	
			sprintf(ExtraFields6, ",%c", Config.LoRaDevices[LoRaChannel].LastCommand[0]);
			if (ShowFields) printf(",Command");
		}
		
	}
	
	// External CSV file
	if (Config.ExternalDataFileName[0])
	{
		if (ExternalFile == NULL)
		{
			{
				// Try to open external file
				ExternalFile = fopen(Config.ExternalDataFileName, "rt");
			}
		}
		else
		{
			// Check if file has been deleted
			if (access(Config.ExternalDataFileName, F_OK ) == -1 )
			{
				// It's been deleted
				ExternalFile = NULL;
			}
		}
		
		if (ExternalFile)
		{
			char line[100];
			
			line[0] = '\0';
			
			// Keep reading lines till we get to the end
			while (fgets(line, sizeof(line), ExternalFile) != NULL)
			{
			}
			
			if (line[0])
			{
				line[strcspn(line, "\n")] = '\0';
				sprintf(ExternalFields, ",%.90s", line);
			}
			fseek(ExternalFile, 0, SEEK_END);
			// clearerr(ExternalFile);
			if (ShowFields) printf(",[ExternalCSV]");
		}
	}	
	
	
	if (Config.EnableCutdown)
	{
		sprintf(ExtraFields7, ",%d", GPS->CutdownStatus);
		if (ShowFields) printf(",CutdownStatus");
	}
	
	// Bouy mode or normal mode ?
	if ((Config.BuoyModeAltitude > 0) && (GPS->Altitude < Config.BuoyModeAltitude))
	{
		sprintf(Sentence, "$$%s,%d,%s,%7.5lf,%7.5lf",
				Config.Channels[Channel].PayloadID,
				Config.Channels[Channel].SentenceCounter,
				TimeBuffer,
				GPS->Latitude,
				GPS->Longitude);
	}
	else
	{
		#ifdef EXTRAS_PRESENT
			ExtraFields8 = misc_get_sentence_fields(GPS, ShowFields);
		#endif
			
		if (ShowFields) printf("\n");
		
		snprintf(Sentence, 512, "$$%.15s,%d,%.9s,%7.5lf,%7.5lf,%5.5" PRId32  ",%d,%d,%d,%3.1f%.12s%.20s%.20s%.40s%.90s%.20s%.10s%.10s%.40s",
				Config.Channels[Channel].PayloadID,
				Config.Channels[Channel].SentenceCounter,
				TimeBuffer,
				GPS->Latitude,
				GPS->Longitude,
				GPS->Altitude,
				(GPS->Speed * 13) / 7,
				GPS->Direction,
				GPS->Satellites,
				GPS->DS18B20Temperature[(GPS->DS18B20Count > 1) ? (1-Config.ExternalDS18B20) : 0],
				ExtraFields1,
				ExtraFields2,
				ExtraFields3,
				ExtraFields4,
				ExternalFields,
				ExtraFields5,
				ExtraFields6,
				ExtraFields7,
				ExtraFields8);
	}
	
	AppendCRC(Sentence);
	
	// Separate sentence for landing prediction ?
	if (Config.PredictionID[0])
	{
		char PredictionPayload[64];
		
		sprintf(PredictionPayload,
				"$$%s,%d,%s,%7.5lf,%7.5lf,%u",
				Config.PredictionID,
				Config.Channels[Channel].SentenceCounter,
				TimeBuffer,
				GPS->PredictedLatitude,
				GPS->PredictedLongitude,
				0);
		AppendCRC((char *)PredictionPayload);
		
		strcpy((char *)TxLine, PredictionPayload);
		strcat((char *)TxLine, Sentence);
	}
	else
	{
		strcpy((char *)TxLine, Sentence);
	}

	return strlen((char *)TxLine) + 1;
}

int FixDirection180(int Angle)
{
	if (Angle < -180)
	{
		return Angle + 180;
	}
	if (Angle > 180)
	{
		return Angle - 180;
	}
	
	return Angle;
}

void SetupPWMFrequency(int Pin, int Frequency)
{
//	return softPwmCreate(Pin, 0, 100);
	gpioSetPWMfrequency(Pin, Frequency);
	// gpioServo(18, 2000);
}

void ControlPWMOutput(int Pin, int Period)
{
//	softPwmWrite (Pin, Level);
	gpioServo(Pin, Period);
}

void DecryptMessage(char *Code, char *Message)
{
	int i, Len;
	
	Len = strlen(Code);
	
	if (Len > 0)
	{
		printf("Decoding ...\n");
		i = 0;
		while (*Message)
		{
			*Message = (*Message ^ Code[i]) & 0x7F;
			Message++;
			i = (i + 1) % Len;
		}
	}
}

char GetChar(char **Message)
{
	return *((*Message)++);
}

void GetString(char *Field, char **Message)
{
	while (**Message && (**Message != '/'))
	{
		*Field++ = *((*Message)++);
	}
	
	*Field = 0;
	if (**Message)
	{
		(*Message)++;
	}
}

int32_t GetInteger(char **Message)
{
	char Temp[32];
	
	GetString(Temp, Message);
	
	return atoi(Temp);
}

double GetFloat(char **Message)
{
	char Temp[32];
	
	GetString(Temp, Message);
	
	return atof(Temp);
}
