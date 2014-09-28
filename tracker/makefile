tracker: tracker.o gps.o misc.o DS18B20.o adc.o snapper.o led.o bmp085.o
	cc -o tracker tracker.o gps.o misc.o DS18B20.o adc.o snapper.o led.o bmp085.o -lm -lwiringPi -lwiringPiDev -l pthread

tracker.o: tracker.c gps.h misc.h DS18B20.h adc.h bmp085.h
	gcc -c tracker.c

gps.o: gps.c gps.h misc.h
	gcc -c gps.c

DS18B20.o: DS18B20.c DS18B20.h misc.h gps.h
	gcc -c DS18B20.c

adc.o: adc.c adc.h
	gcc -c adc.c
	
snapper.o: snapper.c snapper.h misc.h
	gcc -c snapper.c

led.o: led.c led.h misc.h
	gcc -c led.c
	
misc.o: misc.c misc.h
	gcc -c misc.c

bmp085.o: bmp085.c bmp085.h
	gcc -c bmp085.c
