from machinekit import hal,rtapi

assert 'c1-s32out' in hal.signals
assert hal.pins["c1.s32out"].linked is True
assert hal.pins["c2.s32in"].linked is True
