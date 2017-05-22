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
#include <wiringPiSPI.h>

#include "gps.h"
#include "DS18B20.h"
#include "adc.h"
#include "misc.h"
#include "snapper.h"
#include "led.h"
#include "bmp085.h"
#include "lora.h"
#ifdef EXTRAS_PRESENT
#	include "ex_lora.h"
#endif	

// RFM98
uint8_t currentMode = 0x81;

#pragma pack(1)

struct TBinaryPacket
{
	uint8_t 	PayloadIDs;
	uint16_t	Counter;
	uint16_t	Seconds;
	float		Latitude;
	float		Longitude;
	uint16_t	Altitude;
};

struct TLoRaMode 
{
	int	ImplicitOrExplicit;
	int ErrorCoding;
	int Bandwidth;
	int SpreadingFactor;
	int LowDataRateOptimize;
	int BaudRate;
	char *Description;
} LoRaModes[] = 
{
	{EXPLICIT_MODE, ERROR_CODING_4_8, BANDWIDTH_20K8, SPREADING_11, 8,    60, "Telemetry"},			// 0: Normal mode for telemetry
	{IMPLICIT_MODE, ERROR_CODING_4_5, BANDWIDTH_20K8, SPREADING_6,  0,  1400, "SSDV"},				// 1: Normal mode for SSDV
	{EXPLICIT_MODE, ERROR_CODING_4_8, BANDWIDTH_62K5, SPREADING_8,  0,  2000, "Repeater"},			// 2: Normal mode for repeater network	
	{EXPLICIT_MODE, ERROR_CODING_4_6, BANDWIDTH_250K, SPREADING_7,  0,  8000, "Turbo"},				// 3: Normal mode for high speed images in 868MHz band
	{IMPLICIT_MODE, ERROR_CODING_4_5, BANDWIDTH_250K, SPREADING_6,  0, 16828, "TurboX"},			// Fastest mode within IR2030 in 868MHz band
	{EXPLICIT_MODE, ERROR_CODING_4_8, BANDWIDTH_41K7, SPREADING_11, 0,   200, "Calling"},			// Calling mode
	{IMPLICIT_MODE, ERROR_CODING_4_5, BANDWIDTH_41K7, SPREADING_6,  0,  2800, "Uplink"},				// Uplink mode for 868
	{EXPLICIT_MODE, ERROR_CODING_4_5, BANDWIDTH_20K8, SPREADING_7,  0,  2800, "Telnet"}				// 7: Telnet-style comms with HAB on 434
};

int Records, FileNumber;

void writeRegister(int Channel, uint8_t reg, uint8_t val)
{
	unsigned char data[2];
	
	data[0] = reg | 0x80;
	data[1] = val;
	wiringPiSPIDataRW(Channel, data, 2);
}

uint8_t readRegister(int Channel, uint8_t reg)
{
	unsigned char data[2];
	uint8_t val;
	
	data[0] = reg & 0x7F;
	data[1] = 0;
	wiringPiSPIDataRW(Channel, data, 2);
	val = data[1];

    return val;
}

void setMode(int LoRaChannel, uint8_t newMode)
{
  if(newMode == currentMode)
    return;  
  
  switch (newMode) 
  {
    case RF98_MODE_TX:
      writeRegister(LoRaChannel, REG_LNA, LNA_OFF_GAIN);  // TURN LNA OFF FOR TRANSMITT
      writeRegister(LoRaChannel, REG_PA_CONFIG, Config.LoRaDevices[LoRaChannel].Power);
      writeRegister(LoRaChannel, REG_OPMODE, newMode);
      currentMode = newMode; 
      break;
    case RF98_MODE_RX_CONTINUOUS:
      writeRegister(LoRaChannel, REG_PA_CONFIG, PA_OFF_BOOST);  // TURN PA OFF FOR RECIEVE??
      writeRegister(LoRaChannel, REG_LNA, LNA_MAX_GAIN);  // LNA_MAX_GAIN);  // MAX GAIN FOR RECIEVE
      writeRegister(LoRaChannel, REG_OPMODE, newMode);
      currentMode = newMode; 
      break;
    case RF98_MODE_SLEEP:
      writeRegister(LoRaChannel, REG_OPMODE, newMode);
      currentMode = newMode; 
      break;
    case RF98_MODE_STANDBY:
      writeRegister(LoRaChannel, REG_OPMODE, newMode);
      currentMode = newMode; 
      break;
    default: return;
  } 
  
	if(newMode != RF98_MODE_SLEEP)
	{
		// printf("Waiting for Mode Change\n");
		while(digitalRead(Config.LoRaDevices[LoRaChannel].DIO5) == 0)
		{
		} 
		// printf("Mode change completed\n");
	}
	
	return;
}
 
void SetLoRaFrequency(int LoRaChannel, double Frequency)
{
	unsigned long FrequencyValue;

	setMode(LoRaChannel, RF98_MODE_STANDBY);
	setMode(LoRaChannel, RF98_MODE_SLEEP);
	writeRegister(LoRaChannel, REG_OPMODE, 0x80);

	setMode(LoRaChannel, RF98_MODE_SLEEP);

	FrequencyValue = (unsigned long)(Frequency * 7110656 / 434);
	
	printf("Channel %d frequency %lf FrequencyValue = %06lXh\n", LoRaChannel, Frequency, FrequencyValue);
	
	writeRegister(LoRaChannel, 0x06, (FrequencyValue >> 16) & 0xFF);		// Set frequency
	writeRegister(LoRaChannel, 0x07, (FrequencyValue >> 8) & 0xFF);
	writeRegister(LoRaChannel, 0x08, FrequencyValue & 0xFF);
}

void setLoRaMode(int LoRaChannel)
{
	double Frequency;

	if (sscanf(Config.LoRaDevices[LoRaChannel].Frequency, "%lf", &Frequency))
	{
		SetLoRaFrequency(LoRaChannel, Frequency);
	}
}

