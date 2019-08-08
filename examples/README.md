Content of the Example directory

This directory tree needs to be pruned...

* camac/ : To be removed

* crfe/ : 
    * Code for modern slow control front-end "Clock and Reset" to illustrate manual geration of slow control events and hardware updates via cm_watch().

* eventbuilder/ : Not updated
    * Event builder based on Sserial_number for assembly. Requires the EQ_EB flags in the equipment. Can use frontend index for multiple copy of the same code.

* history1/ : Not updated
    * Example and test of a simple periodic frontend writing slow controls data into midas history.

* history2/
    * Example and test of a simple periodic frontend writing slow controls data into midas history in per-variable history mode

* lowlevel/
    * camacsrv.cxx : To be removed
    * consume.cxx : 
        * A simple consumer getting data from a local or remote host and checking the data integrity. First, the first and last word in the events is checked in order to detect overwritten data, then the serial number of the event is checked in order to detect lost events.

    * produce.cxx :
      * A simple data producer. It simply connects to a buffer on the local machine or to a remote host which has the server program running.
    * rpc_srvr.cxx, ppc_clnt.cxx
      * This is a standalone RPC server and client which do not use any midas functionality. This can be used to implement simple RPC client/server programs which have nothing to do with midas
    * rpc_test.cxx
      * A simple program to test the RPC layer in MIDAS. It connects to a MIDAS server and executes a test routine on the server.

* raspberrypi/ : Not updated
    * Example of Midas Slow Control with web interface running on the RaspberryPi

* slowcont/ : 
    * Midas Slow Control using MSCB.

* wdfe/ : 
    * WaveDREAM board frontend

* sy2527/ : Not updated
    * Midas Slow Control frontend equipment for the SY2527 (Use of the DF_xxx flags).

* ybosexpt/ : To be removed

* ccusb/ : To be removed
    * This example provide a template for data acquisition using the CC-USB from Wiener.

* custom/ : Can be obsolete?
    * Example of ODB parameter insertion in GIF using the ODB/Custom definition.

* experiment/ : Main Frontend example
    * Complete example of frontend and analyzer with two equipments.

* mtfe/ : (C-based)
    * Example of Multi-threaded frontend using the "rb" functions (ring buffer).

* basic/ : Would remove it
    * analyzer.c, deferredfe.c, mantrigfe.c, miniana.f, minirc.c, odb_test.c, largefe.c, miniana.c, minife.c, msgdump.c, tinyfe.c

* sequencer/ : Need more examples
    * Simple .msl, xml sequencer file

* Triumf/ 
  *   c/  To be removed
    * c++/ Can be removed

* epics/ : Not updated
    * Midas Slow Control for EPICS through Channel Access (CA).

* gui/ Not updated
    *  ROOT based Midas GUI for histo display and experiment control.

* javascript1/ May be removed or updated
    * Example and test of mhttpd javascript functions for interacting with ODB and MIDAS.

* oofes/ Can be found under different Repo (Deap)
    * v1720/
      * Example of a multi-threaded frontend for multiple CAEN V1720 Digitizers.
    * v1740/
      * Example of a multi-threaded frontend for multiple CAEN V1740 Digitizers.
