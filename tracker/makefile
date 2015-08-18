tracker: tracker.o gps.o misc.o DS18B20.o adc.o adc_i2c.o snapper.o led.o bmp085.o aprs.o lora.o prediction.o
	cc -o tracker tracker.o gps.o misc.o DS18B20.o adc.o adc_i2c.o snapper.o led.o bmp085.o aprs.o  lora.o prediction.o -lm -lwiringPi -lwiringPiDev -pthread


tracker.o: tracker.c gps.h misc.h DS18B20.h adc.h adc_i2c.h bmp085.h lora.h aprs.h prediction.h
	gcc -c -pthread tracker.c

gps.o: gps.c gps.h misc.h
	gcc -c -pthread gps.c

DS18B20.o: DS18B20.c DS18B20.h misc.h gps.h
	gcc -c -pthread DS18B20.c

adc.o: adc.c adc.h misc.h gps.h
	gcc -c -pthread adc.c
	
adc_i2c.o: adc_i2c.c adc_i2c.h misc.h gps.h
	gcc -c -pthread adc_i2c.c
	
snapper.o: snapper.c snapper.h misc.h
	gcc -c -pthread snapper.c

led.o: led.c led.h misc.h
	gcc -c -pthread led.c
	
misc.o: misc.c misc.h
	gcc -c -pthread misc.c

bmp085.o: bmp085.c bmp085.h misc.h
	gcc -c -pthread bmp085.c
	
aprs.o: aprs.c aprs.h misc.h sine_table.h gps.h
	gcc -c -pthread aprs.c

lora.o: lora.c lora.h misc.h gps.h
	gcc -c -pthread lora.c

prediction.o: prediction.c prediction.h misc.h gps.h
	gcc -c -pthread prediction.c
	