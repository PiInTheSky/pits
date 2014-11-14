/* ========================================================================== */
/*   gps.c  Modified from:                                                    */
/*                                                                            */
/*   bcm2835.c                                                                */ 
/*   For Raspberry Pi August 2012                                             */
/*   http://www.byvac.com                                                     */
/*                                                                            */
/*   Description                                                              */
/*   This is a bit banged I2C driver that uses GPIO from user space           */
/*   There is much more control over the bus using this method and any pins   */
/*   can be used. The reason for this file is that the BCM hardware does      */
/*   not appear to support clock stretch                                      */  
/*                                                                            */
/*   12/10/14: Modified for the UBlox Max8 on the B+ board                    */
/*                                                                            */
/*                                                                            */
/* ========================================================================== */
// Version 0.1 7/9/2012
// * removed a line of debug code

#include <stdio.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <time.h>
#include <math.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include "bcm2835.h"  //http://www.open.com.au/mikem/bcm2835/
#include "misc.h"
#include "gps.h"

struct bcm2835_i2cbb {
    uint8_t address; // 7 bit address
    uint8_t sda; // pin used for sda coresponds to gpio
    uint8_t scl; // clock
    uint32_t clock_delay; // proporional to bus speed
    uint32_t timeout; //
};

static volatile uint32_t *gpio = MAP_FAILED;
static volatile uint32_t *pwm  = MAP_FAILED;
static volatile uint32_t *clk  = MAP_FAILED;
static volatile uint32_t *pads = MAP_FAILED;
static volatile uint32_t *spi0 = MAP_FAILED;

static	int     fd = -1;
static 	uint8_t *gpioMem = NULL;
static 	uint8_t *pwmMem  = NULL;
static 	uint8_t *clkMem  = NULL;
static 	uint8_t *padsMem = NULL;
static 	uint8_t *spi0Mem = NULL;

//
// Low level register access functions
//


// safe read from peripheral
uint32_t bcm2835_peri_read(volatile uint32_t* paddr)
{
	// Make sure we dont return the _last_ read which might get lost
	// if subsequent code changes to a differnt peripheral
	uint32_t ret = *paddr;
	uint32_t dummy = *paddr;
	return ret;
}

// read from peripheral without the read barrier
uint32_t bcm2835_peri_read_nb(volatile uint32_t* paddr)
{
	return *paddr;
}

// safe write to peripheral
void bcm2835_peri_write(volatile uint32_t* paddr, uint32_t value)
{
	// Make sure we dont rely on the firs write, which may get
	// list if the previous access was to a different peripheral.
	*paddr = value;
	*paddr = value;
}

// write to peripheral without the write barrier
void bcm2835_peri_write_nb(volatile uint32_t* paddr, uint32_t value)
{
	*paddr = value;
}

// Set/clear only the bits in value covered by the mask
void bcm2835_peri_set_bits(volatile uint32_t* paddr, uint32_t value, uint32_t mask)
{
    uint32_t v = bcm2835_peri_read(paddr);
    v = (v & ~mask) | (value & mask);
    bcm2835_peri_write(paddr, v);
}

//
// Low level convenience functions
//

// Function select
// pin is a BCM2835 GPIO pin number NOT RPi pin number
//      There are 6 control registers, each control the functions of a block
//      of 10 pins.
//      Each control register has 10 sets of 3 bits per GPIO pin:
//
//      000 = GPIO Pin X is an input
//      001 = GPIO Pin X is an output
//      100 = GPIO Pin X takes alternate function 0
//      101 = GPIO Pin X takes alternate function 1
//      110 = GPIO Pin X takes alternate function 2
//      111 = GPIO Pin X takes alternate function 3
//      011 = GPIO Pin X takes alternate function 4
//      010 = GPIO Pin X takes alternate function 5
//
// So the 3 bits for port X are:
//      X / 10 + ((X % 10) * 3)
void bcm2835_gpio_fsel(uint8_t pin, uint8_t mode)
{
    // Function selects are 10 pins per 32 bit word, 3 bits per pin
    volatile uint32_t* paddr = gpio + BCM2835_GPFSEL0/4 + (pin/10);
    uint8_t   shift = (pin % 10) * 3;
    uint32_t  mask = BCM2835_GPIO_FSEL_MASK << shift;
    uint32_t  value = mode << shift;
    bcm2835_peri_set_bits(paddr, value, mask);
}

// Set putput pin
void bcm2835_gpio_set(uint8_t pin)
{
    volatile uint32_t* paddr = gpio + BCM2835_GPSET0/4 + pin/32;
    uint8_t shift = pin % 32;
    bcm2835_peri_write(paddr, 1 << shift);
}

