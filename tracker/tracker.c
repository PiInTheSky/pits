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
| 6 - Sends it to the MTX2/NTX2B radio transmitter                  |
| 7 - repeats steps 2-6                                             |
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
#include <arpa/inet.h>
#include <sys/socket.h>
#include <ifaddrs.h>
#include <sys/statvfs.h>
#include <pigpio.h> 
#include <inttypes.h>

#include "gps.h"
#include "DS18B20.h"
#include "adc.h"
#include "adc_i2c.h"
#include "misc.h"
#include "snapper.h"
#include "led.h"
#include "bmp085.h"
#include "bme280.h"
#include "MS5611.h"
#include "aprs.h"
#include "lora.h"
#include "pipe.h"
#include "prediction.h"
#include "log.h"
#include "cutdown.h"
#include "pin.h"

#ifdef EXTRAS_PRESENT
#	include "ex_tracker.h"
#endif	

struct TConfig Config;

// Pin allocations.  Do not change unless you're using your own hardware
// WIRING PI PINS
#define NTX2B_ENABLE	0
#define UBLOX_ENABLE	2
// BCM PINS
#define NTX2B_ENABLE_BCM	17

int Records, FileNumber;
struct termios options;
char *SSDVFolder="/home/pi/pits/tracker/images";
 




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
	const char* CameraTypes[5] = {"None", "CSI Pi Camera - raspistill", "USB webcam - fswebcam", "USB camera - gphoto2", "Python Script"};
	FILE *fp;
	int BaudRate;
	char *filename = "/boot/pisky.txt";

	if ((fp = fopen(filename, "r")) == NULL)
	{
		printf("\nFailed to open config file %s (error %d - %s).\nPlease check that it exists and has read permission.\n", filename, errno, strerror(errno));
		exit(1);
	}

	ReadBoolean(fp, "disable_monitor", -1, 0, &(Config->DisableMonitor));
	if (Config->DisableMonitor)
	{
		printf("HDMI/Composite outputs will be disabled\n");
	}
	
	ReadBoolean(fp, "Disable_ADC", -1, 0, &(Config->DisableADC));
	ReadBoolean(fp, "Disable_RTTY", -1, 0, &(Config->DisableRTTY));
	Config->Channels[RTTY_CHANNEL].Enabled = !Config->DisableRTTY;
	if (Config->DisableRTTY)
	{
		printf("RTTY Disabled\n");
	}
	else
	{
		ReadString(fp, "payload", -1, Config->Channels[RTTY_CHANNEL].PayloadID, sizeof(Config->Channels[RTTY_CHANNEL].PayloadID), 1);
		printf ("RTTY Payload ID = '%s'\n", Config->Channels[RTTY_CHANNEL].PayloadID);
		
		ReadString(fp, "frequency", -1, Config->Frequency, sizeof(Config->Frequency), 0);

		BaudRate = ReadInteger(fp, "baud", -1, 1, 300);
		
		Config->Channels[RTTY_CHANNEL].BaudRate = BaudRate;
		
		Config->TxSpeed = BaudToSpeed(BaudRate);
		if (Config->TxSpeed == B0)
		{
			printf ("Unknown baud rate %d\nPlease edit in configuration file\n", BaudRate);
			exit(1);
		}
		printf ("Radio baud rate = %d\n", BaudRate);
	}
	
	// Bouy mode for floating trackers
	Config->BuoyModeAltitude = ReadInteger(fp, "buoy_below", -1, 0, 0);
	if (Config->BuoyModeAltitude > 0)
	{
		printf("Buoy mode enabled for altitudes below %" PRId32 " metres\n", Config->BuoyModeAltitude);
	}
	
	// Logging
	Config->EnableGPSLogging = ReadBooleanFromString(fp, "logging", "GPS");
	if (Config->EnableGPSLogging) printf("GPS Logging enabled\n");

	Config->EnableTelemetryLogging = ReadBooleanFromString(fp, "logging", "Telemetry");
	if (Config->EnableTelemetryLogging) printf("Telemetry Logging enabled\n");
	
	Config->TelemetryFileUpdate = ReadInteger(fp, "telemetry_file_update", -1, 0, 0);
	if (Config->TelemetryFileUpdate > 0)
	{
		printf("Telemetry file 'latest.txt' will be created every %d seconds\n", Config->TelemetryFileUpdate);
	}
	
	ReadBoolean(fp, "enable_bmp085", -1, 0, &(Config->EnableBMP085));
	if (Config->EnableBMP085)
	{
		printf("BMP085 Enabled\n");
	}
	
	ReadBoolean(fp, "enable_bme280", -1, 0, &(Config->EnableBME280));
	if (Config->EnableBME280)
	{
		printf("BME280 Enabled\n");
	}

	ReadBoolean(fp, "enable_ms5611", -1, 0, &(Config->EnableMS5611));
	if (Config->EnableMS5611)
	{
		printf("MS5611 Enabled\n");
	}

	ReadString(fp, "pipe_payload", -1, Config->Channels[PIPE_CHANNEL].PayloadID, sizeof(Config->Channels[PIPE_CHANNEL].PayloadID), 0);
	if (Config->Channels[PIPE_CHANNEL].PayloadID[0])
	{
		printf ("PIPE Payload ID = '%s'\n", Config->Channels[PIPE_CHANNEL].PayloadID);
	}

	Config->ExternalDS18B20 = ReadInteger(fp, "external_temperature", -1, 0, 1);
	if (Config->ExternalDS18B20)
	{
		printf("External DS18B20 Enabled\n");
	}
	
	Config->MaxADCVoltage = ReadFloat(fp, "adc_vmax", -1, 0, 18.5);

	Config->Camera = ReadCameraType(fp, "camera");
	printf ("Camera (%s) %s\n", CameraTypes[Config->Camera], Config->Camera ? "Enabled" : "Disabled");
	
	if (Config->Camera)
	{
		ReadString(fp, "camera_settings", -1, Config->CameraSettings, sizeof(Config->CameraSettings), 0);
		if (*Config->CameraSettings)
		{
			printf ("Adding custom camera parameters '%s' to raspistill calls\n", Config->CameraSettings);
		}
		
		Config->SSDVSettings[0] = '\0';
		ReadString(fp, "SSDV_settings", -1, Config->SSDVSettings, sizeof(Config->SSDVSettings), 0);
		if (*Config->SSDVSettings)
		{
			printf ("Adding custom SSDV parameters '%s'\n", Config->SSDVSettings);
		}

		Config->SSDVHigh = ReadInteger(fp, "high", -1, 0, 2000);
		printf ("Image size changes at %dm\n", Config->SSDVHigh);
	
		if (!Config->DisableRTTY)
		{
			Config->Channels[RTTY_CHANNEL].ImageWidthWhenLow = ReadInteger(fp, "low_width", -1, 0, 320);
			Config->Channels[RTTY_CHANNEL].ImageHeightWhenLow = ReadInteger(fp, "low_height", -1, 0, 240);
			printf ("RTTY Low image size %d x %d pixels\n", Config->Channels[RTTY_CHANNEL].ImageWidthWhenLow, Config->Channels[0].ImageHeightWhenLow);
			
			Config->Channels[RTTY_CHANNEL].ImageWidthWhenHigh = ReadInteger(fp, "high_width", -1, 0, 640);
			Config->Channels[RTTY_CHANNEL].ImageHeightWhenHigh = ReadInteger(fp, "high_height", -1, 0, 480);
			printf ("RTTY High image size %d x %d pixels\n", Config->Channels[RTTY_CHANNEL].ImageWidthWhenHigh, Config->Channels[0].ImageHeightWhenHigh);

			Config->Channels[RTTY_CHANNEL].ImagePackets = ReadInteger(fp, "image_packets", -1, 0, 4);
			printf ("RTTY: 1 Telemetry packet every %d image packets\n", Config->Channels[RTTY_CHANNEL].ImagePackets);
			
			Config->Channels[RTTY_CHANNEL].ImagePeriod = ReadInteger(fp, "image_period", -1, 0, 60);
			printf ("RTTY: %d seconds between photographs\n", Config->Channels[RTTY_CHANNEL].ImagePeriod);
		}

		// Set up full-size image parameters		
		Config->Channels[FULL_CHANNEL].ImageWidthWhenLow = ReadInteger(fp, "full_low_width", -1, 0, 640);
		Config->Channels[FULL_CHANNEL].ImageHeightWhenLow = ReadInteger(fp, "full_low_height", -1, 0, 480);
		printf ("Full Low image size %d x %d pixels\n", Config->Channels[FULL_CHANNEL].ImageWidthWhenLow, Config->Channels[FULL_CHANNEL].ImageHeightWhenLow);
		
		Config->Channels[FULL_CHANNEL].ImageWidthWhenHigh = ReadInteger(fp, "full_high_width", -1, 0, 2592);
		Config->Channels[FULL_CHANNEL].ImageHeightWhenHigh = ReadInteger(fp, "full_high_height", -1, 0, 1944);
		printf ("Full High image size %d x %d pixels\n", Config->Channels[FULL_CHANNEL].ImageWidthWhenHigh, Config->Channels[FULL_CHANNEL].ImageHeightWhenHigh);

		Config->Channels[FULL_CHANNEL].ImagePeriod = ReadInteger(fp, "full_image_period", -1, 0, 60);
		printf ("Full size: %d seconds between photographs\n", Config->Channels[FULL_CHANNEL].ImagePeriod);
		
		Config->Channels[FULL_CHANNEL].ImagePackets = Config->Channels[FULL_CHANNEL].ImagePeriod > 0;
		Config->Channels[FULL_CHANNEL].Enabled = Config->Channels[FULL_CHANNEL].ImagePackets;
	}

	// GPS
	Config->GPSSource[0] = '\0';
	ReadString(fp, "gps_source", -1, Config->GPSSource, sizeof(Config->GPSSource), 0);
	ReadBoolean(fp, "Power_Saving", -1, 0, &(Config->Power_Saving));
	printf("GPS Power Saving = %s\n", Config->Power_Saving ? "ON" : "OFF");
	Config->Flight_Mode_Altitude = ReadInteger(fp, "Flight_Mode_Altitude", -1, 0, 1000);
	if (Config->Flight_Mode_Altitude) printf("Switching GPS to flight mode above %d metres\n", Config->Flight_Mode_Altitude);
	
	// Landing prediction
	Config->EnableLandingPrediction = 0;
	ReadBoolean(fp, "landing_prediction", -1, 0, &(Config->EnableLandingPrediction));
	if (Config->EnableLandingPrediction)
	{
		Config->cd_area = ReadFloat(fp, "cd_area", -1, 0, 0.66);
		Config->payload_weight = ReadFloat(fp, "payload_weight", -1, 0, 0.66);
		Config->TargetAltitude = ReadInteger(fp, "Target_Altitude", -1, 0, 0);
		ReadString(fp, "prediction_id", -1, Config->PredictionID, sizeof(Config->PredictionID), 0);
	}

	ReadString(fp, "Uplink_Code", -1, Config->UplinkCode, sizeof(Config->UplinkCode), 0);
	if (*Config->UplinkCode)
	{
		printf("Uplink code is '%s'\n", Config->UplinkCode);
	}
	
	ReadBoolean(fp, "Enable_Cutdown", -1, 0, &(Config->EnableCutdown));
	if (Config->EnableCutdown)
	{
		printf("Cutdown - enabled\n");
		
		Config->MinCutdownAltitude = ReadInteger(fp, "Min_Cutdown_Altitude", -1, 0, 0);
		if (Config->MinCutdownAltitude > 0)
		{
			printf("        - inactive below %" PRId32 " metres\n", Config->MinCutdownAltitude);
		}
		Config->CutdownAltitude = ReadInteger(fp, "Cutdown_Altitude", -1, 0, 0);
		if (Config->CutdownAltitude > 0)
		{
			printf("        - will trigger at an altitude of %" PRId32 " metres\n", Config->CutdownAltitude);
		}
		
		Config->CutdownLatitude = ReadFloat(fp, "Cutdown_Latitude", -1, 0, 0);
		Config->CutdownLongitude = ReadFloat(fp, "Cutdown_Longitude", -1, 0, 0);

		ReadString(fp, "Cutdown_Test", -1, Config->CutdownTest, sizeof(Config->CutdownTest), 0);
		if (strcasecmp("N", Config->CutdownTest) == 0)
		{
			printf("        - will trigger North of latitude %.5f\n", Config->CutdownLatitude);
		}
		else if (strcasecmp("NE", Config->CutdownTest) == 0)
		{
			printf("        - will trigger North-East of latitude %.5f, longitude %.5f\n", Config->CutdownLatitude, Config->CutdownLongitude);
		}
		else if (strcasecmp("E", Config->CutdownTest) == 0)
		{
			printf("        - will trigger East of latitude %.5f\n", Config->CutdownLongitude);
		}
		else if (strcasecmp("SE", Config->CutdownTest) == 0)
		{
			printf("        - will trigger South-East of latitude %.5f, longitude %.5f\n", Config->CutdownLatitude, Config->CutdownLongitude);
		}
		else if (strcasecmp("S", Config->CutdownTest) == 0)
		{
			printf("        - will trigger South of latitude %.5f\n", Config->CutdownLatitude);
		}
		else if (strcasecmp("SW", Config->CutdownTest) == 0)
		{
			printf("        - will trigger South-West of latitude %.5f, longitude %.5f\n", Config->CutdownLatitude, Config->CutdownLongitude);
		}
		else if (strcasecmp("W", Config->CutdownTest) == 0)
		{
			printf("        - will trigger West of latitude %.5f\n", Config->CutdownLongitude);
		}
		else if (strcasecmp("NW", Config->CutdownTest) == 0)
		{
			printf("        - will trigger North-West of latitude %.5f, longitude %.5f\n", Config->CutdownLatitude, Config->CutdownLongitude);
		}

		Config->CutdownTimeSinceLaunch = ReadInteger(fp, "Cutdown_Time_Since_Launch", -1, 0, 0);
		if (Config->CutdownTimeSinceLaunch > 0)
		{
			printf("        - will trigger after a flight time of %lu minutes\n", Config->CutdownTimeSinceLaunch);
			Config->CutdownTimeSinceLaunch *= 60;
		}
		
		ReadBoolean(fp, "Cutdown_Burst", -1, 0, &(Config->CutdownBurst));
		if (Config->CutdownBurst)
		{
			printf("        - will trigger after balloon burst detected\n");
		}
		
		Config->CutdownPeriod = ReadInteger(fp, "Cutdown_Period", -1, 0, 5);
		printf("        - period (on time) is %d seconds\n", Config->CutdownPeriod);
		
		Config->CutdownPin = ReadInteger(fp, "Cutdown_Pin", -1, 0, 22);
		printf("        - pin is %d\n", Config->CutdownPin);
	}
	
