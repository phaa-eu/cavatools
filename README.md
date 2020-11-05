# cavatools
RISC-V instruction set simulator and performance analysis tools
===============================================================

An instruction-set interpretator that produces execution trace in shared memory,
and an example collection of simulators and analysis programs for pipeline and cache
performance evaluation.  You can write your own simulation/analysis programs.
Cavatools can also be retargetted to other RISC-like architectures.


###  Getting the sources

The repository is on GitHub:
    $ git clone https://github.com/pete2222/cavatools


###  Prerequisites

You need Berkeley Softfloat Release 3e (https://github.com/ucb-bar/berkeley-softfloat-3)
library installed at ~/lib/softfloat.a with headers in ~/include/softfloat/.


###  Installation

To build Cavatools:
```
    $ cd cavatools
    $ make install
```

will create the following files:
```
    ~/bin/caveat            - instruction set interpreter
    ~/bin/traceinfo   - prints and summarizes trace from caveat
    ~/bin/pipesim      - very simple pipelined machine simulator
    ~/bin/cachesim     - general cache simulator, can be L1, L2, I, D, I+D...
```

In addition, header files are installed in
```
    ~/include/caveat
```
and the caveat trace handling library in
```
    ~/lib/libcava.a
```

Please create directories ~/include/softfloat, ~/include/caveat, etc.
before installation.


###  Running Cavatools

Programs should be compiled -static using riscv-gnu-toolchain Linux libc:
```
    $ riscv64-unknown-linux-gnu-gcc -static ... testpgm.c -o testpgm
```

To run without tracing:
```
    $ caveat testpgm <any number of flags and arguments to testpgm>
```

To see instruction trace run this in one window:
```
    $ caveat --trace=bufname testpgm <any number of flags and arguments to testpgm>
```
and this in another window:
```
    $ traceinfo --in=bufname --list testpgm
```
The shared memory buffer 'bufname' appears in /dev/shm while processes are running.

There are various other options to traceinfo, and other analysis tools in
```
    cavatools/erised/
```

In the future there will be a presentation slide deck and a brief paper describing
how to use the example analysis tools.
