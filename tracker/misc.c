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

#define MAX_RECENT 10
struct TRecentPacket RecentPackets[4][MAX_RECENT];

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
	static int  boardRev = -1;

	if (boardRev < 0)
	{
		if ((cpuFd = fopen ("/proc/cpuinfo", "r")) != NULL)
		{
			while (fgets (line, 120, cpuFd) != NULL)
			{
				if (strncmp (line, "Hardware", 8) == 0)
				{
					printf ("RPi %s", line);
					if (strstr (line, "BCM2709") != NULL)
					{
						boardRev = 2;
					}
				}
				
				if (strncmp (line, "Revision", 8) == 0)
				{
					if (boardRev < 0)
					{
						printf ("RPi %s", line);
						if ((strstr(line, "0015") != NULL) ||
							(strstr(line, "0010") != NULL) ||
							(strstr(line, "0012") != NULL) ||
							(strstr(line, "0013") != NULL))
						{
							// B+ or A+
							boardRev = 1;
						}
						else if ((strstr(line, "900092") != NULL) ||
						         (strstr(line, "920092") != NULL) ||
						         (strstr(line, "900093") != NULL))
						{
							// Zero
							boardRev = 3;
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
    if (Config.Channels[Channel].ImageFP)
	{
		// At end of file ?
		if (Config.Channels[Channel].SSDVPacketNumber >= (Config.Channels[Channel].SSDVNumberOfPackets))
		{
			fclose(Config.Channels[Channel].ImageFP);
			Config.Channels[Channel].ImageFP = NULL;
		}
	}
		
    if (Config.Channels[Channel].ImageFP == NULL)
    {
		// Not currently sending a file.  Test to see if SSDV file has been marked as complete
		if (FileExists(Config.Channels[Channel].ssdv_done))
		{
			// New file should be ready now
			FILE *fp;
			
			printf("File %s found\n", Config.Channels[Channel].ssdv_done);
			
			// Zap the "done" file and the previous SSDV file
			remove(Config.Channels[Channel].ssdv_done);
			// remove(Config.Channels[Channel].current_ssdv);
			
			// Rename new file to replace that one
			// rename(Config.Channels[Channel].next_ssdv, Config.Channels[Channel].current_ssdv);
			
			if ((fp = fopen(Config.Channels[Channel].ssdv_filename, "rb")) != NULL)
			{
				char filename[100];
				int RecordCount, i;
				
				printf("File %s opened\n", Config.Channels[Channel].ssdv_filename);
				
				// That worked so let's get the file size so we can monitor progress
				fseek(fp, 0L, SEEK_END);
				RecordCount = ftell(fp) / 256;		// SSDV records are 256 bytes			
				fclose(fp);
								
				// Now fill in list of un-sent packets
				for (i=0; i<RecordCount; i++)
				{
					// Config.Channels[Channel].SSDVPackets[0].Packets[i] = ((i & 63)<10) || ((i & 63)>30);
					Config.Channels[Channel].SSDVPackets[0].Packets[i] = 1;
				}
				Config.Channels[Channel].SSDVPackets[0].NumberOfPackets = RecordCount;
				Config.Channels[Channel].SSDVPackets[0].ImageNumber = Config.Channels[Channel].SSDVFileNumber;
				Config.Channels[Channel].NumberOfPacketsInImage[Config.Channels[Channel].SSDVFileNumber] = RecordCount;

				// fseek(Config.Channels[Channel].ImageFP, 0L, SEEK_SET);				
				
				// Set record counter back to zero
				// Config.Channels[Channel].SSDVRecordNumber = 0;
				
				// And clear the flag so that the script can be recreated later
				// Config.Channels[Channel].NextSSDVFileReady = 0;
				sprintf(filename, "ssdv_done_%d", Channel);
				remove(filename);
			}
		}
	}
}

int FindNextUnsentImagePacket(int Channel, int *ImageNumber, int *PacketNumber, int *NumberOfPackets)
{
	int i, j, PacketType;
	
	// for (i=0; i<3; i++)
	for (i=2; i>=0; i--)
	{
		if (Config.Channels[Channel].SSDVPackets[i].ImageNumber >= 0)
		{
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
			
			// This line unused now
			Config.Channels[Channel].SSDVPackets[i].ImageNumber = -1;
		}
	}
	
	// printf("Channel %d no packets\n", Channel);
	*ImageNumber = -1;
	*PacketNumber = -1;
	*NumberOfPackets = 0;
	
	return 0;
}

int ChooseImagePacketToSend(int Channel)
{
	int Index, ImageNumber, PacketNumber, NumberOfPackets, ResentPacket;

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
			char FileName[100];
			
			sprintf(FileName, "ssdv_%d_%d.bin", Channel, ImageNumber);
			printf(">>>> Switching to SSDV file %s\n", FileName);
			Config.Channels[Channel].ImageFP = fopen(FileName, "rb");
		}

		// Note image and packet numbers for next call
		Config.Channels[Channel].SSDVImageNumber = ImageNumber;
		Config.Channels[Channel].SSDVPacketNumber = -1;
	}
		
	// Different packet to existing
	if (Config.Channels[Channel].ImageFP)
	{
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

	// First pass - look for this image number
	for (i=0; i<3; i++)
	{
		if (Config.Channels[Channel].SSDVPackets[i].ImageNumber == ImageNumber)
		{
			return i;
		}
	}
	
	// Second pass - look for unused line
	for (i=0; i<3; i++)
	{
		struct TSSDVPackets ZeroPacket = {0};
		
		if (Config.Channels[Channel].SSDVPackets[i].ImageNumber < 0)
		{
			Config.Channels[Channel].SSDVPackets[i] = ZeroPacket;
			
			Config.Channels[Channel].SSDVPackets[i].ImageNumber = ImageNumber;

			Config.Channels[Channel].SSDVPackets[i].NumberOfPackets = Config.Channels[Channel].NumberOfPacketsInImage[ImageNumber];
			
			return i;
		}
	}
	
	// Not found and already full
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

	if ((Index = FindImageInList(Channel, ImageNumber)) >= 0)
	{
		int i;
		
		for (i=HighestReceived+1; i<Config.Channels[Channel].NumberOfPacketsInImage[ImageNumber]; i++)
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

void ProcessSMSUplinkMessage(int LoRaChannel, unsigned char *Message)
{
	// Process uplink message (e.g. for Astro Pi scrolling LED)
	// Message is like "#001,Hello Dave!\n"
	// or "#001,First Message\nSecond Message\n"
	char FileName[32], *Token;
	int MessageNumber;
	FILE *fp;
	
	Token = strtok(Message+1, ",");
	MessageNumber = atoi(Token);
	
	sprintf(FileName, "Uplink_%d.sms", MessageNumber);
	
	Token = strtok(NULL, "\n");
	
	if ((fp = fopen(FileName, "wt")) != NULL)
	{
		fputs(Token, fp);
		fclose(fp);
		
		Config.LoRaDevices[LoRaChannel].MessageCount++;
		Config.LoRaDevices[LoRaChannel].LastMessageNumber = MessageNumber;
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
		
	while (*Message)
	{
		if (isdigit(*Message))
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

int devicetree(void)
{
  struct stat statBuf ;

  return stat ("/proc/device-tree", &statBuf) == 0;
}
