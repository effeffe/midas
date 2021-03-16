# MIDAS C++ modular frontend TMFE

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

### Equipment configuration and ODB Common

Frontend equipment (TMFeEquipment) is configured by the equipment info (TMFeEqInfo) object. Most data members
directly corresponds to entries in ODB /Equipment/EQNAME/Common:

* Enabled - if set to "false", equipment is disabled, user handlers are not called
* Hidden - if set to "true", do not display this equipment on the midas status page. used by mhttpd, status.html & co
* Event ID - value used to create all new events for this equipment
* Trigger Mask - ditto
* Buffer - send all events into this event buffer. used only in PostInit()
* Type - not used
* Source - not used
* Format  - TBI
* Read On - see bits RO_RUNNING and RO_ODB below
* Period - in milliseconds, period for calling the periodic handler.
* Event Limit - stop run after sending this many events. If "/logger/auto restart" is "true", a new run will be started after waiting for "/logger/auto restart delay" seconds.
* Num subevents - not used
* Log History - control writing equipment variables to history: 0=disable history, 1=write as often as possible, other value=write with this interval, in seconds. used by mlogger.
* Frontend host - hostname where frontend is running
* Frontend name - midas client name of frontend
* Frontend file name - file name of the equipment source file (usually __FILE__)
* Status - equipment status shown on the midas status page. used by mhttpd, status.html & co
* Status color - ditto
* Write cache size - TBI

Some equipment info data members do not have direct equivalents in ODB Common:

* bool ReadEqInfoFromOdb - true: equipment is always configured using values from ODB, false: values specified by in the program always overwrite values in ODB, except for "Common/Enabled"
* bool ReadOnlyWhenRunning - read equipment only when a run is running (stored in "Common/Read on" bits RO_RUNNING|RO_PAUSED|RO_STOPPED)
* bool WriteEventsToOdb - write events sent by this equipment to ODB, if history is enabled, mlogger writes them to the history (stored in "Common/Read on" bit RO_ODB)
* double PeriodStatisticsSec - period in seconds for updating /Equipment/EQNAME/Statistics

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

TBW

### MIDAS RPC Handler

In MIDAS, frontends are controlled using an RPC. To start/stop runs
and cause other activity, programs like mhttpd and odbedit will make RPC
calls to the frontend. Frontend has to listen for the RPC calls, accept them,
process them and send a response "quickly", within a few seconds.

* begin run handler ...
* end run handler ...
* pause run handler ...
* resume run handler ...
* startabort handler ...
* jrpc handler ...

### Periodic Handler

TBW

### Polling Handler

TBW

### ODB Handler

TBI, TBW

### Creating and sending events

TBW
TBW about ODB Statistics

### Multithreaded frontend

TBW

### Thread locking rules

TBW

### The End