#	ifdef EXTRAS_PRESENT
		tracker_load_config(fp, Config);
#	endif
	// External data file
	Config->ExternalDataFileName[0] = '\0';
	ReadString(fp, "external_data", -1, Config->ExternalDataFileName, sizeof(Config->ExternalDataFileName), 0);

	// GPS model
	Config->GPSModel = ReadCharacter(fp, "gps_model");
	if (Config->GPSModel == '\0')
	{
		Config->GPSModel = 'U';			// Default UBlox
	}
	
	// Show GPS NMEA ?
	Config->ShowGPS = 0;
	ReadBoolean(fp, "show_gps", -1, 0, &(Config->ShowGPS));
	
	// Serial GPS
	Config->GPSDevice[0] = '\0';
	ReadString(fp, "gps_device", -1, Config->GPSDevice, sizeof(Config->GPSDevice), 0);
	
	if (!Config->GPSDevice[0])
	{
		// I2C overrides.  Only needed for users own boards, or for some of our prototypes
		if (ReadInteger(fp, "SDA", -1, 0, 0))
		{
			Config->SDA = ReadInteger(fp, "SDA", -1, 0, 0);
			printf ("I2C SDA overridden to %d\n", Config->SDA);
		}

		if (ReadInteger(fp, "SCL", -1, 0, 0))
		{
			Config->SCL = ReadInteger(fp, "SCL", -1, 0, 0);
			printf ("I2C SCL overridden to %d\n", Config->SCL);
		}
	}
	
	Config->InfoMessageCount = ReadInteger(fp, "info_messages", -1, 0, -1);

	Config->QuietRTTYDuringLoRaUplink = 0;
	ReadBoolean(fp, "quiet_rtty_for_uplink", -1, 0, &(Config->QuietRTTYDuringLoRaUplink));
	
	Config->BlinkenLight = ReadInteger(fp, "Strobe_Pin", -1, 0, -1);
	if (Config->BlinkenLight >= 0)
	{
		Config->FlashBelow = ReadInteger(fp, "Strobe_Alt", -1, 0, Config->SSDVHigh);
		Config->Flashing = 0;

		printf("Enabled PCM Strobe Light on pin %d after descending below %d metres\n", Config->BlinkenLight, Config->FlashBelow);
	}
	
	Config->PiezoPin = ReadInteger(fp, "Piezo_Pin", -1, 0, -1);
	if (Config->PiezoPin >= 0)
	{
		Config->WhistleBelow = ReadInteger(fp, "Piezo_Alt", -1, 0, Config->SSDVHigh);

		printf("Enabled Piezo Buzzer on pin %d after descending below %d metres\n", Config->PiezoPin, Config->WhistleBelow);
	}

	LoadAPRSConfig(fp, Config);
	
	LoadLoRaConfig(fp, Config);
	
	fclose(fp);
}

