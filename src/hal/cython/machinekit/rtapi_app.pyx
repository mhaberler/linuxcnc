from .rtapi_app cimport *



def connect(char *uuid, instance=0, uri=None):
    r = rtapi_connect(instance, uri, uuid)
    if r:
        raise RuntimeError("cant connect to rtapi: %s" & rtapi_rpcerror())
