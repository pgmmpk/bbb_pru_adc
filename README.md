# Streaming ADC capture on BeagleBone (Black) with PRU
[![PyPI version](https://badge.fury.io/py/bbb-pru-adc.svg)](https://badge.fury.io/py/bbb-pru-adc)

Provides PRU firmware that captures up to 8 ADC channels, and userspace driver
to receive this as a stream of buffers containing voltage readings from ADC.

Python is the most convenient way of using it. Lower-level API can be also accessed via
dynamic library, if needed.

Features:
- configurable capture speed. Highest speed is around 15KHz.
- configurable number of samples to average over (important to get less noise)
- configurable set of AIN pins to capture. From just one AIN channel and up to 8 AIN channels
- reports dropped readings (when userspace client is not fast enough to process incoming
  buffers data is dropped to avoid buffer overflow)
- uses just 15-20% CPU, leaving plenty of cycles to actually deal with the data
- no dependencies

## Requirements

1. Hardware: BeagleBone (Black), With remoteproc (not UIO) enabled in `/boot/uEnv.txt`
2. OS: Debian GNU/Linux 10 (buster), see https://rcn-ee.com/rootfs/bb.org/testing/2019-12-10/buster-iot/ Or Debian GNU/Linux 9.8 (the latter may need to be re-configured to use remoteproc)
3. Python 3.5 or better
4. Root access rights (needed to install firmware into `/lib/firmware` folder and access sysfs)

Please note that (depending on your environment) you may need root priviledges to run this code.

## Installation

We recommend installing into virtual environment
```bash
python3 -m venv .venv
. .venv/bin/activate
pip install bbb_pru_adc
```

## Running sample code
```bash
python3 -m bbb_pru_adc.main
```

Here is the the code of the `main.py` - an example how to use this driver in Python:
```python
import time
import itertools
from bbb_pru_adc.capture import capture

bad = 0
good = 0
with capture([0, 1, 2, 3, 4 ,5 ,6 ,7], auto_install=True, clk_div=1) as cap:
    start = time.time()
    for num_dropped, timestamps, values in itertools.islice(cap, 0, 1000):
        bad += num_dropped
        good += len(timestamps)

elapsed = time.time() - start
print('Elapsed:', elapsed, bad, good)
print('KHz:', round((bad + good) / elapsed / 1000, 3))
```

## More samples

More code examples can be found in [examples](examples/) folder.

## Building from sources
This step is **not needed** if you installed wheel from PyPI as described above. You need this only
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
    process outgoing and incoming messages. PRU clock runs at 200MHz (5ns per tick). Thus, the 
    timestamp value of 1000 corresponds to 5 millisecons, value of 200000 corresponds to 1kHz, etc.


How many readings do we have per buffer? This depends on the number of channels we capture.
Exact answer is:

```python
num_readings = (512 - 16 - 4) // (4 + 2 * num_readings)
```

This formula is mandated by remoteproc IO buffer size limit (defined as 512 at kernel compile time).

For a given capture session number of readings per buffer stays the same.

Note that driver has `max_num` parameter that allows one to make `num_readings` smaller than the
maximum allowed by the IO buffer size. This is an advanced functionality that may be used to
achieve specific goals (e.g. lower latency or get exact number of readings per buffer). In most
applications, this parameter should not be used.

## Capture API
```python
from bbb_pru_adc.capture import capture

with capture(clk_div=0, step_avg=3, channels=[3, 5, 7], auto_install=False) as cap:
    for num_dropped, timestamps, values in cap:
        # do something with this information
```
This example starts capturing ADC inputs AIN3, AIN5, and AIN7 (`channels=[3, 5, 7]`) 
at full speed (`speed=0`). It will not attempt to
install PRU firmware (`auto_install=False`).
If driver detects that system firmware is missing or obsolete, an error will be thrown.

Capture has to be used as a context manager. The context is a generator spitting out
the pieces of our buffer.

Capture parameters:

`clk_div` - ADC clock divider value. Fastest is `clk_div=0`, capturing at about 15KHz. In many applications 15KHz is just too much data (hard to process), and `clk_div` can be set to other
values. For example, setting `clk_div=9` will capture 10 times slower (at about 1.5KHz).

`step_avg` - How many capture steps to avegare for one sample. Default value is `4`, meaning
averaging over 16 steps. It produces the least amount of noise. Setting to values less than 2
is not recommended, because of the increasing noise in the values. Note that this value affects
capture speed. Higest capture speed of 15kHz is only possible without averaging. Highest
capture frequency with the recommended `step_avg` setting of 4 is about 7kHz.

`channels` - which AIN pins (aka channels) to capture. This is a list of 1 to 8 unique values, 
representing the AIN pins to read. Note that values in the output buffer are layed out in the
same order as `channels`.

`max_num` - limits the number of readings per buffer. This is advanced functionality, see the section
below. Deafult is 0 that disables this limit.

`target_delay` - target number of PRU cycles (5ns per cycle) per capture. This allows one to fine-tune
the capture speed. This is an advanced functionality, see the section below. Default is 0 which
disables this functionality.

`auto_install` - if we detect that firmware is not installed, or is different, attempt to re-install by copying firmware file from python package resources to `/lib/firmware`. This action requires root priveleges. Once installed, you can use the driver as a non-root user.

Important! `timestamps` and `values` returned by the generator are re-used and content will be
overwritten on next iteration. Do not store these buffers. If you are not processing data immediately,
copy them out.

### Advanced use: `target_delay`
Normally, the time between two ADC captures is determined by the following factors:
1. ADC capture speed (see `speed` parameter)
2. Time needed to process and send out the data
This time can slightly vary, because number of operations depends on buffering state and other
factors pertaining to PRU/CPU communication.

Actual number of cycles is reported in the `timestamps` array.

The `target_delay` parameter sets the minimal number of PRU cycles. PRU will idle until the specified
number of PRU timesteps is reached. This allows one to:
a. Remove timestamp jitters
b. Fine-tune the capture speed to any desirable number (limited by the overall capture speed - around 16kHz)

To target a specific capture frequence, do the following:
* choose `speed` parameter to find the largest value that produces capture frequence just above
  the desired one, then
* compute the target number of PRU cycles for the desired frequency and set `target_delay` to that
  value. Remember that PRU runs at 200MHz clock and one cycle takes 5ns.
* measure the actual capture frequency
* if it deviates from the desired one, change `target_delay` a bit to adjust. If actual frequency
  is lower than desired, lower `target_dealy` value. If actual frequency is higher than desired,
  increase the `target_delay` value.

This should allow one to get very precise capture frequency.

### Advanced use: `max_num`
Normally, driver will use all available space in the communication buffer (512-16 bytes). Buffer
size is determined by the `remoteproc` kernel module. Using all available buffer space
minimizes bandwidth loss due to the control information (attached to each buffer sent), and thus
minimizes the chance of data loss. In short, if you want the most efficient data transfer, do not
change this value.

Somethimes, you may want to use smaller buffers. For example, to ensure lower latency (at the cost
of getting less efficient comunication). You can do this by setting `max_num`.

The `max_num` parameter ensures that no more than that many ADC readings will be packed per buffer.
If you set it to a high value, the real limit will be the communication buffer size and parameter
will be effectively ignored.

## Internals

There are three pieces of software:
1. firmware running on PRU side `bbb_pru_adc/resources/am335x-pru0.fw`, built from 
   `src/firmware.c`, `src/firmware.h`, and `src/common.h`.
2. CPU-side userspace driver that handles low-level details of communication with PRU
   `bbb_pru_adc/resources/libdriver.so`, built from `src/driver.c`, `src/driver.h`, and `src/common.h`
3. Python code that is responsible for installing the firmware and starting and terminating
   the PRU processor.

### Firmware
Overall logic is this:
1. Initialize `remoteproc` communication subsystem (this creates character device `/dev/rpmsg-pru30`)
2. Initialize array of 8 ring buffers that we will use for data exchange with CPU
3. Enter main loop, where we:
    a. wait for incoming `START` command with parameters `speed`, `channels`, `max-num`, and
       `target_delay`. When received,
       we initialize ADC for the given channels and capture speed and start capturing.
    b. if `STOP` command arrives from CPU side while we are capturing, we stop the ADC capture
    c. if `ACK` command arrives from CPU, we release one ring buffer (CPU sends this command
       to acknowledge data receipt)
    d. when one ADC capture completes, we push the readings to the ring buffer. If ring buffer is
       full, we send it out to the CPU side and try to get a new ring buffer. When CPU side
       is slow, we may run out of buffers. Then we will drop the reading. After pushing
       the readings to the ring buffer we schedule another ADC capture.

### Driver
On the CPU side we do this:
1. `driver_start` method opens `/dev/rpmsg-pru30` device and writes a message there
   with `command=START`, and `speed`, `channels`, `max_num`, and `target_delay` values
   to ask PRU to start ADC capture
2. `driver_read` method reads device file, blocking until a message arrives. It then sends
   out the `ACK` command, and unpacks the data from received buffer into the caller's buffers.
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
