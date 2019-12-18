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


#define MAX_BUFFER_SIZE			512
#define RPMSG_BUF_HEADER_SIZE           16
#ifndef RPMSG_BUF_SIZE
#define RPMSG_BUF_SIZE 512 
#endif
char payload[RPMSG_BUF_SIZE - RPMSG_BUF_HEADER_SIZE];

/* shared_struct is used to pass data between ARM and PRU */
typedef struct {
	uint16_t voltage;
	uint16_t channel;
	uint32_t cycles[5];
} message_t;


int readVoltage(int channel, double *out)
{

	message_t message;

	if (channel == 5 || channel == 6 || channel == 7) {} else {
		fprintf(stderr, "bad channel, expect: 5, 6, or 7\n");
		return -1;
	}

	/* use character device /dev/rpmsg_pru30 */
	/* open the character device for read/write */
	struct pollfd pfds[1];
	pfds[0].fd = open("/dev/rpmsg_pru30", O_RDWR);
	if (pfds[0].fd < 0) {
		fprintf(stderr, "could not open /dev/rpmsg_pru30\n");
		return -1;  // error
	}

	/* Convert channel number from CHAR to uint16_t */
	message.channel = (uint16_t) channel;

	/* write data to the payload[] buffer in the PRU firmware. */
	size_t result = write(pfds[0].fd, &message, sizeof(message));
	if (result != sizeof(message)) {
		fprintf(stderr, "write failed\n");
		close(pfds[0].fd);
		return -2;
	}

	/* poll for the received message */
	pfds[0].events = POLLIN | POLLRDNORM;
	int pollResult = 0;

	/* loop while rpmsg_pru_poll says there are no kfifo messages. */
	while (pollResult <= 0) {
		pollResult = poll(pfds,1,0);

		/* 
		 * users may prefer to write code that does not block the ARM
		 * core from addressing other tasks, and that contains timeout
		 * logic to avoid an infinite lockup.
		 */
	}

	/* read voltage and channel back */
	double voltage[10];
	for (int i = 0; i < 10; i++) {
		result = read(pfds[0].fd, payload, MAX_BUFFER_SIZE);
		if (result != sizeof(message_t)) {
			fprintf(stderr, "read failed\n");
			close(pfds[0].fd);
			return -3;
		}	
		fprintf(stderr, "voltage[%i]=%x\n", i, ((message_t *)payload)->voltage);
		for (int j = 0; j < 5; j++) {
			fprintf(stderr, "\t%x\n", ((message_t *)payload)->cycles[j]);
		}
		voltage[i] = (double) ((message_t *)payload)->voltage; 
	}
	close(pfds[0].fd);

	*out = voltage[0] * 1.8 / 4095.0;

	return 0;
}
