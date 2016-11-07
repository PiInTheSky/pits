#include <stdio.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include "gps.h"
#include "misc.h"

// Sine wave table for tone generation
uint8_t _sine_table[] = {
#include "sine_table.h"
};

typedef unsigned int	UI;
typedef unsigned long int	UL;
typedef unsigned short int	US;
typedef unsigned char	UC;
typedef signed int		SI;
typedef signed long int	SL;
typedef signed short int	SS;
typedef signed char	SC;
 
#define attr(a) __attribute__((a))
 
#define packed attr(packed)
 
/* WAV header, 44-byte total */
typedef struct{
 UL riff	packed;
 UL len	packed;
 UL wave	packed;
 UL fmt	packed;
 UL flen	packed;
 US one	packed;
 US chan	packed;
 UL hz	packed;
 UL bpsec	packed;
 US bpsmp	packed;
 US bitpsmp	packed;
 UL dat	packed;
 UL dlen	packed;
}WAVHDR;
 
// APRS / AFSK variables
  
#define APRS_DEVID "APEHAB"  
#define APRS_COMMENT "http://www.pi-in-the-sky.com"
 
/* "converts" 4-char string to long int */
#define dw(a) (*(UL*)(a))


static uint8_t *_ax25_callsign(uint8_t *s, char *callsign, char ssid)
{
  char i;
  for(i = 0; i < 6; i++)
  {
    if(*callsign) *(s++) = *(callsign++) << 1;
    else *(s++) = ' ' << 1;
  }
  *(s++) = ('0' + ssid) << 1;
  return(s);
}

char *ax25_base91enc(char *s, uint8_t n, uint32_t v)
{
  /* Creates a Base-91 representation of the value in v in the string */
  /* pointed to by s, n-characters long. String length should be n+1. */

  for(s += n, *s = '\0'; n; n--)
  {
    *(--s) = v % 91 + 33;
    v /= 91;
  }

  return(s);
}

void ax25_frame(uint8_t *frame, int *length, char *scallsign, char sssid, char *dcallsign, char dssid, char ttl1, char ttl2, char *data, ...)
{
  uint8_t *s, j;
  uint16_t CRC;
  va_list va;

  va_start(va, data);

  /* Write in the callsigns and paths */
  s = _ax25_callsign(frame, dcallsign, dssid);
  s = _ax25_callsign(s, scallsign, sssid);
  if (ttl1) s = _ax25_callsign(s, "WIDE1", ttl1);
  if (ttl2) s = _ax25_callsign(s, "WIDE2", ttl2);

  /* Mark the end of the callsigns */
  s[-1] |= 1;

  *(s++) = 0x03; /* Control, 0x03 = APRS-UI frame */
  *(s++) = 0xF0; /* Protocol ID: 0xF0 = no layer 3 data */

  // printf ("Length A is %d\n", s - frame);
  
  // printf ("Adding %d bytes\n", 
  vsnprintf((char *) s, 200 - (s - frame) - 2, data, va);
  va_end(va);

  // printf ("Length B is %d\n", strlen(frame));
  
	// Calculate and append the checksum

	for (CRC=0xffff, s=frame; *s; s++)
    {
		CRC ^= ((unsigned int)*s);
		
        for (j=0; j<8; j++)
        {
			if (CRC & 1)
			{
				CRC = (CRC >> 1) ^ 0x8408;
			}
			else
			{
				CRC >>= 1;
			}
        }
    }
	/* 
	for (CRC=0xffff, s=frame; *s; s++)
    {
  	  CRC = ((CRC) >> 8) ^ ccitt_table[((CRC) ^ *s) & 0xff];
	}
	*/
	
	*(s++) = ~(CRC & 0xFF);
	*(s++) = ~((CRC >> 8) & 0xFF);
  
	// printf ("Checksum = %02Xh %02Xh\n", *(s-2), *(s-1));

	*length = s - frame;
}

