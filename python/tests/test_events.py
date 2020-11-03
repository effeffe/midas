import unittest
import midas.client
import midas.event

try:
    import numpy as np
    have_numpy = True
except ImportError:
    have_numpy = False

class TestEvents(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.client = midas.client.MidasClient("pytest")
        
    @classmethod
    def tearDownClass(cls):
        cls.client.disconnect()
          
    def testBank16(self):
        event = midas.event.Event(bank32=False, align64=False)
        self._testBuffers(event, 8)
        
    def testBank16Align64(self):
        self.assertRaises(ValueError, midas.event.Event, bank32=False, align64=True)
        
    def testBank32(self):
        event = midas.event.Event(bank32=True, align64=False)
        self._testBuffers(event, 12)
        
    def testBank32Align64(self):
        event = midas.event.Event(bank32=True, align64=True)
        self._testBuffers(event, 16)
          
    def _testBuffers(self, event, expected_bank_header_size_bytes):
        event.header.event_id = 1
        event.header.trigger_mask = 0
        event.create_bank("BYTE", midas.TID_BYTE, b"abcdefg")
        event.create_bank("BNUL", midas.TID_BYTE, b"abc\x00def\x00g")
        event.create_bank("SBYT", midas.TID_SBYTE, [-1,2,-3,4])
        event.create_bank("CHAR", midas.TID_CHAR, bytearray("abc123", "ascii"))
        event.create_bank("CNUL", midas.TID_CHAR, bytearray("abc\x00123", "ascii"))
        event.create_bank("WORD", midas.TID_WORD, [1,2,3,4])
        event.create_bank("SHOR", midas.TID_SHORT, [-1,2,-3,4])
        event.create_bank("DWOR", midas.TID_DWORD, [1,2,3,4])
        event.create_bank("SINT", midas.TID_INT, [-1,2,-3,4])
        event.create_bank("QWOR", midas.TID_QWORD, [1,2,3,4])
        event.create_bank("IN64", midas.TID_INT64, [-1,2,-3,4])
        event.create_bank("BOOL", midas.TID_BOOL, [True, False, True, False])
        event.create_bank("FLOA", midas.TID_FLOAT, [-1,2,-3,4])
        event.create_bank("DOUB", midas.TID_DOUBLE, [-1,2,-3,4])
        event.create_bank("BITF", midas.TID_BITFIELD, [1,2,3,4])
        
        if have_numpy:
            event.create_bank("NPSB", midas.TID_SBYTE, np.array([-1,2,-3,4], np.int8))
            event.create_bank("NPWD", midas.TID_WORD, np.array([1,2,3,4], np.uint16))
            event.create_bank("NPSH", midas.TID_SHORT, np.array([-1,2,-3,4], np.int16))
            event.create_bank("NPDW", midas.TID_DWORD, np.array([1,2,3,4], np.uint32))
            event.create_bank("NPSI", midas.TID_INT, np.array([-1,2,-3,4], np.int32))
            event.create_bank("NPQW", midas.TID_QWORD, np.array([1,2,3,4], np.uint64))
            event.create_bank("NPSQ", midas.TID_INT64, np.array([-1,2,-3,4], np.int64))
            event.create_bank("NPBL", midas.TID_BOOL, np.array([True, False, True, False]))
            event.create_bank("NPFL", midas.TID_FLOAT, np.array([-1,2,-3,4], np.float32))
            event.create_bank("NPDB", midas.TID_DOUBLE, np.array([-1,2,-3,4], np.float64))
            event.create_bank("NPBF", midas.TID_BITFIELD, np.array([1,2,3,4], np.uint32))
        
        buffer_handle = self.client.open_event_buffer("PYTESTBUF")
        
        req_id = self.client.register_event_request(buffer_handle)
        
        for serial, use_numpy in enumerate([False, True]):
            if use_numpy and not have_numpy:
                continue
            
            # Send an event
            event.header.serial_number = serial
            self.client.send_event(buffer_handle, event)
            
            # Receive the event
            for i in range(10):
                recv_event = self.client.receive_event(buffer_handle, use_numpy=use_numpy)
                
                if recv_event is None:
                    self.client.communicate(10)
                else:
                    break
            
            self.assertEqual(recv_event.get_bank_header_size(), expected_bank_header_size_bytes)
            self.assertEqual(event.header.serial_number, recv_event.header.serial_number)
            self.assertEqual(len(event.banks), len(recv_event.banks))
            
            for name, bank in event.banks.items():
                if use_numpy:
                    self.assertIsInstance(recv_event.banks[name].data, np.ndarray)
                else:
                    self.assertIsInstance(recv_event.banks[name].data, tuple)
                
                if bank.type in [midas.TID_FLOAT, midas.TID_DOUBLE]:
                    sent = bank.data
                    recv = recv_event.banks[name].data
                    self.assertEqual(len(sent), len(recv))
                    
                    for i in range(len(sent)):
                        self.assertAlmostEqual(sent[i], recv[i], places=5)
                else:
                    self.assertEqual(tuple(bank.data), tuple(recv_event.banks[name].data), name)
        
          
if __name__ == '__main__':
    unittest.main()