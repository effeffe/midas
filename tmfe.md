# MIDAS C++ frontend (TMFE)

### Introduction

The MIDAS C++ frontend ... TBW

* TBW1
* TBW2

### Quick start

TBW - explain creating new frontend starting from tmfe_example_xxx.cxx

* copy example, copy Makefile, copy manalyzer_main.
* create histograms in the BeginRun() method
* save results in the EndRun() method
* process data and fill histograms in the Analyze() method
* extract midas data banks like this: TBW

TBW - explain running the frontend:

* run as ...
* start run, this should happen
* stop run, this should happen
* check that history is working
* stop the frontend (ctrl-C and "programs" page "stop" button)

### tmfe_main command line switches

TBW

* -v - enable verbose ...

### Concept of equipment module

TBW

### Frontend configuration

* std::string fHostname - ???
* std::string fExptname - experiment name
* std::string fFrontendName - midas program name (as shown on the Programs page, etc)
* std::string fFrontendHostname - hostname of machine we are running on (ss_gethostname())
* std::string fFrontendFilename - program source file name if equipment source file name is not available
* bool fIfRunningCallExit - do not allow frontend to start if a run is already running
* bool fIfRunningCallBeginRun - if a run is already running, call begin run handlers

### Equipment configuration and ODB Common

Frontend equipment (TMFeEquipment) is configured by fEqConfFoo data members. Many of them correspond to
entries in ODB /Equipment/EQNAME/Common:

* Enabled - fEqConfEnabled - if set to "false", equipment is disabled, user handlers are not called
* Hidden - fEqConfHidden - if set to "true", do not display this equipment on the midas status page. used by mhttpd, status.html & co
* Event ID - fEqConfEventId - value used to create all new events for this equipment
* Trigger Mask - fEqConfTriggerMask - ditto
* Buffer - fEqConfBuffer - send all events into this event buffer. used only in PostInit()
* Type - not used
* Source - not used
* Format  - fEqConfFormat - TBI
* Read On - see bits RO_RUNNING and RO_ODB below
* Period - fEqConfPeriodMilliSec - in milliseconds, period for calling the periodic handler.
* Event Limit - fEqConfEventLimit - stop run after sending this many events. If "/logger/auto restart" is "true", a new run will be started after waiting for "/logger/auto restart delay" seconds.
* Num subevents - not used
* Log History - fEqConfLogHistory - control writing equipment variables to history: 0=disable history, 1=write as often as possible, other value=write with this interval, in seconds. used by mlogger.
* Frontend host - fMfe->fFrontendHostname - hostname where frontend is running
* Frontend name - fMfe->fFrontendName - midas client name of frontend
* Frontend file name - fEqFilename - file name of the equipment source file (usually __FILE__)
* Status - equipment status shown on the midas status page. used by mhttpd, status.html & co
* Status color - ditto
* Write cache size - fEqConfWriteCacheSize - TBI

Some equipment configuration has no ODB Common equivalents:

* bool fEqConfEnableRpc - enable calling the RPC handlers, enabled by default
* bool fEqConfEnablePeriodic - enable the periodic handler, enabled by default
* bool fEqConfEnablePoll - enable poll handler, disabled by default
* bool fEqConfReadConfigFromOdb - true: equipment is always configured using values from ODB, false: values specified by in the program always overwrite values in ODB, except for "Common/Enabled" and "Common/Event limit"
* bool fEqConfReadOnlyWhenRunning - read equipment only when a run is running (stored in "Common/Read on" bits RO_RUNNING|RO_PAUSED|RO_STOPPED)
* bool fEqConfWriteEventsToOdb - write events sent by this equipment to ODB, if history is enabled, mlogger writes them to the history (stored in "Common/Read on" bit RO_ODB)
* double fEqConfPeriodStatisticsSec - period in seconds for updating /Equipment/EQNAME/Statistics
* double fEqConfPollSleepPeriodSec - short sleep in the poll loop. default is 100 usec. shortest sleep in linux is 50-60 usec. set to 0 for 100% CPU-busy poll loop.

The initial equipment info object is created and initialized by the user, see example at struct EqInfoEverything in tmfe_example_everything.cxx.

On first start of the frontend, ODB /Equipment/NAME/Common is created and filled with these values.

On subsequent start of the frontend, equipment info is initialized as follows:
* initial equipment info object is created, user can specify custom values, see example at struct EqInfoEverything in tmfe_example_everything.cxx.
* unless ReadEqInfoFromOdb set set to false, EqPreInit() reads the data from ODB Common and overwrites the initial values.
* EqPreInit() sets standard values for frontend hostname, equipments status & etc
* in the init handler, user can overwrite ODB values read from ODB with their own data, i.e. set fEqInfo->Buffer = "SYSTEM".
* EqPostInit() writes the equipment info back to ODB Common, configures the frontend, connects to specified event buffer, etc

In other words. Initial equipment configuration is provided by the user, it is written to ODB Common
if ODB Common does not exist. Then, by default, all equipment configuration is read from ODB. If the user
wants some confguration parameters to be fixed, not changable via ODB, they explicitely overwrite
them in the init handler.

Alternatively, experiments that do not want equipment configuration controlled to be controlled by ODB,
should set the initial value of ReadEqInfoFromODB to false.

### Main loop and general control flow

Everything happens in this order:

