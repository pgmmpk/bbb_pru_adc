/*
 * Copyright (C) 2018 Texas Instruments Incorporated - http://www.ti.com/ 
 *  
 *  
 * Redistribution and use in source and binary forms, with or without 
 * modification, are permitted provided that the following conditions 
 * are met:
 * 
 * 	* Redistributions of source code must retain the above copyright 
 * 	  notice, this list of conditions and the following disclaimer.
 * 
 * 	* Redistributions in binary form must reproduce the above copyright
 * 	  notice, this list of conditions and the following disclaimer in the 
 * 	  documentation and/or other materials provided with the   
 * 	  distribution.
 * 
 * 	* Neither the name of Texas Instruments Incorporated nor the names of
 * 	  its contributors may be used to endorse or promote products derived
 * 	  from this software without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS 
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT 
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT 
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, 
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT 
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT 
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE 
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */
#include "driver.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdbool.h>
#include <poll.h>
#include <inttypes.h>
#include <unistd.h>
#include <errno.h>
#include "common.h"


#define RPMSG_BUF_HEADER_SIZE           16
#ifndef RPMSG_BUF_SIZE
#define RPMSG_BUF_SIZE 512 
#endif

#define MAX_BUFFER_SIZE			512

int driver_num_records(unsigned int num_channels) {
    return (512 - 16 - 4) / (4 + 2 * num_channels);
}


typedef struct {
	driver_t pub;
	int dev;
	unsigned short buffer[MAX_BUFFER_SIZE/sizeof(unsigned short)];
	int num_channels;
	int num_records;
} driver_impl_t;


driver_t *driver_start(unsigned int speed, unsigned int num_channels, unsigned char const *channels) {
	static driver_impl_t driver;
	command_start_t command;

	memset(&driver, '\0', sizeof(driver));
	driver.num_channels = num_channels;
	driver.num_records = driver_num_records(num_channels);
	driver.dev = open("/dev/rpmsg_pru30", O_RDWR); // | O_NONBLOCK);
	if (driver.dev < 0) {
		fprintf(stderr, "could not open /dev/rpmsg_pru30\n");
		return NULL;  // error
	}

	memset(&command, '\0', sizeof(command_t));
	command.header.magic = COMMAND_MAGIC;
	command.header.command = COMMAND_START;
	command.speed = speed;
	command.num_channels = num_channels;
	for (int i = 0; i < num_channels; i++) {
		command.channels[i] = channels[i];
	}

	/* write data to the payload[] buffer in the PRU firmware. */
	size_t result = write(driver.dev, &command, sizeof(command));
	if (result != sizeof(command)) {
		fprintf(stderr, "write failed\n");
		close(driver.dev);
		return NULL;
	}

	return &driver.pub;
}

int driver_read(driver_t *drv, int *dropped, unsigned int *timestamps, float *values) {
	driver_impl_t *pdriver = (driver_impl_t *) drv;
	int result;
	command_t command;
	unsigned short *p;

	command.magic = COMMAND_MAGIC;
	command.command = COMMAND_ACK;

	if (pdriver->dev < 0) {
		fprintf(stderr, "attempt to read from closed device\n");
		return -1;
	}

	result = read(pdriver->dev, pdriver->buffer, MAX_BUFFER_SIZE);
	if (result < 0) {
		fprintf(stderr, "read failed\n");
		return -1;
	}

	result = write(pdriver->dev, &command, sizeof(command_t));
	if (result != sizeof(command_t)) {
		fprintf(stderr, "ack write failed\n");
		return -1;
	}

	p = pdriver->buffer;
	*dropped = p[1];
	p += 2;
	for (int i = 0; i < pdriver->num_records; i++) {
		*timestamps = * (unsigned int *) p; p += 2; timestamps += 1;
		for (int j = 0; j < pdriver->num_channels; j++) {
			*values = (*p) * 1.8 / 4095.0; p += 1; values += 1;
		}
	}

	return 0;
}

int driver_stop(driver_t *drv) {
	driver_impl_t *pdriver = (driver_impl_t *) drv;
	command_t command;

	if (pdriver->dev < 0) return 0;  // nothing to do

	command.magic = COMMAND_MAGIC;
	command.command = COMMAND_STOP;

	write(pdriver->dev, &command, sizeof(command));

	close(pdriver->dev);

	pdriver->dev = -1;

	return 0;
}
