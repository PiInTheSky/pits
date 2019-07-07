from goprocam import GoProCamera
import os
import time
from jpegtran import JPEGImage

camera = GoProCamera.GoPro()

if not os.path.isdir('images'):
	os.mkdir('images')
for i in range(0,5):
	folder = 'images/' + ['RTTY','APRS','LORA0','LORA1','FULL'][i]
	if not os.path.isdir(folder):
		os.mkdir(folder)

while True:
	for i in range(0,5):
		filename = 'take_pic_' + str(i)
		if os.path.isfile(filename):
			os.remove(filename) 
			os.chdir('images/' + ['RTTY','APRS','LORA0','LORA1','FULL'][i])
			camera.downloadLastMedia(camera.take_photo(0))
			os.chdir('../..')
		
		filename = 'convert_' + str(i)
		if os.path.isfile(filename):
			# Read and delete conversion file
			file = open(filename, 'r')
			lines = file.read().splitlines()			
			file.close()
			
			if os.path.isfile('ssdv.jpg'):
				os.remove('ssdv.jpg')
			
			# resize selected image file
			image=JPEGImage(lines[3])
			image.downscale(int(lines[4]), int(lines[5])).save('ssdv.jpg')
			
			# run SSDV conversion
			os.system('ssdv ' + lines[1] + ' -e -c ' + lines[0] + ' -i ' + lines[2] + ' ssdv.jpg ' + lines[6])

			# Move all images that werre considered
			folder1 = "images/" + ['RTTY','APRS','LORA0','LORA1','FULL'][i]
			folder2 = folder1 + "/" + time.strftime("%d_%m_%y")
			if not os.path.isdir(folder2):
				os.mkdir(folder2)
			try:
				os.system("mv " + folder1 + "/*.JPG " + folder2)
				os.remove(filename)
			finally:
				pass
			os.system("echo DONE > ssdv_done_" + str(i))

	time.sleep(1)
done
