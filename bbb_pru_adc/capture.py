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
    num_channels = len(channels)
    if not (0 < num_channels <= 8):
        raise ValueError('You can specify from 1 to 8 channels, received ' + len(channels))
    if not all(0 <= x < 8 for x in channels):
        raise ValueError('Channel should be from 0 (AIN1) to 7 (AIN8)')
    if len(set(channels)) != num_channels:
        raise ValueError('Do not repeat channels!')

    reading = Reading()
    channels = (c_ubyte * len(channels))(*channels)

    with _driver_pru(auto_install=auto_install):
        #payload = cast(buffer, POINTER(Payload))
        
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

