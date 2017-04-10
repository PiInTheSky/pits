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

#include "pipe.h"
#include "gps.h"
#include "misc.h"


void *PipeLoop(void *some_void_ptr)
{
	struct TGPS *GPS;
    int fd;
    char *fifo = "pits_pipe";
	unsigned char Sentence[200];

	GPS = (struct TGPS *)some_void_ptr;


    // create the FIFO (named pipe)
    mkfifo(fifo, 0666);
    fd = open(fifo, O_WRONLY);

	while (1)
	{
		// Create sentence
		
		BuildSentence(Sentence, PIPE_CHANNEL, GPS);

		LogMessage("PIPE: %.70s", Sentence);

		write(fd, (char *)Sentence, strlen((char *)Sentence));

		sleep(10);
	}

    // close(fd);
    // unlink(fifo);
	
	return 0;
}
