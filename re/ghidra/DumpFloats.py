# Ghidra post-script: print the float and int value at each address given as argument.
# @category Export
from ghidra.program.model.address import AddressFactory
import struct
af = currentProgram.getAddressFactory()
mem = currentProgram.getMemory()
for a in getScriptArgs():
    addr = af.getAddress(a)
    try:
        b = bytearray(4)
        mem.getBytes(addr, b)
        f = struct.unpack('<f', bytes(b))[0]
        i = struct.unpack('<I', bytes(b))[0]
        print("FLOAT %s = %r (0x%08x)" % (a, f, i))
    except Exception as e:
        print("FLOAT %s = ERROR %s" % (a, e))
