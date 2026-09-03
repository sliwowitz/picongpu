# allocguard

Refuses a device allocation larger than a bound, at the point it is asked for, and says
where it was asked from.

Where the device memory is the host's, an allocation of everything the driver reports free
succeeds, and the machine is then left with nothing: the operating system and the driver
both starve. The memory is held by the driver and is not charged to the process, so the
kernel's own killer picks other victims, a control group cannot bound it, and killing the
process does not give it back in time. Nothing outside the process can help. This refuses
the request inside it, before the driver ever sees it.

## Building and using

    gcc -shared -fPIC -O1 -o allocguard.so allocguard.c -ldl -rdynamic

    LD_PRELOAD=/path/to/allocguard.so \
    ALLOCGUARD_ONE_GIB=4 ALLOCGUARD_ALL_GIB=12 ALLOCGUARD_LOG=alloc.log \
    picongpu -d 1 1 1 -g 32 32 32 -s 10

`ALLOCGUARD_ONE_GIB` bounds one allocation, `ALLOCGUARD_ALL_GIB` bounds them together, and
`ALLOCGUARD_LOG` names a file for the log; without it the log goes to standard error. A
refused request returns the out-of-memory code, which is what the caller would have been
given had the memory really run out.

The log names every request and gives a backtrace for the large ones, so a caller that asks
for too much is found rather than guessed at.

## What it costs

Every device allocation goes through a lock, a line of log, and for the large ones a
backtrace. That is far too much to leave in place while measuring anything: use it to find
out where memory goes, and take it off before taking a number.

## Requires a dynamically linked runtime

It interposes on the runtime and driver entry points, so a program that links the runtime
statically passes straight through it. `ldd <program> | grep cudart` says which it is.
