// Types

typedef enum {fmIdle, fmLaunched, fmDescending, fmHoming, fmDirect, fmDownwind, fmUpwind, fmLanding, fmLanded} TFlightMode;
typedef enum {csUnarmed, csArmed, csAltitude, csPosition, csFlightTime, csBurst, csManual} TCutdownStatus;

struct TGPS
{
	// GPS
	long SecondsInDay;					// Time in seconds since midnight
	int Hours, Minutes, Seconds;
	double Longitude, Latitude;
	int32_t Altitude;
	unsigned int Satellites;
	int Speed;
	int Direction;
	unsigned long SecondsSinceLaunch;					// Time in seconds since midnight
	
	// Calculated from GPS
	int32_t MaximumAltitude, MinimumAltitude;
    double BurstLatitude, BurstLongitude;
	float AscentRate;
	
	// Sensors
	float DS18B20Temperature[2];
	float BatteryVoltage;
	float BMP180Temperature;
	float Humidity;
	float Pressure;
	float BoardCurrent;
	int DS18B20Count;

	// Flight control
	TFlightMode FlightMode;
	
	// Prediction
	double PredictedLongitude, PredictedLatitude;
	float PredictedLandingSpeed;
	int TimeTillLanding;
	float CDA;
		
	// int FlightMode;
	// int PowerMode;
	// int Lock;
	unsigned int MessageCount;
	
	// Cutdown
	TCutdownStatus CutdownStatus;
	

#	ifdef EXTRAS_PRESENT
#		include "ex_gps.h"
#	endif		
};


// functions

void *GPSLoop(void *some_void_ptr);


#ifdef EXTRAS_PRESENT
void gps_postprocess_position(struct TGPS *GPS, int ActionMask, float latitude, float longitude);
void gps_flight_modes(struct TGPS *GPS);
#endif	