void SetMTX2Frequency(char *FrequencyString)
{
	float _mtx2comp;
	int _mtx2int;
	long _mtx2fractional;
	char _mtx2command[17];
	double Frequency;
	int wave_id;
	
	if (strlen(FrequencyString) < 3)
	{
		// Convert channel number to frequency
		Frequency = strtol(FrequencyString, NULL, 16) * 0.003125 + 434.05;
		printf("Channel %s\n", FrequencyString);
	}
	else
	{
		Frequency = atof(FrequencyString);
		printf("Frequency %s\n", FrequencyString);
	}

	printf("MTX2 Frequency to be set to %8.4fMHz\n", Frequency);

	_mtx2comp=(Frequency+0.0015)/6.5;
	_mtx2int=_mtx2comp;
	_mtx2fractional = ((_mtx2comp-_mtx2int)+1) * 524288;
	snprintf(_mtx2command,17,"@PRG_%02X%06lX\r",_mtx2int-1, _mtx2fractional);
	printf("MTX2 command  is %s\n", _mtx2command);
	
	gpioSetMode(NTX2B_ENABLE_BCM, PI_OUTPUT);	
	
	gpioWaveAddNew();
	
	gpioWaveAddSerial(NTX2B_ENABLE_BCM, 9600, 8, 2, 0, strlen(_mtx2command), _mtx2command);
	
	wave_id = gpioWaveCreate();

	if (wave_id >= 0)
	{
		gpioWaveTxSend(wave_id, 0);

		while (gpioWaveTxBusy())
		{
			time_sleep(0.1);
		}
	}
	
	// gpioTerminate();
}

