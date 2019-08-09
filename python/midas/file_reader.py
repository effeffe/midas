"""
Tools to read a midas file.

The midas file format is documented at https://midas.triumf.ca/MidasWiki/index.php/Event_Structure#MIDAS_Format_Event.
Each event has a "header" containing metadata, then a series of names "banks" containing the actual data.
The banks can be of different formats (e.g. lists of floats/doubles/ints).

The tools in this files can read in midas events, and automatically convert the data in the banks
to appropriate python data types (so you get a tuple of ints, rather than just a raw set of bytes).

We can read on files in .mid. .mid.gz and .mid.lz4 format. lz4 support is not present in the standard python
libraries, but can be installed using pip (e.g. `pip install lz4`). See the README file to learn more about pip.


Example usage for reading event information:

```
import midas.file_reader

# Open our file
mfile = midas.file_reader.MidasFile("040129.mid")

# Here we choose to just read in the header of each event, and will read
# the body (the actual banks) later. You could instead just call
# `mfile.read_next_event()` to read the header and body at the same time.
while mfile.read_next_event_header():
    header = mfile.event_header
    
    if header.is_midas_internal_event():
        # Skip over events that contain midas messages or ODB dumps
        continue
    
    print("Overall size of event # %s of type ID %s is %d bytes" % (header.serial_number, header.event_id, header.event_data_size_bytes))
    
    if not mfile.read_this_event_body():
        raise RuntimeError("Unexpectedly failed to read body of event!")
    
    # Loop over the banks of data in this event and print information about them
    for name, bank in mfile.event_body.banks.items():
        # The `bank.data` member is automatically converted to appropriate python data types.
        # Here we're just figuring out what that type is to print it to screen. Normally
        # you already know what to expect for each bank, and could just the tuple of floats,
        # for example.
        
        if isinstance(bank.data, tuple) and len(bank.data):
            # A tuple of ints/floats/etc (a tuple is like a fixed-length list)
            type_str = "tuple of %s containing %d elements" % (type(bank.data[0]).__name__, len(bank.data))
        elif isinstance(bank.data, tuple):
            # A tuple of length zero
            type_str = "empty tuple"
        elif isinstance(bank.data, str):
            # Of the original data was a list of chars, we convert to a string.
            type_str = "string of length %d" % len(bank.data)
        else:
            # Some data types we just leave as a set of bytes.
            type_str = type(bank.data[0]).__name__
            
        print("  - bank %s contains %d bytes of data. Python data type: %s" % (name, bank.size_bytes, type_str))


```



Example usage for reading ODB information:

```
import midas.file_reader

mfile = midas.file_reader.MidasFile("040129.mid")

try:
    # Try to find the special midas event that contains an ODB dump.
    odb = mfile.get_bor_odb_dump()
    
    # The full ODB is stored as a nested dict withing the `odb.data` member.
    run_number = odb.data["Runinfo"]["Run number"]
    print("We are looking at a file from run number %s" % run_number)
except RuntimeError:
    # No ODB dump found (mlogger was probably configured to not dump
    # the ODB at the start of each subrun).
    print("No begin-of-run ODB dump found")

```

"""
import gzip
import struct
import midas
import json
import datetime
import math
from xml.etree import ElementTree

try:
    import lz4.frame
    have_lz4 = True
except ImportError:
    have_lz4 = False
        
    
