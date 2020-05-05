/********************************************************************\

  Name:         odbxx_test.cxx
  Created by:   Stefan Ritt

  Contents:     Test and Demo of Object oriented interface to ODB

\********************************************************************/

#include <string>
#include <iostream>

#include "odbxx.hxx"
#include "midas.h"

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
           {"String Array", {"Hello1", "Hello2", "Hello3"}}
   };

   // ...and push it to ODB. If keys are present in the
   // ODB, their value is kept. If not, the default values
   // from above are copied to the ODB
   o.push("/Test/Settings", true);

   // retrieve, set, and change ODB value
   int i = o["Int32 Key"];
   o["Int32 Key"] = i+1;
   o["Int32 Key"]++;
   o["Int32 Key"] *= 1.3;
   std::cout << "Should be 57: " << o["Int32 Key"] << std::endl;

   // test with bool
   o["Bool Key"] = !o["Bool Key"];

   // test with std::string
   std::string s = o["Subdir"]["Subsub"]["String Key"];
   s += " world!";
   o["Subdir"]["Subsub"]["String Key"] = s;

   // test with a vector
   std::vector<int> v = o["Int Array"];
   v[1] = 10;
   o["Int Array"] = v;        // assign vector to ODB object
   o["Int Array"][1] = 2;     // modify ODB object directly
   o["Int Array"].resize(5);  // resize array
   o["Int Array"]++;          // increment all values of array

   // iterate over array
   int sum = 0;
   for (int i : o["Int Array"])
      sum += i;
   std::cout << "Sum should be 11: " << sum << std::endl;

   // creat key from other key
   midas::odb oi(o["Int32 Key"]);
   oi = 123;

   // test auto refresh
   std::cout << oi << std::endl;    // each read access pulls value from ODB
   oi.set_auto_refresh_read(false); // turn off auto refresh
   std::cout << oi << std::endl;    // this does not pull value from ODB
   oi.pull();                       // this does manual pull
   std::cout << oi << std::endl;

   // iterate over subkeys
   for (auto& oit : o)
      std::cout << oit.get_odb()->get_name() << std::endl;

   // print whole subtree
   std::cout << o.print() << std::endl;

   // dump whole subtree
   std::cout << o.dump() << std::endl;

   cm_disconnect_experiment();
   return 1;
}