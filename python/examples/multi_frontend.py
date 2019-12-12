"""
Example of a more advanced midas frontend that has
* multiple equipment (one polled, one periodic)
* support for the "-i" flag on the command line
* support for user-settings in the ODB
* an earlier transition sequence.

See `examples/basic_frontend.py` to see the core concepts first.

When running with the "-i" flag on the command line, we will set the global
variable `midas.frontend.frontend_index`. This means that you can run the
same program multiple times, but do different things based on the index
(e.g. if you have 4 digitizers, you could run the same program four times,
and connect to a different one based on "-i 1", "-i 2" etc).
"""

import midas
import midas.frontend
import midas.event
import random

class MyMultiPeriodicEquipment(midas.frontend.EquipmentBase):
    """
    This periodic equipment is very similar to the one in `examples/basic_frontend.py`
    """
    def __init__(self, client):
        # If using the frontend_index, you should encode the index in your
        # equipment name.
        equip_name = "MyMultiPeriodicEquipment_%s" % midas.frontend.frontend_index
        
        # The default settings that will be found in the ODB at
        # /Equipment/MyMultiPeriodicEquipment_1/Common etc.
        # Again, the values specified in the code here only apply the first 
        # time a frontend runs; after that the values in the ODB are used.
        #
        # If you need the ODB values for some reason, they are available at
        # self.common.
        #
        # Note that we're setting a UNIQUE EVENT ID for each equipment based
        # on the frontend_index - this is important so you can later 
        # distinguish/assemble the events.
        default_common = midas.frontend.InitialEquipmentCommon()
        default_common.equip_type = midas.EQ_PERIODIC
        default_common.buffer_name = "SYSTEM"
        default_common.trigger_mask = 0
        default_common.event_id = 500 + midas.frontend.frontend_index
        default_common.period_ms = 100
        default_common.read_when = midas.RO_RUNNING
        default_common.log_history = 1

        # These settings will appear in the ODB at
        # /Equipment/MyMultiPeriodicEquipment_1/Settings etc. The settings can
        # be accessed at self.settings, and will automatically update if the 
        # ODB changes.
        default_settings = {"Prescale factor": 10}
        
        # We MUST call __init__ from the base class as part of our __init__!
        midas.frontend.EquipmentBase.__init__(self, client, equip_name, default_common, default_settings)
        
        # This is just a variable we'll use to keep track of how long it's
        # been since we last sent an event to midas.
        self.prescale_count = 0
        
        # Set the status that appears on the midas status page.
        self.set_status("Initialized")
        
    def readout_func(self):
        """
        In this periodic equipment, this function will be called periodically.
        It should return a `midas.event.Event` or None.
        """
        if self.prescale_count == self.settings["Prescale factor"]:
            event = midas.event.Event()
            data = [1,2,3,4,5,6,7,8]
            
            event.create_bank("MYBK", midas.TID_INT, data)
            event.create_bank("BNK2", midas.TID_BOOL, [True, False])
            
            self.prescale_count = 0
            return event
        else:
            self.prescale_count += 1
            return None

    def settings_changed_func(self):
        """
        You can define this function to be told about when the values in
        /Equipment/MyMultiPeriodicEquipment_1/Settings have changed.
        self.settings is updated automatically, and has already changed
        by this time this function is called.
        """
        self.client.msg("Prescale factor is now %d" % self.settings["Prescale factor"])

class MyMultiPolledEquipment(midas.frontend.EquipmentBase):
    """
    Here we're creating a "polled" equipment rather than a periodic one.
    
    You should define an extra function (`poll_func()`) which will be called 
    very often and should say whether there is a new event ready or not. Only 
    then will `readout_func()` be called.
    """
    def __init__(self, client):
        # The differences from the periodic equipment above are:
        # * a different name
        # * a different equipment type (EQ_POLLED)
        # * different event IDs (can be anything, but make sure they're
        #   unique for each equipment!).
        equip_name = "MyMultiPolledEquipment_%s" % midas.frontend.frontend_index
        
        default_common = midas.frontend.InitialEquipmentCommon()
        default_common.equip_type = midas.EQ_POLLED
        default_common.buffer_name = "SYSTEM"
        default_common.trigger_mask = 0
        default_common.event_id = 600 + midas.frontend.frontend_index
        default_common.period_ms = 100
        default_common.read_when = midas.RO_RUNNING
        default_common.log_history = 0
        
        midas.frontend.EquipmentBase.__init__(self, client, equip_name, default_common)
        
        self.set_status("Initialized")
        
    def poll_func(self):
        """
        This function is called very frequently and should return True/False
        depending on whether an event is ready to be read out. It should be
        a quick function. 
        
        In this case we're just choosing random number as we don't have any
        actual device hooked up.
        """
        return random.uniform(1, 100) < 2
        
    def readout_func(self):
        """
        For this polled equipment, readout_func will only be called when 
        poll_func() has returned True (and at transitions if "Common/Read on"
        includes `midas.RO_BOR`, for example).
        """
        event = midas.event.Event()
        data = [4.5, 6.7, 8.9]
        
        event.create_bank("MYFL", midas.TID_FLOAT, data)
        
        return event

class MyMultiFrontend(midas.frontend.FrontendBase):
    def __init__(self):
        # If using the frontend_index, you should encode the index in your
        # equipment name.
        fe_name = "multife_%i" % midas.frontend.frontend_index
        midas.frontend.FrontendBase.__init__(self, fe_name)
        
        # This time we add 2 equipment to this frontend. We can add as many
        # as we want. Whereas the C frontend system only allows one polled
        # equipment per frontend, this python system allows multiple.
        self.add_equipment(MyMultiPeriodicEquipment(self.client))
        self.add_equipment(MyMultiPolledEquipment(self.client))
        
        # You can change the order in which our frontend's begin_of_run/
        # end_of_run function are called during transitions.
        self.client.set_transition_sequence(midas.TR_START, 300)
        self.client.set_transition_sequence(midas.TR_STOP, 700)
        
    def begin_of_run(self, run_number):
        self.set_all_equipment_status("Running", "greenLight")
        self.client.msg("Frontend has seen start of run number %d" % run_number)
        
    def end_of_run(self, run_number):
        self.set_all_equipment_status("Okay", "greenLight")
        self.client.msg("Frontend has seen end of run number %d" % run_number)
        
if __name__ == "__main__":
    # We must call this function to parse the "-i" flag, so it is available
    # as `midas.frontend.frontend_index` when we init the frontend object.
    midas.frontend.parse_args()
    
    # Now we just create and run the frontend like in the basic example.
    my_fe = MyMultiFrontend()
    my_fe.run()