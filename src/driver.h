#ifndef __DRIVER_H
#define __DRIVER_H

typedef struct {
    unsigned int timestamp;
    unsigned short values[8];  // actual number of readings is num_channels
} reading_t;

typedef struct {
    unsigned char eye[8];
} driver_t;

extern driver_t *driver_start(unsigned int num_channels, unsigned char const *channels);

extern int driver_read(driver_t *drv, reading_t *buffer);
extern int driver_stop(driver_t *drv);

#endif