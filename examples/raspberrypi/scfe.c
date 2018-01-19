/********************************************************************\

  Name:         scfe.c
  Created by:   Stefan Ritt Jan. 2018

  Contents:     Example slow control front-end for Raspberry Pi 
                computer with sense hat shield. 

                https://www.raspberrypi.org/products/sense-hat/

                This front-end defines a "RaspberryPi" slow control
                equipment with two device drivers rpi_temp and
                rpi_led which read out the temperature sensor
                on the sense hat and control the 64 LEDs.

                The temperature will show up under
                /Equipment/RaspberryPi/Variables/Input

                The LEDs can be controlled via
                /Equipment/RaspberryPi/Variables/Output[64]

                where each value in the array contains the
                RGB value for that one LED. Each R,G,B value
                has a range 0...255 and gets bitwise or'ed 
                and shifted. Writing 0x0000FF (255) to a value
                turns the corresponding LED blue, writing
                0xFFFFFF (16777215) turns the LED white.

                The custom page rpi.html can be used to control
                the LEDs from a web page. To do so, put following
                into the ODB:

                /Custom/Path = /<path to rpi.html>
                /Custom/RPi = rpi.html

                Befor compiling this program, you have to install
                the I2C package on the Raspberry Pi with:

                $ sudo apt-get install libi2c-dev i2c-tools
                
\********************************************************************/

#include <stdio.h>
#include "midas.h"
#include "class/multi.h"
#include "rpi_temp.h"
#include "rpi_led.h"

/*-- Globals -------------------------------------------------------*/

/* The frontend name (client name) as seen by other MIDAS clients   */
const char *frontend_name = "SC Frontend";
/* The frontend file name, don't change it */
const char *frontend_file_name = __FILE__;

/* frontend_loop is called periodically if this variable is TRUE    */
BOOL frontend_call_loop = FALSE;

/* a frontend status page is displayed with this frequency in ms    */
INT display_period = 1000;

/* maximum event size produced by this frontend */
INT max_event_size = 10000;

/* maximum event size for fragmented events (EQ_FRAGMENTED) */
INT max_event_size_frag = 5 * 1024 * 1024;

/* buffer size to hold events */
INT event_buffer_size = 10 * 10000;

/*-- Equipment list ------------------------------------------------*/

/* device driver list */
DEVICE_DRIVER multi_driver[] = {
   {"Temperature",  rpi_temp,  1, NULL, DF_INPUT  }, // one input channel
   {"LED",          rpi_led,  64, NULL, DF_OUTPUT }, // 64 output channels
   {""}
};

EQUIPMENT equipment[] = {
   {"RaspberryPi",              /* equipment name */
    {4, 0,                      /* event ID, trigger mask */
     "SYSTEM",                  /* event buffer */
     EQ_SLOW,                   /* equipment type */
     0,                         /* event source */
     "MIDAS",                   /* format */
     TRUE,                      /* enabled */
     RO_RUNNING | RO_TRANSITIONS,        /* read when running and on transitions */
     60000,                     /* read every 60 sec */
     0,                         /* stop run after this event limit */
     0,                         /* number of sub events */
     1,                         /* log history every event */
     "", "", ""} ,
    cd_multi_read,              /* readout routine */
    cd_multi,                   /* class driver main routine */
    multi_driver,               /* device driver list */
    NULL,                       /* init string */
    },

   {""}
};


/*-- Dummy routines ------------------------------------------------*/

INT poll_event(INT source[], INT count, BOOL test)
{
   return 1;
};
INT interrupt_configure(INT cmd, INT source[], POINTER_T adr)
{
   return 1;
};

/*-- Frontend Init -------------------------------------------------*/

INT frontend_init()
{
   return CM_SUCCESS;
}

/*-- Frontend Exit -------------------------------------------------*/

INT frontend_exit()
{
   return CM_SUCCESS;
}

/*-- Frontend Loop -------------------------------------------------*/

INT frontend_loop()
{
   return CM_SUCCESS;
}

/*-- Begin of Run --------------------------------------------------*/

INT begin_of_run(INT run_number, char *error)
{
   return CM_SUCCESS;
}

/*-- End of Run ----------------------------------------------------*/

INT end_of_run(INT run_number, char *error)
{
   return CM_SUCCESS;
}

/*-- Pause Run -----------------------------------------------------*/

INT pause_run(INT run_number, char *error)
{
   return CM_SUCCESS;
}

/*-- Resume Run ----------------------------------------------------*/

INT resume_run(INT run_number, char *error)
{
   return CM_SUCCESS;
}

/*------------------------------------------------------------------*/
