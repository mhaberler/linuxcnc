import sys
import os
import binascii
import google.protobuf.text_format
import pb2json # automagic pb2/json/pb2 conversion
import json    # pretty printer
import halext

from types_pb2 import *
from object_pb2 import *
from message_pb2 import *
from halupdate_pb2 import *


keepalive = 500

# fake a handle
def djbhash(a):
    """Hash function from D J Bernstein"""
    h = 5381L
    for i in [ord(x) for x in a]:
        t = (h * 33) & 0xffffffffL
        h = t ^ i
    return h

container = Container()
offer = container.offer

for name,sig in halext.signals().iteritems():
    obj = offer.objects.add()
    obj.name = name
    obj.type = sig.type
    if sig.type == HAL__FLOAT:
        obj.halfloat = sig.value
    if sig.type == HAL__S32:
        obj.hals32 = sig.value
    if sig.type == HAL__U32:
        obj.halu32 = sig.value
    if sig.type == HAL__BIT:
        obj.halbit = sig.value
    if sig.writers:
        obj.direction = HAL__OUT
    else:
        obj.direction = HAL__IN
    obj.handle = djbhash(name)


# obj = offer.objects.add()

# obj = offer.objects.add()
# obj.name = "bar"
# obj.type = HAL__S32
# obj.hals32 = 4711
# obj.direction = HAL__IN
# obj.handle = djbhash(obj.name)

print "payload:", offer.ByteSize()
print "text format:", str(container)

buffer = container.SerializeToString()
print "wire format length=%d: %s" % (len(buffer), binascii.hexlify(buffer))

jsonout = container.SerializeToJSON()
print json.dumps(json.loads(jsonout), indent=4)

jsonoffer = '''
{
    "offer": {
        "objects": [
            {
                "type": 2,
                "direction": 32,
                "handle": 193410979,
                "name": "foo",
                "halfloat": 3.1400000000000001
            },
            {
                "type": 3,
                "direction": 16,
                "handle": 193415156,
                "name": "bar",
                "hals32": 4711
            }
        ],
        "identity": "me"
    }
}
'''
request = Container()

print "Parsing message from JSON back into protobuf: ", jsonoffer
request.ParseFromJSON(jsonoffer)
print "and its protobuf text format parsed back from JSON is:\n", str(request)
