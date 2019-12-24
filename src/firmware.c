/*
 * Copyright (C) 2018 Texas Instruments Incorporated - http://www.ti.com/
 *
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *	* Redistributions of source code must retain the above copyright
 *	  notice, this list of conditions and the following disclaimer.
 *
 *	* Redistributions in binary form must reproduce the above copyright
 *	  notice, this list of conditions and the following disclaimer in the
 *	  documentation and/or other materials provided with the
 *	  distribution.
 *
 *	* Neither the name of Texas Instruments Incorporated nor the names of
 *	  its contributors may be used to endorse or promote products derived
 *	  from this software without specific prior written permission.
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
 */

#include <stdint.h>
#include <stdio.h>
#include <pru_cfg.h>
#include <pru_ctrl.h>
#include <pru_intc.h>
#include <sys_tscAdcSs.h>
#include <rsc_types.h>
#include <pru_rpmsg.h>
#include "firmware_resource_table.h"
#include "common.h"

volatile register uint32_t __R31;

/* Host-0 Interrupt sets bit 30 in register R31 */
#define HOST_INT			((uint32_t) 1 << 30)

/* 
 * The PRU-ICSS system events used for RPMsg are defined in the Linux devicetree
 * PRU0 uses system event 16 (To ARM) and 17 (From ARM)
 * PRU1 uses system event 18 (To ARM) and 19 (From ARM)
 */
#define TO_ARM_HOST			16
#define FROM_ARM_HOST			17

/*
 * Using the name 'rpmsg-pru' will probe the rpmsg_pru driver found
 * at linux-x.y.z/drivers/rpmsg/rpmsg_pru.c
 */
#define CHAN_NAME			"rpmsg-pru"
#define CHAN_DESC			"Channel 30"
#define CHAN_PORT			30

/*
 * Used to make sure the Linux drivers are ready for RPMsg communication
 * Found at linux-x.y.z/include/uapi/linux/virtio_config.h
 */
#define VIRTIO_CONFIG_S_DRIVER_OK	4

/* Control Module registers to enable the ADC peripheral */
#define CM_WKUP_CLKSTCTRL  (*((volatile unsigned int *)0x44E00400))
#define CM_WKUP_ADC_TSC_CLKCTRL  (*((volatile unsigned int *)0x44E004BC))

#define ADC_AVERAGING 0

/* payload receives RPMsg message */
#define RPMSG_BUF_HEADER_SIZE           16
#define MAX_SIZE (RPMSG_BUF_SIZE - RPMSG_BUF_HEADER_SIZE)

typedef struct {
	struct pru_rpmsg_transport transport;
	uint16_t src, dst;
} io_t;

io_t *io_open() {
	static io_t io;
	volatile uint8_t *status;

	/* Make sure the Linux drivers are ready for RPMsg communication */
	status = &resourceTable.rpmsg_vdev.status;
	while (!(*status & VIRTIO_CONFIG_S_DRIVER_OK)) {
		/* Optional: implement timeout logic */
	};

	pru_rpmsg_init(&io.transport, &resourceTable.rpmsg_vring0,
		&resourceTable.rpmsg_vring1, TO_ARM_HOST, FROM_ARM_HOST);

	/* 
	 * Create the RPMsg channel between the PRU and ARM user space using 
	 * the transport structure. 
	 */
	while (pru_rpmsg_channel(RPMSG_NS_CREATE, &io.transport, CHAN_NAME,
			CHAN_DESC, CHAN_PORT) != PRU_RPMSG_SUCCESS) {
		/* Optional: implement timeout logic */
	};

	/* Clear the event status */
	CT_INTC.SICR_bit.STS_CLR_IDX = FROM_ARM_HOST;

	return &io;
}

uint16_t io_recv(io_t *pio, void *buffer) {
	static int state = 0;
	uint16_t len;

	switch(state) {
	case 0:  // waiting to be kicked
		if (__R31 & HOST_INT) {
			state = 1;
			CT_INTC.SICR_bit.STS_CLR_IDX = FROM_ARM_HOST;
		}
		return 0;
	case 1:  // reading
		if (pru_rpmsg_receive(&pio->transport, &pio->src, &pio->dst,
				buffer, &len) == PRU_RPMSG_SUCCESS) {
			return len;
		} else {
			// nothing more to read
			state = 0;
			return 0;
		}
	}
	return 0; // not reachable, makes compiler happy though
}

