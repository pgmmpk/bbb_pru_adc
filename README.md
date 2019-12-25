# Streaming ADC capture on BeagleBone (Black) with PRU
Provides PRU firmware that captures up to 8 ADC channels, and userspace driver
to receive this as a stream of buffers containing voltage readings from ADC.

Python is the most convenient way of using it. Lower-level API can be also accessed via
dynamic library, if needed.

Features:
- configurable capture speed. Highest speed is around 15KHz.
- configurable set of AIN pins to capture. From just one AIN channel and up to 8 AIN channels
- reports dropped readings (when userspace client is not fast enough to process incoming
  buffers data is dropped to avoid buffer overflow)
- Uses just 15-20% CPU, leaving plenty of cycles to actually deal with the data

## Requirements

1. Hardware: BeagleBone (Black)
2. OS: Debian GNU/Linux 10 (buster), see https://rcn-ee.com/rootfs/bb.org/testing/2019-12-10/buster-iot/

## Building and trying

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
    receive `0xffff`, it basically means that delay was at least that big (and probably bigger).
2. `values` - array of readings, packed in the channel-first order. It is an `array.array` object
    with elements of `float` type. Length is `num_readings * num_channels`.
3. `timestamps` - array of relative timestamps, corresponding to the readings. It is an `array.array`
    object with elements of `unsigned int` type.
    Length is `num_readings`. Value is the number of PRU clock ticks
    since the last reading. These values allow one to know exact timing between two readings. Time
    distance between readings is fairly stable, small deviations are due to varying codepaths to
    process outgoing and incoming messages.

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

## Links
1. https://github.com/MarkAYoder/PRUCookbook.git
2. https://markayoder.github.io/PRUCookbook/
3. https://github.com/derekmolloy/exploringBB
4. http://exploringbeaglebone.com/
5. http://theduchy.ualr.edu/?p=289
