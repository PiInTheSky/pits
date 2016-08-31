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

void setMode(int Channel, uint8_t newMode)
{
  if(newMode == currentMode)
    return;  
  
  switch (newMode) 
  {
    case RF98_MODE_TX:
      writeRegister(Channel, REG_LNA, LNA_OFF_GAIN);  // TURN LNA OFF FOR TRANSMITT
      writeRegister(Channel, REG_PA_CONFIG, Config.LoRaDevices[Channel].Power);
      writeRegister(Channel, REG_OPMODE, newMode);
      currentMode = newMode; 
      break;
    case RF98_MODE_RX_CONTINUOUS:
      writeRegister(Channel, REG_PA_CONFIG, PA_OFF_BOOST);  // TURN PA OFF FOR RECIEVE??
      writeRegister(Channel, REG_LNA, LNA_MAX_GAIN);  // LNA_MAX_GAIN);  // MAX GAIN FOR RECIEVE
      writeRegister(Channel, REG_OPMODE, newMode);
      currentMode = newMode; 
      break;
    case RF98_MODE_SLEEP:
      writeRegister(Channel, REG_OPMODE, newMode);
      currentMode = newMode; 
      break;
    case RF98_MODE_STANDBY:
      writeRegister(Channel, REG_OPMODE, newMode);
      currentMode = newMode; 
      break;
    default: return;
  } 
  
	if(newMode != RF98_MODE_SLEEP)
	{
		// printf("Waiting for Mode Change\n");
		while(digitalRead(Config.LoRaDevices[Channel].DIO5) == 0)
		{
		} 
		// printf("Mode change completed\n");
	}
	
	return;
}
 
void setFrequency(int Channel, double Frequency)
{
	unsigned long FrequencyValue;

	printf("Setting LoRa Mode\n");
	setMode(Channel, RF98_MODE_STANDBY);
	setMode(Channel, RF98_MODE_SLEEP);
	writeRegister(Channel, REG_OPMODE, 0x80);

	setMode(Channel, RF98_MODE_SLEEP);

	FrequencyValue = (unsigned long)(Frequency * 7110656 / 434);
	
	printf("Channel %d frequency %lf FrequencyValue = %06Xh\n", Channel, Frequency, FrequencyValue);
	
	writeRegister(Channel, 0x06, (FrequencyValue >> 16) & 0xFF);		// Set frequency
	writeRegister(Channel, 0x07, (FrequencyValue >> 8) & 0xFF);
	writeRegister(Channel, 0x08, FrequencyValue & 0xFF);
}

void setLoRaMode(int Channel)
{
	double Frequency;
	unsigned long FrequencyValue;

	// printf("Setting LoRa Mode\n");
	// setMode(Channel, RF98_MODE_SLEEP);
	// writeRegister(Channel, REG_OPMODE, 0x80);

	// setMode(Channel, RF98_MODE_SLEEP);
  
	if (sscanf(Config.LoRaDevices[Channel].Frequency, "%lf", &Frequency))
	{
		setFrequency(Channel, Frequency);
		// FrequencyValue = (unsigned long)(Frequency * 7110656 / 434);
		// printf("FrequencyValue = %06Xh\n", FrequencyValue);
		// writeRegister(Channel, 0x06, (FrequencyValue >> 16) & 0xFF);		// Set frequency
		// writeRegister(Channel, 0x07, (FrequencyValue >> 8) & 0xFF);
		// writeRegister(Channel, 0x08, FrequencyValue & 0xFF);
	}

	printf("Mode = %d\n", readRegister(Channel, REG_OPMODE));
}

void SetLoRaParameters(int Channel, int ImplicitOrExplicit, int ErrorCoding, int Bandwidth, int SpreadingFactor, int LowDataRateOptimize)
{
	writeRegister(Channel, REG_MODEM_CONFIG, ImplicitOrExplicit | ErrorCoding | Bandwidth);
	writeRegister(Channel, REG_MODEM_CONFIG2, SpreadingFactor | CRC_ON);
	writeRegister(Channel, REG_MODEM_CONFIG3, 0x04 | LowDataRateOptimize);									// 0x04: AGC sets LNA gain
	writeRegister(Channel, REG_DETECT_OPT, (readRegister(Channel, REG_DETECT_OPT) & 0xF8) | ((SpreadingFactor == SPREADING_6) ? 0x05 : 0x03));	// 0x05 For SF6; 0x03 otherwise
	writeRegister(Channel, REG_DETECTION_THRESHOLD, (SpreadingFactor == SPREADING_6) ? 0x0C : 0x0A);		// 0x0C for SF6, 0x0A otherwise
	
	Config.LoRaDevices[Channel].PayloadLength = ImplicitOrExplicit == IMPLICIT_MODE ? 255 : 0;
	
	writeRegister(Channel, REG_PAYLOAD_LENGTH, Config.LoRaDevices[Channel].PayloadLength);
	writeRegister(Channel, REG_RX_NB_BYTES, Config.LoRaDevices[Channel].PayloadLength);
}

