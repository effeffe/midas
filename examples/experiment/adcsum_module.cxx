/********************************************************************\

  Name:         adcsum.c
  Created by:   Stefan Ritt

  Contents:     Example analyzer module for ADC summing. This module
                looks for a bank named CADC and produces an ASUM
                bank which contains the sum of all ADC values. The
                ASUM bank is a "structured" bank. It has been defined
                in the ODB and transferred to experim.h.

  $Id:$

\********************************************************************/

/*-- Include files -------------------------------------------------*/

/* standard includes */
#include <stdio.h>
#include <math.h>

//#define DEFINE_TESTS // must be defined prior to midas.h

/* midas includes */
#include "midas.h"
//#include "rmidas.h"
#include "experim.h"
#include "analyzer.h"

/* root includes */
#include <TH1D.h>

/*-- Parameters ----------------------------------------------------*/

ADC_SUMMING_PARAM adc_summing_param;

/*-- Tests ---------------------------------------------------------*/

//DEF_TEST(low_sum);
//DEF_TEST(high_sum);

#if 0
/*-- Module declaration --------------------------------------------*/

ADC_SUMMING_PARAM_STR(adc_summing_param_str);

ANA_MODULE adc_summing_module = {
   "ADC summing",               /* module name           */
   "Stefan Ritt",               /* author                */
   adc_summing,                 /* event routine         */
   NULL,                        /* BOR routine           */
   NULL,                        /* EOR routine           */
   adc_summing_init,            /* init routine          */
   NULL,                        /* exit routine          */
   &adc_summing_param,          /* parameter structure   */
   sizeof(adc_summing_param),   /* structure size        */
   adc_summing_param_str,       /* initial parameters    */
};
#endif

/*-- Module-local variables-----------------------------------------*/

//static TH1D *hAdcSum, *hAdcAvg;

#if 0

/*-- init routine --------------------------------------------------*/

INT adc_summing_init(void)
{
   /* book ADC sum histo */
   hAdcSum = h1_book<TH1D>("ADCSUM", "ADC sum", 500, 0, 10000);

   /* book ADC average in separate subfolder */
   open_subfolder("Average");
   hAdcAvg = h1_book<TH1D>("ADCAVG", "ADC average", 500000, 0, 10000);
   close_subfolder();

   return SUCCESS;
}

/*-- event routine -------------------------------------------------*/


INT adc_summing(EVENT_HEADER * pheader, void *pevent)
{
   INT i, j, n_adc;
   float *cadc;
   ASUM_BANK *asum;

   /* look for CADC bank, return if not present */
   n_adc = bk_locate(pevent, "CADC", &cadc);
   if (n_adc == 0)
      return 1;

   /* create ADC sum bank */
   bk_create(pevent, "ASUM", TID_STRUCT, (void**)&asum);

   /* sum all channels above threashold */
   asum->sum = 0.f;
   for (i = j = 0; i < n_adc; i++)
      if (cadc[i] > adc_summing_param.adc_threshold) {
         asum->sum += cadc[i];
         j++;
      }

   /* calculate ADC average */
   asum->average = j > 0 ? asum->sum / j : 0;

   /* evaluate tests */
   SET_TEST(low_sum, asum->sum < 1000);
   SET_TEST(high_sum, asum->sum > 1000);

   /* fill sum histo */
   hAdcSum->Fill(asum->sum, 1);

   /* fill average histo */
   hAdcAvg->Fill(asum->average);

   /* close calculated bank */
   bk_close(pevent, asum + 1);

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

struct AdcSum: public TARunObject
{
   TH1D *fAdcSum = NULL;
   TH1D *fAdcAvg = NULL;
   
   AdcSum(TARunInfo* runinfo)
      : TARunObject(runinfo)
   {
      printf("AdcSum::ctor!\n");

      runinfo->fRoot->fOutputFile->cd(); // select correct ROOT directory

      /* book ADC sum histo */
      fAdcSum = new TH1D("ADCSUM", "ADC sum", 500, 0, 10000);
      
      /* book ADC average in separate subfolder */
      TDirectory *subdir = runinfo->fRoot->fOutputFile->mkdir("Average");
      subdir->cd();

      fAdcAvg = new TH1D("ADCAVG", "ADC average", 500000, 0, 10000);

      runinfo->fRoot->fOutputFile->cd(); // select correct ROOT directory
   }

   ~AdcSum()
   {
      printf("AdcSum::dtor!\n");
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

      TMBank* bcadc = event->FindBank("CADC");
      if (!bcadc)
         return flow;

      float* cadc = (float*)event->GetBankData(bcadc);
      if (!cadc)
         return flow;

      int n_adc = bcadc->data_size / sizeof(float);
      if (n_adc == 0)
         return flow;

      ASUM_BANK asum;

      /* sum all channels above threashold */
      asum.sum = 0.f;
      int j = 0;
      for (int i = 0; i < n_adc; i++)
         if (cadc[i] > adc_summing_param.adc_threshold) {
            asum.sum += cadc[i];
            j++;
         }
      
      /* calculate ADC average */
      asum.average = j > 0 ? asum.sum / j : 0;
      
      /* evaluate tests */
      //SET_TEST(low_sum, asum.sum < 1000);
      //SET_TEST(high_sum, asum.sum > 1000);
      
      /* fill sum histo */
      fAdcSum->Fill(asum.sum, 1);
      
      /* fill average histo */
      fAdcAvg->Fill(asum.average);
      
      /* create ADC sum bank */
      event->AddBank("ASUM", TID_STRUCT, (char*)&asum, sizeof(asum));
      
      return flow;
   }
};

class AdcSumFactory: public TAFactory
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
      return new AdcSum(runinfo);
   }
};

static TARegister tar(new AdcSumFactory);

/* emacs
 * Local Variables:
 * tab-width: 8
 * c-basic-offset: 3
 * indent-tabs-mode: nil
 * End:
 */
