/********************************************************************\

  Name:         mscbhv4.c
  Created by:   Stefan Ritt

  Contents:     MSCB Mu3e 4-channel High Voltage Device Driver

  $Id: mscbhvr.c 2753 2005-10-07 14:55:31Z ritt $

\********************************************************************/

#include <stdio.h>
#include <math.h>
#include <stdarg.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include "midas.h"
#include "mscb.h"
#include "mfe.h"

/*---- globals -----------------------------------------------------*/

typedef struct {
   char mscb_device[NAME_LENGTH];
   char pwd[NAME_LENGTH];
   BOOL debug;
   int  *address;
   int  channels;
} MSCBHV4_SETTINGS;

typedef struct {
   unsigned char control;
   float u_demand;
   float u_meas;
   unsigned char enabled;
   unsigned char on[4];
   float i_meas[4];
   bool cached;
} MSCB_NODE_VARS;

typedef struct {
   MSCBHV4_SETTINGS settings;
   int fd;
   MSCB_NODE_VARS *node_vars;
} MSCBHV4_INFO;

INT mscbhv4_read_all(MSCBHV4_INFO * info, int i);

/*---- device driver routines --------------------------------------*/

INT mscbhv4_init(HNDLE hkey, void **pinfo, INT channels, INT(*bd) (INT cmd, ...))
{
   int  status, size;
   HNDLE hDB;
   MSCBHV4_INFO *info;
   MSCB_INFO node_info;
   KEY key;

   /* allocate info structure */
   info = (MSCBHV4_INFO *) calloc(1, sizeof(MSCBHV4_INFO));
   info->node_vars = (MSCB_NODE_VARS *) calloc(channels, sizeof(MSCB_NODE_VARS));
   *pinfo = info;

   cm_get_experiment_database(&hDB, NULL);

   // retrieve device name
   db_get_key(hDB, hkey, &key);

   // create MSCBHV4 settings record
   size = sizeof(info->settings.mscb_device);
   info->settings.mscb_device[0] = 0;
   status = db_get_value(hDB, hkey, "MSCB Device", &info->settings.mscb_device, &size, TID_STRING, TRUE);
   if (status != DB_SUCCESS)
      return FE_ERR_ODB;

   size = sizeof(info->settings.pwd);
   info->settings.pwd[0] = 0;
   status = db_get_value(hDB, hkey, "MSCB Pwd", &info->settings.pwd, &size, TID_STRING, TRUE);
   if (status != DB_SUCCESS)
      return FE_ERR_ODB;

   info->settings.address = (int*)calloc(sizeof(INT), channels/4);
   assert(info->settings.address);

   int n_nodes = ((channels-1) / 4 + 1);
   size = n_nodes * sizeof(INT);
   status = db_get_value(hDB, hkey, "MSCB Address", info->settings.address, &size, TID_INT, FALSE);
   if (status != DB_SUCCESS)
      return FE_ERR_ODB;

   // open device on MSCB
   info->fd = mscb_init(info->settings.mscb_device, NAME_LENGTH, info->settings.pwd, info->settings.debug);
   if (info->fd < 0) {
      cm_msg(MERROR, "mscbhv4_init",
             "Cannot access MSCB submaster at \"%s\". Check power and connection.",
             info->settings.mscb_device);
      return FE_ERR_HW;
   }

   // check nodes
   for (int i=0 ; i<n_nodes ; i++) {
      status = mscb_info(info->fd, info->settings.address[i], &node_info);
      if (status != MSCB_SUCCESS) {
         cm_msg(MERROR, "mscbhv4_init",
                "Cannot access HV4 node at address \"%d\". Please check cabling and power.",
                info->settings.address[0]);
         return FE_ERR_HW;
      }

      if (strcmp(node_info.node_name, "HV+") != 0 &&
          strcmp(node_info.node_name, "HV-") != 0) {
         cm_msg(MERROR, "mscbhvr_init",
                "Found unexpected node \"%s\" at address \"%d\".",
                node_info.node_name, info->settings.address[i]);
         return FE_ERR_HW;
      }
   }

   // read all values from HV4 devices
   for (int i=0 ; i<channels ; i++) {

      if (i % 10 == 0)
         printf("%s: %d\r", key.name, i);

      status = mscbhv4_read_all(info, i);
      if (status != FE_SUCCESS)
         return status;
   }
   printf("%s: %d\n", key.name, channels);

   return FE_SUCCESS;
}

/*----------------------------------------------------------------------------*/

