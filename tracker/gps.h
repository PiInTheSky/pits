// Types

struct TGPS
{
	float Time;
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


