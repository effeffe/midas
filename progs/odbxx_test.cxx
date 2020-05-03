/********************************************************************\

  Name:         odbxx.cxx
  Created by:   Stefan Ritt

  Contents:     Object oriented interface to ODB

\********************************************************************/

#include <string>
#include <iostream>
#include <stdexcept>
#include <initializer_list>
#include <cstring>
#include <bitset>

#include "odbxx.hxx"
#include "midas.h"

/*------------------------------------------------------------------*/

int main() {

   cm_connect_experiment(NULL, NULL, "test", NULL);
   midas::odb::set_debug(true);

    /*
   // test with creating keys
   midas::odb oc("/Test", TID_KEY);
   midas::odb od("/Test/Number", TID_FLOAT32);
   oc.pull();
   std::cout << oc.print() << std::endl;
   */

   // test with initializers
   midas::odb o{1.2f};

   midas::odb oini = {
           {"Key1", 42},
           {"Subdir", {
              {"Int", 123 },
              {"Key2", 1.2},
              {"Subsub", {
                 {"Key3", true},
                 {"Key4", "Hello"},
              }}
           }},
           {"Int Array", {1,2,3}},
           {"Float Array", {1.2,2.3,3.4}},
           {"String Array", {"Hello1","Hello2","Hello3"}}
   };
   std::cout << oini.print() << std::endl;
   oini.push("/Test");

   // test with int
   midas::odb o2("/Experiment/ODB Timeout");
   o2 = 10000;
   std::cout << o2 << std::endl;
   o2 = o2 * 1.3;
   std::cout << o2 << std::endl;
   o2 = o2 - 1;
   std::cout << o2 << std::endl;
   o2++;
   std::cout << o2 << std::endl;

   // test with bool
   midas::odb o3("/Experiment/Enable core dumps");
   bool b = o3;
   std::cout << o3 << std::endl;
   o3 = true;
   std::cout << o3 << std::endl;
   o3 = b;
   std::cout << o3 << std::endl;

   // test with std::string
   midas::odb o4("/Experiment/Name");
   std::cout << o4 << std::endl;

   std::string s2 = o4;
   s2 = o4;
   std::cout << s2 << std::endl;
   o4 = "MEG";
   o4 = std::string("Online");

   // test with a vector
   midas::odb o5("/Experiment/Test", TID_INT, 10);
   o5[4] = 5555;
   std::vector<int> v = o5;
   v[3] = 33333;
   o5 = v;
   std::cout << o5.print() << std::endl;
   v.resize(5);
   o5 = v;
   v.resize(10);
   o5 = v;
   o5.resize(8);
   o5.resize(10);
   o5[0]++;
   o5[0] += 2.5;
   std::cout << o5.print() << std::endl;
   o5++;
   std::cout << o5.print() << std::endl;
   o5 = 10;
   std::cout << o5.print() << std::endl;
   o5 *= 13;
   std::cout << o5.print() << std::endl;
   o5 /= 13;

   // iterate over vector
   int sum = 0;
   for (int i : o5)
      sum += i;

   // test with subkeys
   midas::odb o6("/Experiment");
   std::cout << o6 << std::endl;
   std::cout << o6["ODB timeout"] << std::endl;
   std::string s{"ODB timeout"};
   o6["ODB timeout"] = 12345;
   o6[s] = 54321;
   o6["Security"]["Enable non-localhost RPC"] = true;
   o6["Security/Enable non-localhost RPC"] = false;
   o6["Security/RPC ports/ODBEdit"] = 123;

   // creat key from other key
   midas::odb o7(o6["Security"]);
   std::cout << o7 << std::endl;

   // test auto refresh
   midas::odb o8("/Experiment/ODB Timeout");
   o8.set_auto_refresh_read(false);
   std::cout << o8 << std::endl;
   o8.pull();
   std::cout << o8 << std::endl;

   // test iterator
   for (auto& oit : o6)
      std::cout << oit << std::endl;

   // print whole subtree
   std::cout << o6.print() << std::endl;

   // dump whole subtree
   std::cout << o6.dump() << std::endl;

   cm_disconnect_experiment();
   return 1;
}