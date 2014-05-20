import os
import sys
import select
import pybonjour
import uuid

try:
    svc_uuid = os.getenv("MKUUID")
except Exception:
    print "MKUUID environment not set"
    os.exit(1)

instance_uuid = uuid.uuid1()
def register_callback(sdRef, flags, errorCode, name, regtype, domain):
    if errorCode == pybonjour.kDNSServiceErr_NoError:
        print 'Registered service:'
        print '  name    =', name
        print '  regtype =', regtype
        print '  domain  =', domain

try:
    sdRef1 = pybonjour.DNSServiceRegister(regtype = '_machinekit._tcp,_config',
                                          name = 'fake config server pid=%d' % os.getpid(),
                                          port = 4711,
                                          txtRecord = pybonjour.TXTRecord({'dsn': 'tpc://1.2.3.4:5500',
                                                                           'uuid' : svc_uuid,
                                                                           'instance' : str(instance_uuid)}),
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
    sdRef1.close()
