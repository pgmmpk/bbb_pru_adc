# Streaming ADC capture on BeagleBone (Black) with PRU
[![PyPI version](https://badge.fury.io/py/bbb-pru-adc.svg)](https://badge.fury.io/py/bbb-pru-adc)

Provides PRU firmware that captures up to 8 ADC channels, and userspace driver
to receive this as a stream of buffers containing voltage readings from ADC.

Python is the most convenient way of using it. Lower-level API can be also accessed via
dynamic library, if needed.

Features:
- configurable capture speed. Highest speed is around 15KHz.
- configurable set of AIN pins to capture. From just one AIN channel and up to 8 AIN channels
- reports dropped readings (when userspace client is not fast enough to process incoming
  buffers data is dropped to avoid buffer overflow)
- uses just 15-20% CPU, leaving plenty of cycles to actually deal with the data
- no dependencies

## Requirements

1. Hardware: BeagleBone (Black)
2. OS: Debian GNU/Linux 10 (buster), see https://rcn-ee.com/rootfs/bb.org/testing/2019-12-10/buster-iot/
3. Python 3.7 or better
4. Root access rights (needed to install firmware into `/lib/firmware` folder)

## Installation

We recommend installing into virtual environment
```bash
python3 -m venv .venv
. .venv/bin/activate
pip install bbb_pru_adc
```

## Running an example code
```bash
python3 -m bbb_pru_adc.main
```

Here is the the code of the `main.c`:
```python
import time
import itertools
from bbb_pru_adc.capture import capture

bad = 0
good = 0
with capture([0, 1, 2, 3, 4 ,5 ,6 ,7], auto_install=True, speed=1) as cap:
    start = time.time()
    for num_dropped, timestamps, values in itertools.islice(cap, 0, 10000):
        bad += num_dropped
        good += len(timestamps)

elapsed = time.time() - start
print('Elapsed:', elapsed, bad, good)
print('KHz:', round((bad + good) / elapsed / 1000, 3))
```

## Building from sources
This step is not needed if you installed wheel from PyPI as described above. You need this only
if you plan to make changes in firmware or driver.

```bash
git clone https://github.com/pgmmpk/bbb_pru_adc.git
cd bbb_pru_adc/
make clean
make
python3 -m bbb_pru_adc.main
```

## Stream structure
Each incoming buffer contains three pieces of information:
1. `num_dropped` - the number of dropped readings before this buffer was filled (i.e. between 
    readings from previous buffer and this buffer there was a gap). Under normal conditions
    this value is zero. It can not grow beyond `0xffff`. Thus, if you are unlucky enough to
    receive `0xffff`, it basically means that at least that many readings were dropped (and
    probably more).
2. `values` - array of readings, packed in the channel-first order. It is an `array.array` object
    with elements of `float` type. Length is `num_readings * num_channels`. Values vary between
    0.0 and 1.8 (volts).
3. `timestamps` - array of relative timestamps, corresponding to the readings. It is an `array.array`
    object with elements of `unsigned int` type.
    Length of this array is `num_readings`. Value is the number of PRU clock ticks
    since the last reading. These values allow one to know exact timing between two readings. Time
    distance between readings is fairly stable, small deviations are due to varying codepaths to
    process outgoing and incoming messages. PRU clock runs at 200MHz (5ns per tick).

How many readings do we have per buffer? This depends on the number of channels we capture.
Exact answer is:

```python
num_readings = (512 - 16 - 4) // (4 + 2 * num_readings)
```

This formula is mandated by IO buffer size limit (defined as 512 at kernel compile time).

For a given capture session number of readings per buffer stays the same.

## Capture API
```python
from bbb_pru_adc.capture import capture

with capture(speed=0, channels=[3, 5, 7], auto_install=False) as cap:
    for num_dropped, timestamps, values in cap:
        # do something with this information
```
This example starts capturing ADC inputs 3, 5, and 7 (`channels=[3, 5, 7]`) 
at full speed (`speed=0`). It will not attempt to
install PRU firmware (`auto_install=False`).
If driver detects that system firmware is missing or obsolete, and error will be thrown.

Capture has to be used as a context manager. The context is a generator spitting out
the pieces of our buffer.

Capture parameters:

`speed` - ADC capture speed as a clock divider value. Fastest is `speed=0`, capturing at about 15KHz. In many applications 15KHz is just too much data (hard to process), and `speed` can be set to other
values. For example, setting `speed=9` will capture 10 times slower (at about 1.5KHz).

`channels` - which AIN pins (aka channels) to capture.

`auto_install` - if we detect that firmware is not installed, or is different, attempt to re-install by copying firmware file from python package resources to `/lib/firmware`. This action requires root priveleges. Once installed, you can use the driver as a non-root user.

Important! `timestamps` and `values` returned by the generator are re-used and content will be
overwritten on next iteration. Do not store these buffers. If you are not processing data immediately,
copy them out.

## Internals

There are three pieces of software:
1. firmware running on PRU side `bbb_pru_adc/resources/am335x-pru0.fw`, built from 
   `src/firmware.c`, `src/firmware.h`.
2. CPU-side userspace driver that handles low-level details of communication with PRU
   `bbb_pru_adc/resources/libdriver.so`, built from `src/driver.c`, `src/driver.h`
3. Python code that is responsible for installing the firmware and starting and terminating
   the PRU processor.

### Firmware
Overall logic is this:
1. Initialize RPMSG communication subsystem (this creates character device `/dev/rpmsg-pru30`)
2. Initialize array of 8 ring buffers that we will use for data exchange with CPU
3. Enter main loop, where we:
    a. wait for incoming `START` command with parameters `speed` and `channels`. When received,
       we initialize ADC for the given channels and capture speed and start capturing.
    b. if `STOP` command arrives from CPU side while we are capturing, we stop the ADC capture
    c. if `ACK` command arrives from CPU, we release one ring buffer (CPU sends this command
       to acknowledge data receipt)
    d. when ADC capture finishes we push the readings to the ring buffer. If ring buffer is
       full, we send it out to the CPU side and try to get a new ring buffer. When CPU side
       is slow, we may run out of buffers. Then we will drop the reading. After pushing
       the readings to the ring buffer we schedule another ADC capture.

### Driver
On the CPU side we do this:
1. `driver_start` method opens `/dev/rpmsg-pru30` device and writes a message there
   with `command=START`, and `speed` and `channels` values, to ask PRU to start ADC capture
2. `driver_read` method reads device file, blocking until a message arrives. It then sends
   out and `ACK` command, and unpacks the data from received buffer into the caller's buffers.
3. `driver_stop` sends `STOP` command to the PRU

### Python side
Python code in `bbb_pru_adc/capture.py` does this:
1. loads the driver library
2. loads (installing if needed) the firmware into PRU, starts PRU
3. waits till `/dev/rpmgs-pru30` device is created
4. calls `driver_start` to initiate capture
5. in a loop receives captured data by calling `driver_read`
6. when finished, calls `driver_stop` and stops PRU

## Links
1. https://github.com/MarkAYoder/PRUCookbook.git
2. https://markayoder.github.io/PRUCookbook/
3. https://github.com/derekmolloy/exploringBB
4. http://exploringbeaglebone.com/
5. http://theduchy.ualr.edu/?p=289
