import unittest
import midas.client
import multiprocessing
import subprocess
import midas.utils.helper_test_rpc
import os
import os.path
import sys

"""
This test is a little painful as we only allow one midas client per process
(and we don't even allow multiple connections using multiprocess). So we create
a client here that registers an RPC handler, and then launch a different script
to call the JRPC function.
"""

def rpc_call_helper(queue):
    helper = midas.utils.helper_test_rpc.__file__
    subprocess.run([sys.executable, helper])
    output = midas.utils.helper_test_rpc.file_path
    
    if not os.path.exists(output):
        queue.put("FILE DOESN'T EXIST")
    else:
        with open(output) as f:
            queue.put(f.readlines())

class TestRpc(unittest.TestCase):
    def setUp(self):
        self.client = None
        
    def tearDown(self):
        if self.client:
            self.client.disconnect()
            
        output = midas.utils.helper_test_rpc.file_path
        if os.path.exists(output):
            os.unlink(output)
          
    def rpc_callback(self, client, cmd, args, max_len):
        return (1, midas.utils.helper_test_rpc.rpc_retstr(cmd, args, max_len))
          
    def testJRPC(self):
        cmd = "ABC"
        args = "DEF GHI"
        max_len = 100
        expected = midas.utils.helper_test_rpc.rpc_retstr(cmd, args, max_len)
        
        # Register our function
        self.client = midas.client.MidasClient("pytest")
        self.client.register_jrpc_callback(self.rpc_callback)
        self.client.communicate(10)
        
        # Call our function from a separate process
        queue = multiprocessing.Queue()
        spawned = multiprocessing.Process(target=rpc_call_helper, args=(queue,))
        spawned.start()

        # Wait for things to complete
        for i in range(30):
            self.client.communicate(100)
            if not queue.empty():
                break
            
        spawned.join()
        
        self.assertFalse(queue.empty())
        self.assertEqual(queue.get()[0], expected)        
          
if __name__ == '__main__':
    unittest.main()