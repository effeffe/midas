/********************************************************************\
Name:         mdump.c
Created by:   Pierre-Andre Amaudruz

Contents:     Dump event on screen with MIDAS or YBOS data format

  $Id$

\********************************************************************/

#include "midas.h"
#include "msystem.h"
#include "mrpc.h"
#include "mdsupport.h"
#include "midasio.h"

#ifndef HAVE_STRLCPY
#include "strlcpy.h"
#endif

#include <vector>

#define  REP_HEADER    1
#define  REP_RECORD    2
#define  REP_LENGTH    3
#define  REP_EVENT     4
#define  REP_BANKLIST  5

char bank_name[4], sbank_name[4];
INT hBufEvent;
INT save_dsp = 1, evt_display = 0;
INT speed = 0, dsp_time = 0, dsp_fmt = 0, dsp_mode = 0, bl = -1;
INT consistency = 0, disp_bank_list = 0, openzip = 0;
BOOL via_callback;
INT i, data_fmt;
double count = 0;
KEY key;
HNDLE hSubkey;
INT event_id, event_msk;

struct FMT_ID {
   WORD id;
   WORD msk;
   WORD fmt;
   char Fmt[16];
   char Eqname[256];

   FMT_ID() // ctor
   {
      id = 0;
      msk = 0;
      fmt = 0;
      Fmt[0] = 0;
      Eqname[0] = 0;
   }
};

std::vector<FMT_ID> eq;

/*-------- Check data format ------------*/
DWORD data_format_check(EVENT_HEADER *pevent, INT *i) {

   INT jj, ii;
   BOOL dupflag = FALSE;

   /* check in the active FE list for duplicate event ID */
   ii = 0;
   while (eq[ii].fmt) {
      jj = ii + 1;
      /* problem occur when duplicate ID with different data format */
      while (eq[jj].fmt) {
         if ((eq[jj].fmt != eq[ii].fmt)
             && (eq[jj].id == eq[ii].id)
             && (eq[jj].msk == eq[ii].msk)
             && eq[ii].id != 0) {
            printf("Duplicate eventID[%d] between Eq:%s & %s  ", eq[jj].id, eq[jj].Eqname,
                   eq[ii].Eqname);
            printf("Dumping event in raw format\n");
            dupflag = TRUE;
         }
         jj++;
      }
      ii++;
   }
   if (data_fmt != 0) {
      *i = 0;
      strcpy(eq[*i].Fmt, "GIVEN");
      return data_fmt;
   } else {
      *i = 0;
      if (dupflag)
         strcpy(eq[*i].Fmt, "DUPLICATE");
      else {
         do {
            if (pevent->event_id == eq[*i].id)
               return eq[*i].fmt;
            (*i)++;
         } while (eq[*i].fmt);
      }
   }
   return 0;
}

void md_all_info_display(int seqno, int runno, TMEvent* e)
{
   printf("Evt#%d- ", seqno);
   printf("%irun 0x%4.4uxid 0x%4.4uxmsk %5dmevt#", runno, e->event_id, e->trigger_mask, seqno);
   printf("%5del/x%x %5dserial\n", int(e->data.size()), int(e->data.size()), e->serial_number);
}