/* Makes 44-byte header for 8-bit WAV in memory
usage: wavhdr(pointer,sampleRate,dataLength) */
 
void wavhdr(void*m,UL hz,UL dlen){
 WAVHDR*p=m;
 p->riff=dw("RIFF");
 p->len=dlen+44;
 p->wave=dw("WAVE");
 p->fmt=dw("fmt ");
 p->flen=0x10;
 p->one=1;
 p->chan=1;
 p->hz=hz;
 p->bpsec=hz;
 p->bpsmp=1;
 p->bitpsmp=16;	// 8
 p->dat=dw("data");
 p->dlen=dlen;
}

void make_and_write_freq(FILE *f, UL cycles_per_bit, UL baud, UL lfreq, UL hfreq, int8_t High)
{
	// write 1 bit, which will be several values from the sine wave table
	static uint16_t phase  = 0;
	uint16_t step;
	int i;

	if (High)
	{
		step = (512 * hfreq << 7) / (cycles_per_bit * baud);
		// printf("-");
	}
	else
	{
		step = (512 * lfreq << 7) / (cycles_per_bit * baud);
		// printf("_");
	}
	
	for (i=0; i<cycles_per_bit; i++)
	{
		// fwrite(&(_sine_table[(phase >> 7) & 0x1FF]), 1, 1, f);
		int16_t v = _sine_table[(phase >> 7) & 0x1FF] * 0x80 - 0x4000;
		if (High & Config.APRS_Preemphasis)
		{	
			v *= 0.65;
		}
		else
		{
			v *= 1.3;
		}
		// int16_t v = _sine_table[(phase >> 7) & 0x1FF] * 0x100 - 0x8000;
		fwrite(&v, 2, 1, f);
		phase += step;
	}
}

