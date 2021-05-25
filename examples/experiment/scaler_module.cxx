/********************************************************************\

  Name:         scaler.c
  Created by:   Stefan Ritt

  Contents:     Example scaler analyzer module. This module looks
                for a SCLR banks and accumulates scalers into an
                ACUM bank.

  $Id:$

\********************************************************************/

#if 0
/*-- Include files -------------------------------------------------*/

/* standard includes */
#include <stdio.h>
#include <string.h>
#include <time.h>

/* midas includes */
#include "midas.h"
#include "experim.h"
#include "analyzer.h"

/*-- Module declaration --------------------------------------------*/

INT scaler_accum(EVENT_HEADER *, void *);
INT scaler_clear(INT run_number);
INT scaler_eor(INT run_number);

ANA_MODULE scaler_accum_module = {
   "Scaler accumulation",       /* module name           */
   "Stefan Ritt",               /* author                */
   scaler_accum,                /* event routine         */
   scaler_clear,                /* BOR routine           */
   scaler_eor,                  /* EOR routine           */
   NULL,                        /* init routine          */
   NULL,                        /* exit routine          */
   NULL,                        /* parameter structure   */
   0,                           /* structure size        */
   NULL,                        /* initial parameters    */
};

/*-- accumulated scalers -------------------------------------------*/

double scaler[32];

/*-- BOR routine ---------------------------------------------------*/

INT scaler_clear(INT run_number)
{
   memset(scaler, 0, sizeof(scaler));
   return SUCCESS;
}

/*-- EOR routine ---------------------------------------------------*/

INT scaler_eor(INT run_number)
{
   return SUCCESS;
}

/*-- event routine -------------------------------------------------*/

INT scaler_accum(EVENT_HEADER * pheader, void *pevent)
{
   INT n, i;
   DWORD *psclr;
   double *pacum;

   /* look for SCLR bank */
   n = bk_locate(pevent, "SCLR", &psclr);
   if (n == 0)
      return 1;

   /* create acummulated scaler bank */
   bk_create(pevent, "ACUM", TID_DOUBLE, (void**)&pacum);

   /* accumulate scalers */
   for (i = 0; i < n; i++) {
      scaler[i] += psclr[i];
      pacum[i] = scaler[i];
   }

   /* close bank */
   bk_close(pevent, pacum + n);

   return SUCCESS;
}
#endif

//
// MIDAS analyzer example 2: ROOT analyzer
//
// K.Olchanski
//

#include <stdio.h>

#include "manalyzer.h"
#include "midasio.h"

struct Scaler: public TARunObject
{
   double fScaler[32];
   
   Scaler(TARunInfo* runinfo)
      : TARunObject(runinfo)
   {
      printf("Scaler::ctor!\n");

      memset(fScaler, 0, sizeof(fScaler));
   }

   ~Scaler()
   {
      printf("Scaler::dtor!\n");
   }

   void BeginRun(TARunInfo* runinfo)
   {
      printf("BeginRun, run %d, file %s\n", runinfo->fRunNo, runinfo->fFileName.c_str());

      memset(fScaler, 0, sizeof(fScaler));
   }

   void EndRun(TARunInfo* runinfo)
   {
      printf("EndRun, run %d\n", runinfo->fRunNo);
   }

   TAFlowEvent* Analyze(TARunInfo* runinfo, TMEvent* event, TAFlags* flags, TAFlowEvent* flow)
   {
      printf("Analyze, run %d, event serno %d, id 0x%04x, data size %d\n", runinfo->fRunNo, event->serial_number, (int)event->event_id, event->data_size);

      TMBank* bsclr = event->FindBank("SCLR");
      if (!bsclr)
         return flow;

      uint32_t* psclr = (uint32_t*)event->GetBankData(bsclr);
      if (!psclr)
         return flow;

      int n = bsclr->data_size / sizeof(uint32_t);
      if (n == 0)
         return flow;

      assert(n == 32); // original scaler.cxx has a bug, there is no check for correct size of SCLR bank. K.O.

      double acum[32];
      
      /* accumulate scalers */
      for (int i = 0; i < n; i++) {
         fScaler[i] += psclr[i];
         acum[i] = fScaler[i];
      }
      
      /* create acummulated scaler bank */
      event->AddBank("ACUM", TID_DOUBLE, (char*)&acum, sizeof(acum));
      
      return flow;
   }
};

class ScalerFactory: public TAFactory
{
public:
   void Init(const std::vector<std::string> &args)
   {
      printf("Init!\n");
   }
      
   void Finish()
   {
      printf("Finish!\n");
   }
      
   TARunObject* NewRunObject(TARunInfo* runinfo)
   {
      printf("NewRunObject, run %d, file %s\n", runinfo->fRunNo, runinfo->fFileName.c_str());
      return new Scaler(runinfo);
   }
};

static TARegister tar(new ScalerFactory);

/* emacs
 * Local Variables:
 * tab-width: 8
 * c-basic-offset: 3
 * indent-tabs-mode: nil
 * End:
 */
