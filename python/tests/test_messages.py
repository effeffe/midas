import unittest
from dataclasses import dataclass
import midas.client
import midas
import datetime
import os
import time

"""
Test sending/receiving messages in the message log.

Note this will temporarily create a new "pytest_msg_facility.log" message file.
It should be automatically deleted at the end of the tests. If not, feel free
to delete it manually.
"""

@dataclass
class MsgDetails:
    msg: str
    timestamp: datetime.datetime
    msg_type: int

class TestMessages(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.client = midas.client.MidasClient("pytest_msg")
        cls.facility_name = "pytest_msg_facility"
        
    @classmethod
    def tearDownClass(cls):
        cls.client.deregister_message_callback()
        cls.tidy_up_logfile()
        cls.client.disconnect()

    @classmethod
    def tidy_up_logfile(cls):
        """
        Search in various locations where the message logs might be.
        If we find a file called "pytest_msg_facility.log", delete it.
        """
        if cls.facility_name == "midas":
            # Sanity-check in case user changes the test to write
            # to the midas message log - don't delete midas.log!
            return

        search_dirs = []

        try:
            search_dirs.append(cls.client.odb_get("/Logger/Message dir"))
        except:
            pass

        try:
            search_dirs.append(cls.client.odb_get("/Logger/Data dir"))
        except:
            pass

        search_dirs.append(os.getcwd())
        filename = "%s.log" % cls.facility_name

        for d in search_dirs:
            path = os.path.join(d, filename)

            if os.path.exists(path):
                os.remove(path)
                print("Tidied up log file %s" % path)

    def setUp(self):
        """
        We'll store all the messages received by our callback
        function in this list of MsgDetails objects.
        """
        self.callback_messages = []

    def msg_callback(self, client, msg, timestamp, msg_type):
        """
        Callback function that gets registered with midas.
        """
        self.callback_messages.append(MsgDetails(msg, timestamp, msg_type))

    def filter_received_messages(self):
        """
        Return only the messages that were sent by these tests. There could
        be other messages if the user is running other clients at the same time,
        which we need to ignore when validating the message contents.

        Returns:
            list of str
        """
        return [m for m in self.callback_messages if m.msg.startswith("[pytest_msg,")]

    def send_messages(self, num_info=1, num_error=1):
        """
        Send some messages, separated in time by a little bit.
        Also call communicate() so our callback function can be run.
        """
        for n in range(num_info):
            self.client.msg("Test info message", is_error=False, facility=self.facility_name)

        for i in range(10):
            self.client.communicate(1)

            if i == 3:
                for n in range(num_error):
                    self.client.msg("Test error message", is_error=True, facility=self.facility_name)

    def testMessageCallbackReadInfoAndError(self):
        self.client.register_message_callback(self.msg_callback)
        self.send_messages()
        msgs = self.filter_received_messages()
        self.assertEqual(len(msgs), 2)
        self.assertTrue(msgs[0].msg.endswith("Test info message"))
        self.assertEqual(msgs[0].msg_type, midas.MT_INFO)

        self.assertTrue(msgs[1].msg.endswith("Test error message"))
        self.assertEqual(msgs[1].msg_type, midas.MT_ERROR)

        # Timestamp of 2nd message >= timestamp of first message
        self.assertGreaterEqual(msgs[1].timestamp, msgs[0].timestamp)

    def testMessageCallbackReadOnlyError(self):
        self.client.register_message_callback(self.msg_callback, midas.MT_ERROR)
        self.send_messages()
        msgs = self.filter_received_messages()
        self.assertEqual(len(msgs), 1)
        self.assertTrue(msgs[0].msg.endswith("Test error message"))
        self.assertEqual(msgs[0].msg_type, midas.MT_ERROR)

    def testMessageCallbackReadOnlyInfo(self):
        self.client.register_message_callback(self.msg_callback, [midas.MT_INFO])
        self.send_messages()
        msgs = self.filter_received_messages()
        self.assertEqual(len(msgs), 1)
        self.assertTrue(msgs[0].msg.endswith("Test info message"))
        self.assertEqual(msgs[0].msg_type, midas.MT_INFO)

    def testMessageCallbackReadOnlyInfoMultipleMessages(self):
        self.client.register_message_callback(self.msg_callback, [midas.MT_INFO])

        num_info = 3
        self.send_messages(num_info)
        msgs = self.filter_received_messages()
        self.assertEqual(len(msgs), num_info)

        for i in range(num_info):
            self.assertTrue(msgs[i].msg.endswith("Test info message"))
            self.assertEqual(msgs[i].msg_type, midas.MT_INFO)

    def testMessageCallbackDeregistration(self):
        # Run the actual test
        self.client.register_message_callback(self.msg_callback, [midas.MT_INFO])
        self.client.deregister_message_callback()

        self.send_messages()
        msgs = self.filter_received_messages()
        self.assertEqual(len(msgs), 0)

        # Now flush the message buffer so future tests don't also
        # see the messages we just sent.
        self.client.register_message_callback(self.msg_callback, [midas.MT_INFO])
        self.client.communicate(1)

    def testRecentMessages(self):
        self.client.msg("Different info message", is_error=False, facility=self.facility_name)
        self.client.msg("Different error message", is_error=True, facility=self.facility_name)

        inbetween_time = datetime.datetime.now()

        # `get_recent_messages()`` uses 1s resolution when searching for older messages,
        # so make sure we sleep for at least 1s before adding another message.
        time.sleep(1.1)

        self.client.msg("Really different message", is_error=True, facility=self.facility_name)

        # Should get the "different" but not "really different" messages
        # Most recent message returned first
        msgs = self.client.get_recent_messages(2, before=inbetween_time, facility=self.facility_name)

        self.assertGreaterEqual(len(msgs), 2)
        self.assertTrue(msgs[0].endswith("Different error message"))
        self.assertTrue(msgs[1].endswith("Different info message"))
        self.assertGreaterEqual(msgs[0].find(",ERROR]"), 0)
        self.assertGreaterEqual(msgs[1].find(",INFO]"), 0)

        # Should get the "different" AND "really different" messages
        msgs = self.client.get_recent_messages(3, before=None, facility=self.facility_name)

        self.assertGreaterEqual(len(msgs), 3)
        self.assertTrue(msgs[0].endswith("Really different message"))
        self.assertTrue(msgs[1].endswith("Different error message"))
        self.assertTrue(msgs[2].endswith("Different info message"))
        self.assertGreaterEqual(msgs[0].find(",ERROR]"), 0)
        self.assertGreaterEqual(msgs[1].find(",ERROR]"), 0)
        self.assertGreaterEqual(msgs[2].find(",INFO]"), 0)



if __name__ == '__main__':
    unittest.main()