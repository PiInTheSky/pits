import os
import time
from time import gmtime, strftime
import fnmatch
from sense_hat import SenseHat
import random

def process_file(file, logit):
	print ("File found: ", file)
	f = open(file, 'rt')
	line = f.read()
	f.close()
	os.remove(file)

	print ("Line: ", line)

	if logit:
		f = open('tweets.log', 'at')
		f.write(strftime("%Y-%m-%d %H:%M:%S", gmtime()) + ', ' + line + '\n')
		f.close()
	
	colour = random.randint(1, 7)
	red = (colour & 1) * 255
	green = ((colour >> 1) & 1) * 255
	blue = ((colour >> 2) & 1) * 255
	
	colour = [red, green, blue]
	
	sense.show_message(line, text_colour=colour)
	
	time.sleep(1)
	
sense = SenseHat()
sense.set_rotation(270)		# Because of rotation in Astro Pi housing

while 1:

	for file in os.listdir('.'):
		if fnmatch.fnmatch(file, '*.sms'):
			process_file(file, True)
		elif file == 'latest.txt':
			process_file(file, False)
	time.sleep(1)

