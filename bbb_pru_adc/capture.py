import contextlib
import struct, ctypes
from ctypes import CDLL, Structure, c_int, c_uint, c_ushort, c_ubyte, POINTER, byref, c_void_p
from bbb_pru_adc.driver import Driver, relative

# _driver_arm = CDLL(relative('resources/libdriver.so'))
# class Reading(Structure):
#     _fields_ = [
#         ('timestamp', c_uint),
#         ('values', c_ushort*8)  # actual size varies
#     ]
# _driver_arm.driver_start.restype = c_void_p
# _driver_arm.driver_stop.restype = c_int
# _driver_arm.driver_stop.argtypes = [c_void_p]
# _driver_arm.driver_read.restype = c_int
# _driver_arm.driver_read.argtypes = [c_void_p, POINTER(Reading)]

_driver_pru = Driver(fw0=relative('resources/am335x-pru0.fw'))

class Command(Structure):
    _fields_ = [
        ('magic', c_ushort),
        ('command', c_ushort),
    ]

class CommandStart(Structure):
    _fields_ = [
        ('pub', Command),
        ('num_channels', c_ushort),
        ('channels', c_ubyte * 8)
    ]

class Dr:
    START_COMMAND = struct.Struct('H H H 8B')
    STOP_COMMAND = struct.Struct('H H')
    ACK_COMMAND = struct.Struct('H H')

    def __init__(self, channels):
        num_channels = len(channels)
        if not (0 < num_channels <= 8):
            raise ValueError('You can specify from 1 to 8 channels, received ' + num_channels)
        if not all(0 <= x < 8 for x in channels):
            raise ValueError('Channel should be from 0 (AIN1) to 7 (AIN8)')
        if len(set(channels)) != num_channels:
            raise ValueError('Do not repeat channels!')

        message = ctypes.create_string_buffer(self.START_COMMAND.size)
        channels = channels[:] + [0] * (8 - num_channels)
        self.START_COMMAND.pack_into(message, 0, 0xbeef, 1, num_channels, *channels)
        self._device = open('/dev/rpmsg_pru30', 'rb+', 0)
        self._device.write(message.raw)
        self._num_channels = num_channels
        self._io_bugger = ctypes.create_string_buffer(512)

    def close(self):
        if self._device is None:
            return
        message = ctypes.create_string_buffer(self.STOP_COMMAND.size)
        self.STOP_COMMAND.pack_into(message, 0, 0xbeef, 2)
        self._device.write(message.raw)
        self._device = None

    def read(self):
        bytes_ = self._device.read(512)
        # acknowledge the receipt
        message = ctypes.create_string_buffer(self.ACK_COMMAND.size)
        self.STOP_COMMAND.pack_into(message, 0, 0xbeef, 3)
        self._device.write(message.raw)

        num, num_dropped = struct.unpack_from('H H', bytes_)
        print(num, num_dropped)
        measurement = struct.Struct('I %sH' % self._num_channels)
        for _ in range(num_dropped):
            yield None
        off = 4
        for _ in range(num):
            yield measurement.unpack_from(bytes_, off)
            off += measurement.size


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

    with _driver_pru(auto_install=auto_install):
        drv = Dr(channels)

        def reader():
            while True:
                for x in drv.read():
                    if  x is None:
                        yield x
                    else:
                        timestamp, *value = x
                        # convert to volts
                        # 0-1.8 Volt is the ADC input voltage range
                        # values are unsigned shorts in the range 0 - 4096 (12 bit ADC)
                        voltages = [x * 1.8 / 4095 for x in value]

                        yield (timestamp, *voltages)

        try:
            yield reader()
        finally:
            drv.close()
