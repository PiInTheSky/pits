# PITS - HAB Tracker software for the PITS boards #

Created by Dave Akerman (dave@sccs.co.uk)

This software is written for the PITS Zero with Pi Zero, PITS+ with the various A+/B+ models, and the original PITS with the Pi A/B boards.

PITS Zero and PITS+ can be purchased from board from http://ava.upuaut.net/store/.

Software support is provided for customers who have purchased a PITS, PITS Zero or PITS+ board, for use with that board only.

## Installation ##

Follow the instructions at http://www.pi-in-the-sky.com/index.php?id=sd-card-image-from-scratch


## USB Camera ##

To use a USB camera (i.e. a compact or SLR) instead of the Pi camera, you need to install gphoto2 and imagemagick:

	sudo apt-get install gphoto2 imagemagick

and you need to edit /boot/pisky.txt and change the "camera" line to be:

	Camera=G

(G/g are for gphoto2, U/F/u/f for fswebcam; N/n is for no camera, Y/y/1/TC/c is for CSI camera).

The camera must be able to support remote capture as well as image download, and this excludes a large number of cameras in particular all Canon compacts since 2009.
There's a list of cameras at http://www.gphoto.org/doc/remote/.

The image resolution is not controlled so you should set this up in the camera.  The full image files are stored on Pi SD card, so ensure that it has sufficient free capacity.

If you are transmitting images with SSDV, then these need to be resized before transmission.  This is configured in pisky.txt in the usual way. 
The process though is different - the camera takes the full sized image, and this image is downloaded to the Pi unaltered.  Before transmission the selected images are then resized using imagemagick.
So, regardless of the image size settings in pisky.txt, the Pi SD card will contain the full-size images.

PITS does not set the camera exposure.  Normally with the camera set to an automatic exposure mode you will get plenty of good images, but you can of course set it to any mode you wish including manual.
If you wish to control this from the software rather than on the camera, then create an executable script "take_image" (there is a sample supplied as "take_image.sample").
Note that most settings are only available in the more basic modes - e.g. the aperture can only be controlled in aperture-priority or manual modes.

gphoto2 needs more work than the other camera options, and we strongly recommend some experimentation with the camera you intend to use.  See http://gphoto.org/doc/manual/using-gphoto2.html.


## USB Webcam ##

To use a USB webcam instead of the Pi camera, you need to install fswebcam:

	sudo apt-get install fswebcam

and you need to edit /boot/pisky.txt and change the "camera" line to be:

	Camera=U


## IMAGE PROCESSING ##

All images now include telemetry (longitude, latitude, altitude) in the JPEG comment field, **but only if EXIV2 has been installed**.

It is therefore possible to overlay downloaded images with telemetry data, as text or graphics, using (for example) ImageMagick, and EXIV2 to extract the data from the JPEG being processed.  A sample script "process_image.sample" is provided; to use this, rename to be "process_image", make it executable, and edit to your requirements.  Please note that the sample assumes a particular image resolution and you will need to change the pixel positions of the various items.  Imagemagick is quite complex to use but there is plenty of documentation on the web plus many samples. 


## Change Log ##

09/04/2018
==========

- Added HABPack support for LoRa, using encoder by Phil Crump.  Enable with LORA_HABPack_n=Y

04/02/2018
==========

- Fixed APRS pre-emphasis which was actually de-emphasis!

21/09/2017
==========

- Add LoRa mode 8 for SSDV repeater network

15/09/2017
==========

- Re-enabled temperature sensing for Pi Zero / W

05/09/2017
==========

- Print list of fields when sending first sentence for a channel.  Useful for setting up payload document.

01/09/2017
==========

- Reduce amount of Rx-related guff when in listen-only mode

19/08/2017
==========

- Changed startup to use systemd

10/04/2017
==========

- Added support direct upload to Habitat using an internet connection
 
03/03/2017
==========

- Added support for Pi Zero W 
 
07/11/2016
==========

- Added support for USB cameras via gphoto2

19/09/2016
==========

- Red (warn) LED now flashes during wait for GPS lock; if it's on without flashing then there is no GPS data (which means GPS misconfigured, or Pi revision not recognised)

18/09/2016
==========

- Full list of Pi Zero boards
- Ignore empty CRLF lines in pisky.txt

