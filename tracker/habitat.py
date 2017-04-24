import os, time, sys
import json
import http.client
import urllib.parse
import urllib.request
from base64 import b64encode
from hashlib import sha256
from datetime import datetime

def UploadTelemetry(Callsign, Sentence):
	sentence_b64 = b64encode(Sentence.encode())

	date = datetime.utcnow().isoformat("T") + "Z"
	
	data = {"type": "payload_telemetry", "data": {"_raw": sentence_b64.decode()}, "receivers": {Callsign: {"time_created": date, "time_uploaded": date,},},}
	data = json.dumps(data)

	url = "http://habitat.habhub.org/habitat/_design/payload_telemetry/_update/add_listener/%s" % sha256(sentence_b64).hexdigest()
	req = urllib.request.Request(url)
	req.add_header('Content-Type', 'application/json')
	try:
		response = urllib.request.urlopen(req, data.encode())
	except Exception as e:
		pass
		# return (False,"Failed to upload to Habitat: %s" % (str(e)))

pipe_name = 'pits_pipe'

print ("Waiting for telemetry pipe to be created ...")

while not os.path.exists(pipe_name):
	time.sleep(1)

print ("Waiting for data from pipe ...")

pipein = open(pipe_name, 'r')
while True:
	line = pipein.readline()
	if line:
		print ('Received: "%s"' % (line[:-1]))
		if line[0] == '$':
			UploadTelemetry('3G', line)
	else:
		time.sleep(1)
