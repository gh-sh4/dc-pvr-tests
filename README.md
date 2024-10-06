# `dc-pvr-tests`

A set of discrete tests for verifying the behavior and performance characteristics of the Dreamcast's PVR GPU (TA/ISP/TSP). 

# Running tests
```
make && /opt/toolchains/dc/dcload-ip/host-src/tool/dc-tool-ip -t 192.168.1.232 -x build/dc-pvr-tests.elf -c "data"
```

Make will generate a `data` folder where test dumps of various kinds will go

# How does the server work? (eventually)
> TODO : It doesn't right now. Below is what we want to do 😀

- User `dc-load`'s the DC driver program to Dreamcast. DC driver program waits for commands from the host computer. User invokes `driver.py` ("Host" driver, could actually be in any language). Host driver sends a sequence of commands (stdio) back and forth with the DC to write and read programs/memory dumps/registers/etc. This command-and-control pattern allows variable number of tests to be written, remote control, and more.

- If a test runs away on the DC and stops responding for a set period of time, a watchdog timer interrupt will message the problem to the host driver, then the DC driver will exit back to dc-load.

- In the ideal flow of running a suite of tests, the host driver will send a command to load an ELF via virtual filesystem into memory. Another command from the host driver invokes any function in the elf, such as performing some rendering. The host can invoke any number of functions it likes. Interspersed with these load/run-function commands, the host driver may also dump registers, read ram/vram, and more to capture system state at any point. 
