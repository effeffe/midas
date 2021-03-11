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
#include <mutex>
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
TMFeResult TMFeErrorMessage(const std::string& message);
TMFeResult TMFeMidasError(const std::string& message, const char* midas_function_name, int midas_status);

// Equipment configuration, corresponds to EQUIPMENT_INFO and ODB /Equipement/NAME/Common

struct TMFeEqInfo
{
   uint16_t EventID = 1;
   uint16_t TriggerMask = 0;
   std::string Buffer = "SYSTEM";
   int Type = 0;
   int Source = 0;
   std::string Format = "MIDAS";
   bool Enabled = true;
   int ReadOn = 0;
   int Period = 1000;
   double EventLimit = 0;
   uint32_t NumSubEvents = 0;
   int LogHistory = 0;
   std::string FrontendHost;
   std::string FrontendName;
   std::string FrontendFileName;
   std::string Status;
   std::string StatusColor;
   bool Hidden = false;
   int WriteCacheSize = 100000;

   bool ReadOnlyWhenRunning = false; // RO_RUNNING
   bool WriteEventsToOdb = false; // RO_ODB
   double PeriodStatisticsSec = 1.0; // statistics update period
};

class TMFE;
class MVOdb;

class TMFeEquipment
{
public: // general configuration, should not be changed by user
   std::string fEqName;
   TMFeEqInfo *fEqInfo = NULL;
   TMFE* fMfe = NULL;
   std::string fEqFilename;
   std::mutex  fEqMutex;

public: // conection to event buffer
   size_t fEqBufferSize = 0;
   size_t fEqMaxEventSize = 0;
   int    fEqBufferHandle = 0;
   int    fEqSerial = 0;

public:
   MVOdb* fOdbEq = NULL;           ///< ODB Equipment/EQNAME
   MVOdb* fOdbEqCommon = NULL;     ///< ODB Equipment/EQNAME/Common
   MVOdb* fOdbEqSettings = NULL;   ///< ODB Equipment/EQNAME/Settings
   MVOdb* fOdbEqVariables = NULL;  ///< ODB Equipment/EQNAME/Variables
   MVOdb* fOdbEqStatistics = NULL; ///< ODB Equipment/EQNAME/Statistics

public: // statistics
   double fEqStatEvents = 0;
   double fEqStatBytes  = 0;
   double fEqStatEpS    = 0; // events/sec
   double fEqStatKBpS   = 0; // kbytes/sec (factor 1000, not 1024)

   // statistics rate computations
   double fEqStatLastTime   = 0;
   double fEqStatLastEvents = 0;
   double fEqStatLastBytes  = 0;

   // statistics write to odb timer
   double fEqStatLastWrite = 0;
   double fEqStatNextWrite = 0;

public: // contructors and initialization. not thread-safe.
   TMFeEquipment(TMFE* mfe, const char* eqname, const char* eqfilename, TMFeEqInfo* eqinfo); // ctor
   ~TMFeEquipment(); // dtor
   TMFeResult EqInit(); ///< Initialize equipment
   TMFeResult EqPreInit(); ///< Initialize equipment, before EquipmentBase::Init()
   TMFeResult EqPostInit(); ///< Initialize equipment, after EquipmentBase::Init()
   TMFeResult EqReadCommon(); ///< Read TMFeEqInfo from ODB /Equipment/NAME/Common
   TMFeResult EqWriteCommon() const; ///< Write TMFeEqInfo to ODB /Equipment/NAME/Common

public: // event composition methods
   TMFeResult ComposeEvent(char* pevent, size_t size) const;
   TMFeResult BkInit(char* pevent, size_t size) const;
   void*      BkOpen(char* pevent, const char* bank_name, int bank_type) const;
   TMFeResult BkClose(char* pevent, void* ptr) const;
   int        BkSize(const char* pevent) const;

public: // thread-safe methods
   TMFeResult EqSendEvent(const char* pevent);
   TMFeResult EqWriteEventToOdb(const char* pevent);
   TMFeResult EqZeroStatistics();
   TMFeResult EqWriteStatistics();
   TMFeResult EqSetStatus(const char* status, const char* color);

private: // non-thread-safe methods
   TMFeResult EqWriteEventToOdb_locked(const char* pevent);
};

