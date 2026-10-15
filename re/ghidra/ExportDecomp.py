# Ghidra post-script: write every function of the current program as decompiled C into
# <outdir>/<program name>.c (Jython). Run through analyzeHeadless -postScript ExportDecomp.py <outdir>
# @category Export
import os
from ghidra.app.decompiler import DecompInterface
from ghidra.util.task import ConsoleTaskMonitor

outdir = getScriptArgs()[0]
prog = currentProgram
di = DecompInterface()
di.openProgram(prog)
mon = ConsoleTaskMonitor()
out = open(os.path.join(outdir, prog.getName() + ".c"), "w")
for f in prog.getFunctionManager().getFunctions(True):
    res = di.decompileFunction(f, 180, mon)
    out.write("// ==== %s @ %s  (%d bytes)\n" % (f.getName(True), f.getEntryPoint(), f.getBody().getNumAddresses()))
    if res.decompileCompleted():
        out.write(res.getDecompiledFunction().getC())
    else:
        out.write("// FAILED: %s\n" % res.getErrorMessage())
    out.write("\n")
out.close()
