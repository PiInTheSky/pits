// Types

struct TGPS
{
	float Time;
	float Longitude, Latitude;
	unsigned int Altitude;
	unsigned int Satellites;
	int Speed;
	int Direction;
	float Temperature;
	float BatteryVoltage;
} GPS;


// functions

void *GPSLoop(void *some_void_ptr);