void setupRFM98(int Channel)
{
	if (Config.LoRaDevices[Channel].InUse)
	{
		// initialize the pins
		printf("Channel %d DIO0=%d DIO5=%d\n", Channel, Config.LoRaDevices[Channel].DIO0, Config.LoRaDevices[Channel].DIO5);
		pinMode(Config.LoRaDevices[Channel].DIO0, INPUT);
		pinMode(Config.LoRaDevices[Channel].DIO5, INPUT);

		if (wiringPiSPISetup(Channel, 500000) < 0)
		{
			fprintf(stderr, "Failed to open SPI port.  Try loading spi library with 'gpio load spi'");
			exit(1);
		}
		
		setLoRaMode(Channel);

		SetLoRaParameters(Channel,
						  Config.LoRaDevices[Channel].ImplicitOrExplicit,
						  Config.LoRaDevices[Channel].ErrorCoding,
						  Config.LoRaDevices[Channel].Bandwidth,
						  Config.LoRaDevices[Channel]. SpreadingFactor,
						  Config.LoRaDevices[Channel].LowDataRateOptimize);
		
		// writeRegister(Channel, REG_PAYLOAD_LENGTH, Config.LoRaDevices[Channel].PayloadLength);
		// writeRegister(Channel, REG_RX_NB_BYTES, Config.LoRaDevices[Channel].PayloadLength);

		writeRegister(Channel, REG_FIFO_ADDR_PTR, 0);		// woz readRegister(Channel, REG_FIFO_RX_BASE_AD));   

		// writeRegister(Channel, REG_DIO_MAPPING_1,0x40);
		writeRegister(Channel, REG_DIO_MAPPING_2,0x00);
		
	}
}


void SendLoRaData(int Channel, unsigned char *buffer, int Length)
{
	unsigned char data[257];
	int i;
	
	// printf("LoRa Channel %d Sending %d bytes\n", Channel, Length);
  
	setMode(Channel, RF98_MODE_STANDBY);
	
	writeRegister(Channel, REG_DIO_MAPPING_1, 0x40);		// 01 00 00 00 maps DIO0 to TxDone

	writeRegister(Channel, REG_FIFO_TX_BASE_AD, 0x00);  // Update the address ptr to the current tx base address
	writeRegister(Channel, REG_FIFO_ADDR_PTR, 0x00); 
  
	data[0] = REG_FIFO | 0x80;
	for (i=0; i<Length; i++)
	{
		data[i+1] = buffer[i];
	}
	wiringPiSPIDataRW(Channel, data, Length+1);

// printf("Set Tx Mode\n");

	// Set the length. For implicit mode, since the length needs to match what the receiver expects, we have to set a value which is 255 for an SSDV packet
	writeRegister(Channel, REG_PAYLOAD_LENGTH, Config.LoRaDevices[Channel].PayloadLength ? Config.LoRaDevices[Channel].PayloadLength : Length);

	// go into transmit mode
	setMode(Channel, RF98_MODE_TX);
	
	Config.LoRaDevices[Channel].LoRaMode = lmSending;
}

int BuildLoRaCall(char *TxLine, int Channel)
{
    int Count, i, j;
    unsigned char c;
    unsigned int CRC, xPolynomial;
		
    sprintf(TxLine, "^^%s,%s,%d,%d,%d,%d,%d",
            Config.Channels[LORA_CHANNEL+Channel].PayloadID,
			Config.LoRaDevices[Channel].Frequency,
			Config.LoRaDevices[Channel].ImplicitOrExplicit,
			Config.LoRaDevices[Channel].ErrorCoding,
			Config.LoRaDevices[Channel].Bandwidth,
			Config.LoRaDevices[Channel].SpreadingFactor,
			Config.LoRaDevices[Channel].LowDataRateOptimize);
			
	AppendCRC(TxLine);
		
	return strlen(TxLine) + 1;
}