/*----- Replog function ----------------------------------------*/
int replog(int data_fmt, char *rep_file, int bl, int action, int max_event_size) {
   static char bars[] = "|/-\\";
   static int i_bar;

   TMReaderInterface* r = TMNewReader(rep_file);

   /* open data file */
   if (r->fError) {
      fprintf(stderr, "Cannot open %s: %s\n", rep_file, r->fErrorString.c_str());
      r->Close();
      delete r;
      return (-1);
   }

   int seqno = 0;
   int runno = 0;

   //printf("skip %d\n", bl);

   while (bl > 0) {
      TMEvent* e = TMReadEvent(r);
      if (!e) {
         printf("\n");
         return -1;
      }
      seqno++;
      if (e->event_id == uint16_t(EVENTID_BOR))
         runno = e->serial_number;
      if (seqno < bl) {
         printf("Skipping event_# ... ");
         printf("%d \r", seqno);
         fflush(stdout);
      } else {
         printf("\n");
         delete e;
         break;
      }
      delete e;
   }

   switch (action) {
      case REP_HEADER:
      case REP_RECORD:
      // MIDAS format does not implement REP_HEADER and REP_RECORD
      //   ///* get only physical record header */
      //   //if (md_physrec_skip(data_fmt, bl) != MD_SUCCESS)
      //   //   return (-1);
      //   do {
      //      if (action == REP_HEADER) {
      //         status = md_all_info_display(D_HEADER);
      //      } else if (action == REP_RECORD) {
      //         status = md_physrec_display(data_fmt);
      //      } if ((status == MD_DONE) || (bl != -1)) {
      //         break;
      //      }
      //   } while (md_physrec_get(data_fmt, &physrec, &physize) == MD_SUCCESS);
         break;

      case REP_LENGTH:
      case REP_EVENT:
      case REP_BANKLIST:
         ///* skip will read atleast on record */
         //if (md_physrec_skip(data_fmt, bl) != MD_SUCCESS)
         //   return (-1);
         i = 0;

         TMEvent prev_e; 

         TMEvent* e = NULL;
         while (1) {
            if (e)
               delete e;
            TMEvent* e = TMReadEvent(r);
            if (!e)
               break;
            if (e->error)
               continue;
            seqno++;

            if (e->event_id == uint16_t(EVENTID_BOR))
               runno = e->serial_number;

            if ((consistency == 1) && (data_fmt == FORMAT_MIDAS)) {
               // if ID is given skip the inconsistency event of different ID
               if ((event_id != EVENTID_ALL) && (e->event_id != event_id)) {
                  continue;  // Next event
               } else if (e->serial_number != prev_e.serial_number + 1) {
                  /* event header : show last event consistent and new one (ID may be different!) */
                  printf("\nLast - Evid:%4.4x- Mask:%4.4x- Serial:%i- Time:0x%x- Dsize:%i/0x%x\n",
                         prev_e.event_id, prev_e.trigger_mask, prev_e.serial_number, prev_e.time_stamp,
                         prev_e.data_size, prev_e.data_size);
                  printf("Now  - Evid:%4.4x- Mask:%4.4x- Serial:%i- Time:0x%x- Dsize:%i/0x%x\n",
                         e->event_id, e->trigger_mask, e->serial_number,
                         e->time_stamp, e->data_size, e->data_size);
               } else {
                  // last and current SN are seprate by one
                  printf("Consistency check: %c - %i (Data size:%i)\r", bars[i_bar++ % 4],
                         prev_e.serial_number, prev_e.data_size);
                  fflush(stdout);
               }
               // save current header for later comparison
               prev_e = *e;
               continue;  // Next event
            } // consistency==1
            if (action == REP_LENGTH) {
               md_all_info_display(seqno, runno, e);
            }
            if ((action == REP_BANKLIST) || (disp_bank_list == 1)) {
               if (e->event_id == uint16_t(EVENTID_BOR) ||
                   e->event_id == uint16_t(EVENTID_EOR) || e->event_id == uint16_t(EVENTID_MESSAGE))
                  continue;
               printf("Evid:%4.4x- Mask:%4.4x- Serial:%d- Time:0x%x- Dsize:%d/0x%x\n",
                      e->event_id, e->trigger_mask, e->serial_number, e->time_stamp,
                      e->data_size, e->data_size);
               e->FindAllBanks();
               std::string banklist;
               for (unsigned b=0; b<e->banks.size(); b++) {
                  banklist += msprintf("%4s", e->banks[b].name.c_str());
               }
               printf("#banks:%i Bank list:-%s-\n", int(e->banks.size()), banklist.c_str());
            } else if ((action == REP_EVENT) && (event_id == EVENTID_ALL) && (event_msk == TRIGGER_ALL) &&
                       (sbank_name[0] == 0)) { /* a quick by-pass to prevent bank search if not necessary */
               printf("------------------------ Event# %i --------------------------------\n", i++);
               md_event_display(e->data.data(), data_fmt, dsp_mode, dsp_fmt, sbank_name);
            } else if (action == REP_EVENT) {
               if (e->event_id == uint16_t(EVENTID_BOR) ||
                   e->event_id == uint16_t(EVENTID_EOR) || e->event_id == uint16_t(EVENTID_MESSAGE)) {
                  printf("Searching for Bank -%s- Skiping event...%i\r", sbank_name, i++);
                  fflush(stdout);
                  continue;
               }
               uint16_t id = e->event_id;
               uint16_t msk = e->trigger_mask;
               /* Search bank if necessary */
               TMBank* b = NULL;
               if (sbank_name[0] != 0) {        /* bank name given through argument list */
                  b = e->FindBank(sbank_name);
               }
               /* check user request through switch setting (id, msk ,bank) */
               if ((event_msk != TRIGGER_ALL) || (event_id != EVENTID_ALL) ||
                   (sbank_name[0] != 0)) {      /* check request or skip event if not satisfied */
                  if (((event_id != EVENTID_ALL) && (id != event_id)) ||   /* id check ==> skip */
                      ((event_msk != TRIGGER_ALL) && (msk != event_msk)) ||        /* msk check ==> skip */
                      ((sbank_name[0] != 0) && !b)) {     /* bk check ==> skip *//* skip event */
                     printf("Searching for Bank -%s- Skiping event...%i\r", sbank_name, i++);
                     fflush(stdout);
                  } else {         /* request match ==> display any event */
                     printf("------------------------ Event# %i --------------------------------\n", i++);
                     md_event_display(e->data.data(), data_fmt, dsp_mode, dsp_fmt, sbank_name);
                  }
               } else {            /* no user request ==> display any event */
                  printf("------------------------ Event# %i --------------------------------\n", i++);
                  md_event_display(e->data.data(), data_fmt, dsp_mode, dsp_fmt, sbank_name);
               }
            }
         }
         if (e)
            delete e;
         break;
   }                            /* switch */

   /* close data file */
   printf("\n");

   r->Close();
   delete r;

   return 0;
}