void SetLoRaParameters(int LoRaChannel, int ImplicitOrExplicit, int ErrorCoding, int Bandwidth, int SpreadingFactor, int LowDataRateOptimize)
{
	writeRegister(LoRaChannel, REG_MODEM_CONFIG, ImplicitOrExplicit | ErrorCoding | Bandwidth);
	writeRegister(LoRaChannel, REG_MODEM_CONFIG2, SpreadingFactor | CRC_ON);
	writeRegister(LoRaChannel, REG_MODEM_CONFIG3, 0x04 | LowDataRateOptimize);									// 0x04: AGC sets LNA gain
	writeRegister(LoRaChannel, REG_DETECT_OPT, (readRegister(LoRaChannel, REG_DETECT_OPT) & 0xF8) | ((SpreadingFactor == SPREADING_6) ? 0x05 : 0x03));	// 0x05 For SF6; 0x03 otherwise
	writeRegister(LoRaChannel, REG_DETECTION_THRESHOLD, (SpreadingFactor == SPREADING_6) ? 0x0C : 0x0A);		// 0x0C for SF6, 0x0A otherwise
	
	Config.LoRaDevices[LoRaChannel].PayloadLength = ImplicitOrExplicit == IMPLICIT_MODE ? 255 : 0;
	
	writeRegister(LoRaChannel, REG_PAYLOAD_LENGTH, Config.LoRaDevices[LoRaChannel].PayloadLength);
	writeRegister(LoRaChannel, REG_RX_NB_BYTES, Config.LoRaDevices[LoRaChannel].PayloadLength);
}

void setupRFM98(int LoRaChannel)
{
	if (Config.LoRaDevices[LoRaChannel].InUse)
	{
		// initialize the pins
		printf("LoRa channel %d DIO0=%d DIO5=%d\n", LoRaChannel, Config.LoRaDevices[LoRaChannel].DIO0, Config.LoRaDevices[LoRaChannel].DIO5);
		pinMode(Config.LoRaDevices[LoRaChannel].DIO0, INPUT);
		pinMode(Config.LoRaDevices[LoRaChannel].DIO5, INPUT);

		if (wiringPiSPISetup(LoRaChannel, 500000) < 0)
		{
			fprintf(stderr, "Failed to open SPI port.  Make sure it is enabled in rasp-config");
			exit(1);
		}
		
		setLoRaMode(LoRaChannel);

		SetLoRaParameters(LoRaChannel,
						  Config.LoRaDevices[LoRaChannel].ImplicitOrExplicit,
						  Config.LoRaDevices[LoRaChannel].ErrorCoding,
						  Config.LoRaDevices[LoRaChannel].Bandwidth,
						  Config.LoRaDevices[LoRaChannel]. SpreadingFactor,
						  Config.LoRaDevices[LoRaChannel].LowDataRateOptimize);
		
		writeRegister(LoRaChannel, REG_FIFO_ADDR_PTR, 0);

		writeRegister(LoRaChannel, REG_DIO_MAPPING_2,0x00);		
	}
}


void SendLoRaData(int LoRaChannel, unsigned char *buffer, int Length)
{
	unsigned char data[257];
	int i;
	
	// printf("LoRa Channel %d Sending %d bytes\n", LoRaChannel, Length);

	Config.LoRaDevices[LoRaChannel].MillisSinceLastPacket = 0;

	setMode(LoRaChannel, RF98_MODE_STANDBY);
	
	writeRegister(LoRaChannel, REG_DIO_MAPPING_1, 0x40);		// 01 00 00 00 maps DIO0 to TxDone

	writeRegister(LoRaChannel, REG_FIFO_TX_BASE_AD, 0x00);  // Update the address ptr to the current tx base address
	writeRegister(LoRaChannel, REG_FIFO_ADDR_PTR, 0x00); 
  
	data[0] = REG_FIFO | 0x80;
	for (i=0; i<Length; i++)
	{
		data[i+1] = buffer[i];
	}
	wiringPiSPIDataRW(LoRaChannel, data, Length+1);

// printf("Set Tx Mode\n");

	// Set the length. For implicit mode, since the length needs to match what the receiver expects, we have to set a value which is 255 for an SSDV packet
	writeRegister(LoRaChannel, REG_PAYLOAD_LENGTH, Config.LoRaDevices[LoRaChannel].PayloadLength ? Config.LoRaDevices[LoRaChannel].PayloadLength : Length);

	// go into transmit mode
	setMode(LoRaChannel, RF98_MODE_TX);
	
	Config.LoRaDevices[LoRaChannel].LoRaMode = lmSending;
}

int BuildLoRaCall(unsigned char *TxLine, int LoRaChannel)
{
    sprintf((char *)TxLine, "^^%s,%s,%d,%d,%d,%d,%d",
            Config.Channels[LORA_CHANNEL+LoRaChannel].PayloadID,
			Config.LoRaDevices[LoRaChannel].Frequency,
			Config.LoRaDevices[LoRaChannel].ImplicitOrExplicit,
			Config.LoRaDevices[LoRaChannel].ErrorCoding,
			Config.LoRaDevices[LoRaChannel].Bandwidth,
			Config.LoRaDevices[LoRaChannel].SpreadingFactor,
			Config.LoRaDevices[LoRaChannel].LowDataRateOptimize);
			
	AppendCRC((char *)TxLine);
		
	return strlen((char *)TxLine) + 1;
}



int BuildLoRaPositionPacket(unsigned char *TxLine, int LoRaChannel, struct TGPS *GPS)
{
	int OurID;
	struct TBinaryPacket BinaryPacket;
	
	OurID = Config.LoRaDevices[LoRaChannel].Slot;
	
	Config.Channels[LORA_CHANNEL+LoRaChannel].SentenceCounter++;

	BinaryPacket.PayloadIDs = 0xC0 | (OurID << 3) | OurID;
	BinaryPacket.Counter = Config.Channels[LORA_CHANNEL+LoRaChannel].SentenceCounter;
	BinaryPacket.Seconds = GPS->SecondsInDay >> 1;
	BinaryPacket.Latitude = GPS->Latitude;
	BinaryPacket.Longitude = GPS->Longitude;
	BinaryPacket.Altitude = GPS->Altitude;

	memcpy(TxLine, &BinaryPacket, sizeof(BinaryPacket));
	
	return sizeof(struct TBinaryPacket);
}