class EventHeader:
    """
    Represents a midas EVENT_HEADER struct.
    See https://midas.triumf.ca/MidasWiki/index.php/Event_Structure#MIDAS_Format_Event
    
    Members:
        
    * event_id (int)
    * trigger_mask (int)
    * serial_number (int)
    * timestamp (int) - UNIX timestamp of event
    * event_data_size_bytes (int) - Size of all banks
    """
    def __init__(self):
        self.event_id = None
        self.trigger_mask = None
        self.serial_number = None
        self.timestamp = None
        self.event_data_size_bytes = None        
        
    def is_midas_internal_event(self):
        """
        Whether this is a special event that contains the ODB dumps or midas messages.
        
        Returns:
            bool
        """
        return self.is_bor_event() or self.is_eor_event() or self.is_msg_event()
        
    def is_bor_event(self):
        """
        Whether this is a special event that contains the begin-of-run ODB dump.
        
        Returns:
            bool
        """
        return self.event_id == 0x8000
    
    def is_eor_event(self):
        """
        Whether this is a special event that contains the end-of-run ODB dump.
        
        Returns:
            bool
        """
        return self.event_id == 0x8001
    
    def is_msg_event(self):
        """
        Whether this is a special event that contains a midas message.
        
        Returns:
            bool
        """
        return self.event_id == 0x8002
    
class Bank:
    """
    Represents a midas BANK or BANK32 struct.
    See https://midas.triumf.ca/MidasWiki/index.php/Event_Structure#MIDAS_Format_Event
    
    Members:
        
    * name (str) - 4 characters
    * type (int) - See `TID_xxx` members in `midas` module
    * size_bytes (int)
    * data (bytes) 
    """
    def __init__(self):
        self.name = None
        self.type = None
        self.size_bytes = None
        self.data = None

class EventBody:
    """
    Represents a midas BANK_HEADER and its associated banks.
    See https://midas.triumf.ca/MidasWiki/index.php/Event_Structure#MIDAS_Format_Event
    
    Members:
        
    * all_bank_size_bytes (int)
    * flags (int)
    * banks (dict of {str: `Bank`}) - Keyed by bank name
    * non_bank_data (bytes or None) - Content of some special events that don't use banks (e.g. begin-of-run ODB dump)
    """
    def __init__(self):
        self.all_bank_size_bytes = None
        self.flags = None
        self.banks = {}
        self.non_bank_data = None
        
    def add_bank(self, bank):
        self.banks[bank.name] = bank
        
    def get_bank(self, bank_name):
        return self.banks[bank_name]
    
    def bank_exists(self, bank_name):
        return bank_name in self.banks

        
