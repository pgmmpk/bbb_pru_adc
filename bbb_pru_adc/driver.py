import logging
import hashlib
import contextlib
import shutil
import os
import time


def _fw_name(x):
    return f'/lib/firmware/{os.path.basename(x)}'

def relative(*av):
    '''helper to build file paths relative to the driver location'''
    return os.path.join(os.path.dirname(__file__), *av)


class Driver:
    '''helper class to manage firmware installation and PRU stop/start'''
    def __init__(self,
        fw0=None,  # path to compiled firmware for PRU0
        fw1=None,  # path to compiled firmware for PRU1
    ):
        self.fw0 = fw0
        self.fw1 = fw1

    def _is_installed(self, fname):
        firmware_path = _fw_name(fname)
        if os.path.isfile(firmware_path) and _checksum(fname) == _checksum(firmware_path):
            return True  # OK, all good

    def is_installed(self):
        if self.fw0 is not None and not self._is_installed(self.fw0):
            return False
        if self.fw1 is not None and not self._is_installed(self.fw1):
            return False
        return True

    def install(self):
        if self.fw0 is not None and not self._is_installed(self.fw0):
            logging.info('Installing %s as %s', self.fw0, _fw_name(self.fw0))
            shutil.copyfile(self.fw0, _fw_name(self.fw0))
        if self.fw1 is not None and not self._is_installed(self.fw1):
            logging.info('Installing %s as %s', self.fw1, _fw_name(self.fw1))
            shutil.copyfile(self.fw1, _fw_name(self.fw1))
        assert self.is_installed()

    def start(self):
        if self.fw0 is not None:
            with open('/sys/class/remoteproc/remoteproc1/state', 'r') as f:
                state = f.read()
                if state != 'offline\n':
                    raise RuntimeError('PRU0 is busy')
        if self.fw1 is not None:
            with open('/sys/class/remoteproc/remoteproc2/state', 'r') as f:
                state = f.read()
                if state != 'offline\n':
                    raise RuntimeError('PRU1 is busy')

        if self.fw0 is not None:
            name = os.path.basename(self.fw0)
            with open('/sys/class/remoteproc/remoteproc1/firmware', 'w') as f:
                f.write(name+'\n')
            with open('/sys/class/remoteproc/remoteproc1/state', 'w') as f:
                f.write('start\n')
        if self.fw1 is not None:
            name = os.path.basename(self.fw1)
            with open('/sys/class/remoteproc/remoteproc2/firmware', 'w') as f:
                f.write(name+'\n')
            with open('/sys/class/remoteproc/remoteproc2/state', 'w') as f:
                f.write('start\n')

        if self.fw0 is not None:
            # wait for rpmsg character devices to get ready
            for _ in range(100):
                if os.path.exists('/dev/rpmsg_pru30'):
                    try:
                        with open('/dev/rpmsg_pru30', 'rb'):
                            break
                    except OSError as err:
                        pass
                time.sleep(3.0/100)
            else:
                raise RuntimeError('Timeout waiting for /dev/rpmsg_pru0 device')

        if self.fw1 is not None:
            # wait for rpmsg character devices to get ready
            for _ in range(100):
                if os.path.exists('/dev/rpmsg_pru31'):
                    try:
                        with open('/dev/rpmsg_pru31', 'rb'):
                            break
                    except OSError as err:
                        pass
                time.sleep(3.0/100)
            else:
                raise RuntimeError('Timeout waiting for /dev/rpmsg_pru1 device')
    
    def stop(self):
        if self.fw0 is not None:
            with open(f'/sys/class/remoteproc/remoteproc1/state', 'w') as f:
                f.write('stop\n')
        if self.fw1 is not None:
            with open(f'/sys/class/remoteproc/remoteproc2/state', 'w') as f:
                f.write('stop\n')

    @contextlib.contextmanager
    def __call__(self, auto_install=False):
        if not self.is_installed():
            if auto_install:
                self.install()
            else:
                raise RuntimeError('Firmware not installed into /lib/firmware folder')
        self.start()
        try:
            yield
        finally:
            self.stop()

def _checksum(fname):
    '''computes checksum of a binary file'''
    with open(fname, 'rb') as f:
        return hashlib.md5(f.read()).hexdigest()
