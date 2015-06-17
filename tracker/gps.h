// Types

struct TGPS
{
	long Time;
	long Seconds;					// Time in seconds since midnight.  Used for APRS timing, and LoRa timing in TDM mode
	float Longitude, Latitude;
	unsigned int Altitude;
	unsigned int Satellites;
	int Speed;
	int Direction;
	float DS18B20Temperature[2];
	float BatteryVoltage;
	float BMP180Temperature;
	float Pressure;
	float BoardCurrent;
	int DS18B20Count;
} GPS;


// functions

void *GPSLoop(void *some_void_ptr);