char *SerialPortName(void)
{
	// Put this here in case the serial port name changes sometime
	
	return "/dev/ttyAMA0";
}

void SetNTX2BFrequency(char *FrequencyString)
{
	int fd, Frequency;
	char Command[16];
	struct termios options;
	uint8_t setNMEAoff[] = {0xB5, 0x62, 0x06, 0x00, 0x14, 0x00, 0x01, 0x00, 0x00, 0x00, 0xD0, 0x08, 0x00, 0x00, 0x80, 0x25, 0x00, 0x00, 0x07, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0xA0, 0xA9           };

	// First disable transmitter
	digitalWrite (NTX2B_ENABLE, 0);
	pinMode (NTX2B_ENABLE, OUTPUT);
	delay(200);
	
	fd = open(SerialPortName(), O_WRONLY | O_NOCTTY);
	if (fd >= 0)
	{
		tcgetattr(fd, &options);

		cfsetispeed(&options, B4800);
		cfsetospeed(&options, B4800);

		options.c_cflag &= ~CSTOPB;
		options.c_cflag |= CS8;
		options.c_oflag &= ~ONLCR;
		options.c_oflag &= ~OPOST;
		options.c_iflag &= ~IXON;
		options.c_iflag &= ~IXOFF;
		options.c_lflag &= ~ECHO;
		options.c_cc[VMIN]  = 0;
		options.c_cc[VTIME] = 10;
		
		tcsetattr(fd, TCSANOW, &options);

		// Tel UBlox to shut up
		write(fd, setNMEAoff, sizeof(setNMEAoff));
		tcsetattr(fd, TCSAFLUSH, &options);
		close(fd);
		delay(1000);
		
		fd = open(SerialPortName(), O_WRONLY | O_NOCTTY);
		
		if (strlen(FrequencyString) < 3)
		{
			// Already a channel number
			Frequency = strtol(FrequencyString, NULL, 16);
		}
		else
		{
			// Convert from MHz to channel number
			Frequency = (int)((atof(FrequencyString) - 434.05) / 0.003124);
		}
		
		sprintf(Command, "%cch%02X\r", 0x80, Frequency);

		printf("NTX2B-FA transmitter now set to channel %02Xh which is %8.4lfMHz\n", Frequency, (double)(Frequency) * 0.003125 + 434.05);

		// Let enable line float (but Tx will pull it up anyway)
		delay(200);
		pinMode (NTX2B_ENABLE, INPUT);
		pullUpDnControl(NTX2B_ENABLE, PUD_OFF);
		delay(20);

		write(fd, Command, strlen(Command)); 
		tcsetattr(fd, TCSAFLUSH, &options);
		delay(50);

		close(fd);

		// Switch on the radio
		delay(100);
		digitalWrite (NTX2B_ENABLE, 1);
		pinMode (NTX2B_ENABLE, OUTPUT);
	}
}


