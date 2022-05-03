/*
   mdevice.h

   Base class mdevice to create proper ODB settings under
   /Equipment/<name>/Settings

   Derived class mdevice_mscb for MSCB devices

   Created S. Ritt 11.04.2022
*/

#include <cmath>
#include <history.h>

//---- generic class ------------------------------------------------

class mdevice {

public:

   EQUIPMENT *mEq;
   std::string mDevName;
   std::vector<std::string> mName;
   midas::odb mOdbDev;
   midas::odb mOdbSettings;
   midas::odb mOdbVars;
   int mDevIndex;
   int mNchannels;
   int mNblocks;
   double mThreshold;
   double mFactor;
   double mOffset;

public:
   mdevice(std::string eq_name, std::string dev_name,
          DWORD flags,
          INT(*dd) (INT cmd, ...)) {

      // search equipment in equipment table
      int idx;
      for (idx = 0; equipment[idx].name[0]; idx++)
         if (equipment[idx].name == eq_name)
            break;
      if (equipment[idx].name[0] == 0) {
         char str[256];
         sprintf(str, "Equipment \"%s\" not found in equipment table", eq_name.c_str());
         cm_msg(MERROR, "device::device", "%s", str);
         mthrow(str);
         return;
      }
      mEq = &equipment[idx];

      if (mEq->driver == nullptr) { // create new device driver list
         mEq->driver = (DEVICE_DRIVER *) calloc(2, sizeof(DEVICE_DRIVER));
         mDevIndex = 0;

      } else { // extend existing device driver list

         // check for double name
         int n;
         for (n = 0; mEq->driver[n].name[0]; n++) {
            if (mEq->driver[n].name == dev_name) {
               char str[256];
               sprintf(str, "Device \"%s\" defined twice for equipment \"%s\"", dev_name.c_str(),
                       eq_name.c_str());
               cm_msg(MERROR, "device::device", "%s", str);
               mthrow(str);
               return;
            }
         }
         int size = (n + 2) * sizeof(DEVICE_DRIVER);
         mEq->driver = (DEVICE_DRIVER *) realloc(mEq->driver, size);
         memset((void *) &mEq->driver[n + 1], 0, sizeof(DEVICE_DRIVER));
         mDevIndex = n;
      }

      strlcpy(mEq->driver[mDevIndex].name, dev_name.c_str(), sizeof(mEq->driver[mDevIndex].name));
      mDevName = dev_name;
      mEq->driver[mDevIndex].pequipment_name = new std::string(eq_name);
      if (flags & DF_OUTPUT)
         flags |= DF_PRIO_DEVICE;
      flags |= DF_MULTITHREAD;
      mEq->driver[mDevIndex].flags = flags;
      mEq->driver[mDevIndex].dd = dd;
      mEq->driver[mDevIndex].channels = 0;
      mNchannels = 0;
      mNblocks = 0;
      mThreshold = 0;
      mFactor = 1;
      mOffset = 0;

      mOdbDev.connect("/Equipment/" + eq_name + "/Settings/Devices/" + dev_name);
      mOdbSettings.connect("/Equipment/" + eq_name + "/Settings");
      mOdbVars.connect("/Equipment/" + eq_name + "/Variables");
   }

   void define_var(std::string name = "", double threshold = std::nan(""),
                   double factor = std::nan(""), double offset = std::nan(""))
   {
      int chn_index = 0;

      // put info into settings subtree
      if (mEq->driver[mDevIndex].flags & DF_INPUT) {
         // count total number of input channels
         for (int i=0 ; i <= mDevIndex ; i++)
            if (mEq->driver[i].flags & DF_INPUT)
               chn_index += mEq->driver[i].channels;

         if (std::isnan(threshold))
            threshold = mThreshold;
         if (std::isnan(factor))
            factor = mFactor;
         if (std::isnan(offset))
            offset = mOffset;

         mOdbSettings["Update Threshold"][chn_index] = (float) threshold;
         mOdbSettings["Input Factor"][chn_index] = (float) factor;
         mOdbSettings["Input Offset"][chn_index] = (float) offset;

         mOdbSettings.set_preserve_string_size(true);
         if (chn_index == 0)
            mOdbSettings["Names Input"] = std::string(31, '\0');
         mOdbSettings["Names Input"][chn_index] = name;
         mName.push_back(name);

         std::vector<float> inp = mOdbVars["Input"];
         if ((int)inp.size() < chn_index+1) {
            inp.resize(chn_index+1);
            mOdbVars["Input"] = inp;
         }
      }

      else if (mEq->driver[mDevIndex].flags & DF_OUTPUT) {
         // count total number of output channels
         for (int i=0 ; i <= mDevIndex ; i++)
            if (mEq->driver[i].flags & DF_OUTPUT)
               chn_index += mEq->driver[i].channels;

         mOdbSettings["Output Factor"][chn_index] = (float) factor;
         mOdbSettings["Output Offset"][chn_index] = (float) offset;

         mOdbSettings.set_preserve_string_size(true);
         if (chn_index == 0)
            mOdbSettings["Names Output"] = std::string(31, '\0');
         mOdbSettings["Names Output"][chn_index] = name;
         mName.push_back(name);

         std::vector<float> outp = mOdbVars["Output"];
         if ((int)outp.size() < chn_index+1) {
            outp.resize(chn_index+1);
            mOdbVars["Output"] = outp;
         }
      }

      else {
         // count total number of channels
         for (int i=0 ; i <= mDevIndex ; i++)
            chn_index += mEq->driver[i].channels;

         mOdbSettings.set_preserve_string_size(true);
         if (chn_index == 0)
            mOdbSettings["Names"] = std::string(31, '\0');
         mOdbSettings["Names"][chn_index] = name;
         mName.push_back(name);
      }

      mEq->driver[mDevIndex].channels++;
      mNchannels++;
   }

   void add_func(void func(midas::odb &)) {
      mOdbVars["Input"].watch(func);
   }

   midas::odb *odbDevice()
   {
      return &mOdbDev;
   }

   void define_param(int i, std::string name, std::string str)
   {
      mOdbDev[name][i].set_string_size(str, 32);
   }

   void define_param(int i, std::string name, int p)
   {
      mOdbDev[name][i] = p;
   }

   void define_history_panel(std::string panelName, int i1, int i2 = -1)
   {
      std::vector<std::string> vars;

      if (i2 == -1)
         vars.push_back(mEq->name + std::string(":") + mName[i1]);
      else
         for (int i=i1 ; i<=i2 ; i++)
            vars.push_back(mEq->name + std::string(":") + mName[i]);

      hs_define_panel(mEq->name, panelName.c_str(), vars);
   }

   void define_history_panel(std::string panelName, std::vector<std::string> vars)
   {
      for (std::size_t i=0 ; i<vars.size() ; i++)
         if (vars[i].find(":") == std::string::npos)
            vars[i] = std::string(mEq->name) + ":" + vars[i];
      hs_define_panel(mEq->name, panelName.c_str(), vars);
   }

   void set_threshold(double threshold)
   {
      mThreshold = threshold;
   }

   void set_factor_offset(double factor, double offset)
   {
      mFactor = factor;
      mOffset = offset;
   }

};