// Clear output pin
void bcm2835_gpio_clr(uint8_t pin)
{
    volatile uint32_t* paddr = gpio + BCM2835_GPCLR0/4 + pin/32;
    uint8_t shift = pin % 32;
    bcm2835_peri_write(paddr, 1 << shift);
}

// Read input pin
uint8_t bcm2835_gpio_lev(uint8_t pin)
{
    volatile uint32_t* paddr = gpio + BCM2835_GPLEV0/4 + pin/32;
    uint8_t shift = pin % 32;
    uint32_t value = bcm2835_peri_read(paddr);
    return (value & (1 << shift)) ? HIGH : LOW;
}

// See if an event detection bit is set
// Sigh cant support interrupts yet
uint8_t bcm2835_gpio_eds(uint8_t pin)
{
    volatile uint32_t* paddr = gpio + BCM2835_GPEDS0/4 + pin/32;
    uint8_t shift = pin % 32;
    uint32_t value = bcm2835_peri_read(paddr);
    return (value & (1 << shift)) ? HIGH : LOW;
}

// Write a 1 to clear the bit in EDS
void bcm2835_gpio_set_eds(uint8_t pin)
{
    volatile uint32_t* paddr = gpio + BCM2835_GPEDS0/4 + pin/32;
    uint8_t shift = pin % 32;
    uint32_t value = 1 << shift;
    bcm2835_peri_write(paddr, value);
}

// Rising edge detect enable
void bcm2835_gpio_ren(uint8_t pin)
{
    volatile uint32_t* paddr = gpio + BCM2835_GPREN0/4 + pin/32;
    uint8_t shift = pin % 32;
    uint32_t value = 1 << shift;
    bcm2835_peri_set_bits(paddr, value, value);
}
void bcm2835_gpio_clr_ren(uint8_t pin)
{
    volatile uint32_t* paddr = gpio + BCM2835_GPREN0/4 + pin/32;
    uint8_t shift = pin % 32;
    uint32_t value = 1 << shift;
    bcm2835_peri_set_bits(paddr, 0, value);
}

// Falling edge detect enable
void bcm2835_gpio_fen(uint8_t pin)
{
    volatile uint32_t* paddr = gpio + BCM2835_GPFEN0/4 + pin/32;
    uint8_t shift = pin % 32;
    uint32_t value = 1 << shift;
    bcm2835_peri_set_bits(paddr, value, value);
}
void bcm2835_gpio_clr_fen(uint8_t pin)
{
    volatile uint32_t* paddr = gpio + BCM2835_GPFEN0/4 + pin/32;
    uint8_t shift = pin % 32;
    uint32_t value = 1 << shift;
    bcm2835_peri_set_bits(paddr, 0, value);
}

// High detect enable
void bcm2835_gpio_hen(uint8_t pin)
{
    volatile uint32_t* paddr = gpio + BCM2835_GPHEN0/4 + pin/32;
    uint8_t shift = pin % 32;
    uint32_t value = 1 << shift;
    bcm2835_peri_set_bits(paddr, value, value);
}
void bcm2835_gpio_clr_hen(uint8_t pin)
{
    volatile uint32_t* paddr = gpio + BCM2835_GPHEN0/4 + pin/32;
    uint8_t shift = pin % 32;
    uint32_t value = 1 << shift;
    bcm2835_peri_set_bits(paddr, 0, value);
}

// Low detect enable
void bcm2835_gpio_len(uint8_t pin)
{
    volatile uint32_t* paddr = gpio + BCM2835_GPLEN0/4 + pin/32;
    uint8_t shift = pin % 32;
    uint32_t value = 1 << shift;
    bcm2835_peri_set_bits(paddr, value, value);
}
void bcm2835_gpio_clr_len(uint8_t pin)
{
    volatile uint32_t* paddr = gpio + BCM2835_GPLEN0/4 + pin/32;
    uint8_t shift = pin % 32;
    uint32_t value = 1 << shift;
    bcm2835_peri_set_bits(paddr, 0, value);
}

// Async rising edge detect enable
void bcm2835_gpio_aren(uint8_t pin)
{
    volatile uint32_t* paddr = gpio + BCM2835_GPAREN0/4 + pin/32;
    uint8_t shift = pin % 32;
    uint32_t value = 1 << shift;
    bcm2835_peri_set_bits(paddr, value, value);
}
void bcm2835_gpio_clr_aren(uint8_t pin)
{
    volatile uint32_t* paddr = gpio + BCM2835_GPAREN0/4 + pin/32;
    uint8_t shift = pin % 32;
    uint32_t value = 1 << shift;
    bcm2835_peri_set_bits(paddr, 0, value);
}

