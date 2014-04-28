from machinekit import pyring
x = pyring.BufRing(pyring.Ring("mptx.0.in"))

for i in range(3):
    x.write("frame%d" % i, i+42)
x.flush()
