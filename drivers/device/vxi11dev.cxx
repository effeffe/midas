/********************************************************************\

  Name:         vxi11dev.c
  Created by:   Kei Ieki

  Contents:     VXI11 protocol device driver

  $Id: vxi11dev.c 5219 2011-11-10 23:51:08Z svn $

\********************************************************************/

#include <stddef.h>
#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
/* #include "dbDefs.h" */

#include "vxi11_user.h"
#include "midas.h"
#include "msystem.h"
#include "vxi11dev.h"

/*---- globals -----------------------------------------------------*/

#define CMD_LENGTH 32      /* length of command string */
#define TYPE_LENGTH 32     /* length of type string */

typedef struct {
   CLINK   *clink;
   char    ip_address[32]; // IP address of the device
   INT     num_vars;       // Number of all variables
   char    *cmd_str;       // Command string
   char    *type_str;      // Type string
} VXI_INFO;

static CLINK* clink = 0;
static bool busy = false;

const char* vars_onoff[] = {"OFF", "ON"};
const char* vars_IV[] = {"CURR", "VOLT"};
const char* vars_trig[] = {"INT", "INT2", "EXT"};
const char* vars_func[] = {"SIN", "SQU", "RAMP", "PULS", "NOIS", "DC", "USER"};

/*---- device driver routines --------------------------------------*/

INT vxi11dev_init(HNDLE hKey, void **pinfo, INT nvars)
{

   int status, i, size;
   HNDLE hDB;
   VXI_INFO *info;

   /* initialize database */
   cm_get_experiment_database(&hDB, NULL);

   /* allocate info structure */
   info = (VXI_INFO*)calloc(1, sizeof(VXI_INFO));
   *pinfo = info;
   
   /* IP address */
   size = sizeof(info->ip_address);
   status = db_get_value(hDB, hKey, "IP address", &info->ip_address, &size, TID_STRING, TRUE);
   if (status != DB_SUCCESS)
      return FE_ERR_ODB;

   /* CLINK */
   info->clink = (CLINK*)calloc(1, sizeof(CLINK));
   cm_msg(MINFO, "vxi11dev_init", "vxi11 initialization with IP: %s", info->ip_address);
   status = FE_SUCCESS;
   if (!clink) {
      /* open connection */
      clink = new CLINK();
      int ret = vxi11_open_device(info->ip_address, clink);
      if (ret != 0) {
         status = FE_ERR_DRIVER;
	 cm_msg(MERROR, "vxi11dev_init", "vxi11dev driver error: %d", ret);
      } else {
	cm_msg(MINFO, "vxi11dev_init", "vxi11dev initialized successfully.");
      }
   }
   info->clink = clink;

   /* nvars */
   info->num_vars = nvars;
   
   /* command string */
   info->cmd_str = (char*)calloc(nvars, CMD_LENGTH);
   for (i=0; i<nvars; i++)
      sprintf(info->cmd_str + CMD_LENGTH * i, "<Empty>%d", i);
   db_merge_data(hDB, hKey, "Command", info->cmd_str, CMD_LENGTH * nvars, nvars, TID_STRING);

   /* type string */
   info->type_str = (char*)calloc(nvars, TYPE_LENGTH);
   for (i=0; i<nvars; i++)
      sprintf(info->type_str + TYPE_LENGTH * i, "<Empty>%d", i);
   db_merge_data(hDB, hKey, "Type", info->type_str, TYPE_LENGTH * nvars, nvars, TID_STRING);

   return status;
   
}

/*----------------------------------------------------------------------------*/

INT vxi11dev_exit(VXI_INFO *info)
{

   if (!clink) {
      vxi11_close_device(info->ip_address, info->clink);
      clink = 0;
      free(info->clink);
   }

   if (info->cmd_str) {
      free(info->cmd_str);
      info->cmd_str = 0;
   }

   if (info) {
      free(info);
      info = 0;
   }
   
   return FE_SUCCESS;
}

/*----------------------------------------------------------------------------*/

INT vxi11dev_set(VXI_INFO *info, INT channel, float value)
{
   char	cmd[256];
   char	cmd_base[CMD_LENGTH];
   int index = (int)value;
   strlcpy(cmd_base, info->cmd_str + channel*CMD_LENGTH, CMD_LENGTH);

   // variable type
   char	type_this[TYPE_LENGTH];
   strlcpy(type_this, info->type_str + channel*TYPE_LENGTH, TYPE_LENGTH);

   // make command
   if (equal_ustring(type_this, "TYPE_NUM")) {
      sprintf(cmd, "%s %f", cmd_base, value);
   } else if (equal_ustring(type_this, "TYPE_ONOFF")) {
      if (vars_onoff[index]) {
         sprintf(cmd, "%s %s", cmd_base, vars_onoff[index]);
      }
   } else if (equal_ustring(type_this, "TYPE_IV")) {
      if (vars_IV[index]) {
         sprintf(cmd, "%s %s", cmd_base, vars_IV[index]);
      }
   } else if (equal_ustring(type_this, "TYPE_TRIG")) {
      if (vars_trig[index]) {
         sprintf(cmd, "%s %s", cmd_base, vars_trig[index]);
      }
   } else if (equal_ustring(type_this, "TYPE_FUNC")) {
      if (vars_func[index]) {
         sprintf(cmd, "%s %s", cmd_base, vars_func[index]);
      }
   }

   // send command
   while (busy) {
      sleep(3.0);
   }
   busy = true;
   long ret = vxi11_send(info->clink, cmd);
   busy = false;
   if (ret != 0) {
     cm_msg(MINFO, "vxi11dev_set", "unusual return value in vxi11_send: %ld", ret);
   }
   
   busy = false;
      
   return FE_SUCCESS;
}

