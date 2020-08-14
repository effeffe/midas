import midas.file_reader

# Open our file
mfile = midas.file_reader.MidasFile("/Users/ben/CDMS/Software/dqm/example_data/raw/21180710_1030/21180710_1030_F0002.mid.gz")

# We can simply iterate over all events in the file
for event in mfile:
    if event.header.is_midas_internal_event():
        print("Saw a special event")
        continue
    
    bank_names = ", ".join(b.name for b in event.banks.values())
    print("Event # %s of type ID %s contains banks %s" % (event.header.serial_number, event.header.event_id, bank_names))
    
    for bank_name, bank in event.banks.items():
        # bank.data is generally a python tuple.
        #
        # If use_numpy=True was specified when opening the MidasFile, then
        # bank.data is a numpy array.
        if len(bank.data):
            print("    The first entry in bank %s is %s" % (bank_name, bank.data[0]))