// Async falling edge detect enable
void bcm2835_gpio_afen(uint8_t pin)
{
    volatile uint32_t* paddr = gpio + BCM2835_GPAFEN0/4 + pin/32;
    uint8_t shift = pin % 32;
    uint32_t value = 1 << shift;
    bcm2835_peri_set_bits(paddr, value, value);
}
void bcm2835_gpio_clr_afen(uint8_t pin)
{
    volatile uint32_t* paddr = gpio + BCM2835_GPAFEN0/4 + pin/32;
    uint8_t shift = pin % 32;
    uint32_t value = 1 << shift;
    bcm2835_peri_set_bits(paddr, 0, value);
}

// Set pullup/down
void bcm2835_gpio_pud(uint8_t pud)
{
    volatile uint32_t* paddr = gpio + BCM2835_GPPUD/4;
    bcm2835_peri_write(paddr, pud);
}

// Pullup/down clock
// Clocks the value of pud into the GPIO pin
void bcm2835_gpio_pudclk(uint8_t pin, uint8_t on)
{
    volatile uint32_t* paddr = gpio + BCM2835_GPPUDCLK0/4 + pin/32;
    uint8_t shift = pin % 32;
    bcm2835_peri_write(paddr, (on ? 1 : 0) << shift);
}

// Read GPIO pad behaviour for groups of GPIOs
uint32_t bcm2835_gpio_pad(uint8_t group)
{
    volatile uint32_t* paddr = pads + BCM2835_PADS_GPIO_0_27/4 + group*2;
    return bcm2835_peri_read(paddr);
}

// Set GPIO pad behaviour for groups of GPIOs
// powerup value for al pads is
// BCM2835_PAD_SLEW_RATE_UNLIMITED | BCM2835_PAD_HYSTERESIS_ENABLED | BCM2835_PAD_DRIVE_8mA
void bcm2835_gpio_set_pad(uint8_t group, uint32_t control)
{
    volatile uint32_t* paddr = pads + BCM2835_PADS_GPIO_0_27/4 + group*2;
    bcm2835_peri_write(paddr, control);
}

// Some convenient arduino like functions
// milliseconds
void delayMilliseconds (unsigned int millis)
{
  struct timespec sleeper, dummy ;

  sleeper.tv_sec  = (time_t)(millis / 1000) ;
  sleeper.tv_nsec = (long)(millis % 1000) * 1000000 ;
  nanosleep (&sleeper, &dummy) ;
}

// microseconds
void delayMicroseconds (unsigned int micros)
{
/*
  struct timespec sleeper, dummy ;

  sleeper.tv_sec  = 0 ;
  sleeper.tv_nsec = (long)(micros * 1000) ;
  nanosleep (&sleeper, &dummy) ;
*/
	usleep(micros);
}

//
// Higher level convenience functions
//

// Set the state of an output
void bcm2835_gpio_write(uint8_t pin, uint8_t on)
{
    if (on)
	bcm2835_gpio_set(pin);
    else
	bcm2835_gpio_clr(pin);
}

// Set the pullup/down resistor for a pin
//
// The GPIO Pull-up/down Clock Registers control the actuation of internal pull-downs on
// the respective GPIO pins. These registers must be used in conjunction with the GPPUD
// register to effect GPIO Pull-up/down changes. The following sequence of events is
// required:
// 1. Write to GPPUD to set the required control signal (i.e. Pull-up or Pull-Down or neither
// to remove the current Pull-up/down)
// 2. Wait 150 cycles – this provides the required set-up time for the control signal
// 3. Write to GPPUDCLK0/1 to clock the control signal into the GPIO pads you wish to
// modify – NOTE only the pads which receive a clock will be modified, all others will
// retain their previous state.
// 4. Wait 150 cycles – this provides the required hold time for the control signal
// 5. Write to GPPUD to remove the control signal
// 6. Write to GPPUDCLK0/1 to remove the clock
//
// RPi has P1-03 and P1-05 with 1k8 pullup resistor
void bcm2835_gpio_set_pud(uint8_t pin, uint8_t pud)
{
    bcm2835_gpio_pud(pud);
    delayMicroseconds(10);
    bcm2835_gpio_pudclk(pin, 1);
    delayMicroseconds(10);
    bcm2835_gpio_pud(BCM2835_GPIO_PUD_OFF);
    bcm2835_gpio_pudclk(pin, 0);
}

