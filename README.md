# Streaming ADC capture on BeagleBone (Black) with PRU

## Requirements

1. Hardware: BeagleBone (Black)
2. OS: Debian GNU/Linux 10 (buster), see https://rcn-ee.com/rootfs/bb.org/testing/2019-12-10/buster-iot/

## Building and trying

```bash
git clone <this repo>
cd <dir>
make clean
make
python3 -m venv .venv
. .venv/bin/activate
python main.py
```

## Links
1. https://github.com/MarkAYoder/PRUCookbook.git
2. https://markayoder.github.io/PRUCookbook/
3. https://github.com/derekmolloy/exploringBB
4. http://exploringbeaglebone.com/
5. http://theduchy.ualr.edu/?p=289