/*----- receive_event ----------------------------------------------*/
void process_event(HNDLE hBuf, HNDLE request_id, EVENT_HEADER *pheader, void *pevent) {
   static char bars[] = "|/-\\";
   static int i_bar;
   static EVENT_HEADER pevh;
   INT internal_data_fmt, status, index, size;
   char banklist[STRING_BANKLIST_MAX];
   BANK_HEADER *pmbh;
   DWORD bklen, bktyp;

   if (speed == 1) {
      /* accumulate received event size */
      size = pheader->data_size + sizeof(EVENT_HEADER);
      count += size;
      return;
   }
   if (consistency == 1) {
      if (pheader->serial_number != pevh.serial_number + 1) {
         /* event header */
         printf("\nLast - Evid:%4.4x- Mask:%4.4x- Serial:%i- Time:0x%x- Dsize:%i/0x%x\n",
                pevh.event_id, pevh.trigger_mask, pevh.serial_number, pevh.time_stamp,
                pevh.data_size, pevh.data_size);
         printf("Now  - Evid:%4.4x- Mask:%4.4x- Serial:%i- Time:0x%x- Dsize:%i/0x%x\n",
                pheader->event_id, pheader->trigger_mask, pheader->serial_number,
                pheader->time_stamp, pheader->data_size, pheader->data_size);
      } else {
         printf("Consistency check: %c - %i (Data size:%i)\r", bars[i_bar++ % 4],
                pheader->serial_number, pheader->data_size);
         fflush(stdout);
      }
      memcpy((char *) &pevh, (char *) pheader, sizeof(EVENT_HEADER));
      return;
   }
   if (evt_display > 0) {
      evt_display--;

      internal_data_fmt = data_format_check(pheader, &index);

      if (internal_data_fmt == FORMAT_YBOS)
         assert(!"YBOS not supported anymore");

      /* pointer to data section */
      pmbh = (BANK_HEADER *) pevent;

      /* display header comes ALWAYS from MIDAS header */
      printf("------------------------ Event# %i ------------------------\n",
             save_dsp - evt_display);
      /* selection based on data format */
      if (internal_data_fmt == FORMAT_YBOS) {
         assert(!"YBOS not supported anymore");
      } else if ((internal_data_fmt == FORMAT_MIDAS) &&
                 (md_event_swap(FORMAT_MIDAS, pheader) >= MD_SUCCESS)) {     /* ---- MIDAS FMT ---- */
         if (sbank_name[0] != 0) {
            BANK *pmbk;
            if (bk_find(pmbh, sbank_name, &bklen, &bktyp, (void **) &pmbk) ==
                SS_SUCCESS) {      /* bank name given through argument list */
               status = bk_list(pmbh, banklist);
               printf("#banks:%i Bank list:-%s-", status, banklist);
               if (bk_is32a(pmbh))
                  pmbk = (BANK *) (((char *) pmbk) - sizeof(BANK32A));
               else if (bk_is32(pmbh))
                  pmbk = (BANK *) (((char *) pmbk) - sizeof(BANK32));
               else
                  pmbk = (BANK *) (((char *) pmbk) - sizeof(BANK));
               md_bank_display(pmbh, pmbk, FORMAT_MIDAS, dsp_mode, dsp_fmt);
            } else {
               status = bk_list(pmbh, banklist);
               printf("Bank -%s- not found (%i) in ", sbank_name, status);
               printf("#banks:%i Bank list:-%s-\n", status, banklist);
            }
         } else {               /* Full event or bank list only */
            if (disp_bank_list) {
               /* event header */
               printf("Evid:%4.4x- Mask:%4.4x- Serial:%d- Time:0x%x- Dsize:%d/0x%x\n",
                      pheader->event_id, pheader->trigger_mask, pheader->serial_number,
                      pheader->time_stamp, pheader->data_size, pheader->data_size);
               status = bk_list(pmbh, banklist);
               printf("#banks:%i Bank list:-%s-\n", status, banklist);
            } else
               md_event_display(pheader, FORMAT_MIDAS, dsp_mode, dsp_fmt, sbank_name);
         }
      } else {                  /* unknown format just dump midas event */
         printf("Data format not supported: %s\n", eq[index].Fmt);
         md_event_display(pheader, FORMAT_MIDAS, DSP_RAW, dsp_fmt, sbank_name);
      }
      if (evt_display == 0) {
         /* do not produce shutdown message */
         cm_set_msg_print(MT_ERROR, 0, NULL);
         cm_disconnect_experiment();
         exit(0);
      }
      if (dsp_time != 0)
         ss_sleep(dsp_time);
   }
   return;
}