void make_and_write_bit(FILE *f, UL cycles_per_bit, UL baud, UL lfreq, UL hfreq, unsigned char Bit, int BitStuffing)
{
	static int8_t bc = 0;
	static int8_t High = 0;
			
	if(BitStuffing)
	{
		if(bc >= 5)
		{
			High = !High;
			make_and_write_freq(f, cycles_per_bit, baud, lfreq, hfreq, High);
			bc = 0;
		}
	}
	else
	{
		bc = 0;
	}
	
	if (Bit)
	{
		// Stay with same frequency, but only for a max of 5 in a row
		bc++;
	}
	else
	{
		// 0 means swap frequency
		High = !High;
		bc = 0;
	}
	
	make_and_write_freq(f, cycles_per_bit, baud, lfreq, hfreq, High);	
}

 
void make_and_write_byte(FILE *f, UL cycles_per_bit, UL baud, UL lfreq, UL hfreq, unsigned char Character, int BitStuffing)
{
	int i;
	
	// printf("%02X ", Character);
		
	for (i=0; i<8; i++)
	{
		make_and_write_bit(f, cycles_per_bit, baud, lfreq, hfreq, Character & 1, BitStuffing);
		Character >>= 1;
	}
}

 
/* makes wav file */
void makeafsk(UL freq, UL baud, UL lfreq, UL hfreq, unsigned char Message[4][200], int message_length[], int message_count, int total_message_length)
{
	UL preamble_length, postamble_length, flags_before, flags_after, cycles_per_bit, cycles_per_byte, total_cycles;
	FILE *f;
	int i, j;
	WAVHDR Header;
	
	if ((f = fopen("aprs.wav","wb")) != NULL)
	{
		printf("Building APRS packet\n");
		cycles_per_bit = freq / baud;
		// printf ("cycles_per_bit=%d\n", cycles_per_bit);
		cycles_per_byte = cycles_per_bit * 8;
		// printf ("cycles_per_byte=%d\n", cycles_per_byte);

		preamble_length = 128;
		postamble_length = 64;
		flags_before = 32;
		flags_after = 32;

		// Calculate size of file
		total_cycles = (cycles_per_byte * total_message_length) +
					   (cycles_per_byte * (flags_before + flags_after) * message_count) +
					   ((preamble_length + postamble_length) * cycles_per_bit * message_count);

		// Make header
		wavhdr(&Header, freq, total_cycles * 2 + 10);		// * 2 + 10 is new
		
		// Write wav header
		fwrite(&Header, 1, 44, f);
		
		// Write preamble
		for (j=0; j<message_count; j++)
		{
			for (i=0; i<flags_before; i++)
			{
				make_and_write_byte(f, cycles_per_bit, baud, lfreq, hfreq, 0x7E, 0);
			}
			
			// Create and write actual data
			for (i=0; i<message_length[j]; i++)
			{
				make_and_write_byte(f, cycles_per_bit, baud, lfreq, hfreq, Message[j][i], 1);
			}

			for (i=0; i<flags_after; i++)
			{
				make_and_write_byte(f, cycles_per_bit, baud, lfreq, hfreq, 0x7E, 0);
			}

			// Write postamble
			for (i=0; i< postamble_length; i++)
			{
				make_and_write_freq(f, cycles_per_bit, baud, lfreq, hfreq, 0);
			}
		}
		
		fclose(f);
	}
}

	
void SendAPRS(struct TGPS *GPS)
{
	unsigned char frames[4][200];
	int lengths[4];
	int message_count, total_length;
	char stlm[9];
	char slat[5];
	char slng[5];
	double aprs_lat, aprs_lon;
	int32_t aprs_alt;
	static uint16_t seq = 0;
	uint32_t aprs_temperature, aprs_voltage;

	seq++;
	
	// Convert the min.dec coordinates to APRS compressed format
	aprs_lat = 900000000 - GPS->Latitude * 10000000;
	aprs_lat = aprs_lat / 26 - aprs_lat / 2710 + aprs_lat / 15384615;
	aprs_lon = 900000000 + GPS->Longitude * 10000000 / 2;
	aprs_lon = aprs_lon / 26 - aprs_lon / 2710 + aprs_lon / 15384615;
	aprs_alt = GPS->Altitude * 32808 / 10000;

	// Construct the compressed telemetry format
	ax25_base91enc(stlm + 0, 2, seq);
	ax25_base91enc(stlm + 2, 2, GPS->Satellites);
	aprs_temperature = GPS->DS18B20Temperature[0] + 100;
	GPS->BatteryVoltage = 4.321;
	aprs_voltage = GPS->BatteryVoltage * 1000;
	ax25_base91enc(stlm + 4, 2, aprs_temperature);
	ax25_base91enc(stlm + 6, 2, aprs_voltage);
	
    ax25_frame(frames[0], &lengths[0],
		Config.APRS_Callsign,
		Config.APRS_ID,
		APRS_DEVID, 0,
		(GPS->Altitude > Config.APRS_Altitude) ? 0 : 1,	
		(GPS->Altitude > Config.APRS_Altitude) ? Config.APRS_HighPath : 1,
		"!/%s%sO   /A=%06ld|%s|%s",
		ax25_base91enc(slat, 4, aprs_lat),
		ax25_base91enc(slng, 4, aprs_lon),
		aprs_alt, stlm, APRS_COMMENT);
	total_length = lengths[0];
	message_count = 1;	

		// "!/%s%sO   /A=%06ld|%s|%s/%s,%d'C,http://www.pi-in-the-sky.com",
		// ax25_base91enc(slat, 4, aprs_lat),
		// ax25_base91enc(slng, 4, aprs_lon),
		// aprs_alt, stlm, comment, Config.APRS_Callsign, Count);
		

	if (Config.APRS_Telemetry)
	{
		char s[10];

		sprintf(s, strncpy(s, Config.APRS_Callsign, 7));
		if(Config.APRS_ID) snprintf(s + strlen(s), 4, "-%i", Config.APRS_ID);
		
      // Transmit telemetry definitions
		ax25_frame(frames[1], &lengths[1],
		Config.APRS_Callsign,
		Config.APRS_ID,
        APRS_DEVID, 0,
        0, 0,
        ":%-9s:PARM.Satellites,Temperature,Battery",
        s);
		total_length += lengths[1];

		ax25_frame(frames[2], &lengths[2],
			Config.APRS_Callsign,
			Config.APRS_ID,
			APRS_DEVID, 0,
			0, 0,
			":%-9s:UNIT.Sats,deg.C,Volts",
			s);
		total_length += lengths[2];


		ax25_frame(frames[3], &lengths[3],
			Config.APRS_Callsign,
			Config.APRS_ID,
			APRS_DEVID, 0,
			0, 0,
			":%-9s:EQNS.0,1,0, 0,1,-100, 0,0.001,0, 0,1,0, 0,0,0",
			s);
		total_length += lengths[3];

		message_count += 3;	
	}
			
	makeafsk(48000, 1200, 1200, 2200, frames, lengths, message_count, total_length);
}

