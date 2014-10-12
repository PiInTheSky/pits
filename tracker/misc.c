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
				if (strncmp (line, "Revision", 8) == 0)
					break ;

			fclose (cpuFd) ;

			if (strncmp (line, "Revision", 8) == 0)
			{
				printf ("RPi %s", line);
				boardRev = strstr(line, "0010") != NULL;
			}
		}
	}
	
	return boardRev;
}