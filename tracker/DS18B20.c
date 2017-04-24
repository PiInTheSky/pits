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

#include "gps.h"
#include "DS18B20.h"

void *DS18B20Loop(void *some_void_ptr)
{
    DIR *dir;
    struct dirent *dp;
	char *folder = "/sys/bus/w1/devices";
	FILE *fp;
	char line[100], filename[100];
	char *value;
	float Temperature;
	struct TGPS *GPS;
	int SensorCount;

	GPS = (struct TGPS *)some_void_ptr;

	while (1)
	{
		if ((dir = opendir(folder)) != NULL)
		{
			SensorCount = 0;
			while (((dp = readdir(dir)) != NULL) && (SensorCount < 2))
			{
				if (strlen(dp->d_name) > 3)
				{
					if ((dp->d_name[0] != 'W') && (dp->d_name[2] == '-'))
					{
						sprintf(filename, "%s/%s/w1_slave", folder, dp->d_name);
						if ((fp = fopen(filename, "r")) != NULL)
						{
							// 44 02 4b 46 7f ff 0c 10 ee : crc=ee YES
							// 44 02 4b 46 7f ff 0c 10 ee t=36250
							if (fgets(line, sizeof(line), fp) != NULL)
							{
								if (strstr(line, "YES") != NULL)
								{
									if (fgets(line, sizeof(line), fp) != NULL)
									{
										strtok(line, "=");
										value = strtok(NULL, "\n");
										Temperature = atof(value) / 1000;
										// printf("%d: %5.3fC\n", SensorCount, Temperature);
										GPS->DS18B20Temperature[SensorCount++] = Temperature;
									}
								}
							}
							
							fclose(fp);
						}
//						else
//						{
//							printf("COULD NOT OPEN DS18B20 FILE\n");
//						}
					}
				}
			}
			if (SensorCount > GPS->DS18B20Count) GPS->DS18B20Count = SensorCount;
			// printf("%d DS18B20 sensors found\n", SensorCount);
			
			closedir(dir);
		}
		else
		{
			printf("COULD NOT OPEN DS18B20 FOLDER\n");
		}
		
		if (GPS->DS18B20Count == 0)
		{
			// Use the GPU sensor instead
			FILE *fp;
			double T;
			
			fp = fopen ("/sys/class/thermal/thermal_zone0/temp", "r");
			if (fp != NULL)
			{
				fscanf (fp, "%lf", &T);
				GPS->DS18B20Temperature[0] = T / 1000;
				// printf ("GPU temperature is %6.3f C.\n", GPS->DS18B20Temperature[0]);
				GPS->DS18B20Count = 1;
				fclose (fp);
			}
		}
		
		sleep(5);
	}
}
