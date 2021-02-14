/********************************************************************\

  Name:         tmfe.h
  Created by:   Konstantin Olchanski - TRIUMF

  Contents:     C++ MIDAS frontend

\********************************************************************/

#ifndef TMFE_H
#define TMFE_H

#include <stdint.h>
#include <string>
#include <vector>
//#include <mutex>
//#include "midas.h"
#include "mvodb.h"

// from midas.h

#define TID_BYTE      1       /**< DEPRECATED, use TID_UINT8 instead    */
#define TID_UINT8     1       /**< unsigned byte         0       255    */
#define TID_SBYTE     2       /**< DEPRECATED, use TID_INT8 instead     */
#define TID_INT8      2       /**< signed byte         -128      127    */
#define TID_CHAR      3       /**< single character      0       255    */
#define TID_WORD      4       /**< DEPRECATED, use TID_UINT16 instead   */
#define TID_UINT16    4       /**< two bytes             0      65535   */
#define TID_SHORT     5       /**< DEPRECATED, use TID_INT16 instead    */
#define TID_INT16     5       /**< signed word        -32768    32767   */
#define TID_DWORD     6       /**< DEPRECATED, use TID_UINT32 instead   */
#define TID_UINT32    6       /**< four bytes            0      2^32-1  */
#define TID_INT       7       /**< DEPRECATED, use TID_INT32 instead    */
#define TID_INT32     7       /**< signed dword        -2^31    2^31-1  */
#define TID_BOOL      8       /**< four bytes bool       0        1     */
#define TID_FLOAT     9       /**< 4 Byte float format                  */
#define TID_FLOAT32   9       /**< 4 Byte float format                  */
#define TID_DOUBLE   10       /**< 8 Byte float format                  */
#define TID_FLOAT64  10       /**< 8 Byte float format                  */
#define TID_BITFIELD 11       /**< 32 Bits Bitfield      0  111... (32) */
#define TID_STRING   12       /**< zero terminated string               */
#define TID_ARRAY    13       /**< array with unknown contents          */
#define TID_STRUCT   14       /**< structure with fixed length          */
#define TID_KEY      15       /**< key in online database               */
#define TID_LINK     16       /**< link in online database              */
#define TID_INT64    17       /**< 8 bytes int          -2^63   2^63-1  */
#define TID_UINT64   18       /**< 8 bytes unsigned int  0      2^64-1  */
#define TID_QWORD    18       /**< 8 bytes unsigned int  0      2^64-1  */
#define TID_LAST     19       /**< end of TID list indicator            */

/**
System message types */
#define MT_ERROR           (1<<0)     /**< - */
#define MT_INFO            (1<<1)     /**< - */
#define MT_DEBUG           (1<<2)     /**< - */
#define MT_USER            (1<<3)     /**< - */
#define MT_LOG             (1<<4)     /**< - */
#define MT_TALK            (1<<5)     /**< - */
#define MT_CALL            (1<<6)     /**< - */
#define MT_ALL              0xFF      /**< - */

#define MT_ERROR_STR       "ERROR"
#define MT_INFO_STR        "INFO"
#define MT_DEBUG_STR       "DEBUG"
#define MT_USER_STR        "USER"
#define MT_LOG_STR         "LOG"
#define MT_TALK_STR        "TALK"
#define MT_CALL_STR        "CALL"

#define MERROR             MT_ERROR, __FILE__, __LINE__ /**< - */
#define MINFO              MT_INFO,  __FILE__, __LINE__ /**< - */
#define MDEBUG             MT_DEBUG, __FILE__, __LINE__ /**< - */
#define MUSER              MT_USER,  __FILE__, __LINE__ /**< produced by interactive user */
#define MLOG               MT_LOG,   __FILE__, __LINE__ /**< info message which is only logged */
#define MTALK              MT_TALK,  __FILE__, __LINE__ /**< info message for speech system */
#define MCALL              MT_CALL,  __FILE__, __LINE__ /**< info message for telephone call */

#if defined __GNUC__
#define MATTRPRINTF(a, b) __attribute__ ((format (printf, a, b)))
#else
#define MATTRPRINTF(a, b)
#endif

class TMFeResult
{
 public:
   bool error_flag = false;
   int  error_code = 0;
   std::string error_message = "success";

 public:
   TMFeResult() { // default ctor for success
   }

   TMFeResult(int code, const std::string& str) { // ctor
      error_flag = true;
      error_code = code;
      error_message = str;
   }
};

// special TMFeResult constructors

inline TMFeResult TMFeOk() { return TMFeResult(); }
TMFeResult TMFeMidasError(int status, const std::string& str);

// Equipment Common

class TMFeCommon
{
 public:
   uint16_t EventID;
   uint16_t TriggerMask;
   std::string Buffer;
   int Type;
   int Source;
   std::string Format;
   bool Enabled;
   int ReadOn;
   int Period;
   double EventLimit;
   uint32_t NumSubEvents;
   int LogHistory;
   std::string FrontendHost;
   std::string FrontendName;
   std::string FrontendFileName;
   std::string Status;
   std::string StatusColor;
   bool Hidden;
   int WriteCacheSize;

 public:
   TMFeCommon(); // ctor
};

class TMFE;
class MVOdb;

class TMFeEquipment
{
 public:
   std::string fName;
   TMFeCommon *fCommon;
   TMFE* fMfe;

 public:
   int fBufferHandle;
   int fSerial;

