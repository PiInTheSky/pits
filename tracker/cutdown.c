#include <stdio.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <time.h>
#include <math.h>
#include <unistd.h>
#include <wiringPi.h>

#include "gps.h"
#include "misc.h"
#include "cutdown.h"

int CutdownPeriod = 0;
	
void cutdown_checks(struct TGPS *GPS)
{
	if (Config.EnableCutdown)
	{
		if (GPS->CutdownStatus == csUnarmed)
		{
			if (GPS->Altitude >= Config.MinCutdownAltitude)
			{
				GPS->CutdownStatus = csArmed;
			}
		}

		if (GPS->CutdownStatus == csArmed)
		{
			if ((Config.CutdownAltitude > 0) && (GPS->Altitude >= Config.CutdownAltitude))
			{
				GPS->CutdownStatus = csAltitude;
				printf("Cutdown (Altitude) Triggered\n");
			}

			if ((Config.MinCutdownAltitude > 0) && (GPS->Altitude >= Config.MinCutdownAltitude))
			{
				// Other tests enabled now
				if (strcasecmp("N", Config.CutdownTest) == 0)
				{
					if (GPS->Latitude >= Config.CutdownLatitude)
					{
						GPS->CutdownStatus = csPosition;
					}
				}
				else if (strcasecmp("NE", Config.CutdownTest) == 0)
				{
					if ((GPS->Latitude >= Config.CutdownLatitude) && (GPS->Longitude >= Config.CutdownLongitude))
					{
						GPS->CutdownStatus = csPosition;
					}
				}
				else if (strcasecmp("E", Config.CutdownTest) == 0)
				{
					if (GPS->Longitude >= Config.CutdownLongitude)
					{
						GPS->CutdownStatus = csPosition;
					}
				}
				else if (strcasecmp("SE", Config.CutdownTest) == 0)
				{
					if ((GPS->Latitude <= Config.CutdownLatitude) && (GPS->Longitude >= Config.CutdownLongitude))
					{
						GPS->CutdownStatus = csPosition;
					}
				}
				else if (strcasecmp("S", Config.CutdownTest) == 0)
				{
					if (GPS->Latitude <= Config.CutdownLatitude)
					{
						GPS->CutdownStatus = csPosition;
					}
				}
				else if (strcasecmp("SW", Config.CutdownTest) == 0)
				{
					if ((GPS->Latitude <= Config.CutdownLatitude) && (GPS->Longitude <= Config.CutdownLongitude))
					{
						GPS->CutdownStatus = csPosition;
					}
				}
				else if (strcasecmp("W", Config.CutdownTest) == 0)
				{
					if (GPS->Longitude <= Config.CutdownLongitude)
					{
						GPS->CutdownStatus = csPosition;
					}
				}
				else if (strcasecmp("NW", Config.CutdownTest) == 0)
				{
					if ((GPS->Latitude >= Config.CutdownLatitude) && (GPS->Longitude <= Config.CutdownLongitude))
					{
						GPS->CutdownStatus = csPosition;
					}
				}			
				if (GPS->CutdownStatus == csPosition)
					
				{
					printf("Cutdown (Position) Triggered\n");
				}
				
				if ((Config.CutdownTimeSinceLaunch > 0) && (GPS->SecondsSinceLaunch >= Config.CutdownTimeSinceLaunch))
				{
					GPS->CutdownStatus = csFlightTime;
					printf("Cutdown (Max Flight Time) Triggered\n");
				}

				if (Config.CutdownBurst && (GPS->FlightMode >= fmDescending) && (GPS->FlightMode < fmLanding))
				{
					GPS->CutdownStatus = csBurst;
					printf("Cutdown (Balloon burst detected\n");
				}
			}
			
			if (GPS->CutdownStatus > csArmed)
			{
				Cutdown(Config.CutdownPeriod);
			}
		}
	}
}

void Cutdown(int Period)
{
	CutdownPeriod = Period;
}

void *CutdownLoop(void *some_void_ptr)
{
	if (Config.CutdownPin > 0)
	{
		// Configure output
		pinMode (Config.CutdownPin, OUTPUT);
		digitalWrite (Config.CutdownPin, 0);
		
		while (1)
		{
			if (CutdownPeriod > 0)
			{
				digitalWrite (Config.CutdownPin, 1);
				sleep(CutdownPeriod);
				digitalWrite (Config.CutdownPin, 0);
				CutdownPeriod = 0;
			}
			
			sleep(1);
		}
	}

	return 0;
}
