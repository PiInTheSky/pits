#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <errno.h>
#include <wiringPiSPI.h>
#include <gertboard.h>
#include <inttypes.h>

#include "gps.h"
#include "misc.h"

int SSDVPacketsToSend(int Channel)
{
	int i, j, Count;
	
	Count = 0;
	for (i=0; i<2; i++)
	{
		for (j=0; j<Config.Channels[Channel].SSDVPackets[i].NumberOfPackets; j++)
		{
			if (Config.Channels[Channel].SSDVPackets[i].Packets[j])
			{
				Count++;
			}
		}
	}
		
	// printf("Channel %d Count %d\n", Channel, Count);
	
	return Count;
}

int TimeTillImageCompleted(int Channel)
{

	// Quick check for the "full" channel - for this we aren't transmitting so we need to return a large number so we don't take a photo immediately
	if (Channel == 4)
	{
		return 9999;
	}
	
	return SSDVPacketsToSend(Channel) * 256 * 10 / Config.Channels[Channel].BaudRate;

/*	
	// If we aren't sending a file, then we need a new one NOW!
	if (Config.Channels[Channel].ImageFP == NULL || 
	    (Config.Channels[Channel].SSDVTotalRecords == 0))
	{
		// printf ("Convert image now for channel %d!\n", Channel);
		return 0;
	}

	// If we're on the last packet, convert anyway
	if (Config.Channels[Channel].SSDVRecordNumber >= (Config.Channels[Channel].SSDVTotalRecords-1))
	{
		return 0;
	}
		
	return (Config.Channels[Channel].SSDVTotalRecords - Config.Channels[Channel].SSDVRecordNumber) * 256 * 10 / Config.Channels[Channel].BaudRate;
*/	
}

void FindBestImageAndRequestConversion(int Channel, int width, int height)
{
	size_t LargestFileSize;
	char LargestFileName[100], FileName[100];
	DIR *dp;
	struct dirent *ep;
	struct stat st;
	char *SSDVFolder;
	
	LargestFileSize = 0;
	SSDVFolder = Config.Channels[Channel].SSDVFolder;
	
	dp = opendir(SSDVFolder);
	if (dp != NULL)
	{
		while ((ep = readdir (dp)) != NULL)
		{
			if (strstr(ep->d_name, ".jpg") != NULL)
			{
				if (strchr(ep->d_name, '~') == NULL)
				{
					sprintf(FileName, "%s/%s", SSDVFolder, ep->d_name);
					stat(FileName, &st);
					if (st.st_size > LargestFileSize)
					{
						LargestFileSize = st.st_size;
						strcpy(LargestFileName, FileName);
					}
				}
			}
		}
		(void) closedir (dp);
	}

	if (LargestFileSize > 0)
	{
		FILE *fp;
		
		printf("Found file %s to convert\n", LargestFileName);
		
		// Create SSDV script file
		if ((fp = fopen(Config.Channels[Channel].convert_file, "wt")) != NULL)
		{
			Config.Channels[Channel].SSDVFileNumber++;
			// Config.Channels[Channel].SSDVFileNumber = Config.Channels[Channel].SSDVFileNumber & 255;

			sprintf(Config.Channels[Channel].ssdv_filename, "ssdv_%d_%d.bin", Channel, Config.Channels[Channel].SSDVFileNumber);
			
			// External script for ImageMagick etc.
			fprintf(fp, "rm -f ssdv.jpg\n");
			fprintf(fp, "if [ -e process_image ]\n");
			fprintf(fp, "then\n");
			fprintf(fp, "	./process_image %d %s %d %d\n", Channel, LargestFileName, width, height);
			fprintf(fp, "else\n");
			// Just copy file, unless we're using gphoto2 in which case we need to resize, meaning imagemagick *must* be installed
			if (Config.Camera == 3)
			{
				// resize
				fprintf(fp, "	convert %s -resize %dx%d ssdv.jpg\n", LargestFileName, width, height);
			}
			else
			{
				fprintf(fp, "	cp %s ssdv.jpg\n", LargestFileName);
			}
			fprintf(fp, "fi\n");
			
			fprintf(fp, "ssdv %s -e -c %.6s -i %d %s %s\n", Config.SSDVSettings, Config.Channels[Channel].PayloadID, Config.Channels[Channel].SSDVFileNumber, "ssdv.jpg", Config.Channels[Channel].ssdv_filename);
			fprintf(fp, "mkdir -p %s/$1\n", SSDVFolder);
			fprintf(fp, "mv %s/*.jpg %s/$1\n", SSDVFolder, SSDVFolder);
			fprintf(fp, "echo DONE > %s\n", Config.Channels[Channel].ssdv_done);
			fclose(fp);
			chmod(Config.Channels[Channel].convert_file, S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IWGRP | S_IXGRP | S_IROTH | S_IWOTH | S_IXOTH); 
		}
	}
}

void GetWidthAndHeightForChannel(struct TGPS *GPS, int Channel, int *width, int *height)
{
	if (GPS->Altitude >= Config.SSDVHigh)
	{
		*width = Config.Channels[Channel].ImageWidthWhenHigh;
		*height = Config.Channels[Channel].ImageHeightWhenHigh;
	}
	else
	{
		*width = Config.Channels[Channel].ImageWidthWhenLow;
		*height = Config.Channels[Channel].ImageHeightWhenLow;
	}
	
	// if (*width < 320) *width = 320;
	// if (*height < 240) *height = 240;					

	// SSDV requires dimensions to be multiples of 16 pixels
	*width = (*width / 16) * 16;
	*height = (*height / 16) * 16;
}

