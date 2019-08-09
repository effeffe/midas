"""
This module contains python tools related to midas.

This file contains definitions that come from midas.h
"""

STATE_STOPPED = 1      # MIDAS run stopped
STATE_PAUSED  = 2      # MIDAS run paused 
STATE_RUNNING = 3      # MIDAS run running

# Alarm error codes
AL_SUCCESS      = 1
AL_INVALID_NAME = 1002
AL_ERROR_ODB    = 1003
AL_RESET        = 1004
AL_TRIGGERED    = 1005

# Message types
MT_ERROR = 1
MT_INFO  = 2

# Data types definitions                       min      max    
TID_BYTE     =  1       # unsigned byte         0       255    
TID_SBYTE    =  2       # signed byte         -128      127    
TID_CHAR     =  3       # single character      0       255    
TID_WORD     =  4       # two bytes             0      65535   
TID_SHORT    =  5       # signed word        -32768    32767   
TID_DWORD    =  6       # four bytes            0      2^32-1  
TID_INT      =  7       # signed dword        -2^31    2^31-1  
TID_BOOL     =  8       # four bytes bool       0        1     
TID_FLOAT    =  9       # 4 Byte float format                  
TID_DOUBLE   = 10       # 8 Byte float format                  
TID_BITFIELD = 11       # 32 Bits Bitfield      0     111... (32) 
TID_STRING   = 12       # null-terminated string               
TID_ARRAY    = 13       # array with unknown contents          
TID_STRUCT   = 14       # structure with fixed length          
TID_KEY      = 15       # key in online database               
TID_LINK     = 16       # link in online database              

# Alarm type definitions
AT_INTERNAL  = 1
AT_PROGRAM   = 2
AT_EVALUATED = 3
AT_PERIODIC  = 4

# Run transitions
TR_START  = 1
TR_STOP   = 2
TR_PAUSE  = 4
TR_RESUME = 8

def safe_to_json(input_str):
    """
    Convert input_str to a json structure, with arguments that will catch
    bad bytes.

    Args:

    * input_str (str)

    Returns:
        dict
    """
    return json.loads(input_str.decode("utf-8", "ignore"), strict=False)