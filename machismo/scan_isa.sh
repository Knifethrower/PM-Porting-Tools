#!/bin/bash
# Instructions in an arm64 binary (Mach-O or ELF) that a Cortex-A53 (ARMv8.0 + crc/crypto, the
# RK3326/RK3566/H700 handhelds) cannot run: disassembled for cortex-a53, the words it does not
# know show as <unknown>; those are named with the default decoder and counted by mnemonic.
# Apple Silicon builds use ARMv8.1+ (LSE atomics, SHA3 EOR3/BCAX, ...): every one found here must
# be emulated (machismo's isa_emul.c) or the game dies with SIGILL on the device.
#   scan_isa.sh <binary>        (WSL; needs llvm-objdump, default llvm-objdump-18, or OBJDUMP=...)
# Prints the count per mnemonic and the first few addresses. From Spaghetti Celesti.
B=$1; [ -f "$B" ] || { echo "usage: scan_isa.sh <arm64 binary>"; exit 1; }
OD=${OBJDUMP:-llvm-objdump-18}
T=$(mktemp -d)
$OD -d --mcpu=cortex-a53 --no-show-raw-insn "$B" | awk '/<unknown>/ {sub(":","",$1); print $1}' > $T/unknown.txt
echo "words a Cortex-A53 cannot decode: $(wc -l < $T/unknown.txt)"
$OD -d "$B" | awk 'NR==FNR {u[$1]=1; next} { a=$1; sub(":","",a); if (a in u) print $3, $0 }' $T/unknown.txt - \
    > $T/named.txt
awk '{print $1}' $T/named.txt | sort | uniq -c | sort -rn
head -5 $T/named.txt
rm -rf "$T"