// *****************************************************************************
// open bus, sets structure and initialises GPIO
// The scl and sda line are set to be always 0 (low) output, when a high is
// required they are set to be an input.
// *****************************************************************************
int bcm2835_i2cbb_open(struct bcm2835_i2cbb *bb,
        uint8_t adr, // 7 bit address
        uint8_t data,   // GPIO pin for data 
        uint8_t clock,  // GPIO pin for clock
        uint32_t speed, // clock delay 250 = 100KHz 500 = 50KHz (apx)
        uint32_t timeout) // clock stretch & timeout
{
    bb->address = adr;
    bb->sda = data;
    bb->scl = clock;
    bb->clock_delay = speed;
    bb->timeout = timeout;
    if (!bcm2835_init())  {
	    fprintf(stderr, "bcm2835_i2cbb: Unable to open: %s\n", strerror(errno)) ;
    	return 1;
    }
    // also they should be set low, input - output determins level
    bcm2835_gpio_write(bb->sda, LOW);
    bcm2835_gpio_write(bb->scl, LOW);
    // both pins should be input at start
    bcm2835_gpio_fsel(bb->sda, BCM2835_GPIO_FSEL_INPT);
    bcm2835_gpio_fsel(bb->scl, BCM2835_GPIO_FSEL_INPT);
    return 0;
}

// *****************************************************************************
// bit delay, determins bus speed. nanosleep does not give the required delay
// its too much, by about a factor of 100
// This simple delay using the current Aug 2012 board gives aproximately:
// 500 = 50kHz. Obviously a better method of delay is needed.
// *****************************************************************************
void bcm2835_i2cbb_bitdelay(uint32_t del)
{
    //while (del--);
	usleep(10);
}

// *****************************************************************************
// clock with stretch - bit level
// puts clock line high and checks that it does go high. When bit level
// stretching is used the clock needs checking at each transition
// *****************************************************************************
int bcm2835_i2cbb_sclH(struct bcm2835_i2cbb *bb)
{
    uint32_t to = bb->timeout;
    bcm2835_gpio_fsel(bb->scl, BCM2835_GPIO_FSEL_INPT); // high
    // check that it is high
    while(!bcm2835_gpio_lev(bb->scl))
	{
		/*
        if(!to--) {
            fprintf(stderr, "bcm2835_i2cbb: Clock line held by slave\n");
            return (1);
        }
		*/
    }
    return (0);
}
// *****************************************************************************
// other line conditions
// *****************************************************************************
void bcm2835_i2cbb_sclL(struct bcm2835_i2cbb *bb)
{
    bcm2835_gpio_fsel(bb->scl, BCM2835_GPIO_FSEL_OUTP);
}
void bcm2835_i2cbb_sdaH(struct bcm2835_i2cbb *bb)
{
    bcm2835_gpio_fsel(bb->sda, BCM2835_GPIO_FSEL_INPT);
}
void bcm2835_i2cbb_sdaL(struct bcm2835_i2cbb *bb)
{
    bcm2835_gpio_fsel(bb->sda, BCM2835_GPIO_FSEL_OUTP);
}

// *****************************************************************************
// Returns 1 if bus is free, i.e. both sda and scl high
// *****************************************************************************
int bcm2835_i2cbb_free(struct bcm2835_i2cbb *bb)
{
    return(bcm2835_gpio_lev(bb->sda) && bcm2835_gpio_lev(bb->scl));
}

// *****************************************************************************
// Start condition
// This is when sda is pulled low when clock is high. This also puls the clock
// low ready to send or receive data so both sda and scl end up low after this.
// *****************************************************************************
int bcm2835_i2cbb_start(struct bcm2835_i2cbb *bb)
{
    uint32_t to = bb->timeout;
    // bus must be free for start condition
    while(to--)
        if(bcm2835_i2cbb_free(bb)) break;

    if(to <=0) {
        fprintf(stderr, "bcm2835_i2cbb: Cannot set start condition\n");
        return 1;
    }

    // start condition is when data linegoes low when clock is high
    bcm2835_i2cbb_sdaL(bb);
    bcm2835_i2cbb_bitdelay((bb->clock_delay)/2);
    bcm2835_i2cbb_sclL(bb); // clock low now ready to send data
    bcm2835_i2cbb_bitdelay(bb->clock_delay);
    return 0;
}

