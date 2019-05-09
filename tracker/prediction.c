#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <math.h>
#include <inttypes.h>

#include "gps.h"
#include "misc.h"
#include "prediction.h"

#ifdef EXTRAS_PRESENT
#	include "ex_prediction.h"
#endif	

struct TPosition Positions[SLOTS];		// 100m slots from 0 to 45km

int GetSlot(int32_t Altitude)
{
	int Slot;
	
	Slot = Altitude / SLOTSIZE;
	
	if (Slot < 0) Slot = 0;
	if (Slot >= SLOTS) Slot = SLOTS-1;
	
	return Slot;
}

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
	
	printf("Alt %.0lf, Rate %.1lf, CDA %.1lf\n", Altitude, DescentRate, (Weight * 9.81)/(0.5 * Density * DescentRate * DescentRate));

    return (Weight * 9.81)/(0.5 * Density * DescentRate * DescentRate);
}


	
void *PredictionLoop(void *some_void_ptr)
{
	struct TGPS *GPS;
	double PreviousLatitude, PreviousLongitude;
	unsigned long PreviousAltitude;

	GPS = (struct TGPS *)some_void_ptr;
	
	PreviousLatitude = 0;
	PreviousLongitude = 0;
	PreviousAltitude = 0;
	
	GPS->CDA = Config.cd_area;

	while (1)
	{
		sleep(POLL_PERIOD);
				
		if ((GPS->Satellites >= 4) && (GPS->Latitude >= -90) && (GPS->Latitude <= 90) && (GPS->Longitude >= -180) && (GPS->Longitude <= 180))
		{
			int Slot;
			char Temp[200];
			
			if ((GPS->FlightMode >= fmLaunched) && (GPS->FlightMode < fmLanded))
			{
				
				// Ascent or descent?
				if (GPS->FlightMode == fmLaunched)
				{
					// Going up - store deltas
					Slot = GetSlot(GPS->Altitude/2 + PreviousAltitude/2);
					
					// Deltas are scaled to be horizontal distance per second (i.e. speed)
					Positions[Slot].LatitudeDelta = (GPS->Latitude - PreviousLatitude) / POLL_PERIOD;
					Positions[Slot].LongitudeDelta = (GPS->Longitude - PreviousLongitude) / POLL_PERIOD;
					printf("Slot %d (%" PRId32 "): %lf, %lf\n", Slot, GPS->Altitude, Positions[Slot].LatitudeDelta, Positions[Slot].LongitudeDelta);
				}
				// else if ((GPS->MaximumAltitude > 5000) && (PreviousAltitude < GPS->MaximumAltitude) && (GPS->Altitude < PreviousAltitude) && (GPS->Altitude > Config.TargetAltitude))
				else if ((GPS->FlightMode >= fmDescending) && (GPS->FlightMode <= fmLanding))
				{
					// Coming down - try and calculate how well chute is doing

					GPS->CDA = (GPS->CDA*4 + CalculateCDA(Config.payload_weight,
														GPS->Altitude/2 + PreviousAltitude/2,
														((double)PreviousAltitude - (double)GPS->Altitude) / POLL_PERIOD)) / 5;

				}
				
				
				// Estimate landing position
				GPS->TimeTillLanding = CalculateLandingPosition(GPS, GPS->Latitude, GPS->Longitude, GPS->Altitude, &(GPS->PredictedLatitude), &(GPS->PredictedLongitude));

				GPS->PredictedLandingSpeed = CalculateDescentRate(Config.payload_weight, GPS->CDA, Config.TargetAltitude);
				
				printf("Expected Descent Rate = %4.1lf (now) %3.1lf (landing), time till landing %d\n", 
						CalculateDescentRate(Config.payload_weight, GPS->CDA, GPS->Altitude),
						GPS->PredictedLandingSpeed,
						GPS->TimeTillLanding);

				printf("Current    %f, %f, alt %" PRId32 "\n", GPS->Latitude, GPS->Longitude, GPS->Altitude);
				printf("Prediction %f, %f, CDA %lf\n", GPS->PredictedLatitude, GPS->PredictedLongitude, GPS->CDA);
				
#				ifdef EXTRAS_PRESENT
					Slot = GetSlot(GPS->Altitude/2 + PreviousAltitude/2);
					prediction_descent_calcs(GPS, Slot, PreviousLatitude, PreviousLongitude, PreviousAltitude, Positions[Slot].LatitudeDelta, Positions[Slot].LongitudeDelta);
#				endif	
				
				sprintf(Temp, "%" PRId32 ", %f, %f, %f, %f, %lf\n", GPS->Altitude, GPS->Latitude, GPS->Longitude, GPS->PredictedLatitude, GPS->PredictedLongitude, GPS->CDA);
				WritePredictionLog(Temp);
			}
			
			PreviousLatitude = GPS->Latitude;
			PreviousLongitude = GPS->Longitude;
			PreviousAltitude = GPS->Altitude;
		}
	}

	return 0;
}


int CalculateLandingPosition(struct TGPS *GPS, double Latitude, double Longitude, int32_t Altitude, double *PredictedLatitude, double *PredictedLongitude)
{
	double TimeTillLanding, TimeInSlot, DescentRate;
	int Slot;
	int32_t DistanceInSlot;
	
	TimeTillLanding = 0;
	
	Slot = GetSlot(Altitude);
	DistanceInSlot = Altitude + 1 - (Slot * SLOTSIZE);
	
	while (Altitude > Config.TargetAltitude)
	{
		Slot = GetSlot(Altitude);
		
		if (Slot == GetSlot(Config.TargetAltitude))
		{
			DistanceInSlot = Altitude - Config.TargetAltitude;
		}
		
		DescentRate = CalculateDescentRate(Config.payload_weight, GPS->CDA, Altitude);
		TimeInSlot = DistanceInSlot / DescentRate;
		
		Latitude += Positions[Slot].LatitudeDelta * TimeInSlot;
		Longitude += Positions[Slot].LongitudeDelta * TimeInSlot;
		
		// printf("SLOT %d: alt %lu, lat=%lf, long=%lf, rate=%lf, dist=%lu, time=%lf\n", Slot, Altitude, Latitude, Longitude, DescentRate, DistanceInSlot, TimeInSlot);
		
		TimeTillLanding = TimeTillLanding + TimeInSlot;
		Altitude -= DistanceInSlot;
		DistanceInSlot = SLOTSIZE;
	}
				
	*PredictedLatitude = Latitude;
	*PredictedLongitude = Longitude;
	
	return TimeTillLanding;	
}
