import contextlib
from ctypes import CDLL, c_uint, c_ushort, c_ubyte, byref
from bbb_pru_adc.driver import Driver, relative
import array


_dll = CDLL(relative('resources/libdriver.so'))

@contextlib.contextmanager
def capture(channels, auto_install=False, speed=0):
    '''
    ADC capture.

        channels - a list of channels to capture:
            0 -> AIN1
            1 -> AIN2
            2 -> AIN3
            ...
            7 -> AIN8  (seem to be defunct in the hardware?)

        speed - ADC capture speed (actually, clock divider)
            0 -> highest speed
            1 -> a bit slower
            2 -> even slower
            ...

    To capture just one value per read, pass a channels list of size 1.

    Example:
        [0, 1, 5] -> request to capture AIN1, AIN2, and AIN6

    This is a context manager. Use it like this:
    ```
    with capture([0, 1, 5]) as c:
        for buffer in c:
            ...  # do something with the buffer
    ```

    Context that `capture` creates is an iterator. This iterator produces tuples:

        num_dropped: int - number of datapoints dropped because of buffer overflow (hopefully zero)
        timestamps: array.array of unsigned int values - PRU timestamps of the captured datapoints
        values: array.array of float values - voltages

    Length of timestamps array is constant for the capture session, and depends on the number of
    ADC channels we capture. Size of one data point in exchange buffer is (4 + 2*num_channels).
    Buffer size is fixed (at kernel compilation time), and is equal to (512 - 16).
    Thus, one can compute the expected number of datapoints per buffer as:

        num_datapoints = (512 - 16) // (4 + 2 * num_channels)

    Length of values array is (num_datapoints * num_channels). Data is layed out in channel-first
    fashion. For a hypothetical buffer with num_datapoints = 3 and num_channels = 2, values array will
    be of size 6 and data will be layed out as follows:

        [channel0_0, channel1_0, channel0_1, channel1_1, channel0_1, channel1_1]

    where 
        channel0_0 and channel1_0 are values of channel 0 and 1 at time step 0
        channel0_1 and channel1_1 are values of channel 0 and 1 at time step 1
        channel0_2 and channel1_2 are values of channel 0 and 1 at time step 2

    Driver re-uses timestamps and values buffers and will re-write their contents on next iteration.
    Therefore, you need to copy values out if you are not processing them immediately.
    ```

    Context that `capture` creates is an iterator. This iterator produces tuples
    with first element being the timestamp (in PRU ticks since the last reading. One PRU tick is 5ns),
    followed by voltage readings from the inputs. If you requested to read 3 channels, there will
    be 3 voltage values (and tuple size will be 4 - including the timestamp at the beginning).
    '''

    num_channels = len(channels)
    if not (0 < num_channels <= 8):
        raise ValueError('You can specify from 1 to 8 channels, received ' + num_channels)
    if not all(0 <= x < 8 for x in channels):
        raise ValueError('Channel should be from 0 (AIN1) to 7 (AIN8)')
    if len(set(channels)) != num_channels:
        raise ValueError('Do not repeat channels!')
    if not (0 <= speed <= 0xffff):
        raise ValueError('Speed must be in 0..0xffff')

    num_records = (512-16-4) // (4 + 2 * num_channels)
    timestamps = array.array('I', [0] * num_records)
    values = array.array('f', [0.] * (num_records * num_channels))
    num_dropped = c_ushort()

    pru = Driver(fw0=relative('resources/am335x-pru0.fw'))
    with pru(auto_install=auto_install):
        c_channels = c_ubyte*num_channels
        driver = _dll.driver_start(c_ushort(speed), c_ushort(num_channels), c_channels(*channels))
        def reader():
            tms_addr, _ = timestamps.buffer_info()
            val_addr, _ = values.buffer_info()
            while True:
                rc = _dll.driver_read(driver, byref(num_dropped), tms_addr, val_addr)
                if rc != 0:
                    raise RuntimeError('io error in driver')
                yield num_dropped.value, timestamps, values

        try:
            yield reader()
        finally:
            _dll.driver_stop(driver)