// *****************************************************************************
// re-start condition, SDA goes low when SCL is high
// then set SCL low in prep for sending data
// for restart the SCL line is low already so this needs setting high first
// *****************************************************************************
int bcm2835_i2cbb_restart(struct bcm2835_i2cbb *bb)
{
    uint32_t to = bb->timeout;
    // bus must be free for start condition
    while(to--)
        if(bcm2835_i2cbb_free(bb)) break;

    if(to <=0) {
        fprintf(stderr, "bcm2835_i2cbb: Cannot set re-start condition\n");
        return 1;
    }

    // start condition is when data linegoes low when clock is high
    bcm2835_i2cbb_sdaL(bb);
    bcm2835_i2cbb_bitdelay(bb->clock_delay);
    bcm2835_i2cbb_sclL(bb); // clock low now ready to send data
    bcm2835_i2cbb_sdaH(bb);
    bcm2835_i2cbb_bitdelay(bb->clock_delay);
    return 0;
}

// *****************************************************************************
// stop condition
// when the clock is high, sda goes from low to high
// *****************************************************************************
void bcm2835_i2cbb_stop(struct bcm2835_i2cbb *bb)
{
    bcm2835_i2cbb_sdaL(bb); // needs to be low for this
    bcm2835_i2cbb_bitdelay(bb->clock_delay);
    bcm2835_i2cbb_sclH(bb); // clock will be low from read/write, put high
    bcm2835_i2cbb_bitdelay(bb->clock_delay);
    bcm2835_i2cbb_sdaH(bb); // release bus
}

// *****************************************************************************
// sends a byte to the bus, this is an 8 bit unit so could be address or data
// msb first
// returns 1 for NACK and 0 for ACK (0 is good)
// *****************************************************************************
int bcm2835_i2cbb_send(struct bcm2835_i2cbb *bb, uint8_t value)
{
    uint32_t rv;
    uint8_t j, mask=0x80;

    // clock is already low from start condition
    for(j=0;j<8;j++) {
        bcm2835_i2cbb_bitdelay(bb->clock_delay);
        if(value & mask) bcm2835_i2cbb_sdaH(bb);
        else bcm2835_i2cbb_sdaL(bb);
        // clock out data
        bcm2835_i2cbb_sclH(bb);  // clock it out
        bcm2835_i2cbb_bitdelay(bb->clock_delay);
        bcm2835_i2cbb_sclL(bb);      // back to low so data can change
        mask>>= 1;      // next bit along
    }
    // release bus for slave ack or nack
    bcm2835_i2cbb_sdaH(bb);
    bcm2835_i2cbb_bitdelay(bb->clock_delay);
    bcm2835_i2cbb_sclH(bb);     // and clock high tels slave to NACK/ACK
    bcm2835_i2cbb_bitdelay(bb->clock_delay); // delay for slave to act
    rv=bcm2835_gpio_lev(bb->sda);     // get ACK, NACK from slave
    bcm2835_i2cbb_sclL(bb);     // low to keep hold of bus as start condition
//    bcm2835_i2cbb_bitdelay(bb->clock_delay);
//    bcm2835_i2cbb_scl(bb, 1);     // idle state ready for stop or start
    return rv;
}

// *****************************************************************************
// receive 1 char from bus
// Input
// send: 1=nack, (last byte) 0 = ack (get another)
// *****************************************************************************
uint8_t bcm2835_i2cbb_read(struct bcm2835_i2cbb *bb, uint8_t ack)
{
    uint8_t j, data=0;

    for(j=0;j<8;j++) {
        data<<= 1;      // shift in
        bcm2835_i2cbb_bitdelay(bb->clock_delay);
        bcm2835_i2cbb_sclH(bb);;      // set clock high to get data
        bcm2835_i2cbb_bitdelay(bb->clock_delay); // delay for slave
        if(bcm2835_gpio_lev(bb->sda)) data++;   // get data
        bcm2835_i2cbb_sclL(bb);  // clock back to low
    }

   // clock has been left low at this point
   // send ack or nack
   bcm2835_i2cbb_bitdelay(bb->clock_delay);
   if(ack) bcm2835_i2cbb_sdaH(bb);
   else bcm2835_i2cbb_sdaL(bb);
   bcm2835_i2cbb_bitdelay(bb->clock_delay);
   bcm2835_i2cbb_sclH(bb);    // clock it in
   bcm2835_i2cbb_bitdelay(bb->clock_delay);
   bcm2835_i2cbb_sclL(bb); // bak to low
   bcm2835_i2cbb_sdaH(bb);      // release data line
   return data;
}

