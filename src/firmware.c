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
char payload[RPMSG_BUF_SIZE - RPMSG_BUF_HEADER_SIZE];

/* shared_struct is used to pass data between ARM and PRU */
typedef struct shared_struct{
	uint32_t voltage;
	uint32_t channel;
	uint32_t cycles[5];
} shared_struct;

uint32_t voltage[8];  // here we store captured ADC values (8 channels)

void init_adc();
void read_adc(uint32_t *adc_chan);

void main(void)
{
	struct pru_rpmsg_transport transport;
	uint16_t src, dst, len, i;
	volatile uint8_t *status;
	struct shared_struct message;

	/* 
	 * Allow OCP master port access by the PRU so the PRU can read 
	 * external memories 
	 */
	CT_CFG.SYSCFG_bit.STANDBY_INIT = 0;

	/*
	 * Enable cycle tick register
	 */
  	PRU0_CTRL.CTRL_bit.CTR_EN = 1; //turn on cycle counter

	init_adc();

	/* 
	 * Clear the status of the PRU-ICSS system event that the ARM will 
	 * use to 'kick' us
	 */
	CT_INTC.SICR_bit.STS_CLR_IDX = FROM_ARM_HOST;

	/* Make sure the Linux drivers are ready for RPMsg communication */
	status = &resourceTable.rpmsg_vdev.status;
	while (!(*status & VIRTIO_CONFIG_S_DRIVER_OK)) {
		/* Optional: implement timeout logic */
	};

	/* Initialize the RPMsg transport structure */
	pru_rpmsg_init(&transport, &resourceTable.rpmsg_vring0,
		&resourceTable.rpmsg_vring1, TO_ARM_HOST, FROM_ARM_HOST);

	/* 
	 * Create the RPMsg channel between the PRU and ARM user space using 
	 * the transport structure. 
	 */
	while (pru_rpmsg_channel(RPMSG_NS_CREATE, &transport, CHAN_NAME,
			CHAN_DESC, CHAN_PORT) != PRU_RPMSG_SUCCESS) {
		/* Optional: implement timeout logic */
	};

	while (1) {
		/* Check register R31 bit 30 to see if the ARM has kicked us */
		if (!(__R31 & HOST_INT))
			continue;

		/* Clear the event status */
		CT_INTC.SICR_bit.STS_CLR_IDX = FROM_ARM_HOST;

		/* 
		 * Receive available messages.
		 * Multiple messages can be sent per kick. 
		 */
		while (pru_rpmsg_receive(&transport, &src, &dst,
				payload, &len) == PRU_RPMSG_SUCCESS) {
			for (i = 0; i < 10; i++) {
				read_adc(voltage);
				message.voltage = voltage[2] & 0x0fff;
				message.cycles[0] = PRU0_CTRL.CYCLE;
				message.cycles[1] = PRU0_CTRL.CYCLE;
				message.cycles[2] = PRU0_CTRL.CYCLE;
				message.cycles[3] = PRU0_CTRL.CYCLE;
				message.cycles[3] = 0xffffff;

				/*
				* Send reply message to the address that sent
				* the initial message
				*/
				pru_rpmsg_send(&transport, dst, src, &message,
					sizeof(message));
			}
		}
	}
}

void init_adc()
{

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
	ADC_TSC.ADC_CLKDIV_bit.ADC_CLKDIV = 0;  // set to max speed

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
}

void read_adc(uint32_t *voltage)
{
	/* 
	 * Clear FIFO0 by reading from it
	 * We are using single-shot mode. 
	 * It should not usually enter the for loop
	 */
	uint32_t count = ADC_TSC.FIFO0COUNT;
	uint32_t data;
	uint32_t i;
	for (i = 0; i < count; i++) {
		data = ADC_TSC.FIFO0DATA;
	}

	// /* read from the specified ADC channel */
	// switch (adc_chan) {
	// 	case 5 :
	// 		ADC_TSC.STEPENABLE_bit.STEP1 = 1;
	// 		break;
	// 	case 6 :
	// 		ADC_TSC.STEPENABLE_bit.STEP2 = 1;
	// 		break;
	// 	case 7 :
	// 		ADC_TSC.STEPENABLE_bit.STEP3 = 1;
	// 		break;
	// 	case 8 :
	// 		ADC_TSC.STEPENABLE_bit.STEP4 = 1;
	// 		break;
	// 	default :
	// 		/* 
	// 		 * this error condition should not occur because of
	// 		 * checks in userspace code
	// 		 */
	// 		ADC_TSC.STEPENABLE_bit.STEP1 = 1;
	// }
	// ADC_TSC.STEPENABLE_bit.STEP1 = 1;
	ADC_TSC.STEPENABLE = 0x1fe;  // enable all 8 capture channels

	while (ADC_TSC.FIFO0COUNT < 8) {
		/*
		 * loop until value placed in fifo.
		 * Optional: implement timeout logic.
		 *
		 * Other potential options include: 
		 *   - configure PRU to receive an ADC interrupt. Note that 
		 *     END_OF_SEQUENCE does not mean that the value has been
		 *     loaded into the FIFO yet
		 *   - perform other actions, and periodically poll for 
		 *     FIFO0COUNT > 0
		 */
	}

	/* read the voltage */
	voltage[0] = ADC_TSC.FIFO0DATA;
	voltage[1] = ADC_TSC.FIFO0DATA;
	voltage[2] = ADC_TSC.FIFO0DATA;
	voltage[3] = ADC_TSC.FIFO0DATA;
	voltage[4] = ADC_TSC.FIFO0DATA;
	voltage[5] = ADC_TSC.FIFO0DATA;
	voltage[6] = ADC_TSC.FIFO0DATA;
	voltage[7] = ADC_TSC.FIFO0DATA;
}