uint16_t io_send(io_t *pio, void *payload, uint16_t len) {
	int16_t rc;
	if (len == 0) return 0;

	rc = pru_rpmsg_send(&pio->transport, pio->dst, pio->src, payload, len);
	if (rc == PRU_RPMSG_SUCCESS) {
		return len;
	}
	return 0;
}

void io_close(io_t *pio) {
	while (pru_rpmsg_channel(RPMSG_NS_DESTROY, &pio->transport, CHAN_NAME,
			CHAN_DESC, CHAN_PORT) != PRU_RPMSG_SUCCESS) {
		/* Optional: implement timeout logic */
	};
}

typedef struct {
	uint16_t num_channels;
	uint16_t index[8]; 
	uint16_t value[9];   // extra value is used as a dump
} adc_t;

adc_t *adc_open(uint16_t speed, uint16_t num_channels, uint8_t *channels) {
	static adc_t adc;
	uint16_t i;

	adc.num_channels = num_channels;
	for (i = 0; i < 8; i++) {
		adc.index[i] = 8;  // point to the dump
	}
	for (i = 0; i < num_channels; i++) {
		adc.index[channels[i]] = i;
	}

	/* set the always on clock domain to NO_SLEEP. Enable ADC_TSC clock */
	while (!(CM_WKUP_ADC_TSC_CLKCTRL == 0x02)) {
		CM_WKUP_CLKSTCTRL = 0;
		CM_WKUP_ADC_TSC_CLKCTRL = 0x02;
		/* Optional: implement timeout logic. */
	}

	/* 
	 * Set the ADC_TSC CTRL register. 
	 * Disable TSC_ADC_SS module so we can program it.
	 * Set step configuration registers to writable.
	 */
	ADC_TSC.CTRL_bit.ENABLE = 0;
	ADC_TSC.CTRL_bit.STEPCONFIG_WRITEPROTECT_N_ACTIVE_LOW = 1;
	ADC_TSC.ADC_CLKDIV_bit.ADC_CLKDIV = speed;  // set to max speed

	/* 
	 * set the ADC_TSC STEPCONFIG1 register for channel 5  
	 * Mode = 0; SW enabled, one-shot
	 * Averaging = 0x3; 8 sample average
	 * SEL_INP_SWC_3_0 = 0x0 = Channel 1
	 * use FIFO0
	 */
	ADC_TSC.STEPCONFIG1_bit.MODE = 0;
	ADC_TSC.STEPCONFIG1_bit.AVERAGING = ADC_AVERAGING;
	ADC_TSC.STEPCONFIG1_bit.SEL_INP_SWC_3_0 = 0;
	ADC_TSC.STEPCONFIG1_bit.FIFO_SELECT = 0;

	/*
	 * set the ADC_TSC STEPCONFIG2 register for channel 6
	 * Mode = 0; SW enabled, one-shot
	 * Averaging = 0x3; 8 sample average
	 * SEL_INP_SWC_3_0 = 0x1 = Channel 2
	 * use FIFO0
	 */
	ADC_TSC.STEPCONFIG2_bit.MODE = 0;
	ADC_TSC.STEPCONFIG2_bit.AVERAGING = ADC_AVERAGING;
	ADC_TSC.STEPCONFIG2_bit.SEL_INP_SWC_3_0 = 1;
	ADC_TSC.STEPCONFIG2_bit.FIFO_SELECT = 0;

	/* 
	 * set the ADC_TSC STEPCONFIG3 register for channel 7
	 * Mode = 0; SW enabled, one-shot
	 * Averaging = 0x3; 8 sample average
	 * SEL_INP_SWC_3_0 = 0x2 = Channel 3
	 * use FIFO0
	 */
	ADC_TSC.STEPCONFIG3_bit.MODE = 0;
	ADC_TSC.STEPCONFIG3_bit.AVERAGING = ADC_AVERAGING;
	ADC_TSC.STEPCONFIG3_bit.SEL_INP_SWC_3_0 = 2;
	ADC_TSC.STEPCONFIG3_bit.FIFO_SELECT = 0;

	/* 
	 * set the ADC_TSC STEPCONFIG4 register for channel 8
	 * Mode = 0; SW enabled, one-shot
	 * Averaging = 0x3; 8 sample average
	 * SEL_INP_SWC_3_0 = 0x3= Channel 4
	 * use FIFO0
	 */
	ADC_TSC.STEPCONFIG4_bit.MODE = 0;
	ADC_TSC.STEPCONFIG4_bit.AVERAGING = ADC_AVERAGING;
	ADC_TSC.STEPCONFIG4_bit.SEL_INP_SWC_3_0 = 3;
	ADC_TSC.STEPCONFIG4_bit.FIFO_SELECT = 0;

	/* 
	 * set the ADC_TSC STEPCONFIG1 register for channel 5  
	 * Mode = 0; SW enabled, one-shot
	 * Averaging = 0x3; 8 sample average
	 * SEL_INP_SWC_3_0 = 0x4 = Channel 5
	 * use FIFO0
	 */
	ADC_TSC.STEPCONFIG5_bit.MODE = 0;
	ADC_TSC.STEPCONFIG5_bit.AVERAGING = ADC_AVERAGING;
	ADC_TSC.STEPCONFIG5_bit.SEL_INP_SWC_3_0 = 4;
	ADC_TSC.STEPCONFIG5_bit.FIFO_SELECT = 0;

	/* 
	 * set the ADC_TSC STEPCONFIG1 register for channel 5  
	 * Mode = 0; SW enabled, one-shot
	 * Averaging = 0x3; 8 sample average
	 * SEL_INP_SWC_3_0 = 0x5  = Channel 6
	 * use FIFO0
	 */
	ADC_TSC.STEPCONFIG6_bit.MODE = 0;
	ADC_TSC.STEPCONFIG6_bit.AVERAGING = ADC_AVERAGING;
	ADC_TSC.STEPCONFIG6_bit.SEL_INP_SWC_3_0 = 5;
	ADC_TSC.STEPCONFIG6_bit.FIFO_SELECT = 0;

	/* 
	 * set the ADC_TSC STEPCONFIG1 register for channel 5  
	 * Mode = 0; SW enabled, one-shot
	 * Averaging = 0x3; 8 sample average
	 * SEL_INP_SWC_3_0 = 0x6 = Channel 7
	 * use FIFO0
	 */
	ADC_TSC.STEPCONFIG7_bit.MODE = 0;
	ADC_TSC.STEPCONFIG7_bit.AVERAGING = ADC_AVERAGING;
	ADC_TSC.STEPCONFIG7_bit.SEL_INP_SWC_3_0 = 6;
	ADC_TSC.STEPCONFIG7_bit.FIFO_SELECT = 0;

	/* 
	 * set the ADC_TSC STEPCONFIG1 register for channel 5  
	 * Mode = 0; SW enabled, one-shot
	 * Averaging = 0x3; 8 sample average
	 * SEL_INP_SWC_3_0 = 0x7 = Channel 8
	 * use FIFO0
	 */
	ADC_TSC.STEPCONFIG8_bit.MODE = 0;
	ADC_TSC.STEPCONFIG8_bit.AVERAGING = ADC_AVERAGING;
	ADC_TSC.STEPCONFIG8_bit.SEL_INP_SWC_3_0 = 7;
	ADC_TSC.STEPCONFIG8_bit.FIFO_SELECT = 0;

	/* 
	 * set the ADC_TSC CTRL register
	 * set step configuration registers to protected
	 * store channel ID tag if needed for debug
	 * Enable TSC_ADC_SS module
	 */
	ADC_TSC.CTRL_bit.STEPCONFIG_WRITEPROTECT_N_ACTIVE_LOW = 0;
	ADC_TSC.CTRL_bit.STEP_ID_TAG = 1;
	ADC_TSC.CTRL_bit.ENABLE = 1;

	return &adc;
}