class TMFeHandlerInterface
{
public: // optional RPC handlers run from the frontend RPC thread
   virtual TMFeResult HandleBeginRun(int run_number)  { return TMFeOk(); };
   virtual TMFeResult HandleEndRun(int run_number)    { return TMFeOk(); };
   virtual TMFeResult HandlePauseRun(int run_number)  { return TMFeOk(); };
   virtual TMFeResult HandleResumeRun(int run_number) { return TMFeOk(); };
   virtual TMFeResult HandleStartAbortRun(int run_number) { return TMFeOk(); };
   virtual TMFeResult HandleRpc(const char* cmd, const char* args, std::string& response) { return TMFeOk(); };

public: // optional periodic equipment handler runs from the frontend periodic thread
   virtual void HandlePeriodic() {};

public: // optional polled equipment handler runs from the per-equipment poll thread
   virtual bool HandlePoll() { return false; };
   virtual void HandleRead() {};

public: // optional ODB watch handler runs from the midas poll thread
   virtual void HandleOdbWatch(const std::string& odbpath, int odbarrayindex) {};
};

class TMFeEquipmentBase
{
public:
   TMFE* fMfe = NULL;
   TMFeEquipment* fEq = NULL;
   virtual ~TMFeEquipmentBase();
   virtual TMFeResult HandleInit(const std::vector<std::string>& args) { return TMFeOk(); };
   virtual void HandleUsage() {};
};

class TMFeHooksInterface
{
public:
   virtual void HandlePreConnect(const std::vector<std::string>& args) {};
   virtual void HandlePostConnect(const std::vector<std::string>& args) {};
   virtual void HandlePreDisconnect() {};
   virtual void HandlePostDisconnect() {};
};

class TMFeHandlerData;

class TMFE
{
 public:

   std::string fHostname; ///< hostname where the mserver is running, blank if using shared memory
   std::string fExptname; ///< experiment name, blank if only one experiment defined in exptab

   std::string fFrontendName; ///< frontend program name
   std::string fFrontendHostname; ///< frontend hostname
   std::string fFrontendFilename; ///< frontend program file name

public:
   int    fDB = 0;         ///< ODB database handle
   MVOdb* fOdbRoot = NULL; ///< ODB root

public:
   bool fShutdownRequested = false; ///< shutdown was requested by Ctrl-C or by RPC command

public:
   int  fRunNumber = 0; ///< current run number
   bool fStateRunning = false; ///< run state is running or paused

public:   
   std::vector<TMFeEquipment*> fEquipments;
   std::vector<TMFeEquipmentBase*> fEquipmentBases;

public:
   // NOTE: fHandlers must be protected against multithreaded write access. K.O.
   std::vector<TMFeHandlerData*> fHandlers;

   double fNextPeriodic = 0;
   double fNextPoll = 0;

 public:
   bool fRpcThreadStarting = false;
   bool fRpcThreadRunning  = false;
   bool fRpcThreadShutdownRequested = false;

   bool fPeriodicThreadStarting = false;
   bool fPeriodicThreadRunning  = false;
   bool fPeriodicThreadShutdownRequested = false;
   
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
   
   static bool gfVerbose;

   TMFeResult Connect(const char* progname, const char* filename = NULL, const char*hostname = NULL, const char*exptname = NULL);
   TMFeResult Disconnect();

   TMFeResult CreateEquipment(const char* eqname, const char* eqfile, TMFeEquipmentBase* eqbase, TMFeEqInfo* eqinfo);

   void       Usage();
   TMFeResult InitEquipments(const std::vector<std::string>& args);
   void       DeleteEquipments();


   void RegisterEquipment(TMFeEquipment* eq);
   void RegisterHandler(TMFeEquipment* eq, TMFeHandlerInterface* handler, bool enable_rpc, bool enable_periodic, bool enable_poll);

   void UnregisterEquipment(TMFeEquipment* eq);

   void StartRpcThread();
   void StartPeriodicThread();

   void StopRpcThread();
   void StopPeriodicThread();

   TMFeResult SetWatchdogSec(int sec);

   void PollMidas(int millisec);
   void MidasPeriodicTasks();
   void EquipmentPeriodicTasks();
   void EquipmentPollTasks();

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

public:
   std::vector<TMFeHooksInterface*> fHooks;

public:
   void AddHooks(TMFeHooksInterface*);

public:
   void CallPreConnectHooks(const std::vector<std::string>& args);
   void CallPostConnectHooks(const std::vector<std::string>& args);
   void CallPreDisconnectHooks();
   void CallPostDisconnectHooks();

public:
   static double GetTime(); ///< return current time in seconds, with micro-second precision
   static void Sleep(double sleep_time_sec); ///< sleep, with micro-second precision
   static std::string GetThreadId(); ///< return identification of this thread
};

class TMFeRegister
{
 public:
   TMFeRegister(const char* fename, const char* eqname, const char* eqfile, TMFeEquipmentBase* eqbase, TMFeEqInfo* eqinfo);
};

#endif
/* emacs
 * Local Variables:
 * tab-width: 8
 * c-basic-offset: 3
 * indent-tabs-mode: nil
 * End:
 */
