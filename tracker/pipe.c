#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <inttypes.h>

#include "pipe.h"
#include "gps.h"
#include "misc.h"


void *PipeLoop(void *some_void_ptr)
{
	struct TGPS *GPS;
    int fd;
    char *fifo="pits_pipe";
	unsigned char Sentence[200];
	char Message[300];

	GPS = (struct TGPS *)some_void_ptr;

    // create the FIFO (named pipe)
	mkfifo(fifo, 0666);

	while (1)
	{
		// Create and send sentence	etc
		if (GPS->Satellites > 3)
		{
			BuildSentence(Sentence, PIPE_CHANNEL, GPS);
			
			sprintf(Message, "%sALT=%5.5" PRId32 "\nLAT=%.5lf\nLON=%.5lf\n", Sentence, GPS->Altitude, GPS->Latitude, GPS->Longitude);
				
			LogMessage("PIPE STUFF ................");
			fd = open(fifo, O_WRONLY);
			if (fd >= 0)
			{
				LogMessage("PIPE: %.70s", Sentence);
				write(fd, Message, strlen(Message));
				close(fd);
			}
			else
			{
				LogMessage("PIPE Closed");
			}
			sleep(10);
		}
		else
		{
			sleep(1);
		}
	}

    // unlink(fifo);
	
	return 0;
}