void *CameraLoop(void *some_void_ptr)
{
	int width, height;
	struct TGPS *GPS;
	char filename[100];
	int Channel;
	FILE *fp;

	GPS = (struct TGPS *)some_void_ptr;
	
	for (Channel=0; Channel<5; Channel++)
	{
		Config.Channels[Channel].TimeSinceLastImage = Config.Channels[Channel].ImagePeriod;
		Config.Channels[Channel].SSDVFileNumber = 0;
	}

	while (1)
	{
		for (Channel=0; Channel<5; Channel++)
		{
			if (Config.Channels[Channel].Enabled && (Config.Channels[Channel].ImagePackets > 0))
			{
				// Channel using SSDV
				
				if (++Config.Channels[Channel].TimeSinceLastImage >= Config.Channels[Channel].ImagePeriod)
				{
					// Time to take a photo on this channel

					Config.Channels[Channel].TimeSinceLastImage = 0;
					
					GetWidthAndHeightForChannel(GPS, Channel, &width, &height);
					
					if ((width > 0) && (height > 0))
					{
						// Create name of file
						sprintf(filename, "/home/pi/pits/tracker/take_pic_%d", Channel);
						
						// Leave it alone if it exists (this means that the photo has not been taken yet)
						if (access(filename, F_OK ) == -1)
						{				
							// Doesn't exist, so create it.  Script will run it next time it checks
							if ((fp = fopen(filename, "wt")) != NULL)
							{
								char FileName[256];
								int Mode;
								
								if (Channel == 4)
								{
									// Full size images are saved in dated folder names
									fprintf(fp, "mkdir -p %s/$2\n", Config.Channels[Channel].SSDVFolder);

									sprintf(FileName, "%s/$2/$1.jpg", Config.Channels[Channel].SSDVFolder);				

									if (Config.Camera == 3)
									{
										if (access("take_photo",  X_OK) == 0)
										{
											fprintf(fp, "./take_photo %s\n", FileName);
										}
										else
										{
											fprintf(fp, "gphoto2 --capture-image-and-download --force-overwrite --filename %s\n", FileName);
										}
									}
									else if (Config.Camera == 2)
									{
										fprintf(fp, "fswebcam -r %dx%d --no-banner %s\n", width, height, FileName);
									}
									else
									{
										fprintf(fp, "raspistill -st -w %d -h %d -t 3000 -ex auto -mm matrix %s -o %s\n", width, height, Config.CameraSettings, FileName);
									}
								}
								else
								{
									sprintf(FileName, "%s/$1.jpg", Config.Channels[Channel].SSDVFolder);

									if (Config.Camera == 3)
									{
										// For gphoto2 we do full-res now and resize later
										fprintf(fp, "gphoto2 --capture-image-and-download --force-overwrite --filename %s\n", FileName);
									}
									else if (Config.Camera == 2)
									{
										fprintf(fp, "fswebcam -r %dx%d --no-banner %s\n", width, height, FileName);
									}
									else
									{
										fprintf(fp, "raspistill -st -w %d -h %d -t 3000 -ex auto -mm matrix %s -o %s\n", width, height, Config.CameraSettings, FileName);
									}
								}
								
								// Add telemetry as comment in JPEG file
								// Alt=34156;Lat=51.4321;Long=-2.4321;UTC=10:11:12;Ascent=5.1;Mode=1
								if (GPS->AscentRate >= 2)
								{
									Mode = 1;
								}
								else if (GPS->AscentRate <= -2)
								{
									Mode = 2;
								}
								else if (GPS->Altitude >= Config.SSDVHigh)
								{
									Mode = 1;
								}
								else
								{
									Mode = 0;
								}
								fprintf(fp, "exiv2 -c'Alt=%" PRId32 ";MaxAlt=%" PRId32 ";Lat=%7.5lf;Long=%7.5lf;UTC=%02d:%02d:%02d;Ascent=%.1lf;Mode=%d' %s\n",
													GPS->Altitude,
													GPS->MaximumAltitude,
													GPS->Latitude,
													GPS->Longitude,
													GPS->Hours, GPS->Minutes, GPS->Seconds,
													GPS->AscentRate,
													Mode,
													FileName);
		
								fclose(fp);
								chmod(filename, S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IWGRP | S_IXGRP | S_IROTH | S_IWOTH | S_IXOTH); 
								Config.Channels[Channel].ImagesRequested++;
							}
						}
					}
				}
				
				// Now check if we need to convert the "best" image before the current SSDV file is fully sent

				if (Channel < 4)
				{
					// Exclude full-size images - no conversion for those
					if (TimeTillImageCompleted(Channel) < 25)
					{
						// Need converted file soon
						if (!FileExists(Config.Channels[Channel].convert_file) && !FileExists(Config.Channels[Channel].ssdv_done))
						{
							// Get these in case script needs to resize large images (e.g. from SLR or compact camera)
							GetWidthAndHeightForChannel(GPS, Channel, &width, &height);
							
							// Find image to use, then request conversion (done externally)
							FindBestImageAndRequestConversion(Channel, width, height);
						}
					}
				}
			}
		}
		
		sleep(1);
	}
}


