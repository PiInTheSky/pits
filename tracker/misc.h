// Globals

#define	MAX_SSDV_PACKETS	4096

#include <termios.h>

typedef enum {lmIdle, lmListening, lmSending} tLoRaMode;

typedef enum {ptNormal, ptCallingMode, ptBalloonRepeat, ptUplinkRepeat} tPacketType;

struct TLoRaDevice
{
	int InUse;
	int DIO0;
	int DIO5;
	int CS;
	int RST;
	char Frequency[8];
    float PPM;
	int SpeedMode;
	int Power;
	int PayloadLength;
	int ImplicitOrExplicit;
	int ErrorCoding;
	int Bandwidth;
	int SpreadingFactor;
	int LowDataRateOptimize;
	int CycleTime;
	int Slot;
	int RepeatSlot;
	int UplinkSlot;
	int Binary;
	int HABPack;
	int LastTxAt;
	int LastRxAt;
	int AirCount;
	int GroundCount;
	int BadCRCCount;
	char LastCommand[256];
	unsigned char PacketToRepeat[256];
	unsigned char UplinkPacket[256];
	int PacketRepeatLength;
	int UplinkRepeatLength;
	tPacketType SendPacketType;
	tLoRaMode LoRaMode;
    int SendingRTTY;
	char CallingFrequency[8];
	int CallingCount;
    int CallingSlot;
	int PacketsSinceLastCall;
	int ReturnStateAfterCall;
	
	// For placing a pause between packets (e.g. to allow another payload to repeat our packets)
	int PacketEveryMilliSeconds;
	int MillisSinceLastPacket;

	// Uplink cycle
	int UplinkPeriod;
	int UplinkCycle;
	
	// Uplink settings
	int UplinkMode;
	double UplinkFrequency;

	// Uplink Messaging
	int EnableMessageStatus;
	int EnableRSSIStatus;
	int LastMessageNumber;
	int MessageCount;
	int LastPacketRSSI;
	int LastPacketSNR;
	int PacketCount;
	int ListenOnly;					// True for listen-only payload that waits for an uplink before responding (or times out and sends anyway)
    
    // RTTY settings
    int RTTYBaudRate;
    double RTTYFrequency;
    int RTTYShift;
    int RTTYCount;
    int RTTYEvery;
    int RTTYPacketIndex;
    int RTTYBitLength;
    int InRTTYMode;
    unsigned int FSKBitRate;
    int FSKOverSample;
	char RTTYBuffer[256];
    int RTTYIndex;
    int RTTYMask;
    int RTTYLength;
    int RTTYPreamble;
	
#	ifdef EXTRAS_PRESENT
#		include "ex_misc_lora.h"
#	endif		
};

struct TSSDVPackets
{
	int ImageNumber;
	int NumberOfPackets;
	int InUse;
	unsigned char Packets[MAX_SSDV_PACKETS];
};

struct TRecentPacket
{
	int ImageNumber;
	int PacketNumber;
};

// Structure for all possible radio devices
// 0 is RTTY
// 1 is APRS
// 2 and 3 are for LoRa
// 4 is a pretend channel for full-size images only
// 5 is for piping data to an external program (e.g. for sending as SMS or directly connecting to habitat)
struct TChannel
{
	int Enabled;
	unsigned int SentenceCounter;
	char PayloadID[16];
	int SendTelemetry;						// TRUE to send telemetry on this channel
	char SSDVFolder[200];
	int ImagePackets;						// Image packets per telemetry packet
	// int ImagePacketCount;					// Image packets since last telemetry packet
	int ImageWidthWhenLow;
	int ImageHeightWhenLow;
	int ImageWidthWhenHigh;
	int ImageHeightWhenHigh;
	int ImagePeriod;						// Time in seconds between photographs
	int	TimeSinceLastImage;
	unsigned int BaudRate;
	char take_pic[100];
	// char current_ssdv[100];
	// char next_ssdv[100];
	char convert_file[100];
	char ssdv_done[100];
	char ssdv_filename[100];
	FILE *ImageFP;
	// int SSDVRecordNumber;
	// int SSDVTotalRecords;
	// int NextSSDVFileReady;
	int ImagesRequested;
	
	// SSDV Variables
	int SSDVImageNumber;					// Image number for last Tx
	int SSDVPacketNumber;					// Packet number for last Tx
	int SSDVNumberOfPackets;				// Number of packets in image currently being sent
	int SSDVFileNumber;						// Number of latest converted image
	
	int SendMode;
	
	// SSDV Packet Log
	struct TSSDVPackets SSDVPackets[3];
};

#define RTTY_CHANNEL 0
#define APRS_CHANNEL 1
#define LORA_CHANNEL 2		// 2 for LoRa CE0 and 3 for LoRa CE1
#define FULL_CHANNEL 4
#define PIPE_CHANNEL 5

struct TConfig
{
	// Misc settings
	int DisableMonitor;
	int InfoMessageCount;
	int BoardType;
	int i2cChannel;
	int DisableADC;
	int32_t BuoyModeAltitude;
	double MaxADCVoltage;
	