class MidasFile:
    """
    Provides access to a midas file - either raw (.mid), gzipped (.mid.gz) or lz4 (.mid.lz4).
    
    Members:
        
    * file (file-like object)
    * event_header (`EventHeader`) - The event we've just read
    * event_body (`EventBody`) - The event we've just read
    * next_event_offset (int) - Position in file where the next event starts
    * this_event_payload_offset (int) - Sometimes we just read the event header,
        not the full data. This member is where the data of the current event starts.
    """
    def __init__(self, path):
        """
        Open a midas file.
        
        Args:
            
        * path (str) - Path to the file
        """
        self.file = None
        self.next_event_offset = 0
        self.reset_event()
        self.open(path)
        
        self.__tid_size = {midas.TID_BYTE: 1, 
                           midas.TID_SBYTE: 1,
                           midas.TID_CHAR: 1, 
                           midas.TID_WORD: 2,
                           midas.TID_SHORT: 2,
                           midas.TID_DWORD: 4,
                           midas.TID_INT: 4,
                           midas.TID_BOOL: 4,
                           midas.TID_FLOAT: 4,
                           midas.TID_DOUBLE: 8,
                           midas.TID_BITFIELD: 1,
                           midas.TID_STRING: 0,
                           midas.TID_ARRAY: 0,
                           midas.TID_STRUCT: 0,
                           midas.TID_KEY: 0,
                           midas.TID_LINK: 0}
        
        self.__tid_unpack_format = {midas.TID_BYTE: 'c', # C char / python byte
                                    midas.TID_SBYTE: 'b', # C signed char / python int
                                    midas.TID_CHAR: 'c', # C char / we'll make a python string 
                                    midas.TID_WORD: 'H', # C unsigned short / python int
                                    midas.TID_SHORT: 'h', # C signed short / python int
                                    midas.TID_DWORD: 'I', # C unsigned int / python int
                                    midas.TID_INT: 'i', # C signed int / python int
                                    midas.TID_BOOL: 'I', # C unsigned int / we'll make a list of python bools
                                    midas.TID_FLOAT: 'f', # C float / python float
                                    midas.TID_DOUBLE: 'd', # C double / python double
                                    midas.TID_BITFIELD: 'I', # C unsigned int / python int 
                                    midas.TID_STRING: None, # We just give raw bytes
                                    midas.TID_ARRAY: None, # We just give raw bytes
                                    midas.TID_STRUCT: None, # We just give raw bytes
                                    midas.TID_KEY: None, # We just give raw bytes
                                    midas.TID_LINK: None # We just give raw bytes
                                    }
        
        # Read in little-endian
        self.endian_format_flag = '<'
        
        
    def __del__(self):
        """
        Clean up file handle when we go out of scope.
        """
        if self.file:
            self.file.close()
        
    def reset_event(self):
        """
        Forget about an event we've already read (but don't rewind
        the actual file pointer).
        """
        self.event_header = EventHeader()
        self.event_body = EventBody()
        self.this_event_payload_offset = 0
        
    def open(self, path):
        """
        Open a midas file.
        
        Args:
            
        * path (str) - Path to midas file. Can be raw, gz or lz4 compressed.
        """
        self.reset_event()
        
        if path.endswith(".lz4"):
            if have_lz4:
                self.file = lz4.frame.LZ4FrameFile(path, "rb")
            else:
                raise ImportError("lz4 package not found - install using 'pip install lz4'")
        elif path.endswith(".gz"):
            self.file = gzip.open(path, "rb")
        else:
            self.file = open(path, "rb")
    
    def jump_to_start(self):
        """
        Rewind to the start of the file.
        """
        self.file.seek(0,0)
        self.next_event_offset = 0
        self.reset_event()
    
    def get_bor_odb_dump(self):
        """
        Return the begin-of-run ODB dump as a string (containing XML).
        
        We will jump to the correct place in the file automatically.
        """
        self.jump_to_start()
        
        if self.read_next_event_header() and self.event_header.is_bor_event():
            self.read_this_event_body()
            return Odb(self.event_body.non_bank_data)
        
        self.jump_to_start()
        raise RuntimeError("Unable to find BOR event")
    
    def get_eor_odb_dump(self):
        """
        Return the end-of-run ODB dump as a string (containing XML).
        
        We will jump to the correct place in the file automatically.
        """
        while True:
            if not self.read_next_event_header():
                break
        
            if self.event_header.is_eor_event():
                self.read_this_event_body()
                return Odb(self.event_body.non_bank_data)
        
        self.jump_to_start()
        raise RuntimeError("Unable to find EOR event")
    
    def read_next_event(self):
        """
        Read the header and content of the next event.
        May be slow if there is a lot of data.
        """
        if self.read_next_event_header():
            return self.read_this_event_body()
        else:
            return False
    
    def read_next_event_header(self):
        """
        Just read the header/metadata of the next event.
        If you read it and think it's interesting, you can then call
        read_this_event_body() to grab the actual data.
        If the event isn't interesting, then you saved yourself a lot
        of time by not loading a bunch of data you don't care about.
        
        Populates self.event.header only.
        """
        self.reset_event()
        this_event_offset = self.next_event_offset
        
        self.file.seek(self.next_event_offset, 0)
        header_data = self.file.read(16)
        
        if not header_data:
            return False
        
        unpacked = struct.unpack(self.endian_format_flag + "HHIII", header_data)
        self.event_header.event_id = unpacked[0]
        self.event_header.trigger_mask = unpacked[1]
        self.event_header.serial_number = unpacked[2]
        self.event_header.timestamp = unpacked[3]
        self.event_header.event_data_size_bytes = unpacked[4]
        
        self.this_event_payload_offset = this_event_offset + 4 * 4
        self.next_event_offset += self.event_header.event_data_size_bytes + 4 * 4
        
        return True
    
    def read_this_event_body(self):
        """
        Read the data of the current event (that you've already read the header info of).
        
        Populates self.event.banks or self.event.non_bank_data (depending on the event type).
        """
        self.file.seek(self.this_event_payload_offset, 0)
            
        if self.event_header.is_midas_internal_event():
            self.event_body.non_bank_data = self.file.read(self.event_header.event_data_size_bytes - 1)
        else:
            bank_header_data = self.file.read(8)
            unpacked = struct.unpack(self.endian_format_flag + "II", bank_header_data)
            self.event_body.all_bank_size_bytes = unpacked[0]
            self.event_body.flags = unpacked[1]
            
            is_bank_32 = self.event_body.flags & (1<<4)
            
            while self.file.tell() < self.next_event_offset - 4:
                tmp = self.file.tell()
                
                if is_bank_32:
                    bank_header_data = self.file.read(12)
                    unpacked = struct.unpack(self.endian_format_flag + "ccccII", bank_header_data)
                else:
                    bank_header_data = self.file.read(8)
                    unpacked = struct.unpack(self.endian_format_flag + "ccccHH", bank_header_data)
                    
                bank = Bank()
                bank.name = "".join(x.decode('utf-8') for x in unpacked[:4])
                bank.type = unpacked[4]
                bank.size_bytes = unpacked[5]
                
                if bank.type not in self.__tid_size:
                    raise ValueError("Unexpected bank type %d for name '%s'" % (bank.type, bank.name))
                
                padding = ((bank.size_bytes + 7) & ~7) - bank.size_bytes
                raw_data = self.file.read(bank.size_bytes)
                self.file.read(padding)
                
                # Convert to python types
                
                if self.__tid_size[bank.type] == 0 or self.__tid_unpack_format[bank.type] is None:
                    # No special handling - just return raw bytes.
                    bank.data = raw_data
                else:
                    fmt = self.endian_format_flag + self.__tid_unpack_format[bank.type] * (bank.size_bytes / self.__tid_size[bank.type])
                    unpacked_data = struct.unpack(fmt, raw_data)
                        
                    if bank.type == midas.TID_CHAR:
                        # Make a string
                        bank.data = "".join(unpacked_data)
                    elif bank.type == midas.TID_BOOL:
                        # Convert from 0/1 to False/True
                        bank.data = (u != 0 for u in unpacked_data)
                    else:
                        # Regular list
                        bank.data = unpacked_data
                
                self.event_body.add_bank(bank)
        
        return True

    def get_event_count(self):
        """
        Count the number of events in this file. Does not include
        the special begin-of-ruin or end-of-run events.
        """
        self.jump_to_start()
        count = 0
        
        while self.read_next_event_header():
            if self.event_header.is_midas_internal_event():
                continue
            
            count += 1
            
        return count

