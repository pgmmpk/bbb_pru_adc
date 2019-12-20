PRU_CGT:=/usr/share/ti/cgt-pru
PRU_SUPPORT:=/usr/lib/ti/pru-software-support-package

FIRMWARE:=bbb_pru_adc/resources/am335x-pru0.fw
DRIVER:=bbb_pru_adc/resources/libdriver.so

LIBS=--library=$(PRU_SUPPORT)/lib/rpmsg_lib.lib
INCLUDE=--include_path=$(PRU_SUPPORT)/include --include_path=$(PRU_SUPPORT)/include/am335x --include_path=$(PRU_CGT)/include --include_path=src
STACK_SIZE=0x100
HEAP_SIZE=0x100
GEN_DIR=gen

CFLAGS=-v3 -O2 --display_error_number --endian=little --hardware_mac=on --obj_directory=$(GEN_DIR) --pp_directory=$(GEN_DIR) -ppd -ppa
LFLAGS=--reread_libs --warn_sections --stack_size=$(STACK_SIZE) --heap_size=$(HEAP_SIZE)


all: $(DRIVER) $(FIRMWARE)

$(DRIVER): src/driver.c src/driver.h src/common.h
	gcc -O3 -Wall -Werror -fpic -shared -o $(DRIVER) src/driver.c

$(FIRMWARE): src/firmware.c src/firmware.cmd src/firmware_resource_table.h src/common.h
	/usr/bin/clpru $(INCLUDE) $(CFLAGS) -fe gen/firmware.object src/firmware.c
	/usr/bin/clpru $(CFLAGS) -z -i$(PRU_CGT)/lib -i$(PRU_CGT)/include $(LFLAGS) -o $(FIRMWARE) gen/firmware.object -mgen/firmware.map src/firmware.cmd --library=libc.a $(LIBS)

clean:
	rm -f $(DRIVER) $(FIRMWARE) gen/*