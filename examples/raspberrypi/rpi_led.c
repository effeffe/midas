/********************************************************************\

  Name:         rpi_led.c
  Created by:   Stefan Ritt

  Contents:     Rapberry Pi LED sense hat Device Driver

\********************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <stdint.h>
#include <string.h>
#include <linux/fb.h>
#include <sys/ioctl.h>
#include "midas.h"

/*---- globals -----------------------------------------------------*/

/* following structure contains private variables to the device
   driver. It is necessary to store it here in case the device
   driver is used for more than one device in one frontend. If it
   would be stored in a global variable, one device could over-
   write the other device's variables. */

typedef struct {
   int   fbfd;
   uint16_t *map;
} RPI_LED_INFO;

#define FILESIZE (64 * sizeof(uint16_t))

/*---- device driver routines --------------------------------------*/

/* the init function creates a ODB record which contains the
   settings and initialized it variables as well as the bus driver */

INT rpi_led_init(HNDLE hkey, void **pinfo)
{
   RPI_LED_INFO *info;
   struct fb_fix_screeninfo fix_info;

   /* allocate info structure */
   info = calloc(1, sizeof(RPI_LED_INFO));
   *pinfo = info;

   /* open the led frame buffer device */
   info->fbfd = open("/dev/fb1", O_RDWR);
   if (info->fbfd == -1) {
      perror("Error (call to 'open')");
      exit(EXIT_FAILURE);
   }

   /* read fixed screen info for the open device */
   if (ioctl(info->fbfd, FBIOGET_FSCREENINFO, &fix_info) == -1) {
      perror("Error (call to 'ioctl')");
      close(info->fbfd);
      exit(EXIT_FAILURE);
   }

   /* now check the correct device has been found */
   if (strcmp(fix_info.id, "RPi-Sense FB") != 0) {
      printf("Error: RPi-Sense FB not found\n");
      close(info->fbfd);
      exit(EXIT_FAILURE);
   }

   /* map the led frame buffer device into memory */
   info->map = mmap(NULL, FILESIZE, PROT_READ | PROT_WRITE, MAP_SHARED, info->fbfd, 0);
   if (info->map == MAP_FAILED) {
      close(info->fbfd);
      perror("Error mmapping the file");
      exit(EXIT_FAILURE);
   }

   /* clear the led matrix */
   memset(info->map, 0, FILESIZE);

   return FE_SUCCESS;
}

/*----------------------------------------------------------------------------*/

INT rpi_led_exit(RPI_LED_INFO * info)
{
   /* un-map and close */
   if (munmap(info->map, FILESIZE) == -1) {
      perror("Error un-mmapping the file");
   }
   close(info->fbfd);

   /* free local variables */
   free(info);

   return FE_SUCCESS;
}

/*----------------------------------------------------------------------------*/

INT rpi_led_set(RPI_LED_INFO * info, INT channel, double value)
{
   uint16_t r, g, b, rgb;
   uint32_t col;

   col = (uint32_t)value;

   r = ((col >> 16) & 0xFF) >> 3;
   g = ((col >>  8) & 0xFF) >> 3;
   b = ((col >>  0) & 0xFF) >> 3;
   rgb = (r << 11) | (g << 6) | (b << 1);
   info->map[channel] = rgb;

   return FE_SUCCESS;
}

/*----------------------------------------------------------------------------*/

INT rpi_led_get(RPI_LED_INFO * info, INT channel, float *pvalue)
{
   uint16_t r, g, b, rgb;
   uint32_t col;

   /* read value from channel */
   rgb = info->map[channel];
   r = (rgb >> 11) & 0x1F;
   g = (rgb >>  6) & 0x1F;
   b = (rgb >>  1) & 0x1F;

   col = (r << 16) | (g << 8) | b;
   *pvalue = (float) col;

   return FE_SUCCESS;
}

/*---- device driver entry point -----------------------------------*/

INT rpi_led(INT cmd, ...)
{
   va_list argptr;
   HNDLE   hKey;
   INT     status, channel;
   float   *pvalue;
   double  value;
   void    *info;

   va_start(argptr, cmd);
   status = FE_SUCCESS;

   switch (cmd) {
   case CMD_INIT:
      hKey = va_arg(argptr, HNDLE);
      info = va_arg(argptr, void *);
      status = rpi_led_init(hKey, info);
      break;

   case CMD_EXIT:
      info = va_arg(argptr, void *);
      status = rpi_led_exit(info);
      break;

   case CMD_SET:
      info = va_arg(argptr, void *);
      channel = va_arg(argptr, INT);
      value = va_arg(argptr, double);
      status = rpi_led_set(info, channel, value);
      break;

   case CMD_GET:
      info = va_arg(argptr, void *);
      channel = va_arg(argptr, INT);
      pvalue = va_arg(argptr, float *);
      status = rpi_led_get(info, channel, pvalue);
      break;

   default:
      break;
   }

   va_end(argptr);

   return status;
}

/*------------------------------------------------------------------*/