void SetFrequency(char *Frequency)
{
	if (Config.BoardType)
	{
		SetMTX2Frequency(Frequency);
		SetMTX2Frequency(Frequency);
	}
	else
	{
		SetNTX2BFrequency(Frequency);
	}
}

int OpenSerialPort(void)
{
	int fd;

	fd = open(SerialPortName(), O_WRONLY | O_NOCTTY);	// O_NDELAY);
	if (fd >= 0)
	{
		/* get the current options */
		tcgetattr(fd, &options);

		/* set raw input */
		options.c_lflag &= ~ECHO;
		options.c_cc[VMIN]  = 0;
		options.c_cc[VTIME] = 10;

		cfsetispeed(&options, Config.TxSpeed);
		cfsetospeed(&options, Config.TxSpeed);
		options.c_cflag |= CSTOPB;
		options.c_cflag &= ~CSIZE;
		if (Config.TxSpeed == B50)
		{
			options.c_cflag |= CS7;
		}
		else
		{
			options.c_cflag |= CS8;
		}
		options.c_oflag &= ~ONLCR;
		options.c_oflag &= ~OPOST;
		options.c_iflag &= ~IXON;
		options.c_iflag &= ~IXOFF;
	
		tcsetattr(fd, TCSANOW, &options);
	}

	return fd;
}

void SendSentence(int fd, char *TxLine)
{
	write(fd, TxLine, strlen(TxLine));

	// Log now while we're waiting for the serial port, to eliminate or at least reduce downtime whilst logging
	WriteTelemetryLog(TxLine);
	
	// Wait till those characters get sent
	tcsetattr(fd, TCSAFLUSH, &options);
}

int SendRTTYImage(int fd)
{
    unsigned char Buffer[256];
    size_t Count;
    int SentSomething = 0;

	StartNewFileIfNeeded(RTTY_CHANNEL);
	
	ChooseImagePacketToSend(RTTY_CHANNEL);
	
    if (Config.Channels[RTTY_CHANNEL].ImageFP != NULL)
    {
        Count = fread(Buffer, 1, 256, Config.Channels[RTTY_CHANNEL].ImageFP);
        if (Count > 0)
        {
            // printf("RTTY: SSDV record %d of %d\r\n", Config.Channels[RTTY_CHANNEL].SSDVPacketNumber++, Config.Channels[RTTY_CHANNEL].SSDVNumberOfPackets);
            printf("RTTY: SSDV record %d of %d\r\n", Config.Channels[RTTY_CHANNEL].SSDVPacketNumber, Config.Channels[RTTY_CHANNEL].SSDVNumberOfPackets);

			write(fd, Buffer, Count);

			tcsetattr(fd, TCSAFLUSH, &options);

            SentSomething = 1;
        }
        else
        {
            fclose(Config.Channels[RTTY_CHANNEL].ImageFP);
            Config.Channels[RTTY_CHANNEL].ImageFP = NULL;
        }
    }

    return SentSomething;
}

