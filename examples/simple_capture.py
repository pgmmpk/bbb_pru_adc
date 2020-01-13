'''
simple_capture.py

Demonstrates code to capture 100_000 values from ADC channels 3 and 5 at the
highest possible capture speed.
'''
import array
import math
from bbb_pru_adc import capture


size = 100000
AIN3_buffer = array.array('f', [0] * size)
AIN7_buffer = array.array('f', [0] * size)

print('Capturing %s samples will take approximately %s seconds' % (size, size / 16000))

with capture.capture(channels=[3, 5], auto_install=True) as c:
    offset = 0
    for num_dropped, _, values in c:
        for _ in range(num_dropped):
            AIN3_buffer[offset] = float('NaN')
            AIN7_buffer[offset] = float('NaN')
            offset += 1
            if offset >= size:
                break
        for i in range(0, len(values), 2):
            AIN3_buffer[offset] = values[i]
            AIN3_buffer[offset] = values[i]
            offset += 1
            if offset >= size:
                break
            
        if offset >= size:
            break

# Do something with captured data
def analysis(data):
    num_nans = 0
    num_not_nans = 0
    max_ = -10.
    min_ = 10.
    avg = 0.
    for x in data:
        if math.isnan(x):
            num_nans += 1
        else:
            max_ = max(max_, x)
            min_ = min(min_, x)
            avg += x
            num_not_nans += 1
    avg = avg / num_not_nans

    print('Number of NaNs (dropped readings):', num_nans)
    print('Number of not NaNs:', num_not_nans)
    print('Smallest value:', min_)
    print('Highest value:', max_)
    print('Average value:', avg)

print('AIN3 stats:')
analysis(AIN3_buffer)
print()

print('AIN7 stats:')
analysis(AIN7_buffer)
print()
