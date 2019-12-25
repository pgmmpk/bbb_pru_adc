#ifndef __COMMON_H
#define __COMMON_H

/*
 * Structure of command sent from CPU to PRU
 */
typedef struct {
    uint16_t magic;        // magic number to protect against garbage data
#define COMMAND_MAGIC 0xbeef
    uint16_t command;
#define COMMAND_STOP (2)
#define COMMAND_ACK (3)
#define COMMAND_START (1)
} command_t;

/*
 * CPU sends which channels to capture, by specifying:
 * 1. num_channels - how many channels to capture
 * 2. channels map. First num_channels elements in channels array
 *    should be filled with the channel number to capture:
 *    0 - AIN channel 1
 *    1 - AIN channel 2
 *    etc
 */
typedef struct {
    command_t header;
    uint16_t  speed;          // 0 - highest speed, 8-lowest
    uint16_t  num_channels;   // 1-8
    uint8_t   channels[8];
} command_start_t;

/*
 * structure of reply buffer
 */
typedef struct {
	uint16_t num;
	uint16_t num_dropped;
	uint16_t data[1];
} buffer_t;

#endif
