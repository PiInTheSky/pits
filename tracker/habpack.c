#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <linux/i2c-dev.h>
#include <fcntl.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <errno.h>
#include <wiringPi.h>
#include <wiringPiSPI.h>
#include <sys/types.h>
#include <dirent.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include <inttypes.h>

#include "gps.h"
#include "misc.h"
#include "cmp.h"
#include "habpack.h"

int HABpackBufferLength;

static size_t file_writer(cmp_ctx_t *ctx, const void *data, size_t count) {

	uint16_t i;

	for (i = 0; i < count; i++)
	{
		((char*)ctx->buf)[HABpackBufferLength] = *((uint8_t*)data);
		data++;
		HABpackBufferLength++;
	}
	return count;
}

int BuildHABpackPacket(unsigned char *Packet, int Channel, struct TGPS *GPS)
{	
	int32_t _latitude = GPS->Latitude * 10e6;
	int32_t _longitude = GPS->Longitude * 10e6;
	int32_t _altitude = GPS->Altitude;
	uint8_t _hour = GPS->Hours;
	uint8_t _minute = GPS->Minutes;
	uint8_t _second = GPS->Seconds;
	uint8_t _sats = GPS->Satellites;
	uint8_t total_send, send_voltage, send_prediction, send_temperature;

	Config.Channels[Channel].SentenceCounter++;
	
	cmp_ctx_t cmp;
	cmp_init(&cmp, (void*)Packet, 0, file_writer);
	HABpackBufferLength = 0;
	
	send_voltage = ((Config.BoardType != 3) && (Config.BoardType != 4) && (!Config.DisableADC)) ? 1 : 0;
	send_prediction = (Config.EnableLandingPrediction && (Config.PredictionID[0] == '\0')) ? 1 : 0;
	send_temperature = (GPS->DS18B20Count > 1) ? 2 : 1;

	total_send = 5 + send_temperature + send_voltage + send_prediction;
	
	cmp_write_map(&cmp,total_send);

	cmp_write_uint(&cmp, HABPACK_CALLSIGN);
	cmp_write_str(&cmp, Config.Channels[Channel].PayloadID, strlen(Config.Channels[Channel].PayloadID));

	cmp_write_uint(&cmp, HABPACK_SENTENCE_ID);
	cmp_write_uint(&cmp, Config.Channels[Channel].SentenceCounter);

	cmp_write_uint(&cmp, HABPACK_TIME);
	cmp_write_uint(&cmp, (uint32_t)_hour*(3600) + (uint32_t)_minute*60 + (uint32_t)_second);

	cmp_write_uint(&cmp, HABPACK_POSITION);
	cmp_write_array(&cmp, 3);						// 3 fields to follow - lat/lon/alt
	cmp_write_sint(&cmp, _latitude);
	cmp_write_sint(&cmp, _longitude);
	cmp_write_sint(&cmp, _altitude);

	cmp_write_uint(&cmp, HABPACK_GNSS_SATELLITES);
	cmp_write_uint(&cmp, _sats);
	
	cmp_write_uint(&cmp, HABPACK_INTERNAL_TEMPERATURE);
	cmp_write_sint(&cmp, (int32_t)(GPS->DS18B20Temperature[(GPS->DS18B20Count > 1) ? (1-Config.ExternalDS18B20) : 0] * 1000.0));
	if (send_temperature > 1)
	{
		cmp_write_uint(&cmp, HABPACK_EXTERNAL_TEMPERATURE);
		cmp_write_sint(&cmp, (int32_t)(GPS->DS18B20Temperature[Config.ExternalDS18B20] * 1000.0));
	}

	if (send_voltage)
	{
		cmp_write_uint(&cmp, HABPACK_VOLTAGE);
		cmp_write_sint(&cmp, (int32_t)(GPS->BatteryVoltage * 1000));
	}

	// Landing Prediction, if enabled
	if (send_prediction)
	{
		cmp_write_uint(&cmp, HABPACK_PREDICTED_LANDING_POSITION);
		cmp_write_array(&cmp, 2);				// 2 fields to follow, lat/lon
		
		cmp_write_sint(&cmp, GPS->PredictedLatitude * 10e6);
		cmp_write_sint(&cmp, GPS->PredictedLongitude * 10e6);
	}
	
	return HABpackBufferLength;
}

