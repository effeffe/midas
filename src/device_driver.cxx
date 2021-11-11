/********************************************************************\

  Name:         device_driver.c
  Created by:   Stefan Ritt

  Contents:     The system part of the MIDAS frontend.

\********************************************************************/

#include <stdio.h>
#include <assert.h>
#include "midas.h"
#include "msystem.h"

static int sc_thread(void *info)
{
   DEVICE_DRIVER *device_drv = (DEVICE_DRIVER*)info;
   int i, status, cmd;
   int current_channel = 0;
   int current_priority_channel = 0;
   float value;
   int *last_update;
   unsigned int current_time;
   DWORD last_time;

   ss_thread_set_name(std::string("SC:")+ *device_drv->pequipment_name);
   auto s = ss_thread_get_name();

   last_update = (int*)calloc(device_drv->channels, sizeof(int));
   for (i=0 ; i<device_drv->channels ; i++)
      last_update[i] = ss_millitime() - 20000;
   last_time = ss_millitime();

   // call CMD_START of device driver
   device_drv->dd(CMD_START, device_drv->dd_info, 0, NULL);

   // initialize setting to NAN in order not to trigger an immediate write
   for (i = 0; i < device_drv->channels; i++)
      device_drv->mt_buffer->channel[i].variable[CMD_SET] = (float) ss_nan();

   int skip = 0;
   do {
      /* read one channel from device, skip if time limit is set */

      /* limit data rate if defined in equipment list */
      if (device_drv->pequipment && device_drv->pequipment->event_limit && current_channel == 0) {
         if (ss_millitime() - last_time < (DWORD)device_drv->pequipment->event_limit)
            skip = 1;
         else {
            skip = 0;
            last_time = ss_millitime();
         }
      }

      if (!skip) {
         for (cmd = CMD_GET_FIRST; cmd <= CMD_GET_LAST; cmd++) {
            value = (float) ss_nan();
            status = device_drv->dd(cmd, device_drv->dd_info, current_channel, &value);

            ss_mutex_wait_for(device_drv->mutex, 1000);
            device_drv->mt_buffer->channel[current_channel].variable[cmd] = value;
            device_drv->mt_buffer->status = status;
            ss_mutex_release(device_drv->mutex);
         }
      }

      /* switch to next channel in next loop */
      current_channel = (current_channel + 1) % device_drv->channels;

      /* check for priority channel */
      current_time = ss_millitime();
      i = (current_priority_channel + 1) % device_drv->channels;
      while (!(current_time - last_update[i] < 10000)) {
         i = (i + 1) % device_drv->channels;
         if (i == current_priority_channel) {
            /* non found, so finish */
            break;
         }
      }

      /* updated channel found, so read it additionally */
      if (current_time - last_update[i] < 10000) {
         current_priority_channel = i;

         for (cmd = CMD_GET_FIRST; cmd <= CMD_GET_LAST; cmd++) {
            status = device_drv->dd(cmd, device_drv->dd_info, i, &value);

            ss_mutex_wait_for(device_drv->mutex, 1000);
            device_drv->mt_buffer->channel[i].variable[cmd] = value;
            device_drv->mt_buffer->status = status;
            ss_mutex_release(device_drv->mutex);
         }
      }

      /* check if anything to write to device */
      for (i = 0; i < device_drv->channels; i++) {

         for (cmd = CMD_SET_FIRST; cmd <= CMD_SET_LAST; cmd++) {
            if (!ss_isnan(device_drv->mt_buffer->channel[i].variable[cmd])) {
               ss_mutex_wait_for(device_drv->mutex, 1000);
               value = device_drv->mt_buffer->channel[i].variable[cmd];
               device_drv->mt_buffer->channel[i].variable[cmd] = (float) ss_nan();
               ss_mutex_release(device_drv->mutex);

               status = device_drv->dd(cmd, device_drv->dd_info, i, value);
               device_drv->mt_buffer->status = status;
               if (cmd == CMD_SET)
                  last_update[i] = ss_millitime();
            }
         }
      }

      ss_sleep(10); // don't eat all CPU

   } while (device_drv->stop_thread == 0);

   free(last_update);

   /* signal stopped thread */
   device_drv->stop_thread = 2;

   return SUCCESS;
}

/*------------------------------------------------------------------*/

