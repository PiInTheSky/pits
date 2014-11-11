// Globals

#include <termios.h>

struct TConfig
{
	char PayloadID[16];
	char Frequency[8];
	int DisableMonitor;
	speed_t TxSpeed;
	int Camera;
	int low_width;
	int low_height;
	int high;
	int high_width;
	int high_height;
	int image_packets;
	int EnableBMP085;
	int EnableGPSLogging;
	int EnableTelemetryLogging;
	int LED_OK;
	int LED_Warn;
	int SDA;
	int SCL;
};

extern struct TConfig Config;

char Hex(char Character);
void WriteLog(char *FileName, char *Buffer);
