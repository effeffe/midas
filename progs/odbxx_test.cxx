/********************************************************************\

  Name:         odbxx_test.cxx
  Created by:   Stefan Ritt

  Contents:     Test and Demo of Object oriented interface to ODB

\********************************************************************/

#include <string>
#include <iostream>
#include <array>
#include <functional>

#include "midas.h"
#include "odbxx.hxx"

/*------------------------------------------------------------------*/

int main() {

   cm_connect_experiment(NULL, NULL, "test", NULL);
   midas::odb::set_debug(true);

   // create ODB structure...
   midas::odb o = {
      {"Int32 Key", 42},
      {"Bool Key", true},
      {"Subdir", {
         {"Int32 key", 123 },
         {"Double Key", 1.2},
         {"Subsub", {
            {"Float key", 1.2f},     // floats must be explicitly specified
            {"String Key", "Hello"},
         }}
      }},
      {"Int Array", {1, 2, 3}},
      {"Double Array", {1.2, 2.3, 3.4}},
      {"String Array", {"Hello1", "Hello2", "Hello3"}},
      {"Large Array", std::array<int, 10>{} },   // array with explicit size
      {"Large String", std::string(63, '\0') },  // string with explicit size
   };

   // ...and push it to ODB. If keys are present in the
   // ODB, their value is kept. If not, the default values
   // from above are copied to the ODB
   o.connect("/Test/Settings");

   // alternatively, a structure can be created from an existing ODB subtree
   midas::odb o2("/Test/Settings/Subdir");
   std::cout << o2 << std::endl;

   // set, retrieve, and change ODB value
   o["Int32 Key"] = 42;
   int i = o["Int32 Key"];
   o["Int32 Key"] = i+1;
   o["Int32 Key"]++;
   o["Int32 Key"] *= 1.3;
   std::cout << "Should be 57: " << o["Int32 Key"] << std::endl;

   // test with bool
   o["Bool Key"] = false;
   o["Bool Key"] = !o["Bool Key"];

   // test with std::string
   o["Subdir"]["Subsub"]["String Key"] = "Hello";
   std::string s = o["Subdir"]["Subsub"]["String Key"];
   s += " world!";
   o["Subdir"]["Subsub"]["String Key"] = s;

   // test with a vector
   std::vector<int> v = o["Int Array"]; // read vector
   std::fill(v.begin(), v.end(), 10);
   o["Int Array"] = v;        // assign vector to ODB array
   o["Int Array"][1] = 2;     // modify array element
   i = o["Int Array"][1];     // read from array element
   o["Int Array"].resize(5);  // resize array
   o["Int Array"]++;          // increment all values of array

   // test with a string vector
   std::vector<std::string> sv;
   sv = o["String Array"];
   sv[1] = "New String";
   o["String Array"] = sv;
   o["String Array"][2] = "Another String";

   // iterate over array
   int sum = 0;
   for (int e : o["Int Array"])
      sum += e;
   std::cout << "Sum should be 47: " << sum << std::endl;

   // creat key from other key
   midas::odb oi(o["Int32 Key"]);
   oi = 123;

   // test auto refresh
   std::cout << oi << std::endl;    // each read access reads value from ODB
   oi.set_auto_refresh_read(false); // turn off auto refresh
   std::cout << oi << std::endl;    // this does not read value from ODB
   oi.read();                       // this forces a manual read
   std::cout << oi << std::endl;

   // create ODB entries on-the-fly
   midas::odb ot;
   ot.connect("/Test/Settings/OTF");// this forces /Test/OTF to be created if not already there
   ot["Int32 Key"] = 1;             // create all these keys with different types
   ot["Double Key"] = 1.23;
   ot["String Key"] = "Hello";
   ot["Int Array"] = std::array<int, 10>{};
   ot["Subdir"]["Int32 Key"] = 42;
   ot["String Array"] = std::vector<std::string>{"S1", "S2", "S3"};

   // create key with default value
   i = ot["Int32 Key"](123);        // key exists already -> use key value
   i = ot["New Int32 Key"](123);    // key does not exist -> set it to default value 123
   std::string s1 = ot["New String Key"]("Hi"); // same for strings
   std::cout << ot << std::endl;

   o.read();                        // re-read the underlying ODB tree which got changed by above OTF code
   std::cout << o.print() << std::endl;

   // iterate over sub-keys
   for (midas::odb& oit : o)
      std::cout << oit.get_name() << std::endl;

   // print whole sub-tree
   std::cout << o.print() << std::endl;

   // dump whole subtree
   std::cout << o.dump() << std::endl;

   // delete test key from ODB
   o.delete_key();

   // watch ODB key for any change with lambda function
   midas::odb ow("/Experiment");
   ow.watch([](midas::odb &o) {
      std::cout << "Value of key \"" + o.get_full_path() + "\" changed to " << o << std::endl;
   });

   do {
      int status = cm_yield(100);
      if (status == SS_ABORT || status == RPC_SHUTDOWN)
         break;
   } while (!ss_kbhit());

   cm_disconnect_experiment();
   return 1;
}