uint16_t adc_read(adc_t *padc, uint16_t *values) {
	static int state = 0;
	uint32_t count = ADC_TSC.FIFO0COUNT;
	uint32_t data;
	uint16_t channel;
	uint16_t i;

	switch (state) {
	case 0:	// prepare
		/* 
		* Clear FIFO0 by reading from it
		* We are using single-shot mode. 
		* It should not usually enter the for loop
		*/
		for (i = 0; i < count; i++) {
			data = ADC_TSC.FIFO0DATA;
		}
		state = 1;
		return 0;
	
	case 1: // trigger the capture
		ADC_TSC.STEPENABLE = 0x1fe;  // enable all 8 capture channels
		state = 2;
		return 0;
	
	case 2: // wait for fifo0 to populate

		if (ADC_TSC.FIFO0COUNT < 8) {
			return 0;
		}

		state = 3;
		return 0;

	case 3:  // all 8 channels are ready in fifo0
		for (i = 0; i < 8; i++) {
			data = ADC_TSC.FIFO0DATA;
			channel = (data >> 16) & 0xf;
			padc->value[padc->index[channel]] = data & 0xfff;
		}
		memcpy(values, padc->value, padc->num_channels * sizeof(uint16_t));
		state = 0;
		return padc->num_channels;
	}

	return 0;
}

