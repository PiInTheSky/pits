#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <math.h>
#include <inttypes.h>

#include "gps.h"
#include "misc.h"

#define DEG2RAD (3.142 / 180)
#define SLOTSIZE 100
#define POLL_PERIOD 10

struct TPosition
{
	double LatitudeDelta;
	double LongitudeDelta;
};

struct TPosition Positions[45000 / SLOTSIZE];		// 100m slots from 0 to 45km

double CalculateAirDensity(double Altitude)
{
	double Temperature, Pressure;

    if (Altitude < 11000.0)
	{
        // below 11Km - Troposphere
        Temperature = 15.04 - (0.00649 * Altitude);
        Pressure = 101.29 * pow((Temperature + 273.1) / 288.08, 5.256);
	}
    else if (Altitude < 25000.0)
	{
        // between 11Km and 25Km - lower Stratosphere
        Temperature = -56.46;
        Pressure = 22.65 * exp(1.73 - ( 0.000157 * Altitude));
	}
    else
	{
        // above 25Km - upper Stratosphere
        Temperature = -131.21 + (0.00299 * Altitude);
        Pressure = 2.488 * pow((Temperature + 273.1) / 216.6, -11.388);
    }

    return Pressure / (0.2869 * (Temperature + 273.1));
}

double CalculateDescentRate(double Weight, double CDTimesArea, double Altitude)
{
	double Density;
	
	Density = CalculateAirDensity(Altitude);
	
    return sqrt((Weight * 9.81)/(0.5 * Density * CDTimesArea));
}

double CalculateCDA(double Weight, double Altitude, double DescentRate)
{
	double Density;
	
	Density = CalculateAirDensity(Altitude);
	
	printf("Alt %lf, Rate %lf, CDA %lf\n", Altitude, DescentRate, (Weight * 9.81)/(0.5 * Density * DescentRate * DescentRate));

    return (Weight * 9.81)/(0.5 * Density * DescentRate * DescentRate);
}

void *PredictionLoop(void *some_void_ptr)
{
	struct TGPS *GPS;
	double PreviousLatitude, PreviousLongitude, CDA;
	unsigned long PreviousAltitude;

	GPS = (struct TGPS *)some_void_ptr;
	
	PreviousLatitude = 0;
	PreviousLongitude = 0;
	PreviousAltitude = 0;
	
	CDA = Config.cd_area;

	while (1)
	{
		sleep(POLL_PERIOD);
		
		if (GPS->Satellites >= 4)
		{
			if (PreviousAltitude > 0)
			{
				int Slot;
				double Latitude, Longitude, TimeInSlot, DescentRate, TimeTillLanding;
				unsigned long Altitude, DistanceInSlot;
				char Temp[200];
				
				// Up or down ?
				if (GPS->Altitude > PreviousAltitude)
				{
					// Going up - store deltas
					Slot = (GPS->Altitude/2 + PreviousAltitude/2) / SLOTSIZE;
					
					// Deltas are scaled to be horizontal distance per second (i.e. speed)
					Positions[Slot].LatitudeDelta = (GPS->Latitude - PreviousLatitude) / POLL_PERIOD;
					// Positions[Slot].LongitudeDelta = (GPS->Longitude - PreviousLongitude) * cos(((GPS->Latitude + PreviousLatitude)/2) * DEG2RAD) / POLL_PERIOD;
					Positions[Slot].LongitudeDelta = (GPS->Longitude - PreviousLongitude) / POLL_PERIOD;
					printf("Slot %d (%" PRId32 "): %lf, %lf\n", Slot, GPS->Altitude, Positions[Slot].LatitudeDelta, Positions[Slot].LongitudeDelta);
				}
  				else if ((GPS->MaximumAltitude > 5000) && (PreviousAltitude < GPS->MaximumAltitude) && (GPS->Altitude < PreviousAltitude) && (GPS->Altitude > 100))
				{
					// Coming down - try and calculate how well chute is doing

					CDA = (CDA + CalculateCDA(Config.payload_weight,
											  GPS->Altitude/2 + PreviousAltitude/2,
											  ((double)PreviousAltitude - (double)GPS->Altitude) / POLL_PERIOD)) / 2;
				}
				
				// Estimate landing position
				Altitude = GPS->Altitude;
				Latitude = GPS->Latitude;
				Longitude = GPS->Longitude;
				TimeTillLanding = 0;
				
				Slot = Altitude / SLOTSIZE;
				DistanceInSlot = Altitude - (Slot * SLOTSIZE);
				
				while (Altitude > 0)
				{
					Slot = Altitude / SLOTSIZE;
					
					DescentRate = CalculateDescentRate(Config.payload_weight, CDA, Altitude);
					TimeInSlot = DistanceInSlot / DescentRate;
					
					Latitude += Positions[Slot].LatitudeDelta * TimeInSlot;
					Longitude += Positions[Slot].LongitudeDelta * TimeInSlot;
					
					// printf("alt %lu: lat=%lf, long=%lf, rate=%lf, dist=%lu, time=%lf\n", Altitude, Latitude, Longitude, DescentRate, DistanceInSlot, TimeInSlot);
					
					TimeTillLanding += TimeInSlot;
					Altitude -= DistanceInSlot;
					DistanceInSlot = SLOTSIZE;
				}
				
				GPS->PredictedLatitude = Latitude;
				GPS->PredictedLongitude = Longitude;
				printf("Expected Descent Rate = %4.1lf (now) %3.1lf (landing), time till landing %5.0lf\n", 
						CalculateDescentRate(Config.payload_weight, Config.cd_area, GPS->Altitude),
						CalculateDescentRate(Config.payload_weight, Config.cd_area, 100),
						TimeTillLanding);

				printf("Current    %f, %f, alt %" PRId32 "\n", GPS->Latitude, GPS->Longitude, GPS->Altitude);
				printf("Prediction %f, %f, CDA %lf\n", GPS->PredictedLatitude, GPS->PredictedLongitude, CDA);
				
				sprintf(Temp, "%" PRId32 ", %f, %f, %f, %f, %lf\n", GPS->Altitude, GPS->Latitude, GPS->Longitude, GPS->PredictedLatitude, GPS->PredictedLongitude, CDA);
				WriteLog("prediction.txt", Temp);
			}
			
			PreviousLatitude = GPS->Latitude;
			PreviousLongitude = GPS->Longitude;
			PreviousAltitude = GPS->Altitude;
		}
	}

	return 0;
}

