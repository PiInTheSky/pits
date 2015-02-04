#include <stdio.h>
#include <string.h>

char Hex(char Character)
{
	char HexTable[] = "0123456789ABCDEF";
	
	return HexTable[Character];
}

void WriteLog(char *FileName, char *Buffer)
{
	FILE *fp;
	
	if ((fp = fopen(FileName, "at")) != NULL)
	{
		fputs(Buffer, fp);
		fclose(fp);
	}
}

int NewBoard(void)
{
	FILE *cpuFd ;
	char line [120] ;
	char *c ;
	static int  boardRev = -1 ;

	if (boardRev < 0)
	{
		if ((cpuFd = fopen ("/proc/cpuinfo", "r")) != NULL)
		{
			while (fgets (line, 120, cpuFd) != NULL)
			{
				if (strncmp (line, "Hardware", 8) == 0)
				{
					printf ("RPi %s", line);
					if (strstr (line, "BCM2709") != NULL)
					{
						boardRev = 2;
					}
				}
				
				if (strncmp (line, "Revision", 8) == 0)
				{
					printf ("RPi %s", line);
					if (boardRev < 0)
					{
						boardRev = ((strstr(line, "0010") != NULL) || (strstr(line, "0012") != NULL));	// B+ or A+
					}
				}
			}

			fclose (cpuFd) ;
		}
	}
	
	return boardRev;
}
