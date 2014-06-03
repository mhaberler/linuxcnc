import time

from machinekit import pyring
tx = pyring.BufRing(pyring.Ring("mptx.0.in"))
rx = pyring.BufRing(pyring.Ring("mptx.0.out"))

#for i in range(3):
tx.write("demo",0)
tx.write("bazl",0)
tx.write("cmd 0",0)
tx.flush()

time.sleep(0.1)

print("reading:")
for f in rx.read():
	print(f.data.tobytes(), f.flags)
rx.shift()
