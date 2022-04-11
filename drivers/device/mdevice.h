/*
   mdevice.h

   Base class mdevice to create proper ODB settings under
   /Equipment/<name>/Settings

   Derived class mdevice_mscb for MSCB devices

   Created S. Ritt 11.04.2022
*/

//---- generic class ------------------------------------------------

class mdevice {

public:

   EQUIPMENT *mEq;
   std::string mDevName;
   midas::odb mOdbDev;
   midas::odb mOdbSettings;
   midas::odb mOdbVars;
   int mDevIndex;
   int mNchannels;
   int mNblocks;

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

      mOdbDev.connect("/Equipment/" + eq_name + "/Settings/Devices/" + dev_name);
      mOdbSettings.connect("/Equipment/" + eq_name + "/Settings");
      mOdbVars.connect("/Equipment/" + eq_name + "/Variables");
   }

   void define_var(std::string name = "", double threshold = 0,
                   double factor = 1, double offset = 0)
   {
      int chn_index = 0;

      // put info into settings subtree
      if (mEq->driver[mDevIndex].flags & DF_INPUT) {
         // count total number of input channels
         for (int i=0 ; i <= mDevIndex ; i++)
            if (mEq->driver[i].flags & DF_INPUT)
               chn_index += mEq->driver[i].channels;

         mOdbSettings["Update Threshold"][chn_index] = (float) threshold;

         mOdbSettings["Input Factor"][chn_index] = (float) factor;
         mOdbSettings["Input Offset"][chn_index] = (float) offset;

         mOdbSettings.set_preserve_string_size(true);
         if (chn_index == 0)
            mOdbSettings["Names Input"] = std::string(31, '\0');
         mOdbSettings["Names Input"][chn_index] = name;

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

};

//---- MSCB class ---------------------------------------------------

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
                   std::string name = "", double threshold = 0,
                   double factor = 1, double offset = 0) {
      mdevice::define_var(name, threshold, factor, offset);

      // put info into device subtreee
      mOdbDev["MSCB Address"][mNchannels-1] = address;
      mOdbDev["MSCB Index"][mNchannels-1] = var_index;
   }

   void add_func(void func(midas::odb &)) {
      mOdbVars["Input"].watch(func);
   }

};

