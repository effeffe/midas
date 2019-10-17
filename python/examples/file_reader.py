import midas.file_reader

# Open our file
mfile = midas.file_reader.MidasFile("040644.mid")

# We can simply iterate over all events in the file
for event in mfile:
    bank_names = ", ".join(b.name for b in event.body.banks.values())
    print("Event # %s of type ID %s contains banks %s" % (event.header.serial_number, event.header.event_id, bank_names))
    
    for bank_name, bank in event.body.banks.items():
        if len(bank.data):
            print("    The first entry in bank %s is %s" % (bank_name, bank.data[0]))