int SendLoRaImage(int LoRaChannel)
{
    unsigned char Buffer[256];
    size_t Count;
    int SentSomething = 0;
	int ResentPacket, Channel;
	
	Channel = LORA_CHANNEL + LoRaChannel;

	StartNewFileIfNeeded(Channel);
	
	ResentPacket = ChooseImagePacketToSend(Channel);
	
    if (Config.Channels[Channel].ImageFP != NULL)
    {
        Count = fread(Buffer, 1, 256, Config.Channels[Channel].ImageFP);
        if (Count > 0)
        {
            // printf("Record %d, %d bytes\r\n", ++Records, Count);

			AddImagePacketToRecentList(Channel, Config.Channels[Channel].SSDVImageNumber, Config.Channels[Channel].SSDVPacketNumber);
			
			printf("LORA%d: SSDV image %d packet %d of %d %s\r\n", LoRaChannel, Config.Channels[Channel].SSDVImageNumber, Config.Channels[Channel].SSDVPacketNumber+1, Config.Channels[Channel].SSDVNumberOfPackets, ResentPacket ? "** RESEND **" : "");
			
			SendLoRaData(LoRaChannel, Buffer+1, 255);
		
			SentSomething = 1;
        }
        else
        {
            fclose(Config.Channels[Channel].ImageFP);
            Config.Channels[Channel].ImageFP = NULL;
        }
    }

    return SentSomething;
}

int TDMTimeToSendOnThisChannel(int LoRaChannel, struct TGPS *GPS)
{
	// Can't send till we have the time!
	if (GPS->Satellites > 0)
	{
		// Can't Tx twice at the same time
		if (GPS->SecondsInDay != Config.LoRaDevices[LoRaChannel].LastTxAt)
		{
			long CycleSeconds;
		
			CycleSeconds = GPS->SecondsInDay % Config.LoRaDevices[LoRaChannel].CycleTime;

			if (CycleSeconds == Config.LoRaDevices[LoRaChannel].Slot)
			{
				Config.LoRaDevices[LoRaChannel].LastTxAt = GPS->SecondsInDay;
				Config.LoRaDevices[LoRaChannel].SendRepeatedPacket = 0;
				return 1;
			}

			if (Config.LoRaDevices[LoRaChannel].PacketRepeatLength && (CycleSeconds == Config.LoRaDevices[LoRaChannel].RepeatSlot))
			{
				Config.LoRaDevices[LoRaChannel].LastTxAt = GPS->SecondsInDay;
				Config.LoRaDevices[LoRaChannel].SendRepeatedPacket = 1;
				return 1;
			}
			
			if (Config.LoRaDevices[LoRaChannel].UplinkRepeatLength && (CycleSeconds == Config.LoRaDevices[LoRaChannel].UplinkSlot))
			{
				Config.LoRaDevices[LoRaChannel].LastTxAt = GPS->SecondsInDay;
				Config.LoRaDevices[LoRaChannel].SendRepeatedPacket = 2;
				return 1;
			}
			
		}
	}
	
	return 0;
}
	
int UplinkTimeToSendOnThisChannel(int LoRaChannel, struct TGPS *GPS)
{
	// Can't use time till we have it
	if (GPS->Satellites > 0)
	{
		long CycleSeconds;
		
		CycleSeconds = GPS->SecondsInDay % Config.LoRaDevices[LoRaChannel].UplinkCycle;
	
		if (CycleSeconds < Config.LoRaDevices[LoRaChannel].UplinkPeriod)
		{
			return 0;
		}
	}
	
	return 1;
}
	
int TimeToSendOnThisChannel(int LoRaChannel, struct TGPS *GPS)
{
	if (Config.LoRaDevices[LoRaChannel].ListenOnly)
	{
		// Listen until spoken to, with timeout
		return 0;
	}

	if (Config.LoRaDevices[LoRaChannel].CycleTime > 0)
	{
		// TDM
		return TDMTimeToSendOnThisChannel(LoRaChannel, GPS);
	}
	
	if ((Config.LoRaDevices[LoRaChannel].UplinkPeriod > 0) && (Config.LoRaDevices[LoRaChannel].UplinkCycle > 0))
	{
		// Continuous with uplink
		return UplinkTimeToSendOnThisChannel(LoRaChannel, GPS);
	}
	
	// Continuous
	return 1;
}

void startReceiving(int LoRaChannel)
{
	if (Config.LoRaDevices[LoRaChannel].InUse)
	{
		printf ("Listening on LoRa channel %d\n", LoRaChannel);
		
		writeRegister(LoRaChannel, REG_DIO_MAPPING_1, 0x00);		// 00 00 00 00 maps DIO0 to RxDone
	
		writeRegister(LoRaChannel, REG_FIFO_RX_BASE_AD, 0);
		writeRegister(LoRaChannel, REG_FIFO_ADDR_PTR, 0);
	  
		// Setup Receive Continuous Mode
		setMode(LoRaChannel, RF98_MODE_RX_CONTINUOUS); 
		
		Config.LoRaDevices[LoRaChannel].LoRaMode = lmListening;
	}
}

double BandwidthInKHz(Channel)
{
	if (Config.LoRaDevices[Channel].Bandwidth == BANDWIDTH_7K8) return 7.8;
	if (Config.LoRaDevices[Channel].Bandwidth == BANDWIDTH_10K4) return 10.4;
	if (Config.LoRaDevices[Channel].Bandwidth == BANDWIDTH_15K6) return 15.6;
	if (Config.LoRaDevices[Channel].Bandwidth == BANDWIDTH_20K8) return 20.8;
	if (Config.LoRaDevices[Channel].Bandwidth == BANDWIDTH_31K25) return 31.25;
	if (Config.LoRaDevices[Channel].Bandwidth == BANDWIDTH_41K7) return 41.7;
	if (Config.LoRaDevices[Channel].Bandwidth == BANDWIDTH_62K5) return 62.5;
	if (Config.LoRaDevices[Channel].Bandwidth == BANDWIDTH_125K) return 125;
	if (Config.LoRaDevices[Channel].Bandwidth == BANDWIDTH_250K) return 250;
	if (Config.LoRaDevices[Channel].Bandwidth == BANDWIDTH_500K) return 500;
	
	return 20.8;
};

