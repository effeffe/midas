/********************************************************************\

  Name:         mexception.hxx
  Created by:   Stefan Ritt

  Contents:     Midas exception with stack trace

\********************************************************************/

#ifndef _MEXCEPT_HXX
#define _MEXCEPT_HXX

#include <string>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <execinfo.h>
#include <dlfcn.h>
#include <cxxabi.h>

/*------------------------------------------------------------------*/

// exceptions with line number and file name
#define mthrow(arg) throw mexception(arg, __FILE__, __LINE__);

class mexception : public std::runtime_error {
   std::string msg;
public:
   mexception(const std::string &arg, const char *file, int line) :
           std::runtime_error(arg) {
      std::stringstream trace_buf;
      trace_buf << "\n" << file << ":" << line << ": " << arg << std::endl;

      // create strack trace and add it to message
      trace_buf << "Stack trace:" << std::endl;
      void *callstack[128];
      char buf[1024];
      int n = backtrace(callstack, sizeof(callstack)/sizeof(callstack[0]));
      char **symbols = backtrace_symbols(callstack, n);
      for (int i=1 ; i<n ; i++) {
         Dl_info info;
         if (dladdr(callstack[i], &info)) {
            char *demangled = nullptr;
            int status=0;
            demangled = abi::__cxa_demangle(info.dli_sname, nullptr, 0, &status);
            snprintf(buf, sizeof(buf), "%-3d 0x%018lX %s + %zd\n",
                     i, (uintptr_t)callstack[i],
                     status == 0 ? demangled : info.dli_sname,
                     (char *)callstack[i] - (char *)info.dli_saddr);
            free(demangled);
         } else {
            snprintf(buf, sizeof(buf), "%-3d 0x%018lX\n",
                     i, (uintptr_t)callstack[i]);
         }
         trace_buf << buf;
      }

      free(symbols);
      msg = trace_buf.str();
   }

   ~mexception() throw() {}

   const char *what() const throw() {
      return msg.c_str();
   }
};

#endif // _MEXCEPT_HXX
