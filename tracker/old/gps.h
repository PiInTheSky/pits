// Types

struct TGPS
{
	float Time;
	float Longitude, Latitude;
	unsigned int Altitude;
	unsigned int Satellites;
	int Speed;
	int Direction;
	float InternalTemperature;
	float BatteryVoltage;
	float ExternalTemperature;
	float Pressure;
	float BoardCurrent;
} GPS;


// functions

void *GPSLoop(void *some_void_ptr);