int BuildLoRaSentence(char *TxLine, int Channel, struct TGPS *GPS)
{
	static char ExternalFields[100];
	static FILE *ExternalFile=NULL;
	static int FirstTime=1;
    int Count, i, j;
    unsigned char c;
    unsigned int CRC, xPolynomial;
	char TimeBuffer[12], ExtraFields1[20], ExtraFields2[20], ExtraFields3[20], ExtraFields4[32], ExtraFields5[32], ExtraFields6[32];
	
	if (FirstTime)
	{
		FirstTime = 0;
		ExternalFields[0] = '\0';
	}
	
	Config.Channels[LORA_CHANNEL+Channel].SentenceCounter++;
	
	sprintf(TimeBuffer, "%02d:%02d:%02d", GPS->Hours, GPS->Minutes, GPS->Seconds);
	
	ExtraFields1[0] = '\0';
	ExtraFields2[0] = '\0';
	ExtraFields3[0] = '\0';
	ExtraFields4[0] = '\0';
	ExtraFields5[0] = '\0';
	ExtraFields6[0] = '\0';
	
	if ((Config.BoardType == 3) || (Config.DisableADC))
	{
	}
	else if (Config.BoardType == 0)
	{
		sprintf(ExtraFields1, ",%.3f", GPS->BatteryVoltage);
	}
	else
	{
		sprintf(ExtraFields1, ",%.1f,%.3f", GPS->BatteryVoltage, GPS->BoardCurrent);
	}
	
	if (Config.EnableBMP085)
	{
		sprintf(ExtraFields2, ",%.1f,%.0f", GPS->BMP180Temperature, GPS->Pressure);
	}
	
	if (GPS->DS18B20Count > 1)
	{
		sprintf(ExtraFields3, ",%3.1f", GPS->DS18B20Temperature[Config.ExternalDS18B20]);
	}
	
	if (Config.EnableLandingPrediction && (Config.PredictionID[0] == '\0'))
	{	
		sprintf(ExtraFields4, ",%7.5lf,%7.5lf", GPS->PredictedLatitude, GPS->PredictedLongitude);
	}
	
	if (Config.LoRaDevices[Channel].EnableRSSIStatus)
	{	
		sprintf(ExtraFields5, ",%d,%d,%d", Config.LoRaDevices[Channel].LastPacketRSSI,
										   Config.LoRaDevices[Channel].LastPacketSNR,
										   Config.LoRaDevices[Channel].PacketCount);
	}

	if (Config.LoRaDevices[Channel].EnableMessageStatus)
	{	
		sprintf(ExtraFields6, ",%d,%d", Config.LoRaDevices[Channel].LastMessageNumber, Config.LoRaDevices[Channel].MessageCount);
	}

	// Read latest data from external file
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
				sprintf(ExternalFields, ",%s", line);
			}
			fseek(ExternalFile, 0, SEEK_END);
			// clearerr(ExternalFile);
		}
	}

    sprintf(TxLine, "$$%s,%d,%s,%7.5lf,%7.5lf,%05.5ld,%d,%d,%d,%3.1f%s%s%s%s%s%s%s",
            Config.Channels[LORA_CHANNEL+Channel].PayloadID,
            Config.Channels[LORA_CHANNEL+Channel].SentenceCounter,
			TimeBuffer,
            GPS->Latitude,
            GPS->Longitude,
            GPS->Altitude,
			(GPS->Speed * 13) / 7,
			GPS->Direction,
			GPS->Satellites,
            GPS->DS18B20Temperature[1-Config.ExternalDS18B20],
			ExtraFields1,
			ExtraFields2,
			ExtraFields3,
			ExtraFields4,
			ExternalFields,
			ExtraFields5,
			ExtraFields6);
			
	AppendCRC(TxLine);

	if (Config.PredictionID[0])
	{
		char PredictionPayload[64];
		
		sprintf(PredictionPayload,
				"$$%s,%d,%s,%7.5lf,%7.5lf,%u",
				Config.PredictionID,
				Config.Channels[LORA_CHANNEL+Channel].SentenceCounter,
				TimeBuffer,
				GPS->PredictedLatitude,
				GPS->PredictedLongitude,
				0);
		AppendCRC(PredictionPayload);
		strcat(TxLine, PredictionPayload);
	}
	
	return strlen(TxLine) + 1;
}

int BuildLoRaPositionPacket(char *TxLine, int Channel, struct TGPS *GPS)
{
	int OurID;
	struct TBinaryPacket BinaryPacket;
	
	OurID = Config.LoRaDevices[Channel].Slot;
	
	Config.Channels[LORA_CHANNEL+Channel].SentenceCounter++;

	BinaryPacket.PayloadIDs = 0xC0 | (OurID << 3) | OurID;
	BinaryPacket.Counter = Config.Channels[LORA_CHANNEL+Channel].SentenceCounter;
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
	int ResentPacket;

	StartNewFileIfNeeded(LORA_CHANNEL + LoRaChannel);
	
	ResentPacket = ChooseImagePacketToSend(LORA_CHANNEL + LoRaChannel);
	
    if (Config.Channels[LORA_CHANNEL+LoRaChannel].ImageFP != NULL)
    {
        Count = fread(Buffer, 1, 256, Config.Channels[LORA_CHANNEL+LoRaChannel].ImageFP);
        if (Count > 0)
        {
            // printf("Record %d, %d bytes\r\n", ++Records, Count);

			// Config.Channels[LORA_CHANNEL+LoRaChannel].ImagePacketCount++;
			
			// if (((Config.Channels[LORA_CHANNEL+LoRaChannel].SSDVPacketNumber & 7) < 5) || ResentPacket)	// FOR REPEAT TESTING
			{
				AddImagePacketToRecentList(LORA_CHANNEL+LoRaChannel, Config.Channels[LORA_CHANNEL + LoRaChannel].SSDVImageNumber, Config.Channels[LORA_CHANNEL + LoRaChannel].SSDVPacketNumber);
				
				printf("LORA%d: SSDV %d packet %d of %d %s\r\n", LoRaChannel, Config.Channels[LORA_CHANNEL + LoRaChannel].SSDVImageNumber, Config.Channels[LORA_CHANNEL + LoRaChannel].SSDVPacketNumber, Config.Channels[LORA_CHANNEL + LoRaChannel].SSDVNumberOfPackets, ResentPacket ? "** RESEND **" : "");
				
				SendLoRaData(LoRaChannel, Buffer+1, 255);
			
				SentSomething = 1;
			}
        }
        else
        {
            fclose(Config.Channels[LORA_CHANNEL+LoRaChannel].ImageFP);
            Config.Channels[LORA_CHANNEL+LoRaChannel].ImageFP = NULL;
        }
    }

    return SentSomething;
}

int TDMTimeToSendOnThisChannel(int Channel, struct TGPS *GPS)
{
	// Can't send till we have the time!
	if (GPS->Satellites > 0)
	{
		// Can't Tx twice at the same time
		if (GPS->SecondsInDay != Config.LoRaDevices[Channel].LastTxAt)
		{
			long CycleSeconds;
		
			CycleSeconds = GPS->SecondsInDay % Config.LoRaDevices[Channel].CycleTime;

			if (CycleSeconds == Config.LoRaDevices[Channel].Slot)
			{
				Config.LoRaDevices[Channel].LastTxAt = GPS->SecondsInDay;
				Config.LoRaDevices[Channel].SendRepeatedPacket = 0;
				return 1;
			}

			if (Config.LoRaDevices[Channel].PacketRepeatLength && (CycleSeconds == Config.LoRaDevices[Channel].RepeatSlot))
			{
				Config.LoRaDevices[Channel].LastTxAt = GPS->SecondsInDay;
				Config.LoRaDevices[Channel].SendRepeatedPacket = 1;
				return 1;
			}
			
			if (Config.LoRaDevices[Channel].UplinkRepeatLength && (CycleSeconds == Config.LoRaDevices[Channel].UplinkSlot))
			{
				Config.LoRaDevices[Channel].LastTxAt = GPS->SecondsInDay;
				Config.LoRaDevices[Channel].SendRepeatedPacket = 2;
				return 1;
			}
			
		}
	}
	
	return 0;
}
	
