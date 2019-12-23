import logging
import time
import itertools
from bbb_pru_adc.capture import capture


if __name__ == '__main__':
    logging.basicConfig(level=logging.INFO)

    data = []

    start = time.time()
    bad = 0
    with capture([5, 6, 7], auto_install=True
    ) as cap:
        for x in itertools.islice(cap, 0, 10):
            # data.append(x)
            if x is None:
                bad += 1
            # time.sleep(0.00001)

    elapsed = time.time() - start
    print('Elapsed:', elapsed, bad)