// =============================================================================
// Common scenarios for I2C
// =============================================================================
// *****************************************************************************
// writes just one byte to the given address
// *****************************************************************************
void bcm8235_i2cbb_putc(struct bcm2835_i2cbb *bb, uint8_t value)
{
    bcm2835_i2cbb_start(bb);
    bcm2835_i2cbb_send(bb, bb->address * 2); // address
    bcm2835_i2cbb_send(bb, value);
    bcm2835_i2cbb_stop(bb); // stop    
}
// *****************************************************************************
// writes buffer
// *****************************************************************************
void bcm8235_i2cbb_puts(struct bcm2835_i2cbb *bb, uint8_t *s, uint32_t len)
{
    bcm2835_i2cbb_start(bb);
    bcm2835_i2cbb_send(bb, bb->address * 2); // address
    while(len) {
        bcm2835_i2cbb_send(bb, *(s++));
        len--;
    }
    bcm2835_i2cbb_stop(bb); // stop    
}
// *****************************************************************************
// read one byte
// *****************************************************************************
uint8_t bcm8235_i2cbb_getc(struct bcm2835_i2cbb *bb)
{
    uint8_t rv;
    bcm2835_i2cbb_start(bb);
    bcm2835_i2cbb_send(bb, (bb->address * 2)+1); // address
    rv = bcm2835_i2cbb_read(bb, 1);
    bcm2835_i2cbb_stop(bb); // stop
    return rv;    
}
// *****************************************************************************
// reads into buffer
// *****************************************************************************
void bcm8235_i2cbb_gets(struct bcm2835_i2cbb *bb, uint8_t *buf, uint32_t len)
{
    uint8_t *bp = buf;
    bcm2835_i2cbb_start(bb);
    bcm2835_i2cbb_send(bb, (bb->address * 2)+1); // address
    while(len) {
        if(len == 1) {
            *(bp++) = bcm2835_i2cbb_read(bb, 1);
            *bp = 0; // in case its a string
            break;
        }                        
        *(bp++) = bcm2835_i2cbb_read(bb, 0);
        len--;
    }
    bcm2835_i2cbb_stop(bb); // stop    
}

// Initialise this library
int bcm2835_init()
{
	uint8_t *mapaddr;

	// Open the master /dev/memory device
	if ((fd = open("/dev/mem", O_RDWR | O_SYNC) ) < 0)
	{
	    fprintf(stderr, "bcm2835_init: Unable to open /dev/mem: %s\n", strerror(errno)) ;
	    return 0;
	}
	
	// GPIO:
	// Allocate 2 pages - 1 ...
	if ((gpioMem = malloc(BCM2835_BLOCK_SIZE + (BCM2835_PAGE_SIZE - 1))) == NULL)
	{
	    fprintf(stderr, "bcm2835_init: malloc failed: %s\n", strerror(errno)) ;
	    return 0;
	}
    
	// ... to make sure we can round it up to a whole page size
	mapaddr = gpioMem;
	if (((uint32_t)mapaddr % BCM2835_PAGE_SIZE) != 0)
	    mapaddr += BCM2835_PAGE_SIZE - ((uint32_t)mapaddr % BCM2835_PAGE_SIZE) ;
    
	gpio = (uint32_t *)mmap(mapaddr, BCM2835_BLOCK_SIZE, PROT_READ|PROT_WRITE, MAP_SHARED|MAP_FIXED, fd, BCM2835_GPIO_BASE) ;
    
	if ((int32_t)gpio < 0)
	{
	    fprintf(stderr, "bcm2835_init: mmap failed: %s\n", strerror(errno)) ;
	    return 0;
	}
    
	// PWM
	if ((pwmMem = malloc(BCM2835_BLOCK_SIZE + (BCM2835_PAGE_SIZE - 1))) == NULL)
	{
	    fprintf(stderr, "bcm2835_init: pwmMem malloc failed: %s\n", strerror(errno)) ;
	    return 0;
	}
    
	mapaddr = pwmMem;
	if (((uint32_t)mapaddr % BCM2835_PAGE_SIZE) != 0)
	    mapaddr += BCM2835_PAGE_SIZE - ((uint32_t)mapaddr % BCM2835_PAGE_SIZE) ;
    
	pwm = (uint32_t *)mmap(mapaddr, BCM2835_BLOCK_SIZE, PROT_READ|PROT_WRITE, MAP_SHARED|MAP_FIXED, fd, BCM2835_GPIO_PWM) ;
    
	if ((int32_t)pwm < 0)
	{
	    fprintf(stderr, "bcm2835_init: mmap failed (pwm): %s\n", strerror(errno)) ;
	    return 0;
	}
    
	// Clock control (needed for PWM)
	if ((clkMem = malloc(BCM2835_BLOCK_SIZE + (BCM2835_PAGE_SIZE-1))) == NULL)
	{
	    fprintf(stderr, "bcm2835_init: clkMem malloc failed: %s\n", strerror(errno)) ;
	    return 0;
	}
    
	mapaddr = clkMem;
	if (((uint32_t)mapaddr % BCM2835_PAGE_SIZE) != 0)
	    mapaddr += BCM2835_PAGE_SIZE - ((uint32_t)mapaddr % BCM2835_PAGE_SIZE) ;
    
	clk = (uint32_t *)mmap(mapaddr, BCM2835_BLOCK_SIZE, PROT_READ|PROT_WRITE, MAP_SHARED|MAP_FIXED, fd, BCM2835_CLOCK_BASE) ;
    
	if ((int32_t)clk < 0)
	{
	    fprintf(stderr, "bcm2835_init: mmap failed (clk): %s\n", strerror(errno)) ;
	    return 0;
	}
    
	if ((padsMem = malloc(BCM2835_BLOCK_SIZE + (BCM2835_PAGE_SIZE - 1))) == NULL)
	{
	    fprintf(stderr, "bcm2835_init: padsMem malloc failed: %s\n", strerror(errno)) ;
	    return 0;
	}
    
	mapaddr = padsMem;
	if (((uint32_t)mapaddr % BCM2835_PAGE_SIZE) != 0)
	    mapaddr += BCM2835_PAGE_SIZE - ((uint32_t)mapaddr % BCM2835_PAGE_SIZE) ;
    
	pads = (uint32_t *)mmap(mapaddr, BCM2835_BLOCK_SIZE, PROT_READ|PROT_WRITE, MAP_SHARED|MAP_FIXED, fd, BCM2835_GPIO_PADS) ;
    
	if ((int32_t)pads < 0)
	{
	    fprintf(stderr, "bcm2835_init: mmap failed (pads): %s\n", strerror(errno)) ;
	    return 0;
	}

	if ((spi0Mem = malloc(BCM2835_BLOCK_SIZE + (BCM2835_PAGE_SIZE - 1))) == NULL)
	{
	    fprintf(stderr, "bcm2835_init: spi0Mem malloc failed: %s\n", strerror(errno)) ;
	    return 0;
	}
    
	mapaddr = spi0Mem;
	if (((uint32_t)mapaddr % BCM2835_PAGE_SIZE) != 0)
	    mapaddr += BCM2835_PAGE_SIZE - ((uint32_t)mapaddr % BCM2835_PAGE_SIZE) ;
    
	spi0 = (uint32_t *)mmap(mapaddr, BCM2835_BLOCK_SIZE, PROT_READ|PROT_WRITE, MAP_SHARED|MAP_FIXED, fd, BCM2835_SPI0_BASE) ;
    
	if ((int32_t)spi0 < 0)
	{
	    fprintf(stderr, "bcm2835_init: mmap failed (spi0): %s\n", strerror(errno)) ;
	    return 0;
	}

	return 1; // Success
}