INT mscbhv4_read_all(MSCBHV4_INFO * info, int i)
{
   int size, status;
   unsigned char buffer[256], *pbuf;
   char str[256];

   if (i % 4 != 0)
      return FE_SUCCESS;

   size = sizeof(buffer);
   status = mscb_read_range(info->fd, info->settings.address[i/4], 0, 10, buffer, &size);
   if (status != MSCB_SUCCESS) {
      sprintf(str, "Error reading MSCB HV4 at \"%s:%d\".",
              info->settings.mscb_device, info->settings.address[i/4]);
      mfe_error(str);
      return FE_ERR_HW;
   }

   // decode variables from buffer
   pbuf = buffer;
   DWORD_SWAP(pbuf);
   info->node_vars[i].u_demand = *((float *)pbuf);
   pbuf += sizeof(float);
   DWORD_SWAP(pbuf);
   info->node_vars[i].u_meas = *((float *)pbuf);
   pbuf += sizeof(float);
   info->node_vars[i].enabled = *((unsigned char *)pbuf);
   pbuf += sizeof(char);
   info->node_vars[i].on[0] = *((unsigned char *)pbuf);
   pbuf += sizeof(char);
   info->node_vars[i].on[1] = *((unsigned char *)pbuf);
   pbuf += sizeof(char);
   info->node_vars[i].on[2] = *((unsigned char *)pbuf);
   pbuf += sizeof(char);
   info->node_vars[i].on[3] = *((unsigned char *)pbuf);
   pbuf += sizeof(char);
   DWORD_SWAP(pbuf);
   info->node_vars[i].i_meas[0] = *((float *)pbuf);
   pbuf += sizeof(float);
   DWORD_SWAP(pbuf);
   info->node_vars[i].i_meas[1] = *((float *)pbuf);
   pbuf += sizeof(float);
   DWORD_SWAP(pbuf);
   info->node_vars[i].i_meas[2] = *((float *)pbuf);
   pbuf += sizeof(float);
   DWORD_SWAP(pbuf);
   info->node_vars[i].i_meas[3] = *((float *)pbuf);
   pbuf += sizeof(float);

   // mark voltage/current as valid in cache
   for (int j=i ; j<i+4 ; j++)
      info->node_vars[j].cached = 1;

   return FE_SUCCESS;
}

/*----------------------------------------------------------------------------*/

INT mscbhv4_exit(MSCBHV4_INFO * info)
{
   mscb_exit(info->fd);

   free(info);

   return FE_SUCCESS;
}

/*----------------------------------------------------------------------------*/

INT mscbhv4_set(MSCBHV4_INFO * info, INT channel, float value)
{
   int fc = channel / 4 * 4; // first channel of module
   int mc = channel % 4;     // channel in module

   if (value == 0) {
      // turn channel off
      unsigned char flag = 0;
      mscb_write(info->fd, info->settings.address[channel/4], 3+mc, &flag, 1);
      info->node_vars[channel].u_demand = 0;
      info->node_vars[fc].on[mc] = 0;
   } else {

      mscb_write(info->fd, info->settings.address[channel / 4], 0, &value, 4);

      // set demand value of all four channels, since unit only has one demand
      for (int i = fc; i < fc + 4; i++)
         if (info->node_vars[i].u_demand != 0)
            info->node_vars[i].u_demand = value;

      if (info->node_vars[fc].on[mc] == 0) {
         // turn channel on
         unsigned char flag = 1;
         mscb_write(info->fd, info->settings.address[channel/4], 3+mc, &flag, 1);
         info->node_vars[fc].on[mc] = 1;
      }
   }

   return FE_SUCCESS;
}

/*----------------------------------------------------------------------------*/

INT mscbhv4_get(MSCBHV4_INFO * info, INT channel, float *pvalue)
{
   int fc = channel / 4 * 4; // first channel of module

   // check if value was previously read by mscbhvr_read_all()
   if (info->node_vars[channel].cached) {
      if (info->node_vars[fc].on[channel % 4] == 0)
         *pvalue = 0;
      else
         *pvalue = info->node_vars[fc].u_meas;
      info->node_vars[channel].cached = 0;
      return FE_SUCCESS;
   }

   int status = mscbhv4_read_all(info, channel);

   if (info->node_vars[fc].on[channel % 4] == 0)
      *pvalue = 0;
   else
      *pvalue = info->node_vars[fc].u_meas;
   return status;
}

/*---- device driver entry point -----------------------------------*/

