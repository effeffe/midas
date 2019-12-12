import unittest
import midas.client
import midas.event

class TestEvents(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.client = midas.client.MidasClient("pytest")
        
    @classmethod
    def tearDownClass(cls):
        cls.client.disconnect()
          
    def testBuffers(self):
        event = midas.event.Event()
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
        event.create_bank("BOOL", midas.TID_BOOL, [True, False, True, False])
        event.create_bank("FLOA", midas.TID_FLOAT, [-1,2,-3,4])
        event.create_bank("DOUB", midas.TID_DOUBLE, [-1,2,-3,4])
        event.create_bank("BITF", midas.TID_BITFIELD, [1,2,3,4])
        
        buffer_handle = self.client.open_event_buffer("PYTESTBUF")
        
        req_id = self.client.register_event_request(buffer_handle)
        
        # Send an event
        event.header.serial_number = 1
        self.client.send_event(buffer_handle, event)
        
        # Receive the event
        for i in range(10):
            recv_event = self.client.receive_event(buffer_handle)
            
            if recv_event is None:
                self.client.communicate(10)
            else:
                break
        
        self.assertEqual(event.header.serial_number, recv_event.header.serial_number)
        self.assertEqual(len(event.banks), len(recv_event.banks))
        
        for name, bank in event.banks.items():
            # We provided lists, but get tuples back
            if bank.type in [midas.TID_FLOAT, midas.TID_DOUBLE]:
                sent = bank.data
                recv = recv_event.banks[name].data
                self.assertEqual(len(sent), len(recv))
                
                for i in range(len(sent)):
                    self.assertAlmostEqual(sent[i], recv[i], places=5)
            else:
                self.assertEqual(tuple(bank.data), recv_event.banks[name].data)
        
          
if __name__ == '__main__':
    unittest.main()