'''
capture_with_exact_frequency.py

Demonstrates how to capture AIN1 with exact frequency of 3KHz

Here we use `speed` and `target_delay` to get exactly 3kHz capture frequency.

`target_delay` is computed by dividing PRU clock 200MHz by target frequency of 3kHz:

    200000000 / 3000 = 66667

Then, adjusting a bit to the lower side to get the target frequency.

You should expect to get the following result:

    Measured frequency:  3000.031593701992
'''
import itertools
import time
from bbb_pru_adc import capture


with capture.capture([1], speed=4, target_delay=66656) as c:    
    num_values = 0
    start = time.time()
    for num_dropped, _, values in itertools.islice(c, 0, 1000):
        num_values += num_dropped + len(values)
    end = time.time()

freq = num_values / (end - start)
print('Measured frequency: ', freq)