int GPSChecksumOK(unsigned char *Buffer, int Count)
{
  unsigned char XOR, i, c;

  XOR = 0;
  for (i = 1; i < (Count-4); i++)
  {
    c = Buffer[i];
    XOR ^= c;
  }

  return (Buffer[Count-4] == '*') && (Buffer[Count-3] == Hex(XOR >> 4)) && (Buffer[Count-2] == Hex(XOR & 15));
}


void SendUBX(struct bcm2835_i2cbb *bb, unsigned char *MSG, int len)
{
	bcm8235_i2cbb_puts(bb, MSG, len);
}

void SetFlightMode(struct bcm2835_i2cbb *bb)
{
    // Send navigation configuration command
    unsigned char setNav[] = {0xB5, 0x62, 0x06, 0x24, 0x24, 0x00, 0xFF, 0xFF, 0x06, 0x03, 0x00, 0x00, 0x00, 0x00, 0x10, 0x27, 0x00, 0x00, 0x05, 0x00, 0xFA, 0x00, 0xFA, 0x00, 0x64, 0x00, 0x2C, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x16, 0xDC};
    SendUBX(bb, setNav, sizeof(setNav));
	printf ("Setting flight mode\n");
}

float FixPosition(float Position)
{
	float Minutes, Seconds;
	
	Position = Position / 100;
	
	Minutes = trunc(Position);
	Seconds = fmod(Position, 1);

	return Minutes + Seconds * 5 / 3;
}

