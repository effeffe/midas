/*
   mdevice_mscb.h

   Class mdevice_mscb derived from mdevice to create proper ODB settings under
   /Equipment/<name>/Settings for MSCB devices

   Created S. Ritt 11.04.2022
*/

#include <cmath>
#include <history.h>

class mdevice_mscb : public mdevice {
public:
   mdevice_mscb(std::string eq_name, std::string dev_name,
           DWORD flags,
           std::string submaster,
           std::string pwd = "",
           int pause = 0) : mdevice(eq_name, dev_name, flags, mscbdev){

      if (submaster.empty()) {
         char str[256];
         sprintf(str, "device_mscb definition for equipment \"%s\" device \"%s\" has no submaster",
                 eq_name.c_str(), dev_name.c_str());
         cm_msg(MERROR, "device_mscb::device_mscb", "%s", str);
         mthrow(str);
         return;
      }

      // check flags
      if ((flags & DF_INPUT) == 0 && (flags & DF_OUTPUT) == 0) {
         char str[256];
         sprintf(str, "Device \"%s\" for equipment \"%s\" must be either DF_INPUT or DF_OUTPUT",
                 dev_name.c_str(), eq_name.c_str());
         cm_msg(MERROR, "device::device", "%s", str);
         mthrow(str);
         return;
      }

      // create Settings/Devices in ODB
      midas::odb dev = {
              {"MSCB Device",  ""},
              {"MSCB Pwd",     ""},
              {"MSCB Address", 0},
              {"MSCB Index",   (UINT8) 0},
              {"MSCB Debug",   (INT32) 0},
              {"MSCB Retries", (INT32) 10},
              {"MSCB Pause",   (INT32) 0}
      };
      dev.connect("/Equipment/" + eq_name + "/Settings/Devices/" + dev_name);
      dev["MSCB Device"] = submaster;
      dev["MSCB Pwd"]    = pwd;
      dev["MSCB Pause"]  = pause;
   }

   void define_var(int address, unsigned char var_index,
                   std::string name = "", double threshold = std::nan(""),
                   double factor = std::nan(""), double offset = std::nan("")) {
      mdevice::define_var(name, threshold, factor, offset);

      // put info into device subtreee
      mOdbDev["MSCB Address"][mNchannels-1] = address;
      mOdbDev["MSCB Index"][mNchannels-1] = var_index;
   }

   void add_func(void func(midas::odb &)) {
      mOdbVars["Input"].watch(func);
   }

};