void SendIPAddress(int fd)
{
    struct ifaddrs *ifap, *ifa;
    struct sockaddr_in *sa;
    char *addr;

    if (getifaddrs(&ifap) == 0)
	{
		for (ifa = ifap; ifa; ifa = ifa->ifa_next)
		{
			if (ifa->ifa_addr != NULL)
			{
				// Family is known (which it isn't for a VPN)
				if (ifa->ifa_addr->sa_family==AF_INET)
				{
					sa = (struct sockaddr_in *) ifa->ifa_addr;
					addr = inet_ntoa(sa->sin_addr);
					if (strcmp(addr, "127.0.0.1") != 0)
					{
						char Sentence[200];
						
						sprintf(Sentence, "Interface %s has IP Address: %s\n", ifa->ifa_name, addr);
						printf(Sentence);
						SendSentence(fd, Sentence);
					}
				}
			}
        }
    }

    freeifaddrs(ifap);
}

void SendFreeSpace(int fd)
{
	struct statvfs vfs;

	if (statvfs("/home", &vfs) == 0)
	{
		char Sentence[200];
		
		sprintf(Sentence, "Free SD space = %.1fMB\n", (float)vfs.f_bsize * (float)vfs.f_bfree / (1024 * 1024));
		printf(Sentence);
		SendSentence(fd, Sentence);
	}
}

int LoRaChannelUploadNow(int LoRaChannel, struct TGPS *GPS, int PacketTime)
{
	// Can't use time till we have it
	if ((Config.LoRaDevices[LoRaChannel].UplinkCycle > 0) && (Config.LoRaDevices[LoRaChannel].UplinkPeriod > 0))
	{
		int i;
		long CycleSeconds;
		
		for (i=0; i<=PacketTime; i++)
		{
			CycleSeconds = (GPS->SecondsInDay+i) % Config.LoRaDevices[LoRaChannel].UplinkCycle;
	
			if (CycleSeconds < Config.LoRaDevices[LoRaChannel].UplinkPeriod)
			{
				return 1;
			}
		}
	}
	
	return 0;
}

int LoRaUploadNow(struct TGPS *GPS, int PacketTime)
{
	// Can't use time till we have it
	if (Config.QuietRTTYDuringLoRaUplink && (GPS->Satellites > 0)) // && (GPS->Altitude > Config.SSDVHigh))
	{
		return (LoRaChannelUploadNow(0, GPS, PacketTime) || LoRaChannelUploadNow(1, GPS, PacketTime));
	}
	
	return 0;
}