29/09/2016
==========

- Better CPU/Pi test

25/09/2016
==========

- Added SSDV_settings to pisky.txt - add command-line parameter when calling SSDV program- Removed support for running without device tree
- Fixed setting of implicit/explicit mode manually, ditto low-data-rate optimisation
- Truncate SSDV images larger than max size (currently 4096 packets)

12/09/2016
==========

- Settable delay between packets
- Minimal "buoy mode" telemetry

14/08/2016
==========

- Added option for serial GPS connection.  Use GPS_Device=/dev/ttyACM0 for example.
- Re-enabled APRS script

12/08/2016
==========

- Fixed error where LoRa channel stopped transmitting after a few sentences if SSDV disabled on that channel
- Increased maximum size of SSDV file to 4096 packets
- Add option to disable reading of ADC, and also transmission of ADC values

17/06/2016
==========

- BME280 driver

18/05/2015
==========

- Ability to receive SMS-style messages uplinked from the ground via LoRa.  Messages are saved to a text file and can then be processed externally (e.g. by supplied Python script to display on a Pi Sense HAT)
- Ability to include, in the LoRa telemetry, CSV data from an external file.  Sample Python script supplied to build that file from Sense HAT sensor data.
- Ability to re-send SSDV packets, via LoRa, that were not received on the ground.  Uses a LoRa uplink message to define which packets need to be sent.
- Can include uplink message status in LoRa telemetry.
- Photo sizes forced to be no smaller than 320x240, to avoid limitation in raspistill program.
- RTTY can be switched off (no data no carrier) during LoRa uplink periods.
- Use of EXIV2 (if installed) to insert telemetry data into all stored JPEG images, in the JPEG comment field
- Sample image processing script to extract telemetry from JPEG comment field, and insert into downloaded images as a text overlay, using ImageMagick

01/03/2016
==========

- Test for latest Pi boards; assume PITS+ if not a known board
- clear.txt now actually works
- Better setting of MTX2 frequency

28/11/2015

- Suport for Pi Zero
- Support for USB webcam via fswebcam

12/10/2015

- Added "camera_settings" option for config file
- Fixed IP check which faulted when Pi on a VPN

18/08/2015

- Merged in LoRa code (LoRa branch is now defunct)
- Photographs can now be taken at independent rates for RTTY, LoRa, full-size
- Landing prediction option
- Fixes for multiple DS18B20 sensors
- Ability to run emulated flight from GPS NMEA file
- Ability to modify SSDV images before transmission using ImageMagick etc.
- LoRa Calling Mode
- RTTY serial port kept open continuously
- Startup radio message with IP address etc.
- Stop multiple tracker programs from being run
- Test for latest Pi board
- Option to delete existing image files at startup
- Image files now in names/dated folders e.g. /home/pi/images/RTTY/18_08_2015

17/06/2015

- Merged in APRS code (APRS branch is now defunct)
- Added options to control when APRS packets are sent
- Fixed issue with tracker program failing SD card is pulled and APRS enabled
- NTX2 frequency-setting code now has same improvements as for MTX2
- Serial port kept open now instead of open/close each packet; using flush command to sync instead of closing.  This allows ...
- ... Telemetry log entries now written whilst waiting for telemetry to Tx; removes/reduces delay due to SD card writing.

01/06/2015

- MTX2 frequency-setting code changed to fix random fails to set frequency
- logging now on by default
- log files removed from repository
- Long payload IDs are truncated to 6 before passing to SSDV program
- SSDV image widths and heights now forced to be multiples of 16 pixels
- Added support for second (external) DS18B20 temperature sensor
- Fix to occassional missing packets
- Support for Pi V2 B
- Protect against BMP085/180 being disconnected after initialisation

19/12/2014

- GPS code completely re-written to use WiringPi library instead of bcm library
- default configuration now leaves the monitor on, to ease development
- As the PITS+ boards are set by frequency in MHz, but the earlier board was
  set by channel number, the code now accepts either "frequency=nn" for channel
  number, of frequency=xxx.xxxMHz for actual frequency.  Not that using the
  second form does not give you more options on the older board - the frequency
  will be set to the closest channnel.
- Camera filenames are now the system time
- Camera images are now saved in dated folders