/*----------------------------------------------------------------------------*/

INT vxi11dev_get(VXI_INFO *info, INT channel, float *pvalue)
{

   float val;
   int size = sizeof(val);
   
   int i;
   char	cmd_base[CMD_LENGTH];
   char	cmd[64];
   char buf[256];
   strlcpy(cmd_base, info->cmd_str + channel*CMD_LENGTH, CMD_LENGTH);
   sprintf(cmd, "%s?", cmd_base);

   // send and receive from device
   while (busy) {
      sleep(3.0);
   }
   busy = true;
   long ret = vxi11_send_and_receive(info->clink, cmd, buf, 256, VXI11_READ_TIMEOUT*10);
   busy = false;
   if (ret != 0) {
     cm_msg(MINFO, "vxi11dev_get", "unusual return value in vxi11_send_and_receive: %ld", ret);
   }
   std::string buf_str = buf;
   int index =  buf_str.find('\n');
   if (index >= 0) {
      buf[index] = '\0';
   }

   // variable type
   char	type_this[TYPE_LENGTH];
   strlcpy(type_this, info->type_str + channel*TYPE_LENGTH, TYPE_LENGTH);

   // convert to number
   if (equal_ustring(type_this, "TYPE_NUM")) {
      (*pvalue) = atof(buf);
   } else if (equal_ustring(type_this, "TYPE_ONOFF")) {
      for (i=0; i<2; i++) {
         if (equal_ustring(buf, vars_onoff[i])) {
            (*pvalue) = i;
         }
      }
   } else if (equal_ustring(type_this, "TYPE_IV")) {
      for (i=0; i<2; i++) {
         if (equal_ustring(buf, vars_IV[i])) {
            (*pvalue) = i;
         }
      }
   } else if (equal_ustring(type_this, "TYPE_TRIG")) {
      for (i=0; i<3; i++) {
         if (equal_ustring(buf, vars_trig[i])) {
            (*pvalue) = i;
         }
      }
   } else if (equal_ustring(type_this, "TYPE_FUNC")) {
      for (i=0; i<7; i++) {
         if (equal_ustring(buf, vars_func[i])) {
            (*pvalue) = i;
         }
      }
   }

   return FE_SUCCESS;
}

/*----------------------------------------------------------------------------*/

INT vxi11dev_get_demand(VXI_INFO *info, INT channel, float *pvalue)
{
   return FE_SUCCESS;
}

/*---- device driver entry point -----------------------------------*/

INT vxi11dev(INT cmd, ...)
{
   va_list argptr;
   HNDLE hKey;
   INT channel, status;
   float value, *pvalue;
   VXI_INFO *info;
   char *name;
   
   va_start(argptr, cmd);
   status = FE_SUCCESS;
   
   if (cmd == CMD_INIT) {
      void *pinfo;
      
      hKey = va_arg(argptr, HNDLE);
      pinfo = va_arg(argptr, void *);
      channel = va_arg(argptr, INT);
      status = vxi11dev_init(hKey, (void**)pinfo, channel);
   } else {
      info = va_arg(argptr, VXI_INFO *);
      
      /* only execute command if enabled */
      switch (cmd) {
         case CMD_INIT:
            break;
            
         case CMD_EXIT:
            status = vxi11dev_exit(info);
            break;
            
         case CMD_START:
            break;
            
         case CMD_SET:
            channel = va_arg(argptr, INT);
            value = (float) va_arg(argptr, double);
            status = vxi11dev_set(info, channel, value);
            break;
            
         case CMD_GET:
            channel = va_arg(argptr, INT);
            pvalue = va_arg(argptr, float *);
            status = vxi11dev_get(info, channel, pvalue);
            break;
            
         case CMD_GET_DEMAND:
            channel = va_arg(argptr, INT);
            pvalue = va_arg(argptr, float *);
            status = vxi11dev_get_demand(info, channel, pvalue);
            break;

         case CMD_GET_LABEL:
            channel = va_arg(argptr, INT);
            name = va_arg(argptr, char *);
            strlcpy(name, info->cmd_str + channel*CMD_LENGTH, CMD_LENGTH);
            break;

         case CMD_GET_THRESHOLD:
            channel = va_arg(argptr, INT);
            pvalue = va_arg(argptr, float *);
            *pvalue = 0.01;
            break;
            
         default:
            break;
      }
   }
   
   va_end(argptr);
   
   return status;
}

/*------------------------------------------------------------------*/