int UplinkTimeToSendOnThisChannel(int Channel, struct TGPS *GPS)
{
	// Can't use time till we have it
	if (GPS->Satellites > 0)
	{
		long CycleSeconds;
		
		CycleSeconds = GPS->SecondsInDay % Config.LoRaDevices[Channel].UplinkCycle;
	
		if (CycleSeconds < Config.LoRaDevices[Channel].UplinkPeriod)
		{
			return 0;
		}
	}
	
	return 1;
}
	
int TimeToSendOnThisChannel(int Channel, struct TGPS *GPS)
{
	if (Config.LoRaDevices[Channel].CycleTime > 0)
	{
		// TDM
		return TDMTimeToSendOnThisChannel(Channel, GPS);
	}
	
	if ((Config.LoRaDevices[Channel].UplinkPeriod > 0) && (Config.LoRaDevices[Channel].UplinkCycle > 0))
	{
		// Continuous with uplink
		return UplinkTimeToSendOnThisChannel(Channel, GPS);
	}
	
	// Continuous
	return 1;
}

void startReceiving(int Channel)
{
	if (Config.LoRaDevices[Channel].InUse)
	{
		printf ("Listening on channel %d\n", Channel);
		
		writeRegister(Channel, REG_DIO_MAPPING_1, 0x00);		// 00 00 00 00 maps DIO0 to RxDone
	
		writeRegister(Channel, REG_FIFO_RX_BASE_AD, 0);
		writeRegister(Channel, REG_FIFO_ADDR_PTR, 0);
	  
		// Setup Receive Continuous Mode
		setMode(Channel, RF98_MODE_RX_CONTINUOUS); 
		
		Config.LoRaDevices[Channel].LoRaMode = lmListening;
	}
}

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

	return - ((double)Temp * (1<<24) / 32000000.0) * (125000 / 500000.0);
}	

int receiveMessage(int Channel, unsigned char *message)
{
	int i, Bytes, currentAddr, x;
	unsigned char data[257];

	printf ("Rx channel %d\n", Channel);
	
	Bytes = 0;
	
	x = readRegister(Channel, REG_IRQ_FLAGS);
  
	// clear the rxDone flag
	writeRegister(Channel, REG_IRQ_FLAGS, 0x40); 
   
	// check for payload crc issues (0x20 is the bit we are looking for
	if((x & 0x20) == 0x20)
	{
		// CRC Error
		writeRegister(Channel, REG_IRQ_FLAGS, 0x20);		// reset the crc flags
		Config.LoRaDevices[Channel].BadCRCCount++;
		printf ("CRC Error\n");
	}
	else
	{
		currentAddr = readRegister(Channel, REG_FIFO_RX_CURRENT_ADDR);
		Bytes = readRegister(Channel, REG_RX_NB_BYTES);
		printf ("*** Received %d bytes\n", Bytes);

		// ChannelPrintf(Channel,  9, 1, "Packet   SNR = %4d   ", (char)(readRegister(Channel, REG_PACKET_SNR)) / 4);
		// ChannelPrintf(Channel, 10, 1, "Packet  RSSI = %4d   ", readRegister(Channel, REG_PACKET_RSSI) - 157);
		printf("LORA%d: Freq. Error = %4.1lfkHz ", Channel, FrequencyError(Channel) / 1000);

		writeRegister(Channel, REG_FIFO_ADDR_PTR, currentAddr);   
		
		data[0] = REG_FIFO;
		wiringPiSPIDataRW(Channel, data, Bytes+1);
		for (i=0; i<=Bytes; i++)
		{
			message[i] = data[i+1];
		}
		
		message[Bytes] = '\0';
	} 

	// Clear all flags
	writeRegister(Channel, REG_IRQ_FLAGS, 0xFF); 
  
	return Bytes;
}

