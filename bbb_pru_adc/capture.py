import contextlib
from ctypes import CDLL, Structure, c_int, c_uint, c_ushort, c_ubyte, POINTER, byref, c_void_p
from bbb_pru_adc.driver import Driver, relative

_driver_arm = CDLL(relative('resources/libdriver.so'))
class Reading(Structure):
    _fields_ = [
        ('timestamp', c_uint),
        ('values', c_ushort*8)  # actual size varies
    ]
_driver_arm.driver_start.restype = c_void_p
_driver_arm.driver_stop.restype = c_int
_driver_arm.driver_stop.argtypes = [c_void_p]
_driver_arm.driver_read.restype = c_int
_driver_arm.driver_read.argtypes = [c_void_p, POINTER(Reading)]

_driver_pru = Driver(fw0=relative('resources/am335x-pru0.fw'))

@contextlib.contextmanager
def capture(channels, auto_install=False):
    '''
    ADC capture.

        channels - a list of channels to capture:
            0 -> AIN1
            1 -> AIN2
            2 -> AIN3
            ...
            7 -> AIN8  (seem to be defunct in the hardware?)

    To capture just one value per read, pass a channels list of size 1.

    Example:
        [0, 1, 5] -> request to capture AIN1, AIN2, and AIN6

    This is a context manager. Use it like this:
    ```
    with capture([0, 1, 5]) as c:
        for timestamp, *readings in c:
            ...  # do something with timestamp and readings
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

    reading = Reading()
    channels = (c_ubyte * len(channels))(*channels)

    with _driver_pru(auto_install=auto_install):
        drv = _driver_arm.driver_start(num_channels, byref(channels))
        if drv is None:
            raise RuntimeError('Failed to init driver')

        def reader():
            while True:
                rc = _driver_arm.driver_read(drv, byref(reading))
                if rc != 0:
                    raise RuntimeError('Error reading data from driver')

                # convert to volts
                # 0-1.8 Volt is the ADC input voltage range
                # values are unsigned shorts in the range 0 - 4096 (12 bit ADC)
                doubles = [1.8 * x / 4095 for x in reading.values[:num_channels]]

                yield (reading.timestamp, *doubles)

        try:
            yield reader()
        finally:
            _driver_arm.driver_stop(drv)
