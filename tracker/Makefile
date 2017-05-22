# Generic makefile, thanks to RJH

SRC=$(wildcard *.c)
HED=$(wildcard *.h)
OBJ=$(SRC:.c=.o) # replaces the .c from SRC with .o
EXE=tracker

ifneq ($(wildcard ex_tracker.h),)
  DEFINES=-DEXTRAS_PRESENT
endif

INDOPT= -bap -bl -blf -bli0 -brs -cbi0 -cdw -cs -ci4 -cli4 -i4 -ip0 -nbc -nce -lp -npcs -nut -pmt -psl -prs -ts4

CC=gcc
CFLAGS=-Wall -O3
LDFLAGS= -lm -lwiringPi -lwiringPiDev -lpthread -lpigpio -lrt
RM=rm

%.o: %.c *.h     # combined w/ next line will compile recently changed .c files
	$(CC) $(CFLAGS) $(DEFINES) -o $@ -c $<

.PHONY : all     # .PHONY ignores files named all
all: $(EXE)      # all is dependent on $(EXE) to be complete

$(EXE): $(OBJ)   # $(EXE) is dependent on all of the files in $(OBJ) to exist
	$(CC) $(OBJ) $(LDFLAGS) -o $@

.PHONY : clean   # .PHONY ignores files named clean
clean:
	-$(RM) $(OBJ) 

tidy:
	indent $(INDOPT) $(SRC) $(HED)
	rm *~