double FrequencyError(int Channel)
{
	int32_t Temp;
	
	Temp = (int32_t)readRegister(Channel, REG_FREQ_ERROR) & 7;
	Temp <<= 8L;
	Temp += (int32_t)readRegister(Channel, REG_FREQ_ERROR+1);
	Temp <<= 8L;
	Temp += (int32_t)readRegister(Channel, REG_FREQ_ERROR+2);
	
	if (readRegister(Channel, REG_FREQ_ERROR) & 8)
	{
		Temp = Temp - 524288;
	}

	// return - ((double)Temp * (1<<24) / 32000000.0) * (125000 / 500000.0);
	return - ((double)Temp * (1<<24) / 32000000.0) * (BandwidthInKHz(Channel) / 500.0);
}	

int receiveMessage(int LoRaChannel, unsigned char *message)
{
	int i, Bytes, currentAddr, x;
	unsigned char data[257];

	printf ("Rx LoRa channel %d\n", LoRaChannel);
	
	Bytes = 0;
	
	x = readRegister(LoRaChannel, REG_IRQ_FLAGS);
  
	// clear the rxDone flag
	writeRegister(LoRaChannel, REG_IRQ_FLAGS, 0x40); 
   
	// check for payload crc issues (0x20 is the bit we are looking for
	if((x & 0x20) == 0x20)
	{
		// CRC Error
		writeRegister(LoRaChannel, REG_IRQ_FLAGS, 0x20);		// reset the crc flags
		Config.LoRaDevices[LoRaChannel].BadCRCCount++;
		printf ("CRC Error\n");
	}
	else
	{
		currentAddr = readRegister(LoRaChannel, REG_FIFO_RX_CURRENT_ADDR);
		Bytes = readRegister(LoRaChannel, REG_RX_NB_BYTES);
		printf ("*** Received %d bytes\n", Bytes);

		// ChannelPrintf(Channel,  9, 1, "Packet   SNR = %4d   ", (char)(readRegister(Channel, REG_PACKET_SNR)) / 4);
		// ChannelPrintf(Channel, 10, 1, "Packet  RSSI = %4d   ", readRegister(Channel, REG_PACKET_RSSI) - 157);
		printf("LORA%d: Freq. Error = %4.1lfkHz ", LoRaChannel, FrequencyError(LoRaChannel) / 1000);

		writeRegister(LoRaChannel, REG_FIFO_ADDR_PTR, currentAddr);   
		
		data[0] = REG_FIFO;
		wiringPiSPIDataRW(LoRaChannel, data, Bytes+1);
		for (i=0; i<=Bytes; i++)
		{
			message[i] = data[i+1];
		}
		
		message[Bytes] = '\0';
	} 

	// Clear all flags
	writeRegister(LoRaChannel, REG_IRQ_FLAGS, 0xFF); 
  
	return Bytes;
}

void CheckForPacketOnListeningChannels(void)
{
	int LoRaChannel;
	
	for (LoRaChannel=0; LoRaChannel<=1; LoRaChannel++)
	{
		if (Config.LoRaDevices[LoRaChannel].InUse)
		{
			if (Config.LoRaDevices[LoRaChannel].LoRaMode == lmListening)
			{
				if (digitalRead(Config.LoRaDevices[LoRaChannel].DIO0))
				{
					unsigned char Message[256];
					int Bytes;
					
					Bytes = receiveMessage(LoRaChannel, Message);
					
					if (Bytes > 0)
					{
						int8_t SNR;
						int RSSI;

						SNR = readRegister(LoRaChannel, REG_PACKET_SNR);
						SNR /= 4;
						RSSI = readRegister(LoRaChannel, REG_PACKET_RSSI) - 157;
						if (SNR < 0)
						{
							RSSI += SNR;
						}
						
						Config.LoRaDevices[LoRaChannel].LastPacketSNR = SNR;
						Config.LoRaDevices[LoRaChannel].LastPacketRSSI = RSSI;
						Config.LoRaDevices[LoRaChannel].PacketCount++;
						if (Message[0] == '$')
						{
							// Normal ASCII telemetry message sent by another balloon
							char Payload[32];

							printf("Balloon message\n");
							if (sscanf((char *)(Message+2), "%32[^,]", Payload) == 1)
							{
								if (strcmp(Payload, Config.Channels[LORA_CHANNEL+LoRaChannel].PayloadID) != 0)
								{
									// printf ("%s\n", Message);
							
									strcpy((char *)Config.LoRaDevices[LoRaChannel].PacketToRepeat, (char *)Message);
									Config.LoRaDevices[LoRaChannel].PacketRepeatLength = strlen((char *)Message);
							
									Config.LoRaDevices[LoRaChannel].AirCount++;

									Message[strlen((char *)Message)] = '\0';
								}
							}
						}
						else if ((Message[0] & 0xC0) == 0xC0)
						{
							// Binary telemetry message sent by another balloon
							int SourceID, OurID;
							
							OurID = Config.LoRaDevices[LoRaChannel].Slot;
							SourceID = Message[0] & 0x07;
							
							if (SourceID == OurID)
							{
								printf("Balloon Binary Message - ignored\n");
							}
							else
							{
								printf("Balloon Binary Message from sender %d\n", SourceID);
								
								// Replace the sender ID with ours
								Message[0] = (Message[0] & 0xC7) | (OurID << 3);
								Config.LoRaDevices[LoRaChannel].PacketRepeatLength = sizeof(struct TBinaryPacket);
								memcpy(Config.LoRaDevices[LoRaChannel].PacketToRepeat, Message, Config.LoRaDevices[LoRaChannel].PacketRepeatLength);
							
								Config.LoRaDevices[LoRaChannel].AirCount++;
							}
						}
						else if ((Message[0] & 0xC0) == 0x80)
						{
							// Uplink in TDM mode
							int SenderID, TargetID, OurID;
							
							TargetID = Message[0] & 0x07;
							SenderID = (Message[0] >> 3) & 0x07;
							OurID = Config.LoRaDevices[LoRaChannel].Slot;

							printf("Uplink from %d to %d Message %s\n",
									SenderID,
									TargetID,
									Message+1);
									
							if (TargetID == OurID)
							{
								printf("Message was for us!\n");
								strcpy(Config.LoRaDevices[LoRaChannel].LastCommand, (char *)(Message+1));
								printf("Message is '%s'\n", Config.LoRaDevices[LoRaChannel].LastCommand);
								Config.LoRaDevices[LoRaChannel].GroundCount++;
							}
							else
							{
								printf("Message was for another balloon\n");
								Message[0] = (Message[0] & 0xC7) | (OurID << 3);
								Config.LoRaDevices[LoRaChannel].UplinkRepeatLength = sizeof(struct TBinaryPacket);
								memcpy(Config.LoRaDevices[LoRaChannel].UplinkPacket, Message, Config.LoRaDevices[LoRaChannel].UplinkRepeatLength);
							}
						}
						else if (Message[0] == '!')
						{
							// SSDV Uplink message
							printf("SSDV uplink message %s", Message);
							ProcessSSDVUplinkMessage(LORA_CHANNEL+LoRaChannel, Message);
						}
						else if (Message[0] == '#')
						{
							// SMS Uplink message
							printf("SMS uplink message %s", Message);
							ProcessSMSUplinkMessage(LoRaChannel, Message);
						}
#						ifdef EXTRAS_PRESENT
							else if (ProcessExtraMessage(LoRaChannel, Message, Bytes, GPS))
							{
								// Handled
							}
#						endif	
						else
						{
							printf("Unknown message %02Xh\n", Message[0]);
						}
					}
				}
			}
		}
	}
}

