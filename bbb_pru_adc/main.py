from ctypes import CDLL, c_int, c_double, byref
import logging
import time
from bbb_pru_adc.driver import Driver, relative

_driver_arm = CDLL(relative('resources/libdriver.so'))
_driver_arm.readVoltage.restype = c_int

_driver_pru = Driver(fw0=relative('resources/am335x-pru0.fw'))


if __name__ == '__main__':
    logging.basicConfig(level=logging.INFO)
    with _driver_pru(auto_install=True):
        d = c_double()
        for _ in range(10):
            result = _driver_arm.readVoltage(c_int(5), byref(d))
            print(result, d)
