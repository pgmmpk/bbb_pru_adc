import logging
import time
from bbb_pru_adc.capture import capture


if __name__ == '__main__':
    logging.basicConfig(level=logging.INFO)

    data = []

    start = time.time()
    with capture([5, 6, 7], auto_install=True) as cap:
        counter = 0
        for x in cap:
            data.append(x)
            counter += 1
            if counter > 10000:
                break

    elapsed = time.time() - start
    for timestamp, *x in data[:10]:
        print(timestamp, x)
    print('Elapsed:', elapsed)