int CheckForFreeChannel(struct TGPS *GPS)
{
	int LoRaChannel;
	
	for (LoRaChannel=0; LoRaChannel<=1; LoRaChannel++)
	{
		if (Config.LoRaDevices[LoRaChannel].InUse)
		{
			if ((Config.LoRaDevices[LoRaChannel].LoRaMode != lmSending) || digitalRead(Config.LoRaDevices[LoRaChannel].DIO0))
			{
				// printf ("LoRa Channel %d is free\n", Channel);
				// Either not sending, or was but now it's sent.  Clear the flag if we need to
				if (Config.LoRaDevices[LoRaChannel].LoRaMode == lmSending)
				{
					// Clear that IRQ flag
					writeRegister(LoRaChannel, REG_IRQ_FLAGS, 0x08); 
					Config.LoRaDevices[LoRaChannel].LoRaMode = lmIdle;
				}
				
				// Mow we test to see if we can send now
				// If Tx is continuous, then the answer is yes, of course
				// If there's an uplink period defined, we need to be outside that
				// For TDM, we need to be inside one of our slots
				
				if (TimeToSendOnThisChannel(LoRaChannel, GPS))
				{
					// Either sending continuously, or it's our slot to send in
					// printf("Channel %d is free\n", Channel);
					
					return LoRaChannel;
				}
				else if ((Config.LoRaDevices[LoRaChannel].CycleTime > 0) || (Config.LoRaDevices[LoRaChannel].UplinkCycle > 0) || Config.LoRaDevices[LoRaChannel].ListenOnly)
				{
					// TDM system and not time to send, so we can listen
					if (Config.LoRaDevices[LoRaChannel].LoRaMode == lmIdle)
					{
						printf("Uplink period ...\n");
						
						if (Config.LoRaDevices[LoRaChannel].UplinkFrequency > 0)
						{
							printf("Setting frequency to %.3lfMHz for uplink\n", Config.LoRaDevices[LoRaChannel].UplinkFrequency);
							SetLoRaFrequency(LoRaChannel, Config.LoRaDevices[LoRaChannel].UplinkFrequency);
						}
						
						
						if (Config.LoRaDevices[LoRaChannel].UplinkMode >= 0)
						{
							int UplinkMode;
							
							UplinkMode = Config.LoRaDevices[LoRaChannel].UplinkMode;

							printf("Set Uplink Mode to %d\n", UplinkMode);
							
							SetLoRaParameters(LoRaChannel,
											  LoRaModes[UplinkMode].ImplicitOrExplicit,
											  LoRaModes[UplinkMode].ErrorCoding,
											  LoRaModes[UplinkMode].Bandwidth,
											  LoRaModes[UplinkMode].SpreadingFactor,
											  0);
							Config.LoRaDevices[LoRaChannel].ReturnStateAfterCall = 1;
						}
						
						startReceiving(LoRaChannel);
					}
				}
			}
		}
	}
	
	return -1;
}