/*------------------------------------------------------------------*/
int main(int argc, char **argv) {
   HNDLE hDB, hKey;
   char host_name[HOST_NAME_LENGTH], expt_name[NAME_LENGTH], str[80];
   char buf_name[32] = EVENT_BUFFER_NAME, rep_file[128];
   unsigned int status, start_time, stop_time;
   BOOL debug = FALSE, rep_flag;
   INT ch, request_id, size, get_flag, action, single, i, max_event_size;
   BUFFER_HEADER buffer_header;

   /* set default */
   host_name[0] = 0;
   expt_name[0] = 0;
   sbank_name[0] = 0;
   rep_file[0] = 0;
   event_id = EVENTID_ALL;
   event_msk = TRIGGER_ALL;
   evt_display = 1;
   get_flag = GET_NONBLOCKING;
   dsp_fmt = DSP_UNK;
   dsp_mode = DSP_BANK;
   via_callback = TRUE;
   rep_flag = FALSE;
   dsp_time = 0;
   speed = 0;
   single = 0;
   consistency = 0;
   action = REP_EVENT;
   max_event_size = DEFAULT_MAX_EVENT_SIZE;

   /* Get if existing the pre-defined experiment */
   cm_get_environment(host_name, sizeof(host_name), expt_name, sizeof(expt_name));

   /* scan arg list for -x which specify the replog configuration */
   for (i = 1; i < argc; i++) {
      if (strncmp(argv[i], "-x", 2) == 0) {
         if (i + 1 == argc)
            goto repusage;
         if (strncmp(argv[++i], "online", 6) != 0) {
            rep_flag = TRUE;
            break;
         }
      }
   }
   if (rep_flag) {
      /* get Replay argument list */
      data_fmt = 0;
      for (i = 1; i < argc; i++) {
         if (argv[i][0] == '-' && argv[i][1] == 'd')
            debug = TRUE;
         else if (strncmp(argv[i], "-single", 7) == 0)
            single = 1;
         else if (strncmp(argv[i], "-j", 2) == 0)
            disp_bank_list = 1;
         else if (strncmp(argv[i], "-y", 2) == 0)
            consistency = 1;
         else if (argv[i][0] == '-') {
            if (i + 1 >= argc || argv[i + 1][0] == '-')
               goto repusage;
            if (strncmp(argv[i], "-t", 2) == 0) {
               strlcpy(str, argv[++i], sizeof(str));
               if (strncmp(str, "m", 1) == 0)
                  data_fmt = FORMAT_MIDAS;
               if (strncmp(str, "y", 1) == 0)
                  data_fmt = FORMAT_YBOS;
            } else if (strncmp(argv[i], "-b", 2) == 0)
               memcpy(sbank_name, argv[++i], 4);
            else if (strncmp(argv[i], "-i", 2) == 0)
               event_id = atoi(argv[++i]);
            else if (strncmp(argv[i], "-k", 2) == 0)
               event_msk = atoi(argv[++i]);
            else if (strncmp(argv[i], "-a", 2) == 0)
               max_event_size = atoi(argv[++i]);
            else if (strncmp(argv[i], "-m", 2) == 0) {
               strlcpy(str, argv[++i], sizeof(str));
               if (strncmp(str, "r", 1) == 0)
                  dsp_mode = DSP_RAW;
               if (strncmp(str, "b", 1) == 0)
                  dsp_mode = DSP_BANK;
            } else if (strncmp(argv[i], "-w", 2) == 0) {
               strlcpy(str, argv[++i], sizeof(str));
               if (strncmp(str, "h", 1) == 0)
                  action = REP_HEADER;
               else if (strncmp(str, "r", 1) == 0)
                  action = REP_RECORD;
               else if (strncmp(str, "l", 1) == 0)
                  action = REP_LENGTH;
               else if (strncmp(str, "e", 1) == 0)
                  action = REP_EVENT;
               else if (strncmp(str, "j", 1) == 0)
                  action = REP_BANKLIST;
            } else if (strncmp(argv[i], "-f", 2) == 0) {
               strlcpy(str, argv[++i], sizeof(str));
               if (strncmp(str, "d", 1) == 0)
                  dsp_fmt = DSP_DEC;
               if (strncmp(str, "x", 1) == 0)
                  dsp_fmt = DSP_HEX;
               if (strncmp(str, "a", 1) == 0)
                  dsp_fmt = DSP_ASC;
            } else if (strncmp(argv[i], "-r", 2) == 0)
               bl = atoi(argv[++i]);
            else if (strncmp(argv[i], "-x", 2) == 0) {
               if (i + 1 == argc)
                  goto repusage;
               strcpy(rep_file, argv[++i]);
            } else {
               repusage:
               printf("mdump for replay  -x file name    : file to inspect\n");
               printf("                  -m mode         : Display mode either Bank or raw\n");
               printf("                  -b bank name    : search for bank name (case sensitive)\n");
               printf("                  -i evt_id (any) : event id from the FE\n");
               printf("                  -[single]       : Request single bank only (to be used with -b)\n");
               printf("                  -y              : Serial number consistency check(-i supported)\n");
               printf("                  -j              : Display # of banks and bank name list only for all the event\n");
               printf("                  -k mask (any)   : trigger_mask from FE setting\n");
               printf(">>> -i and -k are valid for YBOS ONLY if EVID bank is present in the event\n");
               printf("                  -w what         : [h]eader, [r]ecord, [l]ength\n");
               printf("                                    [e]vent, [j]bank_list (same as -j)\n");
               printf(">>> Header & Record are not supported for MIDAS as no physical record structure exists\n");
               printf("                  -f format (auto): data representation ([x]/[d]/[a]scii) def:bank header content\n");
               printf("                  -r #            : skip event(MIDAS) to #\n");
               printf("                  -a bytes        : max event size to support (defaults to %d bytes)\n",
                      DEFAULT_MAX_EVENT_SIZE);
               return 0;
            }
         }
      }
   } else {  // Online
      /* get parameters for online */
      for (i = 1; i < argc; i++) {
         if (argv[i][0] == '-' && argv[i][1] == 'd')
            debug = TRUE;
         else if (strncmp(argv[i], "-s", 2) == 0)
            speed = 1;
         else if (strncmp(argv[i], "-y", 2) == 0)
            consistency = 1;
         else if (strncmp(argv[i], "-j", 2) == 0)
            disp_bank_list = 1;
         else if (argv[i][0] == '-') {
            if (i + 1 >= argc || argv[i + 1][0] == '-')
               goto usage;
            else if (strncmp(argv[i], "-x", 2) == 0)
               strncpy(rep_file, argv[++i], 4);
            else if (strncmp(argv[i], "-b", 2) == 0)
               memcpy(sbank_name, argv[++i], 4);
            else if (strncmp(argv[i], "-l", 2) == 0)
               save_dsp = evt_display = atoi(argv[++i]);
            else if (strncmp(argv[i], "-w", 2) == 0)
               dsp_time = 1000 * (atoi(argv[++i]));
            else if (strncmp(argv[i], "-m", 2) == 0) {
               strlcpy(str, argv[++i], sizeof(str));
               if (strncmp(str, "r", 1) == 0)
                  dsp_mode = DSP_RAW;
               if (strncmp(str, "y", 1) == 0)
                  dsp_mode = DSP_BANK;
            } else if (strncmp(argv[i], "-g", 2) == 0) {
               strlcpy(str, argv[++i], sizeof(str));
               if (strncmp(str, "s", 1) == 0)
                  get_flag = GET_NONBLOCKING;
               if (strncmp(str, "a", 1) == 0)
                  get_flag = GET_ALL;
            } else if (strncmp(argv[i], "-f", 2) == 0) {
               strlcpy(str, argv[++i], sizeof(str));
               if (strncmp(str, "d", 1) == 0)
                  dsp_fmt = DSP_DEC;
               if (strncmp(str, "x", 1) == 0)
                  dsp_fmt = DSP_HEX;
               if (strncmp(str, "a", 1) == 0)
                  dsp_fmt = DSP_ASC;
            } else if (strncmp(argv[i], "-i", 2) == 0)
               event_id = atoi(argv[++i]);
            else if (strncmp(argv[i], "-k", 2) == 0)
               event_msk = atoi(argv[++i]);
            else if (strncmp(argv[i], "-z", 2) == 0)
               strcpy(buf_name, argv[++i]);
            else if (strncmp(argv[i], "-t", 2) == 0) {
               strlcpy(str, argv[++i], sizeof(str));
               if (strncmp(str, "m", 1) == 0)
                  data_fmt = FORMAT_MIDAS;
               if (strncmp(str, "y", 1) == 0)
                  data_fmt = FORMAT_YBOS;
            } else if (strncmp(argv[i], "-h", 2) == 0)
               strcpy(host_name, argv[++i]);
            else if (strncmp(argv[i], "-e", 2) == 0)
               strcpy(expt_name, argv[++i]);
            else {
               usage:
               printf("mdump for online  -l #            : display # events (look 1)\n");
               printf
                       ("                  -f format (auto): data representation ([x]/[d]/[a]scii) def:bank header content\n");
               printf
                       ("                  -w time         : insert wait in [sec] between each display\n");
               printf
                       ("                  -m mode         : Display mode either Bank or raw\n");
               printf
                       ("                  -j              : Display # of banks and bank name list only for all the event\n");
               printf
                       ("                  -b bank name    : search for bank name (case sensitive)\n");
               printf
                       ("                  -i evt_id (any) : event id from the FE\n");
               printf
                       ("                  -k mask (any)   : trigger_mask from FE setting\n");
               printf
                       ("                  -g type         : sampling mode either SOME or all)\n");
               printf
                       ("                  -s              : report buffer data rate and fill level\n");
               printf
                       ("                  -s -d           : for use with -s: also report all buffer clients and requests\n");
               printf
                       ("                  -t type (auto)  : Bank format (Midas/Ybos)\n");
               printf
                       ("                  -x Source       : Data source selection def:online (see -x -h)\n");
               printf
                       ("                  -y              : Serial number consistency check\n");
               printf
                       (">>> in case of -y it is recommented to used -g all\n");
               printf
                       ("                  -z buffer name  : Midas buffer name default:[SYSTEM]\n");
               printf
                       ("                  [-h Hostname] [-e Experiment]\n\n");
               return 0;
            }
         }
      }
   }

   // Set flag to inform that we're coming from mdump -> open zip  (.mid.gz)
   if ((strstr(argv[0], "mdump") == NULL)) openzip = 0; else openzip = 1;

   if ((sbank_name[0] != 0) && single) dsp_mode += 1;

   if (rep_flag && data_fmt == 0) {
      char *pext;
      if ((pext = strrchr(rep_file, '.')) != 0) {
         if (equal_ustring(pext + 1, "mid")) {
            data_fmt = FORMAT_MIDAS;
         } else if (equal_ustring(pext + 1, "ybs")) {
            data_fmt = FORMAT_YBOS;
         } else if (equal_ustring(pext + 1, "gz")) {
            if ((pext = strchr(rep_file, '.')) != 0) {
               if (strstr(pext + 1, "mid"))
                  data_fmt = FORMAT_MIDAS;
               else if (strstr(pext + 1, "ybs"))
                  data_fmt = FORMAT_YBOS;
            } else {
               printf("\n>>> data type (-t) should be set by hand in -x mode for tape <<< \n\n");
               goto usage;
            }
         } else if (equal_ustring(pext + 1, "lz4")) {
            data_fmt = FORMAT_MIDAS;
         } else if (equal_ustring(pext + 1, "bz2")) {
            data_fmt = FORMAT_MIDAS;
         } else {
            printf
                    ("\n>>> data type (-t) should be set by hand in -x mode for tape <<< \n\n");
            goto usage;
         }
      }
   }

   /* steer to replog function */
   if (rep_flag) {
      replog(data_fmt, rep_file, bl, action, max_event_size);
      return 0;
   } else {
      /* check parameters */
      if (evt_display < 1 || evt_display > 9999) {
         printf("mdump-F- <-display arg> out of range (1:9999)\n");
         return -1;
      }
   }
   if (dsp_time < 0 || dsp_time > 100) {
      printf("mdump-F- <-delay arg> out of range (1:100)\n");
      return -1;
   }

   /* do not produce startup message */
   cm_set_msg_print(MT_ERROR, 0, NULL);

   /* connect to experiment */
   status = cm_connect_experiment(host_name, expt_name, "mdump", 0);
   if (status != CM_SUCCESS)
      return 1;

#ifdef _DEBUG
   cm_set_watchdog_params(TRUE, 0);
#endif

   /* open the shared memory buffer with proper size */
   status = bm_open_buffer(buf_name, DEFAULT_BUFFER_SIZE, &hBufEvent);
   if (status != BM_SUCCESS && status != BM_CREATED) {
      cm_msg(MERROR, "mdump", "Cannot open buffer \"%s\", bm_open_buffer() status %d", buf_name, status);
      goto error;
   }
   /* set the buffer cache size if requested */
   bm_set_cache_size(hBufEvent, 100000, 0);

   /* place a request for a specific event id */
   bm_request_event(hBufEvent, (WORD) event_id, (WORD) event_msk, get_flag, &request_id, process_event);

   start_time = 0;
   if (speed == 1)
      printf("- MIDAS revision: %s -- Enter <!> to Exit ------- Midas Dump in Speed test mode ---\n",
             cm_get_revision());
   else
      printf("- MIDAS revision: %s -- Enter <!> to Exit ------- Midas Dump ---\n", cm_get_revision());

   /* connect to the database */
   cm_get_experiment_database(&hDB, &hKey);

   {   /* ID block */
      INT l = 0;
      /* check if dir exists */
      if (db_find_key(hDB, 0, "/equipment", &hKey) == DB_SUCCESS) {
         char strtmp[256], equclient[32];
         for (i = 0;; i++) {
            db_enum_key(hDB, hKey, i, &hSubkey);
            if (!hSubkey)
               break;
            db_get_key(hDB, hSubkey, &key);
            FMT_ID f;
            eq.push_back(f);
            strlcpy(eq[l].Eqname, key.name, sizeof(eq[l].Eqname));
            /* check if client running this equipment is present */
            /* extract client name from equipment */
            size = sizeof(strtmp);
            sprintf(strtmp, "/equipment/%s/common/Frontend name", key.name);
            db_get_value(hDB, 0, strtmp, equclient, &size, TID_STRING, TRUE);

            /* search client name under /system/clients/xxx/name */
            /* Outcommented 22 Dec 1997 SR because of problem when
               mdump is started before frontend
               if (status = cm_exist(equclient,FALSE) != CM_SUCCESS)
               continue;
            */
            size = sizeof(WORD);
            sprintf(strtmp, "/equipment/%s/common/event ID", key.name);
            db_get_value(hDB, 0, strtmp, &(eq[l]).id, &size, TID_WORD, TRUE);

            size = sizeof(WORD);
            sprintf(strtmp, "/equipment/%s/common/Trigger mask", key.name);
            db_get_value(hDB, 0, strtmp, &(eq[l]).msk, &size, TID_WORD, TRUE);

            size = sizeof(str);
            sprintf(strtmp, "/equipment/%s/common/Format", key.name);
            db_get_value(hDB, 0, strtmp, str, &size, TID_STRING, TRUE);
            if (equal_ustring(str, "YBOS")) {
               eq[l].fmt = FORMAT_YBOS;
               strcpy(eq[l].Fmt, "YBOS");
            } else if (equal_ustring(str, "MIDAS")) {
               eq[l].fmt = FORMAT_MIDAS;
               strcpy(eq[l].Fmt, "MIDAS");
            } else if (equal_ustring(str, "DUMP")) {
               eq[l].fmt = FORMAT_MIDAS;
               strcpy(eq[l].Fmt, "DUMP");
            } else if (equal_ustring(str, "ASCII")) {
               eq[l].fmt = FORMAT_MIDAS;
               strcpy(eq[l].Fmt, "ASCII");
            } else if (equal_ustring(str, "HBOOK")) {
               eq[l].fmt = FORMAT_MIDAS;
               strcpy(eq[l].Fmt, "HBOOK");
            } else if (equal_ustring(str, "FIXED")) {
               eq[l].fmt = FORMAT_MIDAS;
               strcpy(eq[l].Fmt, "FIXED");
            }
            l++;
         }
      }

      /* for equipment */
      /* check for EBuilder */
      if (db_find_key(hDB, 0, "/EBuilder/Settings", &hKey) == DB_SUCCESS) {
         sprintf(eq[l].Eqname, "EBuilder");
         /* check if client running this equipment is present */
         /* search client name under /system/clients/xxx/name */
         /* Outcommented 22 Dec 1997 SR because of problem when
       mdump is started before frontend
       if (status = cm_exist(equclient,FALSE) != CM_SUCCESS)
       continue;
         */
         size = sizeof(WORD);
         db_get_value(hDB, hKey, "Event ID", &(eq[l]).id, &size, TID_WORD, TRUE);

         size = sizeof(WORD);
         db_get_value(hDB, hKey, "Trigger mask", &(eq[l]).msk, &size, TID_WORD, TRUE);

         size = sizeof(str);
         db_get_value(hDB, hKey, "Format", str, &size, TID_STRING, TRUE);
         if (equal_ustring(str, "YBOS")) {
            eq[l].fmt = FORMAT_YBOS;
            strcpy(eq[l].Fmt, "YBOS");
         } else if (equal_ustring(str, "MIDAS")) {
            eq[l].fmt = FORMAT_MIDAS;
            strcpy(eq[l].Fmt, "MIDAS");
         } else {
            printf("Format unknown for Event Builder (%s)\n", str);
            goto error;
         }
         l++;
      }

      /* Debug */
      if (debug) {
         i = 0;
         printf("ID\tMask\tFormat\tEq_name\n");
         while (eq.size() > 0 && eq[i].fmt) {
            printf("%d\t%d\t%s\t%s\n", eq[i].id, eq[i].msk, eq[i].Fmt, eq[i].Eqname);
            i++;
         }
      }
   }  /* ID block */

   do {
      if (via_callback)
         status = cm_yield(1000);
      if (speed == 1) {
         /* calculate rates each second */
         if (ss_millitime() - start_time > 1000) {
            stop_time = ss_millitime();
            double rate = count / 1024.0 / 1024.0 / ((stop_time - start_time) / 1000.0);

            /* get information about filling level of the buffer */
            bm_get_buffer_info(hBufEvent, &buffer_header);
            int filled = buffer_header.read_pointer - buffer_header.write_pointer;
            if (filled <= 0)
               filled += buffer_header.size;

            if (debug) {
               int now = ss_millitime();

               printf("buffer name [%s], clients: %d, max: %d, size: %d, rp: %d, wp: %d, ine: %d, oute: %d\n",
                      buffer_header.name,
                      buffer_header.num_clients,
                      buffer_header.max_client_index,
                      buffer_header.size,
                      buffer_header.read_pointer,
                      buffer_header.write_pointer,
                      buffer_header.num_in_events,
                      buffer_header.num_out_events
               );

               int max_used = 0;
               int max_used_client = 0;

               for (int i = 0; i < buffer_header.max_client_index; i++) {
                  if (buffer_header.client[i].pid) {
                     int used = buffer_header.write_pointer - buffer_header.client[i].read_pointer;
                     if (used < 0)
                        used += buffer_header.size;

                     if (buffer_header.client[i].all_flag) {
                        if (used > max_used) {
                           max_used = used;
                           max_used_client = i;
                        }
                     }

                     printf("  client %d: name [%s], pid: %d, port: %d, rp: %d, used: %d, max_req: %d, read_wait: %d, write_wait: %d, wake_up: %d, get_all: %d, active: %d, timeout: %d\n",
                            i,
                            buffer_header.client[i].name,
                            buffer_header.client[i].pid,
                            buffer_header.client[i].port,
                            buffer_header.client[i].read_pointer,
                            used,
                            buffer_header.client[i].max_request_index,
                            buffer_header.client[i].read_wait,
                            buffer_header.client[i].write_wait,
                            buffer_header.client[i].wake_up,
                            buffer_header.client[i].all_flag,
                            now - buffer_header.client[i].last_activity,
                            buffer_header.client[i].watchdog_timeout);

                     for (int j = 0; j < buffer_header.client[i].max_request_index; j++)
                        if (buffer_header.client[i].event_request[j].valid)
                           printf("    request %d: id: %d, valid: %d, event_id: %d, trigger_mask: 0x%x, type: %d\n",
                                  j,
                                  buffer_header.client[i].event_request[j].id,
                                  buffer_header.client[i].event_request[j].valid,
                                  buffer_header.client[i].event_request[j].event_id,
                                  buffer_header.client[i].event_request[j].trigger_mask,
                                  buffer_header.client[i].event_request[j].sampling_type);
                  }
               }

               printf("buffer name [%s], ", buffer_header.name);
               printf("filled: %4.1f%%, ", 100 - 100.0 * filled / buffer_header.size);
               printf("used: %4.1f%% by [%s], ", 100.0 * max_used / buffer_header.size,
                      buffer_header.client[max_used_client].name);
               printf("rate: %1.3f MiB/sec\n", rate);
            }

            start_time = stop_time;
            count = 0;
         }
      } // Speed

      /*  speed */
      /* check keyboard */
      ch = 0;
      if (ss_kbhit()) {
         ch = ss_getchar(0);
         if (ch == -1)
            ch = getchar();
         if ((char) ch == '!')
            break;
      }
   } while (status != RPC_SHUTDOWN && status != SS_ABORT);

   error:
   /* do not produce shutdown message */
   cm_set_msg_print(MT_ERROR, 0, NULL);

   cm_disconnect_experiment();

   return 1;
}