void CheckForPacketOnListeningChannels(void)
{
	int Channel;
	
	for (Channel=0; Channel<=1; Channel++)
	{
		if (Config.LoRaDevices[Channel].InUse)
		{
			if (Config.LoRaDevices[Channel].LoRaMode == lmListening)
			{
				if (digitalRead(Config.LoRaDevices[Channel].DIO0))
				{
					unsigned char Message[256];
					int Bytes;
					
					Bytes = receiveMessage(Channel, Message);
					
					if (Bytes > 0)
					{
						int8_t SNR;
						int RSSI;

						SNR = readRegister(Channel, REG_PACKET_SNR);
						SNR /= 4;
						RSSI = readRegister(Channel, REG_PACKET_RSSI) - 157;
						if (SNR < 0)
						{
							RSSI += SNR;
						}
						
						Config.LoRaDevices[Channel].LastPacketSNR = SNR;
						Config.LoRaDevices[Channel].LastPacketRSSI = RSSI;
						Config.LoRaDevices[Channel].PacketCount++;
						if (Message[0] == '$')
						{
							// Normal ASCII telemetry message sent by another balloon
							char Payload[32];

							printf("Balloon message\n");
							if (sscanf(Message+2, "%32[^,]", Payload) == 1)
							{
								if (strcmp(Payload, Config.Channels[LORA_CHANNEL+Channel].PayloadID) != 0)
								{
									// printf ("%s\n", Message);
							
									strcpy(Config.LoRaDevices[Channel].PacketToRepeat, Message);
									Config.LoRaDevices[Channel].PacketRepeatLength = strlen(Message);
							
									Config.LoRaDevices[Channel].AirCount++;

									Message[strlen(Message)] = '\0';
								}
							}
						}
						else if ((Message[0] & 0xC0) == 0xC0)
						{
							// Binary telemetry message sent by another balloon
							char Payload[32];
							int SourceID, OurID;
							
							OurID = Config.LoRaDevices[Channel].Slot;
							SourceID = Message[0] & 0x07;
							
							if (SourceID == OurID)
							{
								printf("Balloon Binary Message - ignored\n");
							}
							else
							{
								printf("Balloon Binary Message from sender %d\n", SourceID);
								
								// Replace the sender ID with ours
								Message[0] = Message[0] & 0xC7 | (OurID << 3);
								Config.LoRaDevices[Channel].PacketRepeatLength = sizeof(struct TBinaryPacket);
								memcpy(Config.LoRaDevices[Channel].PacketToRepeat, Message, Config.LoRaDevices[Channel].PacketRepeatLength);
							
								Config.LoRaDevices[Channel].AirCount++;
							}
						}
						else if ((Message[0] & 0xC0) == 0x80)
						{
							// Uplink in TDM mode
							int SenderID, TargetID, OurID;
							
							TargetID = Message[0] & 0x07;
							SenderID = (Message[0] >> 3) & 0x07;
							OurID = Config.LoRaDevices[Channel].Slot;

							printf("Uplink from %d to %d Message %s\n",
									SenderID,
									TargetID,
									Message+1);
									
							if (TargetID == OurID)
							{
								printf("Message was for us!\n");
								strcpy(Config.LoRaDevices[Channel].LastCommand, Message+1);
								printf("Message is '%s'\n", Config.LoRaDevices[Channel].LastCommand);
								Config.LoRaDevices[Channel].GroundCount++;
							}
							else
							{
								printf("Message was for another balloon\n");
								Message[0] = Message[0] & 0xC7 | (OurID << 3);
								Config.LoRaDevices[Channel].UplinkRepeatLength = sizeof(struct TBinaryPacket);
								memcpy(Config.LoRaDevices[Channel].UplinkPacket, Message, Config.LoRaDevices[Channel].UplinkRepeatLength);
							}
						}
						else if (Message[0] == '!')
						{
							// SSDV Uplink message
							printf("SSDV uplink message %s", Message);
							ProcessSSDVUplinkMessage(LORA_CHANNEL+Channel, Message);
						}
						else if (Message[0] == '#')
						{
							// SMS Uplink message
							printf("SMS uplink message %s", Message);
							ProcessSMSUplinkMessage(Channel, Message);
						}
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
	int Channel;
	
	for (Channel=0; Channel<=1; Channel++)
	{
		if (Config.LoRaDevices[Channel].InUse)
		{
			if ((Config.LoRaDevices[Channel].LoRaMode != lmSending) || digitalRead(Config.LoRaDevices[Channel].DIO0))
			{
				// printf ("LoRa Channel %d is free\n", Channel);
				// Either not sending, or was but now it's sent.  Clear the flag if we need to
				if (Config.LoRaDevices[Channel].LoRaMode == lmSending)
				{
					// Clear that IRQ flag
					writeRegister(Channel, REG_IRQ_FLAGS, 0x08); 
					Config.LoRaDevices[Channel].LoRaMode = lmIdle;
				}
				
				// Mow we test to see if we can send now
				// If Tx is continuous, then the answer is yes, of course
				// If there's an uplink period defined, we need to be outside that
				// For TDM, we need to be inside one of our slots
				
				if (TimeToSendOnThisChannel(Channel, GPS))
				{
					// Either sending continuously, or it's our slot to send in
					// printf("Channel %d is free\n", Channel);
					
					return Channel;
				}
				else if ((Config.LoRaDevices[Channel].CycleTime > 0) || (Config.LoRaDevices[Channel].UplinkCycle > 0))
				{
					// TDM system and not time to send, so we can listen
					if (Config.LoRaDevices[Channel].LoRaMode == lmIdle)
					{
						if (Config.LoRaDevices[Channel].SpeedMode == 3)
						{
							// We're using a different frequency and mode for the uplink
							setFrequency(Channel, 869.490);
							SetLoRaParameters(Channel, EXPLICIT_MODE, ERROR_CODING_4_8, BANDWIDTH_125K, SPREADING_8, 0);
							Config.LoRaDevices[Channel].ReturnStateAfterCall = 1;
							printf("Set Uplink Mode\n");
						}
						
						startReceiving(Channel);
					}
				}
			}
		}
	}
	
	return -1;
}

void LoadLoRaConfig(FILE *fp, struct TConfig *Config)
{
	const char *LoRaModes[5] = {"slow", "SSDV", "repeater", "turbo", "TurboX"};
	int Channel;
	
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


	for (Channel=0; Channel<=1; Channel++)
	{
		int Temp;
		char TempString[64];
		
		strcpy(Config->LoRaDevices[Channel].LastCommand, "None");
		
		Config->LoRaDevices[Channel].Frequency[0] = '\0';
		ReadString(fp, "LORA_Frequency", Channel, Config->LoRaDevices[Channel].Frequency, sizeof(Config->LoRaDevices[Channel].Frequency), 0);
		
		if (Config->LoRaDevices[Channel].Frequency[0])
		{
			printf("LORA%d frequency set to %s\n", Channel, Config->LoRaDevices[Channel].Frequency);
			Config->LoRaDevices[Channel].InUse = 1;
			Config->Channels[LORA_CHANNEL+Channel].Enabled = 1;

			ReadString(fp, "LORA_Payload", Channel, Config->Channels[LORA_CHANNEL+Channel].PayloadID, sizeof(Config->Channels[LORA_CHANNEL+Channel].PayloadID), 1);
			printf ("LORA%d Payload ID = '%s'\n", Channel, Config->Channels[LORA_CHANNEL+Channel].PayloadID);
			
			Config->LoRaDevices[Channel].SpeedMode = ReadInteger(fp, "LORA_Mode", Channel, 0, 0);
			printf("LORA%d %s mode\n", Channel, LoRaModes[Config->LoRaDevices[Channel].SpeedMode]);

			// DIO0 / DIO5 overrides
			Config->LoRaDevices[Channel].DIO0 = ReadInteger(fp, "LORA_DIO0", Channel, 0, Config->LoRaDevices[Channel].DIO0);
			Config->LoRaDevices[Channel].DIO5 = ReadInteger(fp, "LORA_DIO5", Channel, 0, Config->LoRaDevices[Channel].DIO5);
			printf("LORA%d DIO0=%d DIO5=%d\n", Channel, Config->LoRaDevices[Channel].DIO0, Config->LoRaDevices[Channel].DIO5);
			
			if (Config->Camera)
			{
				Config->Channels[LORA_CHANNEL+Channel].ImageWidthWhenLow = ReadInteger(fp, "LORA_low_width", Channel, 0, 320);
				Config->Channels[LORA_CHANNEL+Channel].ImageHeightWhenLow = ReadInteger(fp, "LORA_low_height", Channel, 0, 240);
				printf ("LORA%d Low image size %d x %d pixels\n", Channel, Config->Channels[LORA_CHANNEL+Channel].ImageWidthWhenLow, Config->Channels[LORA_CHANNEL+Channel].ImageHeightWhenLow);
				
				Config->Channels[LORA_CHANNEL+Channel].ImageWidthWhenHigh = ReadInteger(fp, "LORA_high_width", Channel, 0, 640);
				Config->Channels[LORA_CHANNEL+Channel].ImageHeightWhenHigh = ReadInteger(fp, "LORA_high_height", Channel, 0, 480);
				printf ("LORA%d High image size %d x %d pixels\n", Channel, Config->Channels[LORA_CHANNEL+Channel].ImageWidthWhenHigh, Config->Channels[LORA_CHANNEL+Channel].ImageHeightWhenHigh);

				Config->Channels[LORA_CHANNEL+Channel].ImagePackets = ReadInteger(fp, "LORA_image_packets", Channel, 0, 4);
				printf ("LORA%d: 1 Telemetry packet every %d image packets\n", Channel, Config->Channels[LORA_CHANNEL+Channel].ImagePackets);
				
				Config->Channels[LORA_CHANNEL+Channel].ImagePeriod = ReadInteger(fp, "LORA_image_period", Channel, 0, 60);
				printf ("LORA%d: %d seconds between photographs\n", Channel, Config->Channels[LORA_CHANNEL+Channel].ImagePeriod);
			}
			else
			{
				Config->Channels[LORA_CHANNEL+Channel].ImagePackets = 0;
			}
			

			Config->LoRaDevices[Channel].CycleTime = ReadInteger(fp, "LORA_Cycle", Channel, 0, 0);			
			if (Config->LoRaDevices[Channel].CycleTime > 0)
			{
				printf("LORA%d cycle time %d\n", Channel, Config->LoRaDevices[Channel].CycleTime);

				Config->LoRaDevices[Channel].Slot = ReadInteger(fp, "LORA_Slot", Channel, 0, 0);
				printf("LORA%d Slot %d\n", Channel, Config->LoRaDevices[Channel].Slot);

				Config->LoRaDevices[Channel].RepeatSlot = ReadInteger(fp, "LORA_Repeat", Channel, 0, 0);			
				printf("LORA%d Repeat Slot %d\n", Channel, Config->LoRaDevices[Channel].RepeatSlot);

				Config->LoRaDevices[Channel].UplinkSlot = ReadInteger(fp, "LORA_Uplink", Channel, 0, 0);			
				printf("LORA%d Uplink Slot %d\n", Channel, Config->LoRaDevices[Channel].UplinkSlot);

				ReadBoolean(fp, "LORA_Binary", Channel, 0, &(Config->LoRaDevices[Channel].Binary));			
				printf("LORA%d Set To %s\n", Channel, Config->LoRaDevices[Channel].Binary ? "Binary" : "ASCII");
			}

			Config->LoRaDevices[Channel].UplinkPeriod = ReadInteger(fp, "LORA_Uplink_Period", Channel, 0, 0);			
			Config->LoRaDevices[Channel].UplinkCycle = ReadInteger(fp, "LORA_Uplink_Cycle", Channel, 0, 0);			
			ReadBoolean(fp, "LORA_Message_Status", Channel, 0, &(Config->LoRaDevices[Channel].EnableMessageStatus));
			ReadBoolean(fp, "LORA_RSSI_Status", Channel, 0, &(Config->LoRaDevices[Channel].EnableRSSIStatus));
			if ((Config->LoRaDevices[Channel].UplinkPeriod > 0) && (Config->LoRaDevices[Channel].UplinkCycle > 0))
			{
				printf("LORA%d uplink period %ds every %ds\n", Channel, Config->LoRaDevices[Channel].UplinkPeriod, Config->LoRaDevices[Channel].UplinkCycle);
			}
			
			if (Config->LoRaDevices[Channel].SpeedMode == 4)
			{
				// Testing
				Config->LoRaDevices[Channel].ImplicitOrExplicit = IMPLICIT_MODE;
				Config->LoRaDevices[Channel].ErrorCoding = ERROR_CODING_4_5;
				Config->LoRaDevices[Channel].Bandwidth = BANDWIDTH_250K;
				Config->LoRaDevices[Channel].SpreadingFactor = SPREADING_6;
				Config->LoRaDevices[Channel].LowDataRateOptimize = 0;		
				Config->Channels[LORA_CHANNEL+Channel].BaudRate = 16828;
			}
			else if (Config->LoRaDevices[Channel].SpeedMode == 3)
			{
				// Normal mode for high speed images in 868MHz band
				Config->LoRaDevices[Channel].ImplicitOrExplicit = EXPLICIT_MODE;
				Config->LoRaDevices[Channel].ErrorCoding = ERROR_CODING_4_6;
				Config->LoRaDevices[Channel].Bandwidth = BANDWIDTH_250K;
				Config->LoRaDevices[Channel].SpreadingFactor = SPREADING_7;
				Config->LoRaDevices[Channel].LowDataRateOptimize = 0;		
				Config->Channels[LORA_CHANNEL+Channel].BaudRate = 8000;
			}
			else if (Config->LoRaDevices[Channel].SpeedMode == 2)
			{
				// Normal mode for repeater network
				// 72 byte packet is approx 1.5 seconds so needs at least 30 seconds cycle time if repeating one balloon
				Config->LoRaDevices[Channel].ImplicitOrExplicit = EXPLICIT_MODE;
				Config->LoRaDevices[Channel].ErrorCoding = ERROR_CODING_4_8;
				Config->LoRaDevices[Channel].Bandwidth = BANDWIDTH_62K5;
				Config->LoRaDevices[Channel].SpreadingFactor = SPREADING_8;
				Config->LoRaDevices[Channel].LowDataRateOptimize = 0;		
				Config->Channels[LORA_CHANNEL+Channel].BaudRate = 2000;		// Not used (only for SSDV modes)
			}
			else if (Config->LoRaDevices[Channel].SpeedMode == 1)
			{
				// Normal mode for SSDV
				Config->LoRaDevices[Channel].ImplicitOrExplicit = IMPLICIT_MODE;
				Config->LoRaDevices[Channel].ErrorCoding = ERROR_CODING_4_5;
				Config->LoRaDevices[Channel].Bandwidth = BANDWIDTH_20K8;
				Config->LoRaDevices[Channel].SpreadingFactor = SPREADING_6;
				Config->LoRaDevices[Channel].LowDataRateOptimize = 0;
				Config->Channels[LORA_CHANNEL+Channel].BaudRate = 1400;		// Used to calculate time till end of image
			}
			else
			{
				// Normal mode for telemetry
				Config->LoRaDevices[Channel].ImplicitOrExplicit = EXPLICIT_MODE;
				Config->LoRaDevices[Channel].ErrorCoding = ERROR_CODING_4_8;
				Config->LoRaDevices[Channel].Bandwidth = BANDWIDTH_20K8;
				Config->LoRaDevices[Channel].SpreadingFactor = SPREADING_11;
				Config->LoRaDevices[Channel].LowDataRateOptimize = 0x08;		
				Config->Channels[LORA_CHANNEL+Channel].BaudRate = 60;		// Not used (only for SSDV modes)
			}
			
			Temp = ReadInteger(fp, "LORA_SF", Channel, 0, 0);
			if ((Temp >= 6) && (Temp <= 12))
			{
				Config->LoRaDevices[Channel].SpreadingFactor = Temp << 4;
				printf("LoRa Setting SF=%d\n", Temp);
			}

			ReadString(fp, "LORA_Bandwidth", Channel, TempString, sizeof(TempString), 0);
			if (*TempString)
			{
				printf("LoRa Setting BW=%s\n", TempString);
			}
			if (strcmp(TempString, "7K8") == 0)
			{
				Config->LoRaDevices[Channel].Bandwidth = BANDWIDTH_7K8;
			}
			if (strcmp(TempString, "10K4") == 0)
			{
				Config->LoRaDevices[Channel].Bandwidth = BANDWIDTH_10K4;
			}
			if (strcmp(TempString, "15K6") == 0)
			{
				Config->LoRaDevices[Channel].Bandwidth = BANDWIDTH_15K6;
			}
			if (strcmp(TempString, "20K8") == 0)
			{
				Config->LoRaDevices[Channel].Bandwidth = BANDWIDTH_20K8;
			}
			if (strcmp(TempString, "31K25") == 0)
			{
				Config->LoRaDevices[Channel].Bandwidth = BANDWIDTH_31K25;
			}
			if (strcmp(TempString, "41K7") == 0)
			{
				Config->LoRaDevices[Channel].Bandwidth = BANDWIDTH_41K7;
			}
			if (strcmp(TempString, "62K5") == 0)
			{
				Config->LoRaDevices[Channel].Bandwidth = BANDWIDTH_62K5;
			}
			if (strcmp(TempString, "125K") == 0)
			{
				Config->LoRaDevices[Channel].Bandwidth = BANDWIDTH_125K;
			}
			if (strcmp(TempString, "250K") == 0)
			{
				Config->LoRaDevices[Channel].Bandwidth = BANDWIDTH_250K;
			}
			if (strcmp(TempString, "500K") == 0)
			{
				Config->LoRaDevices[Channel].Bandwidth = BANDWIDTH_500K;
			}
			
			if (ReadBoolean(fp, "LORA_Implicit", Channel, 0, &Temp))
			{
				if (Temp)
				{
					Config->LoRaDevices[Channel].ImplicitOrExplicit = IMPLICIT_MODE;
				}
			}
			
			Temp = ReadInteger(fp, "LORA_Coding", Channel, 0, 0);
			if ((Temp >= 5) && (Temp <= 8))
			{
				Config->LoRaDevices[Channel].ErrorCoding = (Temp-4) << 1;
				printf("LoRa Setting Error Coding=%d\n", Temp);
			}

			if (ReadBoolean(fp, "LORA_LowOpt", Channel, 0, &Temp))
			{
				if (Temp)
				{
					Config->LoRaDevices[Channel].LowDataRateOptimize = 0x08;
				}
			}

			Config->LoRaDevices[Channel].Power = ReadInteger(fp, "LORA_Power", Channel, 0, PA_MAX_UK);
			printf("LORA%d power set to %02Xh\n", Channel, Config->LoRaDevices[Channel].Power);

			// Config->LoRaDevices[Channel].PayloadLength = Config->LoRaDevices[Channel].ImplicitOrExplicit == IMPLICIT_MODE ? 255 : 0;
			
			Config->LoRaDevices[Channel].CallingFrequency[0] = '\0';
			ReadString(fp, "LORA_Calling_Frequency", Channel, Config->LoRaDevices[Channel].CallingFrequency, sizeof(Config->LoRaDevices[Channel].CallingFrequency), 0);
		
			if (Config->LoRaDevices[Channel].CallingFrequency[0])
			{
				// Calling frequency enabled
				
				Config->LoRaDevices[Channel].CallingCount = ReadInteger(fp, "LORA_Calling_Count", Channel, 0, 0);
				if (Config->LoRaDevices[Channel].CallingCount)
				{
					printf("LoRa channel %d will Tx on calling frequency %s every %d packets\n", Channel, Config->LoRaDevices[Channel].CallingFrequency, Config->LoRaDevices[Channel].CallingCount);
				}
			}
		}
		else
		{
			Config->LoRaDevices[Channel].InUse = 0;
		}
	}
}
	
void *LoRaLoop(void *some_void_ptr)
{
	int ReturnCode, LoRaChannel;
	unsigned long Sentence_Counter = 0;
	char Sentence[200], Command[100];
	struct stat st = {0};
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
		delay(5);								// To stop this loop gobbling up CPU

		CheckForPacketOnListeningChannels();
		
		LoRaChannel = CheckForFreeChannel(GPS);		// 0 or 1 if there's a free channel and we should be sending on that channel now
		
		if (LoRaChannel >= 0)
		{
			if (Config.LoRaDevices[LoRaChannel].ReturnStateAfterCall)
			{
				double Frequency;
				
				Config.LoRaDevices[LoRaChannel].ReturnStateAfterCall = 0;

				sscanf(Config.LoRaDevices[LoRaChannel].Frequency, "%lf", &Frequency);
				setFrequency(LoRaChannel, Frequency);

				SetLoRaParameters(LoRaChannel,
								  Config.LoRaDevices[LoRaChannel].ImplicitOrExplicit,
								  Config.LoRaDevices[LoRaChannel].ErrorCoding,
								  Config.LoRaDevices[LoRaChannel].Bandwidth,
								  Config.LoRaDevices[LoRaChannel]. SpreadingFactor,
								  Config.LoRaDevices[LoRaChannel].LowDataRateOptimize);
				printf("Reset after Uplink Mode\n");
			}
			
			if (Config.LoRaDevices[LoRaChannel].SendRepeatedPacket == 2)
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
				setFrequency(LoRaChannel, Frequency);
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
				int PacketLength, MaxImagePackets;

				if ((Config.Channels[LORA_CHANNEL+LoRaChannel].SendMode == 0) || (Config.Channels[LORA_CHANNEL+LoRaChannel].ImagePackets == 0))
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
						PacketLength = BuildLoRaSentence(Sentence, LoRaChannel, GPS);
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

				if (Config.Channels[LORA_CHANNEL+LoRaChannel].ImagePackets == 0)
				{
					MaxImagePackets = 0;
				}
				else
				{
					MaxImagePackets = ((GPS->Altitude > Config.SSDVHigh) || (Config.Channels[LORA_CHANNEL+LoRaChannel].BaudRate > 2000)) ? Config.Channels[LORA_CHANNEL+LoRaChannel].ImagePackets : 1;
				}

				if (++Config.Channels[LORA_CHANNEL+LoRaChannel].SendMode > MaxImagePackets)
				{
					Config.Channels[LORA_CHANNEL+LoRaChannel].SendMode = 0;
				}
			}
		}
	}
}