int main(void)
{
	int fd=0;
	int i;
	int ImagePacketCount, MaxImagePackets;
	unsigned char Sentence[200];
	struct stat st = {0};
	struct TGPS GPS;
	pthread_t PredictionThread, LoRaThread, APRSThread, GPSThread, DS18B20Thread, ADCThread, CameraThread, BMP085Thread, BME280Thread, MS5611Thread, LEDThread, CutdownThread, PinThread, LogThread, PipeThread;
	if (prog_count("tracker") > 1)
	
	{
		printf("\nThe tracker program is already running!\n");
		printf("It is started automatically, with the camera script, when the Pi boots.\n\n");
		printf("If you just want the tracker software to run, it already is,\n");
		printf("and its output can be viewed on a monitor attached to a Pi video socket.\n\n");
		printf("If instead you want to view the tracker output via ssh,\n");
		printf("then you should first stop it by typing the following command:\n");
		printf("	sudo killall tracker\n\n");
		printf("and then restart manually with\n");
		printf("	sudo ./tracker\n\n");
		exit(1);
	}
	
	printf("\n\nRASPBERRY PI-IN-THE-SKY FLIGHT COMPUTER\n");
	printf(    "=======================================\n\n");

	Config.BoardType = GetBoardType(&Config.i2cChannel);

	if (Config.BoardType)
	{
		if (Config.BoardType == 4)
		{
			printf("RPi Zero W\n");
			printf("PITS Zero Board\n");
		}
		else if (Config.BoardType == 3)
		{
			printf("RPi Zero\n");
			printf("PITS Zero Board\n");
		}
		else
		{
			if (Config.BoardType == 2)
			{
				printf("RPi 3 B\n");
			}
			else
			{
				printf("RPi Model A+ or B+ or B V2\n");
			}
			printf("PITS+ Board\n");
		}
				
		Config.LED_OK = 25;
		Config.LED_Warn = 24;
		
		Config.SDA = 2;
		Config.SCL = 3;
	}
	else
	{
		printf("RPi Model A or B\n");
		printf("PITS Board\n");

		Config.LED_OK = 11;
		Config.LED_Warn = 4;
		
		Config.SDA = 5;
		Config.SCL = 6;
	}
	
	LoadConfigFile(&Config);

	if (Config.DisableMonitor)
	{
		system("/opt/vc/bin/tvservice -off");
	}
	
	if (FileExists("/boot/clear.txt"))
	{
		// remove SSDV and other camera images, plus log files

		printf("Removing existing photo files\n");
		remove("gps.txt");
		remove("telemetry.txt");
		remove("/boot/clear.txt");
		system("rm -rf /home/pi/pits/tracker/images/*");
	}
		
	// Remove any old SSDV files
	system("rm -f ssdv*.bin");

	memset(&GPS, 0, sizeof(struct TGPS));
	
	// Set up I/O
	if (wiringPiSetup() == -1)
	{
		printf("Cannot initialise WiringPi\n");
		exit (1);
	}
	
	if (gpioInitialise() < 0)
	{
		printf("pigpio initialisation failed.\n");
		exit (1);
	}	

	// Switch off the radio till it's configured
	pinMode (NTX2B_ENABLE, OUTPUT);
	digitalWrite (NTX2B_ENABLE, 0);
		
	// Switch on the GPS
	if (Config.BoardType == 0)
	{
		// Only PITS board had this, not PITS+
		pinMode (UBLOX_ENABLE, OUTPUT);
		digitalWrite (UBLOX_ENABLE, 0);
	}
	
		
	if (!Config.DisableRTTY)
	{
		if (*Config.Frequency)
		{
			SetFrequency(Config.Frequency);
		}
	
		fd = OpenSerialPort();

		if (fd < 0)
		{
			printf("\nFailed to open serial port for RTTY.  Please check the serial port steps in the PITS manual.\n");
			if (Config.BoardType == 4)
			{
				printf("As this is a Pi Zero W, make sure that you followed the special steps for the Pi Zero W / Pi 3 B\n");
			}
			if (Config.BoardType == 2)
			{
				printf("As this is a Pi 3 B, make sure that you followed the special steps for the Pi Zero W / Pi 3 B\n");
			}

			exit(1);
		}
		
		digitalWrite (NTX2B_ENABLE, 1);
	}
	
	
	// Turn strobe light off
	if (Config.BlinkenLight >= 0)
	{
		SetupPWMFrequency(Config.BlinkenLight, 50);
		ControlPWMOutput(Config.BlinkenLight, 1000);
	}
	
	// Turn piezo buzzer off
	if (Config.PiezoPin >= 0)
	{
		digitalWrite (Config.PiezoPin, 0);
		pinMode (Config.PiezoPin, OUTPUT);
	}
	
			
	// SSDV Folders
	sprintf(Config.Channels[0].SSDVFolder, "%s/RTTY", SSDVFolder);
	*Config.Channels[1].SSDVFolder = '\0';										// No folder for APRS images
	sprintf(Config.Channels[2].SSDVFolder, "%s/LORA0", SSDVFolder);
	sprintf(Config.Channels[3].SSDVFolder, "%s/LORA1", SSDVFolder);
	sprintf(Config.Channels[4].SSDVFolder, "%s/FULL", SSDVFolder);
		
	if (Config.Camera)
	{
		// Create SSDV Folders
		if (stat(SSDVFolder, &st) == -1)
		{
			mkdir(SSDVFolder, 0777);
		}	
	
		for (i=0; i<5; i++)
		{
			if (*Config.Channels[i].SSDVFolder)
			{
				if (stat(Config.Channels[i].SSDVFolder, &st) == -1)
				{
					mkdir(Config.Channels[i].SSDVFolder, 0777);
				}
			}
		}

		// Filenames for SSDV
		for (i=0; i<5; i++)
		{
			sprintf(Config.Channels[i].take_pic, "take_pic_%d", i);
			// sprintf(Config.Channels[i].current_ssdv, "ssdv_%d.bin", i);
			// sprintf(Config.Channels[i].next_ssdv, "ssdv_%d.nxt", i);
			sprintf(Config.Channels[i].convert_file, "convert_%d", i);
			sprintf(Config.Channels[i].ssdv_done, "ssdv_done_%d", i);
			
			Config.Channels[i].SSDVImageNumber = -1;
			Config.Channels[i].SSDVPacketNumber = -1;
			
			Config.Channels[i].ImageFP = NULL;
		}
	}
	
	if (pthread_create(&GPSThread, NULL, GPSLoop, &GPS))
	{
		fprintf(stderr, "Error creating GPS thread\n");
		return 1;
	}

	if (*(Config.APRS_Callsign) && Config.APRS_ID && Config.APRS_Period)
	{
		if (pthread_create(&APRSThread, NULL, APRSLoop, &GPS))
		{
			fprintf(stderr, "Error creating APRS thread\n");
			return 1;
		}
	}
	
	if (Config.LoRaDevices[0].InUse || Config.LoRaDevices[1].InUse)
	{
		if (pthread_create(&LoRaThread, NULL, LoRaLoop, &GPS))
		{
			fprintf(stderr, "Error creating LoRa thread\n");
		}
	}
	
	if (pthread_create(&DS18B20Thread, NULL, DS18B20Loop, &GPS))
	{
		fprintf(stderr, "Error creating DS18B20s thread\n");
		return 1;
	}

	if ((Config.BoardType != 3) && (Config.BoardType != 4) && (!Config.DisableADC))
	{
		// Not a zero, so should have ADC on it
		if (I2CADCExists())
		{
			printf ("V2.4 or later board with I2C ADC\n");
			
			if (pthread_create(&ADCThread, NULL, I2CADCLoop, &GPS))
			{
				fprintf(stderr, "Error creating ADC thread\n");
				return 1;
			}
		}
		else
		{
			printf ("Older board with SPI ADC\n");
			
			if (Config.LoRaDevices[0].InUse)
			{
				printf ("Disabling SPI ADC code as LoRa CE0 is enabled!!\n");
				Config.DisableADC = 1;
			}
			else
			{
				if (pthread_create(&ADCThread, NULL, ADCLoop, &GPS))
				{
					fprintf(stderr, "Error creating ADC thread\n");
					return 1;
				}
			}
		}
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
	
	if (pthread_create(&CutdownThread, NULL, CutdownLoop, &GPS))
	{
		fprintf(stderr, "Error creating cutdown thread\n");
		return 1;
	}

	if (pthread_create(&PinThread, NULL, PinLoop, &GPS))
	{
		fprintf(stderr, "Error creating pin thread\n");
		return 1;
	}

	if (Config.TelemetryFileUpdate > 0)
	{
		if (pthread_create(&LogThread, NULL, LogLoop, &GPS))
		{
			fprintf(stderr, "Error creating Log thread\n");
			return 1;
		}
	}
	
	if (Config.EnableBMP085)
	{
		if (pthread_create(&BMP085Thread, NULL, BMP085Loop, &GPS))
		{
			fprintf(stderr, "Error creating BMP085 thread\n");
			return 1;
		}
	}

	if (Config.EnableBME280)
	{
		if (pthread_create(&BME280Thread, NULL, BME280Loop, &GPS))
		{
			fprintf(stderr, "Error creating BME280 thread\n");
			return 1;
		}
	}
		
	if (Config.EnableMS5611)
	{
		if (pthread_create(&MS5611Thread, NULL, MS5611Loop, &GPS))
		{
			fprintf(stderr, "Error creating MS5611 thread\n");
			return 1;
		}
	}
		
	if (Config.Channels[PIPE_CHANNEL].PayloadID[0])
	{
		if (pthread_create(&PipeThread, NULL, PipeLoop, &GPS))
		{
			fprintf(stderr, "Error creating pipe thread\n");
			return 1;
		}
	}

	if (Config.EnableLandingPrediction)
	{
		if (pthread_create(&PredictionThread, NULL, PredictionLoop, &GPS))
		{
			fprintf(stderr, "Error creating prediction thread\n");
		}
	}	
	
#	ifdef EXTRAS_PRESENT
		tracker_load_threads(&GPS);
#	endif
	if (!Config.DisableRTTY && (fd >= 0))
	{
		if (Config.InfoMessageCount < 0)
		{
			// Default number depends on baud rate
			Config.InfoMessageCount = (Config.TxSpeed < B300) ? 2 : 4;
		}
	
		for (i=0; i<Config.InfoMessageCount; i++)
		{
			SendIPAddress(fd);
			SendFreeSpace(fd);
		}
	}

	ImagePacketCount = 0;
	
	while (1)
	{
		static int CarrierOn=1;
		
		if (Config.DisableRTTY || (fd < 0))
		{
			delay(200);
		}
		else if (LoRaUploadNow(&GPS, 10))
		{
			if (CarrierOn)
			{
				digitalWrite (NTX2B_ENABLE, 0);
				CarrierOn = 0;
				printf("Switching RTTY carrier off\n");
			}
			delay(200);
		}
		else
		{
			if (!CarrierOn)
			{
				digitalWrite (NTX2B_ENABLE, 1);
				printf("Switching RTTY carrier on\n");
				CarrierOn = 1;
			}
			
			MaxImagePackets = (GPS.Altitude > Config.SSDVHigh) ? Config.Channels[RTTY_CHANNEL].ImagePackets : 1;
			
			if (ImagePacketCount++ < MaxImagePackets)
			{
				SendRTTYImage(fd);
			}
			else
			{
				ImagePacketCount = 0;
				
				BuildSentence(Sentence, RTTY_CHANNEL, &GPS);

				LogMessage("RTTY: %.70s", Sentence);

				SendSentence(fd, (char *)Sentence);
			}
		}
	}
	
	gpioTerminate();
}
