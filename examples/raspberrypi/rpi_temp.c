/********************************************************************   \

  Name:         rpi_temp.c
  Created by:   Stefan Ritt

  Contents:     Rapberry Pi sense hat temperature driver

  Prerequisite: $ sudo apt-get install libi2c-dev i2c-tools

\********************************************************************/

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <linux/i2c-dev.h>
#include "midas.h"

/*---- globals -----------------------------------------------------*/

/* following structure contains private variables to the device
   driver. It is necessary to store it here in case the device
   driver is used for more than one device in one frontend. If it
   would be stored in a global variable, one device could over-
   write the other device's variables. */

typedef struct {
   int   fd;
} RPI_TEMP_INFO;

#define DEV_ID 0x5c
#define WHO_AM_I 0x0F
#define CTRL_REG1 0x20
#define CTRL_REG2 0x21
#define PRESS_OUT_XL 0x28
#define PRESS_OUT_L 0x29
#define PRESS_OUT_H 0x2A
#define TEMP_OUT_L 0x2B
#define TEMP_OUT_H 0x2C

/*---- device driver routines --------------------------------------*/

/* the init function creates a ODB record which contains the
   settings and initialized it variables as well as the bus driver */

INT rpi_temp_init(HNDLE hkey, void **pinfo)
{
   RPI_TEMP_INFO *info;

   /* allocate info structure */
   info = calloc(1, sizeof(RPI_TEMP_INFO));
   *pinfo = info;

   /* open i2c comms */
   if ((info->fd = open("/dev/i2c-1", O_RDWR)) < 0) {
      perror("Unable to open i2c device");
      exit(1);
   }

   /* configure i2c slave */
   if (ioctl(info->fd, I2C_SLAVE, DEV_ID) < 0) {
      perror("Unable to configure i2c slave device");
      close(info->fd);
      exit(1);
   }

   /* check we are who we should be */
   if (i2c_smbus_read_byte_data(info->fd, WHO_AM_I) != 0xBD) {
      printf("%s\n", "who_am_i error");
      close(info->fd);
      exit(1);
   }

   /* Power down the device (clean start) */
   i2c_smbus_write_byte_data(info->fd, CTRL_REG1, 0x00);

   /* Turn on the pressure sensor analog front end in single shot mode  */
   i2c_smbus_write_byte_data(info->fd, CTRL_REG1, 0x84);

   return FE_SUCCESS;
}

/*----------------------------------------------------------------------------*/

INT rpi_temp_exit(RPI_TEMP_INFO * info)
{
   close(info->fd);

   /* free local variables */
   free(info);

   return FE_SUCCESS;
}

/*----------------------------------------------------------------------------*/

INT rpi_temp_get(RPI_TEMP_INFO * info, INT channel, float *pvalue)
{
   uint8_t status = 0;
   uint8_t temp_l, temp_h;
   int16_t temp;
   float   deg;
   
   /* Run one-shot measurement (temperature and pressure), the set bit will be reset by the
    * sensor itself after execution (self-clearing bit) */
   i2c_smbus_write_byte_data(info->fd, CTRL_REG2, 0x01);

   /* Wait until the measurement is complete */
   do {
      usleep(25*1000);	/* 25 milliseconds */
      status = i2c_smbus_read_byte_data(info->fd, CTRL_REG2);
   }
   while (status != 0);

   /* Read the temperature measurement (2 bytes to read) */
   temp_l = i2c_smbus_read_byte_data(info->fd, TEMP_OUT_L);
   temp_h = i2c_smbus_read_byte_data(info->fd, TEMP_OUT_H);
   temp = temp_h << 8 | temp_l;
   
   /* calculate temperature  */
   deg = (42.5 + (temp / 480.0));
   *pvalue = ((int)(deg*100+0.5))/100.0;;

   // printf("Temp = %.2f°C\n", *pvalue);

   return FE_SUCCESS;
}

/*---- device driver entry point -----------------------------------*/

INT rpi_temp(INT cmd, ...)
{
   va_list argptr;
   HNDLE   hKey;
   INT     status, channel;
   float   *pvalue;
   void    *info;

   va_start(argptr, cmd);
   status = FE_SUCCESS;

   switch (cmd) {
   case CMD_INIT:
      hKey = va_arg(argptr, HNDLE);
      info = va_arg(argptr, void *);
      status = rpi_temp_init(hKey, info);
      break;

   case CMD_EXIT:
      info = va_arg(argptr, void *);
      status = rpi_temp_exit(info);
      break;

   case CMD_GET:
      info = va_arg(argptr, void *);
      channel = va_arg(argptr, INT);
      pvalue = va_arg(argptr, float *);
      status = rpi_temp_get(info, channel, pvalue);
      break;

   default:
      break;
   }

   va_end(argptr);

   return status;
}

/*------------------------------------------------------------------*/