void LoadLoRaConfig(FILE *fp, struct TConfig *Config)
{
	int LoRaChannel;
	
	if (Config->BoardType)
	{
		// For dual card.  These are for the production cards and second prototype (earlier one will need override - see below)

		Config->LoRaDevices[0].DIO0 = 6;
		Config->LoRaDevices[0].DIO5 = 5;
		
		Config->LoRaDevices[1].DIO0 = 27;		// Earlier prototypes = 31
		Config->LoRaDevices[1].DIO5 = 26;
	}
	else
	{
		// Only used for handmade test boards
		Config->LoRaDevices[0].DIO0 = 6;
		Config->LoRaDevices[0].DIO5 = 5;
		
		Config->LoRaDevices[1].DIO0 = 3;
		Config->LoRaDevices[1].DIO5 = 4;
	}

	Config->LoRaDevices[0].InUse = 0;
	Config->LoRaDevices[1].InUse = 0;
	
	Config->LoRaDevices[0].LoRaMode = lmIdle;
	Config->LoRaDevices[1].LoRaMode = lmIdle;


	for (LoRaChannel=0; LoRaChannel<=1; LoRaChannel++)
	{
		int Temp, Channel;
		char TempString[64];
		
		Channel = LoRaChannel + LORA_CHANNEL;
		
		strcpy(Config->LoRaDevices[LoRaChannel].LastCommand, "None");
		
		Config->LoRaDevices[LoRaChannel].Frequency[0] = '\0';
		ReadString(fp, "LORA_Frequency", LoRaChannel, Config->LoRaDevices[LoRaChannel].Frequency, sizeof(Config->LoRaDevices[LoRaChannel].Frequency), 0);
		
		if (Config->LoRaDevices[LoRaChannel].Frequency[0])
		{
			printf("LORA%d frequency set to %s\n", LoRaChannel, Config->LoRaDevices[LoRaChannel].Frequency);
			Config->LoRaDevices[LoRaChannel].InUse = 1;
			Config->Channels[Channel].Enabled = 1;

			ReadString(fp, "LORA_Payload", LoRaChannel, Config->Channels[Channel].PayloadID, sizeof(Config->Channels[Channel].PayloadID), 1);
			printf ("LORA%d Payload ID = '%s'\n", LoRaChannel, Config->Channels[Channel].PayloadID);
			
			Config->LoRaDevices[LoRaChannel].SpeedMode = ReadInteger(fp, "LORA_Mode", LoRaChannel, 0, 0);
			if ((Config->LoRaDevices[LoRaChannel].SpeedMode < 0) || (Config->LoRaDevices[LoRaChannel].SpeedMode >= sizeof(LoRaModes)/sizeof(LoRaModes[0]))) Config->LoRaDevices[LoRaChannel].SpeedMode = 0;
			printf("LORA%d %s mode\n", LoRaChannel, LoRaModes[Config->LoRaDevices[LoRaChannel].SpeedMode].Description);

			// DIO0 / DIO5 overrides
			Config->LoRaDevices[LoRaChannel].DIO0 = ReadInteger(fp, "LORA_DIO0", LoRaChannel, 0, Config->LoRaDevices[LoRaChannel].DIO0);
			Config->LoRaDevices[LoRaChannel].DIO5 = ReadInteger(fp, "LORA_DIO5", LoRaChannel, 0, Config->LoRaDevices[LoRaChannel].DIO5);
			printf("LORA%d DIO0=%d DIO5=%d\n", LoRaChannel, Config->LoRaDevices[LoRaChannel].DIO0, Config->LoRaDevices[LoRaChannel].DIO5);
			
			if (Config->Camera)
			{
				Config->Channels[Channel].ImageWidthWhenLow = ReadInteger(fp, "LORA_low_width", LoRaChannel, 0, 320);
				Config->Channels[Channel].ImageHeightWhenLow = ReadInteger(fp, "LORA_low_height", LoRaChannel, 0, 240);
				printf ("LORA%d Low image size %d x %d pixels\n", LoRaChannel, Config->Channels[Channel].ImageWidthWhenLow, Config->Channels[Channel].ImageHeightWhenLow);
				
				Config->Channels[Channel].ImageWidthWhenHigh = ReadInteger(fp, "LORA_high_width", LoRaChannel, 0, 640);
				Config->Channels[Channel].ImageHeightWhenHigh = ReadInteger(fp, "LORA_high_height", LoRaChannel, 0, 480);
				printf ("LORA%d High image size %d x %d pixels\n", LoRaChannel, Config->Channels[Channel].ImageWidthWhenHigh, Config->Channels[Channel].ImageHeightWhenHigh);

				Config->Channels[Channel].ImagePackets = ReadInteger(fp, "LORA_image_packets", LoRaChannel, 0, 4);
				printf ("LORA%d: 1 Telemetry packet every %d image packets\n", LoRaChannel, Config->Channels[Channel].ImagePackets);
				
				Config->Channels[Channel].ImagePeriod = ReadInteger(fp, "LORA_image_period", LoRaChannel, 0, 60);
				printf ("LORA%d: %d seconds between photographs\n", LoRaChannel, Config->Channels[Channel].ImagePeriod);
			}
			else
			{
				Config->Channels[Channel].ImagePackets = 0;
			}			

			Config->LoRaDevices[LoRaChannel].CycleTime = ReadInteger(fp, "LORA_Cycle", LoRaChannel, 0, 0);			
			if (Config->LoRaDevices[LoRaChannel].CycleTime > 0)
			{
				printf("LORA%d cycle time %d\n", LoRaChannel, Config->LoRaDevices[LoRaChannel].CycleTime);

				Config->LoRaDevices[LoRaChannel].Slot = ReadInteger(fp, "LORA_Slot", LoRaChannel, 0, 0);
				printf("LORA%d Slot %d\n", LoRaChannel, Config->LoRaDevices[LoRaChannel].Slot);

				Config->LoRaDevices[LoRaChannel].RepeatSlot = ReadInteger(fp, "LORA_Repeat", LoRaChannel, 0, 0);			
				printf("LORA%d Repeat Slot %d\n", LoRaChannel, Config->LoRaDevices[LoRaChannel].RepeatSlot);

				Config->LoRaDevices[LoRaChannel].UplinkSlot = ReadInteger(fp, "LORA_Uplink", LoRaChannel, 0, 0);			
				printf("LORA%d Uplink Slot %d\n", LoRaChannel, Config->LoRaDevices[LoRaChannel].UplinkSlot);
			}
			else
			{
				Config->LoRaDevices[LoRaChannel].PacketEveryMilliSeconds = ReadInteger(fp, "LORA_PacketEvery", LoRaChannel, 0, 0);
				if (Config->LoRaDevices[LoRaChannel].PacketEveryMilliSeconds > 0)
				{
					printf ("LORA%d: Packet sent every %d milliseconds\n", LoRaChannel, Config->LoRaDevices[LoRaChannel].PacketEveryMilliSeconds);
					Config->LoRaDevices[LoRaChannel].MillisSinceLastPacket = Config->LoRaDevices[LoRaChannel].PacketEveryMilliSeconds;
				}
			}

			ReadBoolean(fp, "LORA_Binary", LoRaChannel, 0, &(Config->LoRaDevices[LoRaChannel].Binary));			
			printf("LORA%d Set To %s\n", LoRaChannel, Config->LoRaDevices[LoRaChannel].Binary ? "Binary" : "ASCII");
			
			ReadBoolean(fp, "LORA_ListenOnly", LoRaChannel, 0, &(Config->LoRaDevices[LoRaChannel].ListenOnly));
			if (Config->LoRaDevices[LoRaChannel].ListenOnly)
			{
				printf("LORA%d Set To ListenOnly Mode\n", LoRaChannel);
			}
			
			Config->LoRaDevices[LoRaChannel].UplinkPeriod = ReadInteger(fp, "LORA_Uplink_Period", LoRaChannel, 0, 0);			
			Config->LoRaDevices[LoRaChannel].UplinkCycle = ReadInteger(fp, "LORA_Uplink_Cycle", LoRaChannel, 0, 0);	
			Config->LoRaDevices[LoRaChannel].UplinkMode = ReadInteger(fp, "LORA_Uplink_Mode", LoRaChannel, 0, -1);
			Config->LoRaDevices[LoRaChannel].UplinkFrequency = ReadFloat(fp, "LORA_Uplink_Frequency", LoRaChannel, 0, 0);	

			ReadBoolean(fp, "LORA_Message_Status", LoRaChannel, 0, &(Config->LoRaDevices[LoRaChannel].EnableMessageStatus));
			ReadBoolean(fp, "LORA_RSSI_Status", LoRaChannel, 0, &(Config->LoRaDevices[LoRaChannel].EnableRSSIStatus));
			if ((Config->LoRaDevices[LoRaChannel].UplinkPeriod > 0) && (Config->LoRaDevices[LoRaChannel].UplinkCycle > 0))
			{
				printf("LORA%d uplink period %ds every %ds\n", LoRaChannel, Config->LoRaDevices[LoRaChannel].UplinkPeriod, Config->LoRaDevices[LoRaChannel].UplinkCycle);
			}
			
			Config->LoRaDevices[LoRaChannel].ImplicitOrExplicit = LoRaModes[Config->LoRaDevices[LoRaChannel].SpeedMode].ImplicitOrExplicit;
			Config->LoRaDevices[LoRaChannel].ErrorCoding = LoRaModes[Config->LoRaDevices[LoRaChannel].SpeedMode].ErrorCoding;
			Config->LoRaDevices[LoRaChannel].Bandwidth = LoRaModes[Config->LoRaDevices[LoRaChannel].SpeedMode].Bandwidth;
			Config->LoRaDevices[LoRaChannel].SpreadingFactor = LoRaModes[Config->LoRaDevices[LoRaChannel].SpeedMode].SpreadingFactor;
			Config->LoRaDevices[LoRaChannel].LowDataRateOptimize = LoRaModes[Config->LoRaDevices[LoRaChannel].SpeedMode].LowDataRateOptimize;
			
			Config->Channels[Channel].BaudRate = LoRaModes[Config->LoRaDevices[LoRaChannel].SpeedMode].BaudRate;
				
			Temp = ReadInteger(fp, "LORA_SF", LoRaChannel, 0, 0);
			if ((Temp >= 6) && (Temp <= 12))
			{
				Config->LoRaDevices[LoRaChannel].SpreadingFactor = Temp << 4;
				printf("LoRa Setting SF=%d\n", Temp);
			}

			ReadString(fp, "LORA_Bandwidth", LoRaChannel, TempString, sizeof(TempString), 0);
			if (*TempString)
			{
				printf("LoRa Setting BW=%s\n", TempString);
			}
			if (strcmp(TempString, "7K8") == 0)
			{
				Config->LoRaDevices[LoRaChannel].Bandwidth = BANDWIDTH_7K8;
			}
			if (strcmp(TempString, "10K4") == 0)
			{
				Config->LoRaDevices[LoRaChannel].Bandwidth = BANDWIDTH_10K4;
			}
			if (strcmp(TempString, "15K6") == 0)
			{
				Config->LoRaDevices[LoRaChannel].Bandwidth = BANDWIDTH_15K6;
			}
			if (strcmp(TempString, "20K8") == 0)
			{
				Config->LoRaDevices[LoRaChannel].Bandwidth = BANDWIDTH_20K8;
			}
			if (strcmp(TempString, "31K25") == 0)
			{
				Config->LoRaDevices[LoRaChannel].Bandwidth = BANDWIDTH_31K25;
			}
			if (strcmp(TempString, "41K7") == 0)
			{
				Config->LoRaDevices[LoRaChannel].Bandwidth = BANDWIDTH_41K7;
			}
			if (strcmp(TempString, "62K5") == 0)
			{
				Config->LoRaDevices[LoRaChannel].Bandwidth = BANDWIDTH_62K5;
			}
			if (strcmp(TempString, "125K") == 0)
			{
				Config->LoRaDevices[LoRaChannel].Bandwidth = BANDWIDTH_125K;
			}
			if (strcmp(TempString, "250K") == 0)
			{
				Config->LoRaDevices[LoRaChannel].Bandwidth = BANDWIDTH_250K;
			}
			if (strcmp(TempString, "500K") == 0)
			{
				Config->LoRaDevices[LoRaChannel].Bandwidth = BANDWIDTH_500K;
			}
			
			if (ReadBoolean(fp, "LORA_Implicit", LoRaChannel, 0, &Temp))
			{
				Config->LoRaDevices[LoRaChannel].ImplicitOrExplicit = Temp ? IMPLICIT_MODE : EXPLICIT_MODE;
			}
			
			Temp = ReadInteger(fp, "LORA_Coding", LoRaChannel, 0, 0);
			if ((Temp >= 5) && (Temp <= 8))
			{
				Config->LoRaDevices[LoRaChannel].ErrorCoding = (Temp-4) << 1;
				printf("LoRa Setting Error Coding=%d\n", Temp);
			}

			if (ReadBoolean(fp, "LORA_LowOpt", LoRaChannel, 0, &Temp))
			{
				Config->LoRaDevices[LoRaChannel].LowDataRateOptimize = Temp ? 0x08 : 0;
			}

			Config->LoRaDevices[LoRaChannel].Power = ReadInteger(fp, "LORA_Power", LoRaChannel, 0, PA_MAX_UK);
			printf("LORA%d power set to %02Xh\n", LoRaChannel, Config->LoRaDevices[LoRaChannel].Power);

			Config->LoRaDevices[LoRaChannel].CallingFrequency[0] = '\0';
			ReadString(fp, "LORA_Calling_Frequency", LoRaChannel, Config->LoRaDevices[LoRaChannel].CallingFrequency, sizeof(Config->LoRaDevices[LoRaChannel].CallingFrequency), 0);
		
			if (Config->LoRaDevices[LoRaChannel].CallingFrequency[0])
			{
				// Calling frequency enabled
				
				Config->LoRaDevices[LoRaChannel].CallingCount = ReadInteger(fp, "LORA_Calling_Count", LoRaChannel, 0, 0);
				if (Config->LoRaDevices[LoRaChannel].CallingCount)
				{
					printf("LoRa channel %d will Tx on calling frequency %s every %d packets\n", LoRaChannel, Config->LoRaDevices[LoRaChannel].CallingFrequency, Config->LoRaDevices[LoRaChannel].CallingCount);
				}
			}
		}
		else
		{
			Config->LoRaDevices[LoRaChannel].InUse = 0;
		}
	}
}
	
