# Makefile for CAMAC examples

all:: camac_init.exe
all:: camac_init_null.exe
all:: camac_init_rpc.exe
all:: camac_init_hyt1331.exe
#all:: camac_init_gpmc.exe
all:: camac_init_cc32.exe
all:: camac_init_dsp.exe
# jorway73a.c does not compile all:: camac_init_jorway.exe
all:: camac_init_ces8210_gef.exe
all:: camac_scan.exe
#all:: camac_scan_gpmc.exe
all:: camac_scan_hyt1331.exe
all:: camac_scan_ces8210_gef.exe
all:: camac_mcnaf.exe
all:: camac_mcnaf_rpc.exe
all:: camac_mcnaf_hyt1331.exe
all:: camac_mcnaf_ces8210_gef.exe
#all:: camac_mcnaf_gpmc.exe

gpmc: camac_init_gpmc.exe camac_scan_gpmc.exe camac_mcnaf_gpmc.exe

MIDASARCH=linux

MIDASLIB=$(MIDASSYS)/$(MIDASARCH)/lib/libmidas.a -lrt -lutil

VME_VMIC=$(MIDASSYS)/drivers/vme/vmic/vmicvme.c -I$(MIDASSYS)/drivers/vme/vmic
VME_GEF=$(MIDASSYS)/drivers/vme/vmic/gefvme.c -I$(MIDASSYS)/drivers/vme/vmic

GPMC_DIR=$(HOME)/daq/CAMAC-Crate-Controller/edevel00195/CAMAC-Crate-Controller/LINUX

CAMAC_NULL=$(MIDASSYS)/drivers/camac/camacnul.c
CAMAC_RPC=$(MIDASSYS)/drivers/camac/camacrpc.c
CAMAC_HYT1331=$(MIDASSYS)/drivers/camac/hyt1331.c -I$(MIDASSYS)/drivers/camac
CAMAC_CC32=$(MIDASSYS)/drivers/camac/wecc32.c -I$(MIDASSYS)/drivers/camac
CAMAC_DSP=$(MIDASSYS)/drivers/camac/dsp004.c -I$(MIDASSYS)/drivers/camac
CAMAC_JORWAY=$(MIDASSYS)/drivers/camac/jorway73a.c -I$(MIDASSYS)/drivers/camac
CAMAC_CES8210=$(MIDASSYS)/drivers/camac/ces8210.c -I$(MIDASSYS)/drivers/camac $(VME_VMIC)
CAMAC_CES8210_GEF=$(MIDASSYS)/drivers/camac/ces8210.c -I$(MIDASSYS)/drivers/camac $(VME_GEF)
CAMAC_GPMC= -I$(GPMC_DIR) $(GPMC_DIR)/mcstd_libgpmc_camac.c $(GPMC_DIR)/libgpmc_camac.c

CAMAC_DRIVER=$(CAMAC_CES8210_GEF)

CFLAGS=-O2 -g -Wall -Wuninitialized

%.exe: %.o
	$(CXX) $(CFLAGS) -o $@ $^ $(CAMAC_DRIVER) -I$(MIDASSYS)/include $(MIDASLIB)

%_null.exe: %.o
	$(CXX) $(CFLAGS) -o $@ $^ $(CAMAC_NULL) -I$(MIDASSYS)/include

%_rpc.exe: %.o
	$(CXX) $(CFLAGS) -o $@ $^ $(CAMAC_RPC) -I$(MIDASSYS)/include $(MIDASLIB)

%_hyt1331.exe: %.o
	$(CXX) $(CFLAGS) -o $@ $^ $(CAMAC_HYT1331) -I$(MIDASSYS)/include $(MIDASLIB)

%_gpmc.exe: %.o
	$(CXX) $(CFLAGS) -o $@ $^ $(CAMAC_GPMC) -I$(MIDASSYS)/include $(MIDASLIB)

%_cc32.exe: %.o
	$(CXX) $(CFLAGS) -o $@ $^ $(CAMAC_CC32) -I$(MIDASSYS)/include $(MIDASLIB)

%_dsp.exe: %.o
	$(CXX) $(CFLAGS) -o $@ $^ $(CAMAC_DSP) -I$(MIDASSYS)/include $(MIDASLIB)

%_jorway.exe: %.o
	$(CXX) $(CFLAGS) -o $@ $^ $(CAMAC_JORWAY) -I$(MIDASSYS)/include $(MIDASLIB)

%_ces8210.exe: %.o
	$(CXX) $(CFLAGS) -o $@ $^ $(CAMAC_CES8210) -I$(MIDASSYS)/include $(MIDASLIB)

%_ces8210_gef.exe: %.o
	$(CXX) $(CFLAGS) -o $@ $^ $(CAMAC_CES8210_GEF) -I$(MIDASSYS)/include $(MIDASLIB)

%.o: %.c
	$(CXX) $(CFLAGS) -c -o $@ $^ -I$(MIDASSYS)/include

%.o: %.cxx
	$(CXX) $(CFLAGS) -c -o $@ $^ -I$(MIDASSYS)/include

camac_mcnaf.c:
	-ln -s $(MIDASSYS)/utils/mcnaf.c camac_mcnaf.c

clean::
	-rm -f *.o *.exe

#end
