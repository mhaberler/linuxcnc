import time

from machinekit import pyring
tx = pyring.BufRing(pyring.Ring("mptx.0.in"))
rx = pyring.BufRing(pyring.Ring("mptx.0.out"))

for i in range(3):
    tx.write("frame%d" % i, i+42)
tx.flush()

time.sleep(0.1)

print("reading:")
for f in rx.read():
	print(f.data.tobytes(), f.flags)
rx.shift()
