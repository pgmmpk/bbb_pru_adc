import logging
import time
import itertools
from bbb_pru_adc.capture import capture


if __name__ == '__main__':
    logging.basicConfig(level=logging.INFO)

    data = []

    bad = 0
    good = 0
    count = 0
    with capture([0, 1, 2, 3, 4, 5, 6, 7], auto_install=True, step_avg=4) as cap:
        start = time.time()
        for num_dropped, timestamps, values in itertools.islice(cap, 0, 1000):
            bad += num_dropped
            good += len(timestamps)
            count += 1
            if count % 100 == 0:
                print(count)
            # time.sleep(0.001)
            #data.append(list(values))

    elapsed = time.time() - start
    print('Elapsed:', elapsed, bad, good)
    print('KHz:', round((bad + good) / elapsed / 1000, 3))

'''
speed=0    speed=1   speed=32    speed=16
1: 16.2    8.1       0.54        1.037
2: 16.2
3: 16.1
4: 16.1    7.733                 1.025
5: 16.0              0.54
6: 16.0
7: 15.9
8: 15.8    7.266     0.53        1.024
'''
