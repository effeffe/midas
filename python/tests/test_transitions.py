import unittest
import midas.client

"""
WARNING - these tests will start and stop runs on your experiment!
"""

class TestTransitions(unittest.TestCase):
    def setUp(self):
        self.called_start_run = False
    
    def tearDown(self):
        if self.called_start_run:
            self.client.stop_run()
    
    @classmethod
    def setUpClass(cls):
        cls.client = midas.client.MidasClient("pytest")
        
    @classmethod
    def tearDownClass(cls):
        cls.client.disconnect()
            
    def transition_func(self, client, run_number):
        self.callback_called = True
        self.callback_run_number = run_number
        self.callback_client = client
        
    def transition_failure_func(self, client, run_number):
        return (999, "I caused transition to fail")
    
    def testCallback(self):
        self.callback_called = False
        self.client.register_transition_callback(midas.TR_START, 300, self.transition_func)
        
        state = self.client.odb_get("/Runinfo/State")
        self.assertEqual(state, midas.STATE_STOPPED, "Stop the run before running this test!")
        
        # Sync start
        self.client.start_run()
        self.called_start_run = True
        
        state = self.client.odb_get("/Runinfo/State")
        self.assertEqual(state, midas.STATE_RUNNING)
                
        # Check the callback function was called
        self.assertTrue(self.callback_called)
        
        # Check the new value is correctly in ODB
        rdb = self.client.odb_get("/Runinfo/Run number")
        self.assertEqual(rdb, self.callback_run_number)
        
        # Check the other parameters to callback function are as expected
        self.assertIs(self.client, self.callback_client)
        
        self.client.deregister_transition_callback(midas.TR_START)

    def testAsync(self):
        state = self.client.odb_get("/Runinfo/State")
        self.assertEqual(state, midas.STATE_STOPPED, "Stop the run before running this test!")
        
        for i in range(20):
            self.client.communicate(10)
            
            if i == 1:
                self.client.start_run(async_flag=True)
                self.called_start_run = True
                
            state = self.client.odb_get("/Runinfo/State")
            
            if state == midas.STATE_RUNNING:
                break
        
        state = self.client.odb_get("/Runinfo/State")
        self.assertEqual(state, midas.STATE_RUNNING)
        
    def testTransitionFailure(self):
        self.client.register_transition_callback(midas.TR_START, 400, self.transition_failure_func)
        
        state = self.client.odb_get("/Runinfo/State")
        self.assertEqual(state, midas.STATE_STOPPED, "Stop the run before running this test!")
        
        with self.assertRaises(midas.TransitionFailedError) as cm:
            self.client.start_run()
            
        self.assertEqual(cm.exception.code, 999)
        
        self.client.deregister_transition_callback(midas.TR_START)
        
if __name__ == '__main__':
    unittest.main()