void *LoRaLoop(void *some_void_ptr)
{	
	int LoopMS = 5;
	int LoRaChannel;
	unsigned char Sentence[200];
	struct TGPS *GPS;

	GPS = (struct TGPS *)some_void_ptr;

	for (LoRaChannel=0; LoRaChannel<2; LoRaChannel++)
	{
		setupRFM98(LoRaChannel);
		if (Config.LoRaDevices[LoRaChannel].SpeedMode == 2)
		{
			startReceiving(LoRaChannel);
		}
		
		Config.LoRaDevices[LoRaChannel].PacketsSinceLastCall = Config.LoRaDevices[LoRaChannel].CallingCount;		// So we do the calling channel first
	}
	
	while (1)
	{	
		delay(LoopMS);								// To stop this loop gobbling up CPU

		CheckForPacketOnListeningChannels();
		
		LoRaChannel = CheckForFreeChannel(GPS);		// 0 or 1 if there's a free channel and we should be sending on that channel now

		if ((LoRaChannel >= 0) && (LoRaChannel <= 1))
		{
			int Channel;
			
			Channel = LoRaChannel + LORA_CHANNEL;
			
			if (Config.LoRaDevices[LoRaChannel].ReturnStateAfterCall)
			{
				double Frequency;
				
				Config.LoRaDevices[LoRaChannel].ReturnStateAfterCall = 0;

				sscanf(Config.LoRaDevices[LoRaChannel].Frequency, "%lf", &Frequency);
				SetLoRaFrequency(LoRaChannel, Frequency);

				SetLoRaParameters(LoRaChannel,
								  Config.LoRaDevices[LoRaChannel].ImplicitOrExplicit,
								  Config.LoRaDevices[LoRaChannel].ErrorCoding,
								  Config.LoRaDevices[LoRaChannel].Bandwidth,
								  Config.LoRaDevices[LoRaChannel]. SpreadingFactor,
								  Config.LoRaDevices[LoRaChannel].LowDataRateOptimize);
				printf("Reset after Uplink Mode\n");
			}
			
			// Now decide what, if anything, to send

			// Handle deliberate delays between packets
			if ((Config.LoRaDevices[LoRaChannel].PacketEveryMilliSeconds > 0) && ((Config.LoRaDevices[LoRaChannel].MillisSinceLastPacket += LoopMS) < Config.LoRaDevices[LoRaChannel].PacketEveryMilliSeconds))
			{
				// Deliberate delay, and we're not there yet
				// DO NOTHING!
			}			
			else if (Config.LoRaDevices[LoRaChannel].SendRepeatedPacket == 2)
			{
				printf("Repeating uplink packet of %d bytes\n", Config.LoRaDevices[LoRaChannel].UplinkRepeatLength);
				
				SendLoRaData(LoRaChannel, Config.LoRaDevices[LoRaChannel].UplinkPacket, Config.LoRaDevices[LoRaChannel].UplinkRepeatLength);
				
				Config.LoRaDevices[LoRaChannel].UplinkRepeatLength = 0;
			}
			else if (Config.LoRaDevices[LoRaChannel].SendRepeatedPacket == 1)
			{
				printf("Repeating balloon packet of %d bytes\n", Config.LoRaDevices[LoRaChannel].PacketRepeatLength);
				
				SendLoRaData(LoRaChannel, Config.LoRaDevices[LoRaChannel].PacketToRepeat, Config.LoRaDevices[LoRaChannel].PacketRepeatLength);
				
				Config.LoRaDevices[LoRaChannel].PacketRepeatLength = 0;
			}
			else if (Config.LoRaDevices[LoRaChannel].CallingFrequency[0] &&
					 Config.LoRaDevices[LoRaChannel].CallingCount &&
					 (Config.LoRaDevices[LoRaChannel].PacketsSinceLastCall >= Config.LoRaDevices[LoRaChannel].CallingCount))
			{
				int PacketLength;
				double Frequency;

				sscanf(Config.LoRaDevices[LoRaChannel].CallingFrequency, "%lf", &Frequency);
				SetLoRaFrequency(LoRaChannel, Frequency);
				printf("Calling frequency is %lf\n", Frequency);
				
				SetLoRaParameters(LoRaChannel, EXPLICIT_MODE, ERROR_CODING_4_8, BANDWIDTH_41K7, SPREADING_11, 0);	// 0x08);

				PacketLength = BuildLoRaCall(Sentence, LoRaChannel);
				printf("LORA%d: %s", LoRaChannel, Sentence);
									
				SendLoRaData(LoRaChannel, Sentence, PacketLength);		
				
				Config.LoRaDevices[LoRaChannel].ReturnStateAfterCall = 1;

				Config.LoRaDevices[LoRaChannel].PacketsSinceLastCall = 0;
			}
			else
			{			
				int MaxImagePackets;

				if ((Config.Channels[Channel].SendMode == 0) || (Config.Channels[Channel].ImagePackets == 0))
				{
					int PacketLength;

					// Telemetry packet
					
					if (Config.LoRaDevices[LoRaChannel].Binary)
					{
						PacketLength = BuildLoRaPositionPacket(Sentence, LoRaChannel, GPS);
						printf("LoRa%d: Binary packet %d bytes\n", LoRaChannel, PacketLength);
					}
					else
					{
						PacketLength = BuildSentence(Sentence, Channel, GPS);
						LogMessage("LORA%d: %s", LoRaChannel, Sentence);
					}
									
					SendLoRaData(LoRaChannel, Sentence, PacketLength);		

					Config.LoRaDevices[LoRaChannel].PacketsSinceLastCall++;
				}
				else
				{
					// Image packet
					
					SendLoRaImage(LoRaChannel);
				}

				if (Config.Channels[Channel].ImagePackets == 0)
				{
					MaxImagePackets = 0;
				}
				else
				{
					MaxImagePackets = ((GPS->Altitude > Config.SSDVHigh) || (Config.Channels[Channel].BaudRate > 2000)) ? Config.Channels[Channel].ImagePackets : 1;
				}

				if (++Config.Channels[Channel].SendMode > MaxImagePackets)
				{
					Config.Channels[Channel].SendMode = 0;
				}
			}
		}
	}
}