uint8_t recv_buffer[MAX_SIZE];

typedef struct {
#define RING_SIZE 8
	uint16_t available;
	uint16_t head;
	uint8_t rings[RING_SIZE][MAX_SIZE];
} ring_t;


void *ring_allocate_buffer(ring_t *ring) {
	void *p = (void *) ring->rings[ring->head];
	if (ring->available == 0) return NULL;
	ring->head = (ring->head + 1) & (RING_SIZE - 1);
	ring->available -= 1;
	return p;
}

void ring_release_buffer(ring_t *ring) {
	ring->available += 1;
}

ring_t *ring_open() {
	static ring_t r;
	r.head = 0;
	r.available = RING_SIZE;
	return &r;
}

void send_to_buffer(io_t *pio, ring_t *ring,
		uint32_t cycles, uint16_t *values, uint16_t num_channels) {
	static int offset = 0;
	static int dropped = 0;
	static buffer_t *b = NULL;
	int size;

	if (b == NULL) {
		b = (buffer_t *) ring_allocate_buffer(ring);
		if (b == NULL) {
			// no more buffers!
			dropped += 1;
			return;
		}
		b->num_dropped = dropped > 0xffff ? 0xffff : dropped;
		b->num = 0;
		offset = 0;
		dropped = 0;
	}

	memcpy(&b->data[offset], &cycles, sizeof(uint32_t)); offset += 2;
	memcpy(&b->data[offset], values, sizeof(uint16_t) * num_channels); offset += num_channels;
	b->num += 1;

	size = ((uint8_t *) &b->data[offset]) - ((uint8_t *) b);
	if (size + sizeof(uint16_t) * (2 + num_channels)  > MAX_SIZE) {
		// next measurement will not fit here, have to send!
		if (io_send(pio, b, size) != size) {
			b->num_dropped = 0xffff; // (b->num + b->num_dropped) > 0xffff ? 0xffff : (b->num + b->num_dropped);
			b->num = 0;
			dropped = 0;
			offset = 0;
		} else {
			b = NULL;
			dropped = 0;
		}
	}
}


void main(void) {
	io_t *pio;
	ring_t *ring;
	adc_t *padc = NULL;
	command_t *cmd = (command_t *) recv_buffer;

	/* 
	 * Allow OCP master port access by the PRU so the PRU can read 
	 * external memories 
	 */
	CT_CFG.SYSCFG_bit.STANDBY_INIT = 0;

	/*
	 * Enable cycle tick register
	 */
  	PRU0_CTRL.CTRL_bit.CTR_EN = 1; // turn on cycle counter

	pio = io_open();
	ring = ring_open();

	while (1) {
		uint16_t len = io_recv(pio, recv_buffer);
		if (len >= sizeof(command_t) && cmd->magic == COMMAND_MAGIC) {
			if (padc == NULL && cmd->command == COMMAND_START) {
				command_start_t *start = (command_start_t *) recv_buffer;
				padc = adc_open(start->speed, start->num_channels, start->channels);
				PRU0_CTRL.CYCLE = 0;
			} else if (padc != NULL && cmd->command == COMMAND_ACK) {
				ring_release_buffer(ring);  // CPU acknowledged receiving data buffer
			} else if (padc != NULL && cmd->command == COMMAND_STOP) {
				padc = NULL;
			}
		}

		if (padc != NULL) {
			uint16_t values[8];
			uint16_t len = adc_read(padc, values);
			if (len > 0) {
				// assert (len == padc->num_channels);
				uint32_t cycles = PRU0_CTRL.CYCLE;
				PRU0_CTRL.CYCLE = 0;
				send_to_buffer(pio, ring, cycles, values, len);
				// __delay_cycles(20000); // trying to add stability (avoid kernel lock ups)
			}
		}
	}
}
