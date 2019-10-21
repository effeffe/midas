import unittest
import midas.client

"""
WARNING - these tests will start and stop runs on your experiment!

There's no way to deregister a deferred transition, so we have the
deferred transition tests in their own class.
"""

class TestDeferredTransition(unittest.TestCase):
    def setUp(self):
        self.called_start_run = False
        self.num_deferrals_called = 0
    
    def tearDown(self):
        if self.called_start_run:
            self.client.stop_run()
    
    @classmethod
    def setUpClass(cls):
        cls.client = midas.client.MidasClient("pytest")
        
    @classmethod
    def tearDownClass(cls):
        cls.client.disconnect()
                
    def transition_deferred_func(self, client, run_number):
        self.num_deferrals_called += 1
            
        return self.num_deferrals_called == 5

    def testDeferral(self):    
        state = self.client.odb_get("/Runinfo/State")
        self.assertEqual(state, midas.STATE_STOPPED, "Stop the run before running this test!")

        self.client.register_deferred_transition_callback(midas.TR_START, self.transition_deferred_func)
        
        for i in range(10):
            self.client.communicate(2)
            
            if i == 1:
                self.assertRaises(midas.TransitionDeferredError, self.client.start_run, async_flag=True)
                self.called_start_run = True
                
                state = self.client.odb_get("/Runinfo/State")
                self.assertEqual(state, midas.STATE_STOPPED)
            
            self.client.lib.c_cm_check_deferred_transition()
            
            state = self.client.odb_get("/Runinfo/State")
            
            if i < 3:
                self.assertEqual(state, midas.STATE_STOPPED)
                
            if i > 6:
                self.assertEqual(state, midas.STATE_RUNNING)
                
        
if __name__ == '__main__':
    unittest.main()