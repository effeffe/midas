/********************************************************************\

  Name:         adccalib.c
  Created by:   Stefan Ritt

  Contents:     Example analyzer module for ADC calibration. Looks
                for ADC0 bank, subtracts pedestals and applies gain
                calibration. The resulting values are appended to 
                the event as an CADC bank ("calibrated ADC"). The
                pedestal values and software gains are stored in
                adccalib_param structure which was defined in the ODB
                and transferred to experim.h.

  $Id:$

\********************************************************************/

/*-- Include files -------------------------------------------------*/

/* standard includes */
#include <stdio.h>
//#include <time.h>

/* midas includes */
#include "midas.h"
//#include "rmidas.h"
#include "experim.h"
#include "analyzer.h"

/* root includes */
#include <TH1D.h>
//#include <TTree.h>

/*-- Parameters ----------------------------------------------------*/

ADC_CALIBRATION_PARAM adccalib_param;
//extern EXP_PARAM exp_param;
//extern RUNINFO runinfo;

/*-- Module declaration --------------------------------------------*/

//INT adc_calib(EVENT_HEADER *, void *);
//INT adc_calib_init(void);
//INT adc_calib_bor(INT run_number);
//INT adc_calib_eor(INT run_number);

ADC_CALIBRATION_PARAM_STR(adc_calibration_param_str);

//ANA_MODULE adc_calib_module = {
//   "ADC calibration",           /* module name           */
//   "Stefan Ritt",               /* author                */
//   adc_calib,                   /* event routine         */
//   adc_calib_bor,               /* BOR routine           */
//   adc_calib_eor,               /* EOR routine           */
//   adc_calib_init,              /* init routine          */
//   NULL,                        /* exit routine          */
//   &adccalib_param,             /* parameter structure   */
//   sizeof(adccalib_param),      /* structure size        */
//   adc_calibration_param_str,   /* initial parameters    */
//};

/*-- module-local variables ----------------------------------------*/

/*-- init routine --------------------------------------------------*/

#define ADC_N_BINS   500
#define ADC_X_LOW      0
#define ADC_X_HIGH  4000

#if 0
/*-- event routine -------------------------------------------------*/

INT adc_calib(EVENT_HEADER * pheader, void *pevent)
{
   INT i;
   WORD *pdata;
   float *cadc;

   /* look for ADC0 bank, return if not present */
   if (!bk_locate(pevent, "ADC0", &pdata))
      return 1;

   /* create calibrated ADC bank */
   bk_create(pevent, "CADC", TID_FLOAT, (void**)&cadc);

   /* zero cadc bank */
   for (i = 0; i < N_ADC; i++)
      cadc[i] = 0.f;

   /* subtract pedestal */
   for (i = 0; i < N_ADC; i++)
      cadc[i] = (float) ((double) pdata[i] - adccalib_param.pedestal[i] + 0.5);

   /* apply software gain calibration */
   for (i = 0; i < N_ADC; i++)
      cadc[i] *= adccalib_param.software_gain[i];

   /* fill ADC histos if above threshold */
   for (i = 0; i < N_ADC; i++)
      if (cadc[i] > (float) adccalib_param.histo_threshold)
         hAdcHists[i]->Fill(cadc[i], 1);

   /* close calculated bank */
   bk_close(pevent, cadc + N_ADC);

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

#include "TH1D.h"

struct AdcCalib: public TARunObject
{
   TH1D *fAdcHists[N_ADC];
   
   AdcCalib(TARunInfo* runinfo)
      : TARunObject(runinfo)
   {
      printf("AdcCalib::ctor!\n");

      runinfo->fRoot->fOutputFile->cd(); // select correct ROOT directory

      char name[256];
      int i;
      
      /* book CADC histos */
      
      for (i = 0; i < N_ADC; i++) {
         char title[256];
         
         sprintf(name, "CADC%02d", i);
         sprintf(title, "ADC %d", i);
         
         fAdcHists[i] = new TH1D(name, title, ADC_N_BINS, ADC_X_LOW, ADC_X_HIGH);
      }
   }

   ~AdcCalib()
   {
      printf("AdcCalib::dtor!\n");
   }

   void BeginRun(TARunInfo* runinfo)
   {
      printf("BeginRun, run %d, file %s\n", runinfo->fRunNo, runinfo->fFileName.c_str());
   }

   void EndRun(TARunInfo* runinfo)
   {
      printf("EndRun, run %d\n", runinfo->fRunNo);
   }

   TAFlowEvent* Analyze(TARunInfo* runinfo, TMEvent* event, TAFlags* flags, TAFlowEvent* flow)
   {
      printf("Analyze, run %d, event serno %d, id 0x%04x, data size %d\n", runinfo->fRunNo, event->serial_number, (int)event->event_id, event->data_size);

      TMBank* badc0 = event->FindBank("ADC0");
      if (!badc0)
         return flow;

      WORD* pdata = (WORD*)event->GetBankData(badc0);
      if (!pdata)
         return flow;

      float cadc[N_ADC];
      
      /* zero cadc bank */
      for (int i = 0; i < N_ADC; i++)
         cadc[i] = 0.f;
      
      /* subtract pedestal */
      for (int i = 0; i < N_ADC; i++)
         cadc[i] = (float) ((double) pdata[i] - adccalib_param.pedestal[i] + 0.5);
      
      /* apply software gain calibration */
      for (int i = 0; i < N_ADC; i++)
         cadc[i] *= adccalib_param.software_gain[i];
      
      /* fill ADC histos if above threshold */
      for (int i = 0; i < N_ADC; i++)
         if (cadc[i] > (float) adccalib_param.histo_threshold)
            fAdcHists[i]->Fill(cadc[i], 1);

      /* create calibrated ADC bank */
      event->AddBank("CADC", TID_FLOAT, (char*)cadc, sizeof(cadc));
      
      return flow;
   }
};

class AdcCalibFactory: public TAFactory
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
      return new AdcCalib(runinfo);
   }
};

static TARegister tar(new AdcCalibFactory);

/* emacs
 * Local Variables:
 * tab-width: 8
 * c-basic-offset: 3
 * indent-tabs-mode: nil
 * End:
 */
