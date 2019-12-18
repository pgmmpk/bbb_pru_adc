#ifndef __COMMON_H
#define __COMMON_H

/*
 * Structure of command sent from CPU to PRU
 */
typedef struct {
    uint16_t magic;      // magic number to protect against garbage data
    uint8_t channels[8]; // up to 8 configurable ADC channels
} command_t;

#define COMMAND_MAGIC 0xbeef
#define COMMAND_NO_CHANNEL 0xff
#define COMMAND_CHANNEL_1  0x01
#define COMMAND_CHANNEL_2  0x02
#define COMMAND_CHANNEL_3  0x03
#define COMMAND_CHANNEL_4  0x04
#define COMMAND_CHANNEL_5  0x05
#define COMMAND_CHANNEL_6  0x06
#define COMMAND_CHANNEL_7  0x07
#define COMMAND_CHANNEL_8  0x08

/*
 * structure of reply buffer
 */
typedef struct {
    uint16_t cycles;     // cycles elapsed since the last reading
    uint16_t numval;     // number of values that follow
    uint16_t values[1];  // actual number of values varies
} buffer_t;

#endif