	// Camera
	int Camera;	
	int SSDVHigh;
	char CameraSettings[80];
	char SSDVSettings[16];
	
	// Extra devices
	int EnableBMP085;
	int EnableBME280;
    int EnableMS5611;
	int ExternalDS18B20;
	
	// Logging
	int EnableGPSLogging;
	int EnableTelemetryLogging;
	int TelemetryFileUpdate;		// Period in seconds
	
	// LEDs
	int LED_OK;
	int LED_Warn;
	
	// GPS Settings
	int GPSModel;
	int ShowGPS;
	int SDA;
	int SCL;
	char GPSDevice[64];
	
	// RTTY Settings
	int DisableRTTY;
	char Frequency[8];
	speed_t TxSpeed;
	int QuietRTTYDuringLoRaUplink;

	// APRS Settings
	char APRS_Callsign[16];
	int APRS_ID;
	int APRS_Period;
	int APRS_Offset;
	int APRS_Random;
	int APRS_Altitude;
	int APRS_HighPath;
	int APRS_Preemphasis;
	int APRS_Telemetry;
	
	// LoRa Settings
	struct TLoRaDevice LoRaDevices[2];

	// Radio channels
	struct TChannel Channels[6];		// 0 is RTTY, 1 is APRS, 2/3 are LoRa, 4 is for full-size images, 5 is for piping to external software
	
	// GPS
	char GPSSource[128];
	int Power_Saving;
	int Flight_Mode_Altitude;
	
	// Landing prediction
	int EnableLandingPrediction;
	int32_t TargetAltitude;
	
	float cd_area;
	float payload_weight;
	char PredictionID[16];
	
	// External data file (read into telemetry)
	char ExternalDataFileName[100];
    
	// Lights and buzzers
    int BlinkenLight;
    int FlashBelow;
    int Flashing;
    int PiezoPin;
    int WhistleBelow;
	
	// Cutdown
	int EnableCutdown;								// 1 = Enable cutdown tests.  Adds status to telemetry
	int CutdownPin;									// WiringPi pin for cutdown output.  Typically drives nichrome via MOSFET.  No support yet for servo cutdown.
	float CutdownLongitude, CutdownLatitude;		// Used to set where cutdown will fire to limit distance from launch
	int32_t MinCutdownAltitude;						// Cutdown will not fire below this altitude (except for manual uplink)
	int32_t CutdownAltitude;						// Cut down above this altitude
	unsigned long CutdownTimeSinceLaunch;			// Cutdown after this flight timne in seconds
	int CutdownPeriod;								// Time that cutdown is triggered for (manual uplink can override)
	char CutdownTest[16];							// Strting mask that, with lat/lon settings, controls where cutdown will trigger by position
	int CutdownBurst;								// 1 = cutdown once burst detected
	
	// Uplink
	char UplinkCode[32];
		
	
#	ifdef EXTRAS_PRESENT
#		include "ex_misc_config.h"
#	endif		
};

extern struct TConfig Config;

char Hex(unsigned char Character);
void WriteGPSLog(char *Buffer);
void WriteTelemetryLog(char *Buffer);
void WritePredictionLog(char *Buffer);
short open_i2c(int address);
int FileExists(char *filename);
int ReadBooleanFromString(FILE *fp, char *keyword, char *searchword);
int ReadBoolean(FILE *fp, char *keyword, int Channel, int NeedValue, int *Result);
void ReadString(FILE *fp, char *keyword, int Channel, char *Result, int Length, int NeedValue);
int ReadCameraType(FILE *fp, char *keyword);
int ReadInteger(FILE *fp, char *keyword, int Channel, int NeedValue, int DefaultValue);
char ReadCharacter(FILE *fp, char *keyword);
double ReadFloat(FILE *fp, char *keyword, int Channel, int NeedValue, double DefaultValue);
void AppendCRC(char *Temp);
void LogMessage(const char *format, ...);
int devicetree(void);
void ProcessSMSUplinkMessage(int LoRaChannel, unsigned char *Message);
void ProcessSSDVUplinkMessage(int Channel, unsigned char *Message);
void AddImagePacketToRecentList(int Channel, int ImageNumber, int PacketNumber);
int ChooseImagePacketToSend(int Channel);
void StartNewFileIfNeeded(int Channel);
int prog_count(char* name);
int GetBoardType(int *i2cChannel);
int NoMoreSSDVPacketsToSend(int Channel);
int BuildSentence(unsigned char *TxLine, int Channel, struct TGPS *GPS);
int FixDirection180(int Angle);
void SetupPWMFrequency(int Pin, int Frequency);
void ControlPWMOutput(int Pin, int Period);
void DecryptMessage(char *Code, char *Message);
char GetChar(char **Message);
void GetString(char *Field, char **Message);
int32_t GetInteger(char **Message);
double GetFloat(char **Message);