void LoadAPRSConfig(FILE *fp, struct TConfig *Config)
{
	// APRS settings

	Config->APRS_Altitude = 1500;

	ReadString(fp, "APRS_Callsign", -1, Config->APRS_Callsign, sizeof(Config->APRS_Callsign), 0);
	Config->APRS_ID = ReadInteger(fp, "APRS_ID", -1, 0, 11);
	Config->APRS_Period = ReadInteger(fp, "APRS_Period", -1, 0, 1);
	Config->APRS_Offset = ReadInteger(fp, "APRS_Offset", -1, 0, 0);
	Config->APRS_Random = ReadInteger(fp, "APRS_Random", -1, 0, 0);
	ReadBoolean(fp, "APRS_HighPath", -1, 0, &(Config->APRS_HighPath));
	Config->APRS_Altitude = ReadInteger(fp, "APRS_Altitude", -1, 0, 0);
	ReadBoolean(fp, "APRS_Preemphasis", -1, 0, &(Config->APRS_Preemphasis));
	ReadBoolean(fp, "APRS_Telemetry", -1, 0, &(Config->APRS_Telemetry));
	
	if (*(Config->APRS_Callsign) && Config->APRS_ID && Config->APRS_Period)
	{
		printf("APRS enabled for callsign %s:%d every %d minute%s with offset %ds\n", Config->APRS_Callsign, Config->APRS_ID, Config->APRS_Period, Config->APRS_Period > 1 ? "s" : "", Config->APRS_Offset);
		printf("APRS path above %d metres is %s; below that it is WIDE1-1, WIDE2-1\n", Config->APRS_Altitude, Config->APRS_HighPath ? "WIDE2-1" : "(none)");
		printf("APRS Pre-emphasis is %s\n", Config->APRS_Preemphasis ? "ON" : "OFF");
		printf("APRS Telemetry is %s\n", Config->APRS_Telemetry ? "ON" : "OFF");
	}
}

int TimeToSendAPRS(long GPS_Seconds, long APRS_Period, long APRS_Offset)
{
	return ((GPS_Seconds + APRS_Period - APRS_Offset) % APRS_Period) <= 1;
}

void *APRSLoop(void *some_void_ptr)
{
	struct TGPS *GPS;
	long RandomOffset;

	GPS = (struct TGPS *)some_void_ptr;
	RandomOffset = 0;
	
    while (1)
	{
		if (GPS->Satellites > 3)
		{
			if (TimeToSendAPRS(GPS->SecondsInDay, Config.APRS_Period * 60, Config.APRS_Offset + RandomOffset))
			{
				SendAPRS(GPS);
				
				if (Config.APRS_Random)
				{
					RandomOffset = rand() % Config.APRS_Random;
				}
				
				sleep(2 + Config.APRS_Random);			// So we don't Tx again almost immediately
			}
		}
		sleep(1);
	}
}
