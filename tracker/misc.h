// Globals

#include <termios.h>

struct TConfig
{
	char PayloadID[16];
	char Frequency[8];
	speed_t TxSpeed;
	int Camera;
	int low_width;
	int low_height;
	int high;
	int high_width;
	int high_height;
	int image_packets;
};

extern struct TConfig Config;

char Hex(char Character);
