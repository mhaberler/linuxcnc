import os
import sys
import select
import pybonjour

def register_callback(sdRef, flags, errorCode, name, regtype, domain):
	if errorCode == pybonjour.kDNSServiceErr_NoError:
		print 'Registered service:'
		print '  name    =', name
		print '  regtype =', regtype
		print '  domain  =', domain

try:
	sdRef1 = pybonjour.DNSServiceRegister(regtype = '_machinekit._tcp',
					      name = 'machinekit on wheezy',
					      port = 47815,
					      txtRecord = pybonjour.TXTRecord({'config': 'blahfasel', 'noch' : 'einer'}),
					      callBack = register_callback)

except:
	print 'cannot register DNS service'

	sys.exit(1)

try:
	try:
		while True:
			ready = select.select([sdRef1], [], [])

			if sdRef1 in ready[0]:
				pybonjour.DNSServiceProcessResult(sdRef1)


	except KeyboardInterrupt:
		pass

finally:
	sdRef.close()