* main()
* constructor of frontend object
** user: set the frontend name
** user: create equipment objects
* call frontend main function FeMain(), inside:
* call FeInit(), inside:
* process command line arguments
* call frontend arguments handler
** user: create more equipments, as needed
* if called with "-h", report program usage, call frontend usage handler, call equipment usage handlers and exit
* connect to MIDAS
* call frontend init handler
** user: open vme/usb/network interfaces
** user: initialize hardware common to all equipments
* initialize equipments FeInitEquipments(), for each equipment, call EqInit():
* call EqPreInit() (prepare ODB common, etc)
* call equipment init handler (initialize hardware, etc)
** user: initialize hardware, enable trigger
** user: start the poll thread if needed
* call EqPostInitHandler() (open midas event buffer, etc)
* call frontend ready handler
** user: do the very last hardware initializations
** user: start the periodic thread if needed
** user: start the RPC thread if needed
* call the frontend begin run function if run is already running (see explanation of run transitions)
* call FeMainLoop(), inside:
* while (1) call FePollMidas(), inside:
* call scheduled equipment periodic handlers
* call equipment poll handlers
* write equipment statistics
* run midas periodic tasks (update watchdog timeouts, etc)
* poll midas RPC (run transitions, db_watch() notifications, etc)
* frontend shutdown is initiated by setting fMfe->fShutdownRequested to "true"
* FeMainLoop() returns
* call FeShutdown()
* stop periodic thread, stop poll threads, stop rpc thread
* call frontend exit handler
** user: shutdown hardware, etc
* delete all equipment objects (stop the equipment poll thread, call equipment object destructor)
* disconnect from MIDAS
* FeMain() returns
** user: last chance to do something
* main() returns

### Frontend handlers

User code is connected with the TMFE framework in several places:

* frontend constructor
** user: set program name, create equipments, etc (MIDAS not connected yet)
* before calling FeMain()
** user: ditto
* frontend arguments handler
** first and last opportunity to examine our command line arguments before we connect to MIDAS
** for example, adjust our program name according command line arguments
* frontend init handler, called immediately after connecting to MIDAS
** user: initialize hardware
** user: examine ODB and create more equipments
* frontend ready handler, called immediately after all initialization is completed
** user: last chance to adjust, modify and initialze anything before frontend starts running
** user: start the periodic thread and the rpc thread, as needed
* frontend exit handler, called after shutting down all threads, right before disconnecting from MIDAS
** user: last chance to do anything, no other frontend or equipment handler will be called after this
** user: shutdown the hardware, etc
* after FeMain() returns
** user: not much to do here, all equipment objects have been deleted, midas is disconnected

### Equipment handlers

User code is connected with the TMFE framework in several places:

* equipment constructor, called from frontend constructor or from frontend arguments handler (MIDAS is disconnected)
or from frontend init hanmdler (MIDAS is connected)
** user: configure the equipment here: set the event id, set the event buffer name, etc
** user: do all hardware setup and initialization in the equipment init handler, not here
* equipment usage handler
** user: print usage information, what command line arguments we accept, etc
* equipment init handler
** user: examine command line arguments and ODB
** user: do additional configuration, i.e. according to ODB, modify the event id, modify the event buffer name, etc
** user: initialize and setup the hardware
** user: start the equipment poll thread as needed
* equipment RPC handlers, see next section
* equipment periodic handler, see next section
* equipment poll handler, see next section

### Equipment RPC Handlers

In MIDAS, frontends are controlled using the MIDAS RPC (Remote Procedure Call) functions.

To start/stop runs and cause other activity, programs like mhttpd and odbedit will make RPC
calls to the frontend. Frontend has to listen for the RPC calls, accept them,
process them and reply in a timely manner (within a few seconds or there will be general trouble).

* begin run handler ...
* end run handler ...
* pause run handler ...
* resume run handler ...
* startabort handler ...
* jrpc handler ...

### Equipment Periodic Handler

TBW

### Equipment Poll Handler

TBW

### ODB Handler

TBI, TBW

### Creating and sending events

TBW
TBW about ODB Statistics

### Multithreaded frontend

By default, MIDAS frontends are single threaded. It is the most safe
and easy to debug configuration. If threads are started, one must be
sure that all shared resources - global variables, object data members,
access to hardware and 3rd party libraries - are protected by locks -
mutexes or condition variables.

Typical problems present in most multithreaded programs are:
* race conditions (results depend on which thread does something first)
* crashes of non-thread-safe 3rd party libraries
* crashes of non-thread-safe C++ constructs (std::string, std::vector, etc)
* access to stale pointers (one thread deletes an object, another thread still has a pointer to it)
* torn reads and writes (global variable "double foo", one thread does "foo=1", another thread does "foo=2",
result is unpredictable because writing "double" values is not atomic at the hardware level and
data in memory will be half from one thread half from another thread
* delivery of UNIX signals (SIGALARM, etc) is unpredictable (which thread will get the signal?)

In DAQ applications use of threads is often unavoidable to improve performance,
to use all the available CPU cores, to improve program parallelism and to provide
good real-time behaviour.

In a MIDAS frontend, the two main reasons to use threads is to improve real-time
performance and to improve response time to run transition RPCs. Data readout will stall
when the main loop is sending events, updating ODB statistics, etc. RPCs will stall and timeout
if hardware readout has lengthy delays.

To help with this the TMFE frontend provides standard threads:

* midas rpc thread: all handling of MIDAS RPCs (run transitions, etc) ...
* frontend periodic thread
* equipment poll thread

### Thread locking rules

TBW

### Example programs

* tmfe_example_frontend - simple frontend with a periodic equipment simulating slow control readout of a sine wave data and a polled equipment readout of a block of scalers/counters
* tmfe_example_everything - example frontend and equipment that implement all available frontend functions
* fetest - frontend for generating test data for the mserver, analyzer and mlogger.

### The End
