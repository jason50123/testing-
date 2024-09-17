# SimpleSSD version 2.0
Open-Source Licensed Educational SSD Simulator for High-Performance Storage and Full-System Evaluations

This project is managed by [CAMELab](http://camelab.org).
For more information, please visit [SimpleSSD homepage](http://simplessd.org).

## Licenses
SimpleSSD is released under the GPLv3 license. See `LICENSE` file for details.
SimpleSSD uses open-source libraries.

 - [inih](https://github.com/benhoyt/inih) located at `/lib/inih`  
INI parser library released under the New BSD license.
 - [McPAT](https://github.com/HewlettPackard/mcpat) located at `/lib/mcpat`  
Multicore Power Area and Timing calculator.
We modified source code to separate initialize phase and calculation phase.

## What should you after adding new slet?

1. Update CMakeLists.txt
2. Include your slet header and add `addSlet()` to `hil/hil.cc`
3. Update `enum NAMESPACE` and `enum FUNCTION` in `cpu/def.hh`
4. Update `enum LOG_ID` in `sim/trace.hh` and `logName` in `sim/log.cc`
5. update `cpu/generator/functions.py` and run `./cpu/gencpi.sh`
6. Update `cpu/cpu.cc` using the output of `./cpu/gencpi.h`

> Check commit 86006e9 for example

After all the previous changes are applied, you can run the command to perform a simple test:

```bash
make -f scripts/Makefile test-slet KEYS="$YOUR_SLET_OPT_KEY_1 $YOUR_SLET_OPT_KEY_2" VALUES="$YOUR_SLET_OPT_VAL_1 $YOUR_SLET_OPT_VAL_2" SLET_ID=$YOUR_SLET_ID
```