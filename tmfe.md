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

Frontend equipment configuration is stored in the TMFeEqInfo object. It directly
corresponds to ODB /Equipment/NAME/Common.

The initial equipment info object is created and filled by the user,
see struct EqInfoEverything in tmfe_example_everything.cxx.

On first start of the frontend, ODB /Equipment/NAME/Common will be created and filled with these values.

On the next start of the frontend, the equipment PreInit() method will read the equipment info from ODB (and
overwrite the initial equipment info). Then in the user-provided InitHandler() ther user can overwrite
the initial and the ODB values with their preferences (i.e. set fInfo->Buffer = "SYSTEM"). Corresponding
values will no longer be controlled by ODB. Then in the PostInit() method, equipment info is written
back into ODB to reflect the actual configuration of the equipment.

In other words. Initial equipment configuration is provided by the user, it is written to ODB Common
if ODB Common does not exist. Then, by default, all equipment configuration is read from ODB. If the user
wants some confguration parameters to be fixed, not changable via ODB, they explicitely overwrite
them in their InitHandler().

TBI: Alternatively, for experiments that do not want equipment configuration controlled from ODB,
set "ReadEqInfoFromODB = false" (default value is "true"). If this case, only the following values in ODB Common
an be changed by user: Active, Hidden, etc? (to match mfe.cxx).

TBI: Alternatively, if we do not want the default to be "ODB controls everything", we set the default value
of ReadEqInfoFronOdb to "false" and users who want to control their equipment from ODB, have to set it to "true".

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

### Multithreaded frontend

TBW

### Thread locking rules

TBW

### The End
