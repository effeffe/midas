/********************************************************************\

  Name:         mleak.hxx
  Created by:   Stefan Ritt

  Contents:     Simple leak detection library based on overloading
                of the new() and delete() operators.

                Each allocation is stored in an internal _leak_list
                and removed on the corresponding delete.

                Calling mleak_print() shows the remaining allocations
                which have not been deleted.

                Use mleak_reset() to clear that list manually

                Use mleak_log(true) to log each
                allocation/de-allocation

\********************************************************************/

#ifndef _MLEAK_HXX
#define _MLEAK_HXX

#include <map>
#include <sstream>

static bool _mleak_log{};
std::map<void *,std::string> _mleak_list;

/*------------------------------------------------------------------*/

void *operator new(std::size_t size, const char *file, int line) {
   void *pfs = malloc(size);
   if (pfs == nullptr) {
      std::cerr << "No heap to allocate" << std::endl;
      exit(-1);
   }
   std::stringstream s;
   s << std::hex << pfs << std::dec << " at " << file << ":" << line << " size " << size;
   _mleak_list[pfs] = s.str();

   if (_mleak_log)
      std::cout << "Allocated " << s.str() << std::endl;

   return pfs;
}

void *operator new[](size_t size, const char *file, int line) {
   return operator new(size, file, line);
}

void operator delete(void *pfs) noexcept {
   if (_mleak_log)
      std::cout << "Deleted   " << std::hex << pfs << std::dec  << std::endl;

   // erase previous allocation from list
   if (_mleak_list.find(pfs) != _mleak_list.end())
      _mleak_list.erase(pfs);

   free(pfs); // free pointer
   return;
}

void operator delete[](void *pfs) noexcept {
   operator delete(pfs);
}

void mleak_reset() {
   _mleak_list.clear();
}

void mleak_print() {
   if (_mleak_list.size() == 0)
      std::cout << "Leak list is empty." << std::endl;
   else
      std::cout << "Leak list:" << std::endl;

   // print contents of list
   for (auto &e : _mleak_list) {
      std::cout << e.second << std::endl;
   }
}

void mleak_log(bool flag) {
   _mleak_log = flag;
}

// replace new to catch file name and line number
#define new new(__FILE__,__LINE__)

/*------------------------------------------------------------------*/

#endif // _MLEAK_HXX