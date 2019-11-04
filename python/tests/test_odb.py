import unittest
import ctypes
import midas.client
import collections

class TestOdb(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.client = midas.client.MidasClient("pytest")
        cls.client.odb_set("/pytest", {})
        
    @classmethod
    def tearDownClass(cls):
        cls.client.odb_delete("/pytest")
        cls.client.disconnect()

    def assert_equal(self, val1, val2, midas_type=None):
        if midas_type == midas.TID_STRING:
            if isinstance(val1, ctypes.Array):
                val1 = val1.value.decode("ascii")
            elif isinstance(val1, bytes):
                # List element was python bytes
                val1 = val1.decode("ascii")
            
        if midas_type in [midas.TID_FLOAT, midas.TID_DOUBLE]:
            self.assertAlmostEqual(val1, val2, places=5)
        else:
            self.assertEqual(val1, val2)

    def validate_readback(self, value, retval, expected_key_type):
        if isinstance(value, list) or isinstance(value, tuple) or isinstance(value, ctypes.Array):
            if expected_key_type == midas.TID_STRING:
                # Compare as strings
                if isinstance(value, ctypes.Array):
                    # User provided a ctypes string buffer
                    self.assert_equal(value.value.decode("ascii"), retval, expected_key_type)
                else:
                    # We have a list of objects
                    self.assert_equal(len(value), len(retval))
                        
                    for i, val in enumerate(value):
                        self.assert_equal(val, retval[i], expected_key_type)
            else:
                self.assert_equal(len(value), len(retval))
                
                for i, val in enumerate(value):
                    if isinstance(val, ctypes._SimpleCData):
                        self.assert_equal(val.value, retval[i], expected_key_type)
                    else:
                        self.assert_equal(val, retval[i], expected_key_type)
        elif isinstance(value, ctypes._SimpleCData):
            self.assert_equal(value.value, retval, expected_key_type)
        elif isinstance(value, dict):
            self.assert_equal(len(value), len(retval))
            
            for k, v in value.items():
                if isinstance(v, dict):
                    # We don't do well at validating strings in dicts...
                    self.validate_readback(v, retval[k], midas.TID_KEY)
                elif isinstance(v, ctypes._SimpleCData):
                    self.assert_equal(v.value, retval[k])
                else:
                    self.assert_equal(v, retval[k])
        else:
            self.assert_equal(value, retval, expected_key_type)
            
            
    def set_and_readback(self, path, value, expected_key_type):
        self.client.odb_set(path, value)
        retval = self.client.odb_get(path)
        
        midas_type = self.client._odb_get_type(path)
        assert(midas_type == expected_key_type)
        
        self.validate_readback(value, retval, expected_key_type)
        
    def set_and_readback_from_parent_dir(self, parent_path, key_name, value, expected_key_type):
        child_path = parent_path + "/" + key_name
        self.client.odb_set(child_path, value)
        retval = self.client.odb_get(parent_path)
        
        midas_type = self.client._odb_get_type(child_path)
        assert(midas_type == expected_key_type)
        
        self.validate_readback(value, retval[key_name], expected_key_type)
        
        
    def testBool(self):
        self.set_and_readback("/pytest/bool", True, midas.TID_BOOL)
        self.set_and_readback("/pytest/bool", False, midas.TID_BOOL)
        self.set_and_readback("/pytest/bool", [True, False], midas.TID_BOOL)
        self.set_and_readback("/pytest/bool", ctypes.c_bool(0), midas.TID_BOOL)
        self.set_and_readback("/pytest/bool", [ctypes.c_bool(0), ctypes.c_bool(1), ctypes.c_bool(1)], midas.TID_BOOL)
        self.set_and_readback("/pytest/bool", (ctypes.c_bool * 2)(*[True, False]), midas.TID_BOOL)
        
        self.set_and_readback_from_parent_dir("/pytest", "bool2", [False, True], midas.TID_BOOL)
        self.set_and_readback_from_parent_dir("/pytest", "bool2", True, midas.TID_BOOL)
        self.set_and_readback_from_parent_dir("/pytest", "bool2", False, midas.TID_BOOL)
        
    def testInt(self):
        self.set_and_readback("/pytest/int", -1, midas.TID_INT)
        self.set_and_readback("/pytest/int", 1, midas.TID_INT)
        self.set_and_readback("/pytest/int", [123, 4], midas.TID_INT)
        self.set_and_readback("/pytest/int", [ctypes.c_int32(-23), ctypes.c_int32(147)], midas.TID_INT)
        self.set_and_readback("/pytest/int", ctypes.c_int32(135), midas.TID_INT)
        self.set_and_readback("/pytest/int", (ctypes.c_int32 * 4)(*[1,2,3,4]), midas.TID_INT)
        
        self.set_and_readback_from_parent_dir("/pytest", "int2", [123, 4], midas.TID_INT)
        self.set_and_readback_from_parent_dir("/pytest", "int2", 37, midas.TID_INT)
        
    def testDword(self):
        self.set_and_readback("/pytest/uint", [ctypes.c_uint32(789), ctypes.c_uint32(135)], midas.TID_DWORD)
        self.set_and_readback("/pytest/uint", ctypes.c_uint32(123), midas.TID_DWORD)
        self.set_and_readback("/pytest/uint", ctypes.c_uint32(456), midas.TID_DWORD)
        self.set_and_readback("/pytest/uint", (ctypes.c_uint32 * 5)(*[1,2,3,4,5]), midas.TID_DWORD)
        
        self.set_and_readback_from_parent_dir("/pytest", "uint2", [ctypes.c_uint32(789), ctypes.c_uint32(135)], midas.TID_DWORD)
        self.set_and_readback_from_parent_dir("/pytest", "uint2", ctypes.c_uint32(456), midas.TID_DWORD)
        
    def testFloat(self):
        self.set_and_readback("/pytest/float", [ctypes.c_float(1.23), ctypes.c_float(3.45)], midas.TID_FLOAT)
        self.set_and_readback("/pytest/float", 4.56, midas.TID_FLOAT)
        self.set_and_readback("/pytest/float", [9.81, 4.34], midas.TID_FLOAT)
        self.set_and_readback("/pytest/float", (1.1, 2.2), midas.TID_FLOAT)
        self.set_and_readback("/pytest/float", ctypes.c_float(1.23), midas.TID_FLOAT)
        self.set_and_readback("/pytest/float", (ctypes.c_float * 3)(*[1.1,2.2,3.3]), midas.TID_FLOAT)
        
        self.set_and_readback_from_parent_dir("/pytest", "float2", [ctypes.c_float(1.23), ctypes.c_float(3.45)], midas.TID_FLOAT)
        self.set_and_readback_from_parent_dir("/pytest", "float2", ctypes.c_float(3.5), midas.TID_FLOAT)
        
    def testDouble(self):
        self.set_and_readback("/pytest/double", (ctypes.c_double(9.11), ctypes.c_double(3.14)), midas.TID_DOUBLE)
        self.set_and_readback("/pytest/double", [ctypes.c_double(1.23), ctypes.c_double(3.45)], midas.TID_DOUBLE)
        self.set_and_readback("/pytest/double", ctypes.c_double(1.23), midas.TID_DOUBLE)
        self.set_and_readback("/pytest/double", (ctypes.c_double * 3)(*[1.1,2.2,3.3]), midas.TID_DOUBLE)
        
        self.set_and_readback_from_parent_dir("/pytest", "double2", (ctypes.c_double(9.11), ctypes.c_double(3.14)), midas.TID_DOUBLE)
        self.set_and_readback_from_parent_dir("/pytest", "double2", ctypes.c_double(3.14), midas.TID_DOUBLE)
        
    def testWord(self):
        self.set_and_readback("/pytest/word", [ctypes.c_uint16(789), ctypes.c_uint16(135)], midas.TID_WORD)
        self.set_and_readback("/pytest/word", ctypes.c_uint16(123), midas.TID_WORD)
        self.set_and_readback("/pytest/word", ctypes.c_uint16(456), midas.TID_WORD)
        self.set_and_readback("/pytest/word", (ctypes.c_uint16 * 5)(*[1,2,3,4,5]), midas.TID_WORD)
        
        self.set_and_readback_from_parent_dir("/pytest", "word2", [ctypes.c_uint16(789), ctypes.c_uint16(135)], midas.TID_WORD)
        self.set_and_readback_from_parent_dir("/pytest", "word2", ctypes.c_uint16(135), midas.TID_WORD)
        
    def testShort(self):
        self.set_and_readback("/pytest/short", [ctypes.c_int16(789), ctypes.c_int16(-135)], midas.TID_SHORT)
        self.set_and_readback("/pytest/short", ctypes.c_int16(-123), midas.TID_SHORT)
        self.set_and_readback("/pytest/short", ctypes.c_int16(456), midas.TID_SHORT)
        self.set_and_readback("/pytest/short", (ctypes.c_int16 * 5)(*[1,2,3,4,5]), midas.TID_SHORT)
        
        single_element = self.client.odb_get("/pytest/short[2]")
        self.assert_equal(single_element, 3, midas.TID_SHORT)
        
        self.set_and_readback_from_parent_dir("/pytest", "short2", [ctypes.c_int16(789), ctypes.c_int16(-135)], midas.TID_SHORT)
        self.set_and_readback_from_parent_dir("/pytest", "short2", ctypes.c_int16(135), midas.TID_SHORT)
        
    def testByte(self):    
        self.set_and_readback("/pytest/byte", [ctypes.c_uint8(255), ctypes.c_ubyte(4)], midas.TID_BYTE)
        self.set_and_readback("/pytest/byte", ctypes.c_ubyte(123), midas.TID_BYTE)
        self.set_and_readback("/pytest/byte", (ctypes.c_uint8 * 3)(*[1,2,3]), midas.TID_BYTE)
        
        self.set_and_readback_from_parent_dir("/pytest", "byte2", [ctypes.c_uint8(255), ctypes.c_ubyte(4)], midas.TID_BYTE)
        self.set_and_readback_from_parent_dir("/pytest", "byte2", ctypes.c_uint8(57), midas.TID_BYTE)
        
    def testSByte(self):    
        self.set_and_readback("/pytest/sbyte", [ctypes.c_int8(-12), ctypes.c_byte(4)], midas.TID_SBYTE)
        self.set_and_readback("/pytest/sbyte", ctypes.c_byte(123), midas.TID_SBYTE)
        self.set_and_readback("/pytest/sbyte", (ctypes.c_int8 * 3)(*[1,2,3]), midas.TID_SBYTE)
        
        self.set_and_readback_from_parent_dir("/pytest", "sbyte2", [ctypes.c_int8(-12), ctypes.c_byte(4)], midas.TID_SBYTE)
        self.set_and_readback_from_parent_dir("/pytest", "sbyte2", ctypes.c_byte(13), midas.TID_SBYTE)
        
    def testString(self):    
        self.set_and_readback("/pytest/str", "Hello!", midas.TID_STRING)
        self.set_and_readback("/pytest/str", ["Hello", "World!!!!!!!"], midas.TID_STRING)
        self.set_and_readback("/pytest/str", ctypes.create_string_buffer(b"From Ctypes", 32), midas.TID_STRING)
        self.set_and_readback("/pytest/str", [ctypes.create_string_buffer(b"From Ctypes", 32), ctypes.create_string_buffer(b"... and a list", 16)], midas.TID_STRING)
        self.set_and_readback("/pytest/str", "123456789012345678901234567901234567890", midas.TID_STRING)
        self.set_and_readback("/pytest/str", b"Hello!", midas.TID_STRING)
        self.set_and_readback("/pytest/str", ["Hello", b"World!!!!!!!", ctypes.create_string_buffer(b"From Ctypes", 14)], midas.TID_STRING)
    
        single_value = self.client.odb_get("/pytest/str[1]")
        self.assert_equal(single_value, "World!!!!!!!", midas.TID_STRING)
        
        self.client.odb_set("/pytest/str[1]", "new value")
        single_value = self.client.odb_get("/pytest/str[0]")
        self.assert_equal(single_value, "Hello", midas.TID_STRING)
        single_value = self.client.odb_get("/pytest/str[1]")
        self.assert_equal(single_value, "new value", midas.TID_STRING)
        single_value = self.client.odb_get("/pytest/str[2]")
        self.assert_equal(single_value, "From Ctypes", midas.TID_STRING)
        
        self.set_and_readback_from_parent_dir("/pytest", "str2", ["Hello", "World!!!!!!!"], midas.TID_STRING)
        self.set_and_readback_from_parent_dir("/pytest", "str2", "Longer string..................", midas.TID_STRING)
    
    def testKey(self):
        self.set_and_readback("/pytest/key", {}, midas.TID_KEY)
        self.set_and_readback("/pytest/key", {"int": 1, "float": 2}, midas.TID_KEY)
        self.set_and_readback("/pytest/key", {"int": 1, "float": 2, "nested": {"bool": True}}, midas.TID_KEY)
        
        retval = self.client.odb_get("/pytest/key", recurse_dir=False)
        self.assertEqual(retval["nested"], {})
        
        self.set_and_readback("/pytest/key", {"uint": ctypes.c_uint32(15)}, midas.TID_KEY)

        # Set an ordered dict
        od = collections.OrderedDict([("a", 4), ("c", 5), ("b", 6)])
        self.set_and_readback("/pytest/key", od, midas.TID_KEY)
        set_key_order = [k for k in od.keys()]
        read_key_order = [k for k in self.client.odb_get("/pytest/key").keys()]
        self.assertEqual(len(set_key_order), 3)
        self.assertEqual(len(set_key_order), len(read_key_order))
        self.assertEqual(set_key_order[0], read_key_order[0])
        self.assertEqual(set_key_order[1], read_key_order[1])
        self.assertEqual(set_key_order[2], read_key_order[2])

        # Add another value using an ordered dict - should appear
        # in the correct place afterwards
        od = collections.OrderedDict([("a", 4), ("d", 7), ("c", 5), ("b", 6)])
        self.set_and_readback("/pytest/key", od, midas.TID_KEY)
        set_key_order = [k for k in od.keys()]
        read_key_order = [k for k in self.client.odb_get("/pytest/key").keys()]
        self.assertEqual(len(set_key_order), 4)
        self.assertEqual(len(set_key_order), len(read_key_order))
        self.assertEqual(set_key_order[0], read_key_order[0])
        self.assertEqual(set_key_order[1], read_key_order[1])
        self.assertEqual(set_key_order[2], read_key_order[2])
        self.assertEqual(set_key_order[3], read_key_order[3])
        
    def testCreateParents(self):
        self.set_and_readback("/pytest/deep/link/needed/str", "Hello!", midas.TID_STRING)
    
    def testSingleIndex(self):
        self.set_and_readback("/pytest/int_si", [1, 2], midas.TID_INT)
        self.client.odb_set("/pytest/int_si[1]", 3)
        self.assert_equal(self.client.odb_get("/pytest/int_si[1]"), 3, midas.TID_INT)
        self.client.odb_set("/pytest/int_si[4]", 5)
        self.assert_equal(self.client.odb_get("/pytest/int_si[4]"), 5, midas.TID_INT)
        self.assert_equal(self.client.odb_get("/pytest/int_si"), [1,3,0,0,5], midas.TID_INT)

        self.set_and_readback("/pytest/str_si", ["Hello", "World"], midas.TID_STRING)
        self.client.odb_set("/pytest/str_si[1]", "Changed")
        self.assert_equal(self.client.odb_get("/pytest/str_si[1]"), "Changed", midas.TID_STRING)
        self.client.odb_set("/pytest/str_si[4]", "?")
        self.assert_equal(self.client.odb_get("/pytest/str_si[4]"), "?", midas.TID_STRING)
        self.assert_equal(self.client.odb_get("/pytest/str_si"), ["Hello", "Changed", "", "", "?"], midas.TID_STRING)
    
    def testUpdateStructureOnly(self):
        struc = collections.OrderedDict([("int", 1), ("float", 2), ("nested", collections.OrderedDict([("bool", True)]))])
        self.set_and_readback("/pytest/structonly", struc, midas.TID_KEY)
        
        new_struc = struc.copy()
        new_struc["addition"] = 4.5
        new_struc["int"] = 3
        new_struc["nested"]["bool"] = False
        new_struc["nested"]["nested_add"] = "hello"
        del new_struc["float"]
        
        self.client.odb_set("/pytest/structonly", new_struc, update_structure_only=True)
        retval = self.client.odb_get("/pytest/structonly")
        
        self.assertNotIn("float", retval)
        self.assert_equal(retval["int"], 1, midas.TID_INT)
        self.assert_equal(retval["addition"], 4.5, midas.TID_DOUBLE)
        self.assert_equal(retval["nested"]["bool"], True, midas.TID_BOOL)
        self.assert_equal(retval["nested"]["nested_add"], "hello", midas.TID_STRING)
        
    def hotlink_func(self, client, path, odb_value):
        self.seen_hotlink = True
        self.odb_watched_value = odb_value
        self.odb_watched_path = path
        self.odb_watched_client = client
    
    def testWatch(self):
        self.seen_hotlink = False
        self.client.odb_set("/pytest/watch", 1.23)
        self.client.odb_watch("/pytest/watch", self.hotlink_func)
        
        new_value = 3.45
        
        for i in range(4):
            self.client.communicate(10)
            
            if i == 1:
                self.client.odb_set("/pytest/watch", new_value)
                
        # Check the callback function was called
        self.assertTrue(self.seen_hotlink)
        
        # Check the new value was passed to callback
        self.assert_equal(self.odb_watched_value, new_value)
        
        # Check the new value is correctly in ODB
        rdb = self.client.odb_get("/pytest/watch")
        self.assert_equal(rdb, new_value)
        
        # Check the other parameters to callback function are as expected
        self.assertEqual("/pytest/watch", self.odb_watched_path)
        self.assertIs(self.client, self.odb_watched_client)
        
    def testWatchDir(self):
        self.seen_hotlink = False
        self.client.odb_set("/pytest/watchdir", {"a": 1, "b":2})
        self.client.odb_watch("/pytest/watchdir", self.hotlink_func)
        
        changed_path = "/pytest/watchdir/a"
        new_value = 3
        
        for i in range(4):
            self.client.communicate(10)
            
            if i == 1:
                self.client.odb_set(changed_path, new_value)
                
        # Check the callback function was called
        self.assertTrue(self.seen_hotlink)
        
        # Check the new value was passed to callback
        self.assert_equal(self.odb_watched_value["a"], new_value)
        
        # Check the new value is correctly in ODB
        rdb = self.client.odb_get(changed_path)
        self.assert_equal(rdb, new_value)
        
        # Check the other parameters to callback function are as expected
        self.assertEqual("/pytest/watchdir", self.odb_watched_path)
        self.assertIs(self.client, self.odb_watched_client)
        
    def testLink(self):
        self.client.odb_set("/pytest/link_dest_arr", [1,2,3], midas.TID_INT)
        self.client.odb_set("/pytest/link_dest", 4, midas.TID_INT)
        self.client.odb_link("/pytest/link_src_arr", "/pytest/link_dest_arr")
        self.client.odb_link("/pytest/link_src", "/pytest/link_dest")
        
        link_dest_arr = self.client.odb_get_link_destination("/pytest/link_src_arr")
        link_dest = self.client.odb_get_link_destination("/pytest/link_src")
        self.assertEqual(link_dest_arr, "/pytest/link_dest_arr")
        self.assertEqual(link_dest, "/pytest/link_dest")
        
        rdb_arr = self.client.odb_get("/pytest/link_src_arr")
        rdb = self.client.odb_get("/pytest/link_src")
        self.assert_equal(rdb_arr, [1,2,3], midas.TID_INT)
        self.assert_equal(rdb, 4, midas.TID_INT)
        
if __name__ == '__main__':
    unittest.main()