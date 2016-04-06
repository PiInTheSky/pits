from sense_hat import SenseHat
import time

sense = SenseHat()

while True:
	t = sense.get_temperature()
	p = sense.get_pressure()
	h = sense.get_humidity()

	t = round(t, 1)
	p = round(p, 1)
	h = round(h, 1)

	# print("Temperature = %s, Pressure=%s, Humidity=%s" % (t,p,h))

	pitch, roll, yaw = sense.get_orientation().values()

	pitch=round(pitch, 0)
	roll=round(roll, 0)
	yaw=round(yaw, 0)
	
	# print("pitch=%s, roll=%s, yaw=%s" % (pitch,yaw,roll))

	x, y, z = sense.get_accelerometer_raw().values()

	x=round(x, 0)
	y=round(y, 0)
	z=round(z, 0)

	# print("x=%s, y=%s, z=%s" % (x, y, z))
	
	line = "%s,%s,%s,%s,%s,%s,%s,%s,%s" % (t,h,p,pitch,roll,yaw,x,y,z)
	
	print (line)

	with open("astropi.txt", "a") as myfile:
		myfile.write(line + '\n')

	time.sleep(0.5)