class Odb:
    """
    Helps read an XML representation of an ODB, and convert it to a python dict.
    
    Members:
        
    * written_time (datetime.datetime) - Time the ODB dump was written (as encoded in a comment in the dump)
    * data (dict) - The actual ODB structure
    """
    def __init__(self, odb_string = None):
        """
        Initialize an ODB object py parsing an ODB dump.
        
        Args:
            odb_string (str) - Either XML or JSON representation of an ODB dump. 
        """
        self.written_time = None 
        self.data = {}
        
        if odb_string is not None and len(odb_string) > 0:
            if odb_string[0] == "<":
                # This decode/encode is needed so that we can handle non-ascii values
                # that may be in the dump.
                self.load_from_xml_string(odb_string.decode('utf-8').encode('utf-8'))
            elif odb_string[0] == "{":
                self.load_from_json_string(odb_string)
            else:
                raise ValueError("Couldn't determine ODB dump format (first character is '%s', rather than expected '<' or '{')" % odb_string[0])
        
    def load_from_json_string(self, json_string):
        """
        """
        self.written_time = None
        self.data = midas.safe_to_json(json_string)
        
    def load_from_xml_string(self, xml_string):
        """
        Parse the XML string to populate self.data and self.written_time.
        
        Args:
            
        * xml_string (str)
        """
        self.written_time = None
        self.data = {}
        
        """
        Header looks like:
        
        <?xml version="1.0" encoding="ISO-8859-1"?>
        <!-- created by MXML on Fri Jan  4 10:51:22 2019 -->
        
        Extract the creation time.
        """
        comment_start = xml_string.find("<!--")
        
        if comment_start != -1:
            ts_start = xml_string.find(" on ", comment_start)
            ts_end = xml_string.find(" -->", comment_start)
            
            if ts_start != -1 and ts_end != -1:
                ts_str = xml_string[ts_start+4:ts_end]
                self.written_time = datetime.datetime.strptime(ts_str, "%c")
        
        """
        Now parse the actual XML.
        """
        root = ElementTree.fromstring(xml_string)
        self.handle_node(root, self.data)
        
    def text_to_value(self, text, type_str):
        """
        Type-conversion of XML node content.
        
        Args:
            
        * text (str) - Content of the node ("1.23", "135", "some string" etc)
        * type_str (str) - INT/WORD/FLOAT etc
        
        Returns:
            int/float/string etc as appropriate
        """
        if type_str in ["INT"]:
            return int(text)
        elif type_str in ["WORD", "DWORD"]:
            return "0x%x" % int(text)
        elif type_str == "BOOL":
            return text == "y"
        elif type_str in ["STRING", "LINK"]:
            return text
        elif type_str in ["DOUBLE", "FLOAT"]:
            val = float(text)
            if math.isnan(val):
                return "NaN"
            return val
        else:
            raise ValueError("Unhandled ODB type %s" % type_str)
            return 0
        
    def type_to_int(self, type_str):
        """
        Convert e.g. "INT" to "7", the midas code for TID_INT.
        
        Args:
            
        * type_str (str) INT/WORD/FLOAT etc
        
        Returns:
            int
        """
        try:
            return getattr(midas, "TID_" + type_str)
        except:
            raise ValueError("Unknown ODB type TID_%s" % type_str)
        
    def create_key_entry(self, node):
        """
        Metadata for node "X" is stored in an extra dict "X/key".
        
        Args:
            
        * node (`xml.etree.ElementTree.Element`)
        
        Returns:
            dict
        """
        type_str = node.attrib["type"]
        type_int = self.type_to_int(type_str)
        
        key_dict = {"type": type_int}
        
        if node.tag == "keyarray":
            key_dict["num_values"] = int(node.attrib["num_values"])
            
        if type_int == midas.TID_LINK:
            key_dict["link"] = node.text
            
        if type_int == midas.TID_STRING:
            key_dict["item_size"] = node.attrib["size"]
            
        return key_dict
            
    def handle_node(self, node, obj):
        """
        Called recursively to work through the whole XML tree,
        converting nodes to a nested dict.
        
        Args:
            
        * node (`xml.etree.ElementTree.Element`) - Current position in XML tree
        * obj (dict) - Object to add more elements to
        """
        for child in node:
            
            if child.tag == "dir":
                name = child.attrib["name"]
                obj[name] = {}
                self.handle_node(child, obj[name])
            elif child.tag == "keyarray":
                name = child.attrib["name"]
                type_str = child.attrib["type"]
                obj[name] = []
                obj[name + "/key"] = self.create_key_entry(child)
                
                for val_node in child:
                    val = self.text_to_value(val_node.text, type_str)
                    obj[name].append(val)
            elif child.tag == "key":
                name = child.attrib["name"]
                type_str = child.attrib["type"]
                val = self.text_to_value(child.text, type_str)
                obj[name] = val                
                obj[name + "/key"] = self.create_key_entry(child)
                
            else:
                raise ValueError("Unhandled tag %s" % child.tag)
                
