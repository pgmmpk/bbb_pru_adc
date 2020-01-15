#ifndef __DRIVER_H
#define __DRIVER_H

typedef struct {
    unsigned int timestamp;
    unsigned short values[8];  // actual number of readings is num_channels
} reading_t;

typedef struct {
    unsigned char eye[8];
} driver_t;

extern driver_t *driver_start(
   unsigned int clk_div, unsigned int step_avg,
   unsigned int num_channels, unsigned char const *channels,
   unsigned int max_num, unsigned int target_delay);

extern int driver_read(driver_t *drv, int *num_dropped, unsigned int *timestamps, float *values);
extern int driver_stop(driver_t *drv);

extern int driver_num_records(unsigned int num_channels, unsigned int max_num);

#endif