void ProcessLine(struct bcm2835_i2cbb *bb, struct TGPS *GPS, char *Buffer, int Count)
{
    float utc_time, latitude, longitude, hdop, altitude, speed, course;
	int lock, satellites, date;
	char ns, ew, units;
	
    if (GPSChecksumOK(Buffer, Count))
	{
		satellites = 0;
	
		if (strncmp(Buffer+3, "GGA", 3) == 0)
		{
			if (sscanf(Buffer+7, "%f,%f,%c,%f,%c,%d,%d,%f,%f,%c", &utc_time, &latitude, &ns, &longitude, &ew, &lock, &satellites, &hdop, &altitude, &units) >= 1)
			{	
				// $GPGGA,124943.00,5157.01557,N,00232.66381,W,1,09,1.01,149.3,M,48.6,M,,*42
				if (satellites >= 4)
				{
					GPS->Time = utc_time;
					GPS->Latitude = FixPosition(latitude);
					if (ns == 'S') GPS->Latitude = -GPS->Latitude;
					GPS->Longitude = FixPosition(longitude);
					if (ew == 'W') GPS->Longitude = -GPS->Longitude;
					GPS->Altitude = altitude;
				}
				GPS->Satellites = satellites;
			}
			if (Config.EnableGPSLogging)
			{
				WriteLog("gps.txt", Buffer);
			}
		}
		else if (strncmp(Buffer+3, "RMC", 3) == 0)
		{
			if (sscanf(Buffer+7, "%f,%f,%c,%f,%c,%f,%f,%d", &utc_time, &latitude, &ns, &longitude, &ew, &speed, &course, &date) >= 1)
			{
				// $GPRMC,124943.00,A,5157.01557,N,00232.66381,W,0.039,,200314,,,A*6C
				GPS->Speed = (int)speed;
				GPS->Direction = (int)course;
			}

			if (Config.EnableGPSLogging)
			{
				WriteLog("gps.txt", Buffer);
			}
		}
		else if (strncmp(Buffer+3, "GSV", 3) == 0)
        {
            // Disable GSV
            printf("Disabling GSV\r\n");
            unsigned char setGSV[] = { 0xB5, 0x62, 0x06, 0x01, 0x08, 0x00, 0xF0, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x03, 0x39 };
            SendUBX(bb, setGSV, sizeof(setGSV));
        }
		else if (strncmp(Buffer+3, "GLL", 3) == 0)
        {
            // Disable GLL
            printf("Disabling GLL\r\n");
            unsigned char setGLL[] = { 0xB5, 0x62, 0x06, 0x01, 0x08, 0x00, 0xF0, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x2B };
            SendUBX(bb, setGLL, sizeof(setGLL));
        }
		else if (strncmp(Buffer+3, "GSA", 3) == 0)
        {
            // Disable GSA
            printf("Disabling GSA\r\n");
            unsigned char setGSA[] = { 0xB5, 0x62, 0x06, 0x01, 0x08, 0x00, 0xF0, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x02, 0x32 };
            SendUBX(bb, setGSA, sizeof(setGSA));
        }
		else if (strncmp(Buffer+3, "VTG", 3) == 0)
        {
            // Disable VTG
            printf("Disabling VTG\r\n");
            unsigned char setVTG[] = {0xB5, 0x62, 0x06, 0x01, 0x08, 0x00, 0xF0, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x05, 0x47};
            SendUBX(bb, setVTG, sizeof(setVTG));
        }
        else
        {
            printf("Unknown NMEA sentence: %s\n", Buffer);
        }
    }
    else
    {
       printf("Bad checksum\r\n");
	}
}


void *GPSLoop(void *some_void_ptr)
{
	unsigned char Line[100];
	int id, Length;
	struct bcm2835_i2cbb bb;
	int i;
	struct TGPS *GPS;

	GPS = (struct TGPS *)some_void_ptr;
	
	if (bcm2835_i2cbb_open(&bb, 0x42, Config.SDA, Config.SCL, 250, 1000000))		// struct, i2c address, SDA, SCL, ?, ?
	{
		printf("Failed to open I2C\n");
		exit(1);
	}

	Length = 0;

    while (1)
    {
        int i;
		unsigned char Character;

        bcm2835_i2cbb_start(&bb);

		SetFlightMode(&bb);

        for (i=0; i<10000; i++)
        {
            Character = bcm8235_i2cbb_getc(&bb);

			if (Character == 0xFF)
			{
				delayMilliseconds (100);
			}
            else if (Character == '$')
			{
				Line[0] = Character;
				Length = 1;
			}
            else if (Length > 90)
			{
				Length = 0;
            }
            else if ((Length > 0) && (Character != '\r'))
            {
               	Line[Length++] = Character;
               	if (Character == '\n')
               	{
               		Line[Length] = '\0';
               		ProcessLine(&bb, GPS, Line, Length);
					delayMilliseconds (100);
               		Length = 0;
               	}
            }
		}

		bcm2835_i2cbb_stop(&bb);
	}
}