INT device_driver(DEVICE_DRIVER * device_drv, INT cmd, ...)
{
   va_list argptr;
   HNDLE hKey;
   INT channel, status, i, j;
   float value, *pvalue;
   char *name, *label;

   va_start(argptr, cmd);
   status = FE_SUCCESS;

   /* don't execute command if driver is disabled */
   if (!device_drv->enabled)
      return FE_PARTIALLY_DISABLED;
   
   switch (cmd) {
   case CMD_INIT:
      hKey = va_arg(argptr, HNDLE);

      if (device_drv->flags & DF_MULTITHREAD) {
         status = device_drv->dd(CMD_INIT, hKey, &device_drv->dd_info,
                                    device_drv->channels, device_drv->flags,
                                    device_drv->bd);

         if (status == FE_SUCCESS && (device_drv->flags & DF_MULTITHREAD)) {
            /* create inter-thread data exchange buffers */
            device_drv->mt_buffer = (DD_MT_BUFFER *) calloc(1, sizeof(DD_MT_BUFFER));
            device_drv->mt_buffer->n_channels = device_drv->channels;
            device_drv->mt_buffer->channel = (DD_MT_CHANNEL *) calloc(device_drv->channels, sizeof(DD_MT_CHANNEL));
            assert(device_drv->mt_buffer->channel);

            /* set all set values to NaN */
            for (i=0 ; i<device_drv->channels ; i++)
               for (j=CMD_SET_FIRST ; j<=CMD_SET_LAST ; j++)
                  device_drv->mt_buffer->channel[i].variable[j] = (float)ss_nan();

            /* get default names for this driver already now */
            for (i = 0; i < device_drv->channels; i++) {
               device_drv->dd(CMD_GET_LABEL, device_drv->dd_info, i,
                              device_drv->mt_buffer->channel[i].label);
            }
            /* create semaphore */
            status = ss_mutex_create(&device_drv->mutex, FALSE);
            if (status != SS_CREATED && status != SS_SUCCESS)
               return FE_ERR_DRIVER;
            status = FE_SUCCESS;
         }
      } else {
         status = device_drv->dd(CMD_INIT, hKey, &device_drv->dd_info,
                                    device_drv->channels, device_drv->flags,
                                    device_drv->bd);
      }
      break;

   case CMD_START:
      if (device_drv->flags & DF_MULTITHREAD && device_drv->mt_buffer != NULL) {
         /* create dedicated thread for this device */
         device_drv->mt_buffer->thread_id = ss_thread_create(sc_thread, device_drv);
      }
      break;

   case CMD_CLOSE:
      /* signal all threads to stop */
      if (device_drv->flags & DF_MULTITHREAD && device_drv->mt_buffer != NULL)
         device_drv->stop_thread = 1;
      break;

   case CMD_STOP:
      if (device_drv->flags & DF_MULTITHREAD && device_drv->mt_buffer != NULL) {
         if (device_drv->stop_thread == 0)
            device_drv->stop_thread = 1;

         /* wait for max. 10 seconds until thread has gracefully stopped */
         for (i = 0; i < 1000; i++) {
            if (device_drv->stop_thread == 2)
               break;
            ss_sleep(10);
         }

         /* if timeout expired, kill thread */
         if (i == 1000)
            ss_thread_kill(device_drv->mt_buffer->thread_id);

         ss_mutex_delete(device_drv->mutex);
         free(device_drv->mt_buffer->channel);
         free(device_drv->mt_buffer);
      }
      break;

   case CMD_EXIT:
      status = device_drv->dd(CMD_EXIT, device_drv->dd_info);
      break;

   case CMD_SET_LABEL:
      channel = va_arg(argptr, INT);
      label = va_arg(argptr, char *);
      status = device_drv->dd(CMD_SET_LABEL, device_drv->dd_info, channel, label);
      break;

   case CMD_GET_LABEL:
      channel = va_arg(argptr, INT);
      name = va_arg(argptr, char *);
      status = device_drv->dd(CMD_GET_LABEL, device_drv->dd_info, channel, name);
      break;

   default:

      if (cmd >= CMD_SET_FIRST && cmd <= CMD_SET_LAST) {

         /* transfer data to sc_thread for SET commands */
         channel = va_arg(argptr, INT);
         value = (float) va_arg(argptr, double);        // floats are passed as double
         if (device_drv->flags & DF_MULTITHREAD) {
            ss_mutex_wait_for(device_drv->mutex, 1000);
            device_drv->mt_buffer->channel[channel].variable[cmd] = value;
            status = device_drv->mt_buffer->status;
            ss_mutex_release(device_drv->mutex);
         } else {
            status = device_drv->dd(cmd, device_drv->dd_info, channel, value);
         }

      } else if (cmd >= CMD_GET_FIRST && cmd <= CMD_GET_LAST) {

         /* transfer data from sc_thread for GET commands */
         channel = va_arg(argptr, INT);
         pvalue = va_arg(argptr, float *);
         if (device_drv->flags & DF_MULTITHREAD) {
            ss_mutex_wait_for(device_drv->mutex, 1000);
            *pvalue = device_drv->mt_buffer->channel[channel].variable[cmd];
            status = device_drv->mt_buffer->status;
            ss_mutex_release(device_drv->mutex);
         } else
            status = device_drv->dd(cmd, device_drv->dd_info, channel, pvalue);

      } else {

         /* all remaining commands which are passed directly to the device driver */
         channel = va_arg(argptr, INT);
         pvalue = va_arg(argptr, float *);
         status = device_drv->dd(cmd, device_drv->dd_info, channel, pvalue);
      }

      break;
   }

   va_end(argptr);
   return status;
}

/* emacs
 * Local Variables:
 * tab-width: 8
 * c-basic-offset: 3
 * indent-tabs-mode: nil
 * End:
 */