 public:
   MVOdb* fOdbEq;           ///< ODB Equipment/EQNAME
   MVOdb* fOdbEqCommon;     ///< ODB Equipment/EQNAME/Common
   MVOdb* fOdbEqSettings;   ///< ODB Equipment/EQNAME/Settings
   MVOdb* fOdbEqVariables;  ///< ODB Equipment/EQNAME/Variables
   MVOdb* fOdbEqStatistics; ///< ODB Equipment/EQNAME/Statistics

 public:
   double fStatEvents;
   double fStatBytes;
   double fStatEpS; // events/sec
   double fStatKBpS; // kbytes/sec (factor 1000, not 1024)

   double fStatLastTime;
   double fStatLastEvents;
   double fStatLastBytes;

 public:
   TMFeEquipment(TMFE* mfe, const char* name, TMFeCommon* common); // ctor
   TMFeResult Init(); ///< Initialize equipment
   TMFeResult SendData(const char* data, int size);    ///< ...
   TMFeResult ComposeEvent(char* pevent, int size);
   TMFeResult BkInit(char* pevent, int size);
   void*      BkOpen(char* pevent, const char* bank_name, int bank_type);
   TMFeResult BkClose(char* pevent, void* ptr);
   int       BkSize(const char* pevent);
   TMFeResult SendEvent(const char* pevent);
   TMFeResult ZeroStatistics();
   TMFeResult WriteStatistics();
   TMFeResult SetStatus(const char* status, const char* color);
};

class TMFeRpcHandlerInterface
{
 public:
   virtual TMFeResult HandleBeginRun(int run_number);
   virtual TMFeResult HandleEndRun(int run_number);
   virtual TMFeResult HandlePauseRun(int run_number);
   virtual TMFeResult HandleResumeRun(int run_number);
   virtual TMFeResult HandleStartAbortRun(int run_number);
   virtual TMFeResult HandleRpc(const char* cmd, const char* args, std::string& response);
};

class TMFePeriodicHandlerInterface
{
 public:
   virtual void HandlePeriodic() = 0;
};

class TMFePeriodicHandler
{
 public:
   TMFeEquipment *fEq;
   TMFePeriodicHandlerInterface *fHandler;
   double fLastCallTime;
   double fNextCallTime;

 public:
   TMFePeriodicHandler();
   ~TMFePeriodicHandler();
};

class TMFE
{
 public:

   std::string fHostname; ///< hostname where the mserver is running, blank if using shared memory
   std::string fExptname; ///< experiment name, blank if only one experiment defined in exptab

   std::string fFrontendName; ///< frontend program name
   std::string fFrontendHostname; ///< frontend hostname
   std::string fFrontendFilename; ///< frontend program file name

 public:
   int  fDB; ///< ODB database handle
   MVOdb* fOdbRoot; ///< ODB root

 public:
   bool fShutdownRequested; ///< shutdown was requested by Ctrl-C or by RPC command

 public:   
   std::vector<TMFeEquipment*> fEquipments;
   std::vector<TMFeRpcHandlerInterface*> fRpcHandlers;
   std::vector<TMFePeriodicHandler*> fPeriodicHandlers;
   double fNextPeriodic;

 public:
   bool fRpcThreadStarting;
   bool fRpcThreadRunning;
   bool fRpcThreadShutdownRequested;

   bool fPeriodicThreadStarting;
   bool fPeriodicThreadRunning;
   bool fPeriodicThreadShutdownRequested;
   
 private:
   /// TMFE is a singleton class: only one
   /// instance is allowed at any time
   static TMFE* gfMFE;
   
   TMFE(); ///< default constructor is private for singleton classes
   virtual ~TMFE(); ///< destructor is private for singleton classes

 public:
   
   /// TMFE is a singleton class. Call instance() to get a reference
   /// to the one instance of this class.
   static TMFE* Instance();
   
   TMFeResult Connect(const char* progname, const char* filename = NULL, const char*hostname = NULL, const char*exptname = NULL);
   TMFeResult Disconnect();

   TMFeResult RegisterEquipment(TMFeEquipment* eq);
   void RegisterRpcHandler(TMFeRpcHandlerInterface* handler); ///< RPC handlers are executed from the RPC thread, if started
   void RegisterPeriodicHandler(TMFeEquipment* eq, TMFePeriodicHandlerInterface* handler); ///< periodic handlers are executed from the periodic thread, if started

   void StartRpcThread();
   void StartPeriodicThread();

   void StopRpcThread();
   void StopPeriodicThread();

   TMFeResult SetWatchdogSec(int sec);

   void PollMidas(int millisec);
   void MidasPeriodicTasks();
   void EquipmentPeriodicTasks();

   TMFeResult TriggerAlarm(const char* name, const char* message, const char* aclass);
   TMFeResult ResetAlarm(const char* name);

   void Msg(int message_type, const char *filename, int line, const char *routine, const char *format, ...) MATTRPRINTF(6,7);

   void SetTransitionSequenceStart(int seqno);
   void SetTransitionSequenceStop(int seqno);
   void SetTransitionSequencePause(int seqno);
   void SetTransitionSequenceResume(int seqno);
   void SetTransitionSequenceStartAbort(int seqno);
   void DeregisterTransitions();
   void DeregisterTransitionStart();
   void DeregisterTransitionStop();
   void DeregisterTransitionPause();
   void DeregisterTransitionResume();
   void DeregisterTransitionStartAbort();
   void RegisterTransitionStartAbort();

   static double GetTime(); ///< return current time in seconds, with micro-second precision
   static void Sleep(double sleep_time_sec); ///< sleep, with micro-second precision
   static std::string GetThreadId(); ///< return identification of this thread
};

#endif
/* emacs
 * Local Variables:
 * tab-width: 8
 * c-basic-offset: 3
 * indent-tabs-mode: nil
 * End:
 */