INT mscbhv4(INT cmd, ...)
{
   va_list argptr;
   HNDLE hKey;
   INT channel, status;
   float value, *pvalue;
   int *pivalue, i;
   INT(*bd)(INT,...);
   MSCBHV4_INFO *info;

   va_start(argptr, cmd);
   status = FE_SUCCESS;

   switch (cmd) {
      case CMD_INIT:
         hKey = va_arg(argptr, HNDLE);
         info = va_arg(argptr, MSCBHV4_INFO *);
         channel = va_arg(argptr, INT);
         bd = va_arg(argptr, INT(*)(INT,...));
         status = mscbhv4_init(hKey, (void**)info, channel, bd);
         break;

      case CMD_EXIT:
         info = va_arg(argptr, MSCBHV4_INFO *);
         status = mscbhv4_exit(info);
         break;

      case CMD_SET:
         info = va_arg(argptr, MSCBHV4_INFO *);
         channel = va_arg(argptr, INT);
         value = (float) va_arg(argptr, double);
         status = mscbhv4_set(info, channel, value);
         break;

      case CMD_GET:
         info = va_arg(argptr, MSCBHV4_INFO *);
         channel = va_arg(argptr, INT);
         pvalue = va_arg(argptr, float *);
         status = mscbhv4_get(info, channel, pvalue);
         break;

      case CMD_GET_DEMAND:
         info = va_arg(argptr, MSCBHV4_INFO *);
         channel = va_arg(argptr, INT);
         pvalue = va_arg(argptr, float *);
         i = channel / 4 * 4;

         if (info->node_vars[i].on[channel % 4] == 0)
            *pvalue = 0;
         else
            *pvalue = info->node_vars[i].u_demand;
         break;

      case CMD_GET_CURRENT:
         info = va_arg(argptr, MSCBHV4_INFO *);
         channel = va_arg(argptr, INT);
         pvalue = va_arg(argptr, float *);
         i = channel / 4 * 4;
         *pvalue = info->node_vars[i].i_meas[channel % 4];
         break;

      case CMD_GET_LABEL:
      case CMD_SET_LABEL:
         status = FE_SUCCESS;
         break;

      case CMD_GET_THRESHOLD:
         info = va_arg(argptr, MSCBHV4_INFO *);
         channel = va_arg(argptr, INT);
         pvalue = va_arg(argptr, float *);
         *pvalue = 0.01f;
         break;

      case CMD_GET_THRESHOLD_CURRENT:
         info = va_arg(argptr, MSCBHV4_INFO *);
         channel = va_arg(argptr, INT);
         pvalue = va_arg(argptr, float *);
         *pvalue = 0.05f;
         break;

      case CMD_GET_THRESHOLD_ZERO:
         info = va_arg(argptr, MSCBHV4_INFO *);
         channel = va_arg(argptr, INT);
         pvalue = va_arg(argptr, float *);
         *pvalue = 15;
         break;

      case CMD_GET_STATUS:
         info = va_arg(argptr, MSCBHV4_INFO *);
         channel = va_arg(argptr, INT);
         pivalue = va_arg(argptr, INT *);
         i = channel / 4 * 4;
         *pivalue = info->node_vars[i].enabled;
         break;

      case CMD_GET_VOLTAGE_LIMIT:
         info = va_arg(argptr, MSCBHV4_INFO *);
         channel = va_arg(argptr, INT);
         pvalue = va_arg(argptr, float *);
         *pvalue = 120;
         break;

      case CMD_GET_CURRENT_LIMIT:
         info = va_arg(argptr, MSCBHV4_INFO *);
         channel = va_arg(argptr, INT);
         pvalue = va_arg(argptr, float *);
         *pvalue = 1.25;
         break;

      case CMD_GET_RAMPDOWN:
      case CMD_GET_RAMPUP:
      case CMD_GET_TRIP_TIME:
      case CMD_GET_TRIP:
      case CMD_GET_TEMPERATURE:
         info = va_arg(argptr, MSCBHV4_INFO *);
         channel = va_arg(argptr, INT);
         pivalue = va_arg(argptr, INT *);
         *pivalue = 0; // not implemented for the moment...
         status = FE_SUCCESS;
         break;

      case CMD_SET_TRIP_TIME:
      case CMD_SET_VOLTAGE_LIMIT:
      case CMD_SET_CURRENT_LIMIT:
      case CMD_START:
      case CMD_STOP:
      case CMD_SET_RAMPUP:
      case CMD_SET_RAMPDOWN:
         break;

      default:
         cm_msg(MERROR, "mscbhv4 device driver", "Received unknown command %d", cmd);
         status = FE_ERR_DRIVER;
         break;
   }

   va_end(argptr);

   return status;
}

/*------------------------------------------------------------------*/
