/********************************************************************\

Name:         dd_sy4527.c
Created by:   Pierre-Andre Amaudruz

Contents:     SY4527 Device driver. 
Contains the bus call to the HW as it uses the
CAENHV Wrapper library for (XP & Linux).

$Id: dd_sy4527.c 2780 2005-10-19 13:20:29Z ritt $

\********************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <stdbool.h>
#define ALARM XALARM
#include "midas.h"
#undef ALARM
#include "CAENHVWrapper.h"

/*---- globals -----------------------------------------------------*/

#define CAEN_SYSTEM_TYPE  (CAENHV_SYSTEM_TYPE_t)2
#define DEFAULT_TIMEOUT 10000	/* 10 sec. */
#define SY4527_MAX_SLOTS   6

/* Store any parameters the device driver needs in following 
structure.  Edit the DDSY4527_SETTINGS_STR accordingly. This 
contains  usually the address of the device. For a CAMAC device
this  could be crate and station for example. */

typedef struct
{
  char Model[15];		// Model ID for the HV in this slot
  char Name[32];		// System Name (duplication)
  WORD channels;			// # of channels from this HV
} DDSY4527_SLOT;

typedef struct
{
  char name[32];		// System name (sy4527)
  char ip[32];			// IP# for network access
  int linktype;			// Connection type (0:TCP/IP, 1:, 2:)
  int begslot;			// First slot# belonging to this experiment
  int crateMap;			// integer code describing number of slots and size of each card 
} DDSY4527_SETTINGS;

#define DDSY4527_SETTINGS_STR "\
System Name = STRING : [32] daqhv02\n\
IP = STRING : [32] 142.90.101.75\n\
LinkType = INT : 0\n\
First Slot = INT : 0\n\
crateMap = INT : 0\n\
"

/* following structure contains private variables to the device
driver. It  is necessary to store it here in case the device
driver  is used for more than one device in one frontend. If it
would be  stored in a global variable, one device could over-
write the other device's  variables. */

typedef struct
{
  int handle;
  DDSY4527_SETTINGS dd_sy4527_settings;
  DDSY4527_SLOT slot[SY4527_MAX_SLOTS];
  float *array;
  INT num_channels;		// Total # of channels
  INT (*bd) (INT cmd, ...);	/* bus driver entry function */
  void *bd_info;		/* private info of bus driver */
  HNDLE hkey;			/* ODB key for bus driver info */
} DDSY4527_INFO;

void get_slot (DDSY4527_INFO * info, WORD channel, WORD * chan, WORD * slot);
INT dd_sy4527_Name_set (DDSY4527_INFO * info, WORD nchannel, WORD , char *chName);
INT dd_sy4527_Name_get (DDSY4527_INFO * info, WORD nchannel, WORD ,  char (*chnamelist)[MAX_CH_NAME]);
INT dd_sy4527_lParam_set (DDSY4527_INFO * info, WORD nchannel, WORD , char const *ParName, DWORD * lvalue);
INT dd_sy4527_lParam_get (DDSY4527_INFO * info, WORD nchannel, WORD , char const  *ParName, DWORD * lvalue);
INT dd_sy4527_fParam_set (DDSY4527_INFO * info, WORD nchannel, WORD , char const  *ParName, float *fvalue);
INT dd_sy4527_fParam_get (DDSY4527_INFO * info, WORD nchannel, WORD , char const *ParName, float *fvalue);
INT dd_sy4527_fBoard_set (DDSY4527_INFO * info, WORD nchannel, WORD , char const *ParName, float *fvalue);
INT dd_sy4527_fBoard_get (DDSY4527_INFO * info, WORD nchannel, WORD , char const *ParName, float *fvalue);

/*---- device driver routines --------------------------------------*/
/* the init function creates a ODB record which contains the
settings  and initialized it variables as well as the bus driver */

INT dd_sy4527_init (HNDLE hkey, void **pinfo, WORD channels,
                INT (*bd) (INT cmd, ...))
{
  int status, size, ret, islot;
  HNDLE hDB, hkeydd;
  DDSY4527_INFO *info;
  char keyloc[128], username[30], passwd[30];
  //  char   listName[32][MAX_CH_NAME];
  //char model[15], descr[80];
  HNDLE shkey;

  /*  allocate info structure */
  info = (DDSY4527_INFO *) calloc (1, sizeof (DDSY4527_INFO));
  *pinfo = info;

  cm_get_experiment_database (&hDB, NULL);

  /*  create DDSY4527 settings record */
  status = db_create_record (hDB, hkey, "DD", DDSY4527_SETTINGS_STR);
  if (status != DB_SUCCESS)
    return FE_ERR_ODB;

  db_find_key (hDB, hkey, "DD", &hkeydd);
  size = sizeof (info->dd_sy4527_settings);
  ret = db_get_record (hDB, hkeydd, &info->dd_sy4527_settings, &size, 0);
  
  //  Connect to device
  strcpy (username, "admin");
  strcpy (passwd, "4Hackers!");
  ret = CAENHV_InitSystem (CAEN_SYSTEM_TYPE, info->dd_sy4527_settings.linktype, info->dd_sy4527_settings.ip, username, passwd, &info->handle);
  //cm_msg (MINFO, "dd_sy4527", "device name: %s link type: %d ip: %s user: %s pass: %s",
  //  DevName, info->dd_sy4527_settings.linktype, info->dd_sy4527_settings.ip, username, passwd);
  cm_msg (MINFO, "dd_sy4527", "CAENHV_InitSystem: %d (%s)", ret, CAENHV_GetError(info->handle));
  
  //  Retrieve slot table for channels construction
  for (channels = 0, islot = info->dd_sy4527_settings.begslot;  islot < SY4527_MAX_SLOTS; islot++)
    {
      unsigned short NrOfCh, serNumb;
      unsigned char fmwMax, fmwMin;
      char* model = NULL;
      char* descr = NULL;
      ret = CAENHV_TestBdPresence (info->handle, islot, &NrOfCh, &model, &descr, &serNumb, &fmwMin, &fmwMax);
      printf("slot %d, ret %d\n", islot, ret);
      if (ret == CAENHV_OK) {
	cm_msg (MINFO, "dd_sy4527"
		, "Slot %d: Mod. %s %s Nr.Ch: %d  Ser. %d Rel. %d.%d", islot, model, descr, NrOfCh, serNumb, fmwMax, fmwMin);
           
	sprintf (keyloc, "Slot %i", islot);
	
	//  Check for existance of the Slot
	if (db_find_key (hDB, hkey, keyloc, &shkey) == DB_SUCCESS) {
	  size = sizeof (info->slot[islot].Model);
	  db_get_value (hDB, shkey, "Model", info->slot[islot].Model, &size, TID_STRING, FALSE);
	  //   Check for correct Model in that slot
	  if ((strcmp (info->slot[islot].Model, model)) == 0) {
	    // device model found in ODB use ODB settings
	    info->slot[islot].channels = NrOfCh;
	    channels += NrOfCh;
	    continue;
	  } else {
	    //  Wrong Model, delete and Create new one
	    db_delete_key (hDB, shkey, FALSE);
	  }
	}
      
	// No Slot entry in ODB, read defaults from device
	sprintf (keyloc, "Slot %i/Description", islot);
	db_set_value (hDB, hkey, keyloc, descr, strlen(descr)+1, 1, TID_STRING);
	
	sprintf (keyloc, "Slot %i/Model", islot);
	db_set_value (hDB, hkey, keyloc, model, strlen(model)+1, 1, TID_STRING);
	strcpy (info->slot[islot].Model, model);
	
	sprintf (keyloc, "Slot %i/Channels", islot);
	info->slot[islot].channels = NrOfCh;
	db_set_value (hDB, hkey, keyloc, &NrOfCh, sizeof (WORD), 1, TID_WORD);
	channels += NrOfCh;
      }
    }
  // initialize driver
  info->num_channels = channels;
  info->array = (float *) calloc (channels, sizeof (float));
  info->hkey = hkey;
  
  return FE_SUCCESS;
}

/*----------------------------------------------------------------------------*/
INT dd_sy4527_exit (DDSY4527_INFO * info)
{
  /*  free local variables */
  if (info->array)
    free (info->array);

  free (info);

  return FE_SUCCESS;
}

/*----------------------------------------------------------------------------*/
void get_slot (DDSY4527_INFO * info, WORD channel, WORD * chan, WORD * slot)
{
  *slot = info->dd_sy4527_settings.begslot;
  *chan = 0;
  while ((channel >= info->slot[*slot].channels) && (*slot < SY4527_MAX_SLOTS)) {
    //printf("slot %d, channels %d\n", *slot, info->slot[*slot].channels);
    channel -= info->slot[*slot].channels;
    *slot += 1;
  }
  *chan = channel;
}

/*----------------------------------------------------------------------------*/
INT dd_sy4527_lParam_set (DDSY4527_INFO * info, WORD nchannel, WORD channel,
                      char const *ParName, DWORD * lvalue)
{
  WORD islot, ch;
  DWORD tipo;
  CAENHVRESULT ret;
  WORD chlist[32];

  // Find out what slot we need to talk to.
  get_slot (info, channel, &ch, &islot);
  chlist[0] = ch;
  ret = CAENHV_GetChParamProp (info->handle, islot, chlist[0], ParName, "Type", &tipo);
  if (ret != CAENHV_OK)
    cm_msg (MERROR, "lParam_get", "Type : %d", tipo);
  if ((ret == CAENHV_OK) && (tipo != PARAM_TYPE_NUMERIC))
  {
    ret = CAENHV_SetChParam(info->handle, islot, ParName, nchannel, chlist, lvalue);
    //printf("SetChParam: slot %d, parname [%s], nchannel %d, chlist[] [ %d...], lvalue %d\n", islot, ParName, nchannel, chlist[0], lvalue[0]);
    if (ret != CAENHV_OK)
      // cm_msg(MINFO,"dd_sy4527","Set lParam - chNum:%i Value: %ld %ld %ld", nchannel, lvalue[0], lvalue[1], lvalue[2]);
      cm_msg (MERROR, "lParam", "SetChParam returns %d", ret);
  }

  return FE_SUCCESS;
}

/*----------------------------------------------------------------------------*/
INT
dd_sy4527_lParam_get (DDSY4527_INFO * info, WORD nchannel, WORD channel,
                      char const *ParName, DWORD * lvalue)
{
  WORD islot, ch;
  DWORD tipo;
  CAENHVRESULT ret;
  WORD chlist[32];

  // Find out what slot we need to talk to.
  get_slot (info, channel, &ch, &islot);
  chlist[0] = ch;
  ret = CAENHV_GetChParamProp(info->handle, islot, chlist[0], ParName, "Type", &tipo);
  if (ret != CAENHV_OK)
    cm_msg (MERROR, "lParam_get", "Type : %d", tipo);
  if ((ret == CAENHV_OK) && (tipo != PARAM_TYPE_NUMERIC))
  {
    ret = CAENHV_GetChParam(info->handle, islot, ParName, nchannel, chlist, lvalue);

    if (ret != CAENHV_OK)
      cm_msg (MERROR, "lParam", "GetChlParam returns %d", ret);
  }
  return ret;
}

/*----------------------------------------------------------------------------*/
INT dd_sy4527_fParam_set (DDSY4527_INFO * info, WORD nchannel, WORD channel,
                      char const *ParName, float *fvalue)
{
  WORD islot, ch;
  DWORD tipo;
  CAENHVRESULT ret;
  WORD chlist[32];

  // Find out what slot we need to talk to.
  get_slot (info, channel, &ch, &islot);
  printf("fSet chi:%d cho:%d slot:%d value:%f\n", *chlist, ch, islot, *fvalue);

  chlist[0] = ch;
  ret = CAENHV_GetChParamProp(info->handle, islot, chlist[0], ParName, "Type", &tipo);
  if (ret != CAENHV_OK)
  {
    cm_msg (MERROR, "fParam_get", "Param Not Found Type : %d", tipo);
    return ret;
  }
  if ((ret == CAENHV_OK) && (tipo == PARAM_TYPE_NUMERIC))
  {
    ret = CAENHV_SetChParam(info->handle, islot, ParName, nchannel, chlist, fvalue);
    if (ret != CAENHV_OK)
    {
      cm_msg(MINFO,"dd_sy4527","Set fParam - chNum:%i Value: %f %f %f", nchannel, fvalue[0], fvalue[1], fvalue[2]);
      cm_msg (MERROR, "fParam", "SetChfParam returns %d", ret);
    }
  }
  return FE_SUCCESS;
}

/*----------------------------------------------------------------------------*/
INT dd_sy4527_fParam_get (DDSY4527_INFO * info, WORD nchannel, WORD channel,
                      char const *ParName, float *fvalue)
{
  WORD islot, ch;
  DWORD tipo;
  CAENHVRESULT ret;
  WORD chlist[32];

  // Find out what slot we need to talk to.
  get_slot (info, channel, &ch, &islot);

  chlist[0] = ch;
  ret = CAENHV_GetChParamProp(info->handle, islot, chlist[0], ParName, "Type", &tipo);
  if (ret != CAENHV_OK)
  {
    //cm_msg (MERROR, "fParam_get", "Param Not Found Type : %d", tipo);
    cm_msg (MERROR, "fParam_get", "Parameter: %s", ParName);
    return ret;
  }
  if ((ret == CAENHV_OK) && (tipo == PARAM_TYPE_NUMERIC))
  {
    ret = CAENHV_GetChParam(info->handle, islot, ParName, nchannel, chlist, fvalue);
    if (ret != CAENHV_OK)
      //      cm_msg(MINFO,"dd_sy4527","Get fParam - chNum:%i Value: %f %f %f", nchannel, fvalue[0], fvalue[1], fvalue[2]);
      cm_msg (MERROR, "fParam", "GetChfParam returns %d", ret);
  }
  return ret;
}


/*----------------------------------------------------------------------------*/

INT dd_sy4527_fBoard_set (DDSY4527_INFO * info, WORD nchannel, WORD channel,
                      char const *ParName, float *fvalue)
{
  WORD islot, ch;
  DWORD tipo;
  CAENHVRESULT ret;
  WORD chlist[32];

  // Find out what slot we need to talk to.
  get_slot (info, channel, &ch, &islot);

  chlist[0] = islot;
  ret = CAENHV_GetBdParamProp(info->handle, islot, ParName, "Type", &tipo);
  if (ret != CAENHV_OK)
  {
    cm_msg (MERROR, "fBoard_get", "Param Not Found Type : %d", tipo);
    return ret;  
  }
  if ((ret == CAENHV_OK) && (tipo == PARAM_TYPE_NUMERIC))
  {
    ret = CAENHV_SetBdParam(info->handle, nchannel, chlist, ParName, fvalue); /*chlist should be the list of slots...?*/
    if (ret != CAENHV_OK)
    {
      cm_msg (MERROR, "fParam", "SetChfParam returns %d", ret);
    }
  }
  return FE_SUCCESS;
}

/*----------------------------------------------------------------------------*/

INT dd_sy4527_fBoard_get (DDSY4527_INFO * info, WORD nchannel, WORD channel,
                      char const *ParName, float *fvalue)
{
  WORD islot, ch;
  DWORD tipo;
  CAENHVRESULT ret;
  WORD chlist[32];
                      
  // Find out what slot we need to talk to.
  get_slot (info, channel, &ch, &islot);

  chlist[0] = islot;
  ret = CAENHV_GetBdParamProp(info->handle, islot, ParName, "Type", &tipo);
  if (ret != CAENHV_OK)
  {
    cm_msg (MERROR, "fBoard_get", "Param Not Found Type : %d", tipo);
    return ret;  
  }
  if ((ret == CAENHV_OK) && (tipo == PARAM_TYPE_NUMERIC))
  {
    ret = CAENHV_GetBdParam(info->handle, nchannel, chlist, ParName, fvalue); /*chlist should be the list of slots...?*/
    if (ret != CAENHV_OK)
      cm_msg (MERROR, "fParam", "GetChfParam returns %d", ret);
  }
  return ret;
}

/*---------------------------------------------------------------------------*/

//Return number of channels in a given slot:
int howBig(DDSY4527_INFO * info, int slot){
 
  unsigned short NrOfSlot, *NrOfChList, *SerNumList;
  char *ModelList, *DescriptionList;
  unsigned char *FmwRelMinList, *FmwRelMaxList;
  
  CAENHV_GetCrateMap(info->handle, &NrOfSlot, &NrOfChList, &ModelList, &DescriptionList, &SerNumList, &FmwRelMinList, &FmwRelMaxList);
  
  return NrOfChList[slot];

}

//is this the first channel in the slot?
int isFirst(DDSY4527_INFO * info, WORD channel){
  
  if(channel == 0) return 1;
    
  WORD islot, ch, prevSlot, prevCh;
  get_slot (info, channel, &ch, &islot);
  get_slot (info, channel-1, &prevCh, &prevSlot);
   
  if(islot == prevSlot) return 0;
  else return 1;
  
}
  
/*----------------------------------------------------------------------------*/
INT dd_sy4527_Label_set (DDSY4527_INFO * info, WORD channel, char *label)
{
  CAENHVRESULT ret;

  if (strlen (label) < MAX_CH_NAME)
  {
    ret = dd_sy4527_Name_set (info, 1, channel, label);

    return ret;
  }
  else
    return 1;
}

/*----------------------------------------------------------------------------*/
INT dd_sy4527_Name_set (DDSY4527_INFO * info, WORD nchannel, WORD channel,
                    char *chName)
{
  WORD islot, ch;
  CAENHVRESULT ret;
  WORD chlist[32];

  // Find out what slot we need to talk to.
  get_slot (info, channel, &ch, &islot);
  chlist[0] = ch;
  ret = CAENHV_SetChName (info->handle, islot, nchannel, chlist, chName);
  if (ret != CAENHV_OK) {
    cm_msg (MERROR, "Name Set", "SetChName returns %d", ret);
  }
  return ret;
}

/*----------------------------------------------------------------------------*/
INT dd_sy4527_Label_get (DDSY4527_INFO * info, WORD channel, char *label)
{
  char (*chnamelist)[MAX_CH_NAME];
  WORD nchannel;
  CAENHVRESULT ret;

  chnamelist = new char[1][MAX_CH_NAME];
  nchannel = 1;
  ret = dd_sy4527_Name_get (info, nchannel, channel, chnamelist);
  strcpy(label, chnamelist[0]);

  delete[] chnamelist;

  return ret == 0 ? FE_SUCCESS : 0;
}

/*----------------------------------------------------------------------------*/
INT dd_sy4527_Name_get (DDSY4527_INFO * info, WORD nchannel, WORD channel, char (*chnamelist)[MAX_CH_NAME])
{
  WORD ch, islot;
  CAENHVRESULT ret;
  char (*name)[MAX_CH_NAME];
  // Find out what slot we need to talk to.
  get_slot (info, channel, &ch, &islot);
  name = new char[nchannel][MAX_CH_NAME];

  ret = CAENHV_GetChName(info->handle, islot, nchannel, &ch, (char (*)[12]) name);
  printf("slot %d, nchannel %d, channel %d, ch %d, name [%s], ret %d\n", islot, nchannel, channel, ch, name[0], ret);

  //  strcpy(chnamelist, &name[0]);
  
  ret = CAENHV_GetChName(info->handle, islot, nchannel, &ch, (char (*)[12]) chnamelist);
  printf("slot %d, nchannel %d, channel %d, ch %d, name [%s], ret %d\n", islot, nchannel, channel, ch, chnamelist[0], ret);
  
  //  printf("slot %d, nchannel %d, channel %d, ch %d, name [%s], ret %d\n", islot, nchannel, channel, ch, chnamelist[0], ret);
  if (ret != CAENHV_OK) {
    cm_msg (MERROR, "Name Get", "GetChName returns %d", ret);
  }

  delete[] name;

  return ret;
}

/*----------------------------------------------------------------------------*/
/*----------------------------------------------------------------------------*/
/**  Get voltage
*/
INT dd_sy4527_get (DDSY4527_INFO * info, WORD channel, float *pvalue)
{
  CAENHVRESULT ret;

  ret = dd_sy4527_fParam_get (info, 1, channel, "VMon", pvalue);
  return ret == 0 ? FE_SUCCESS : 0;
}

/*----------------------------------------------------------------------------*/
INT dd_sy4527_demand_get (DDSY4527_INFO * info, WORD channel, float *value)
{
  CAENHVRESULT ret;

  ret = dd_sy4527_fParam_get (info, 1, channel, "V0Set", value);
  return ret == 0 ? FE_SUCCESS : 0;
}

/*----------------------------------------------------------------------------*/
INT dd_sy4527_current_get (DDSY4527_INFO * info, WORD channel, float *pvalue)
{
  CAENHVRESULT ret;

  WORD islot, ch;    
  // Find out what slot we need to talk to.
  get_slot (info, channel, &ch, &islot);
  //how many channels are in this slot?
  int nChan = howBig(info, islot);

  int isPrimary = isFirst(info, channel);

  //do the normal thing for 12 and 24 channel cards and channel 0 of 48 chan cards;
  //return an error code for non-primary channels of 48 chan cards.
  if(nChan == 12 || nChan == 24 || isPrimary == 1 ){
    ret = dd_sy4527_fParam_get (info, 1, channel, "IMon", pvalue);
    return ret == 0 ? FE_SUCCESS : 0;
  } else {
    *pvalue = -9999;
    return FE_SUCCESS;
  }

}

/*---------------------------------------------------------------------------*/
/** Set demand voltage */
INT dd_sy4527_set (DDSY4527_INFO * info, WORD channel, float value)
{
  DWORD temp;
  CAENHVRESULT ret;

  if (value < 0.01)
    {
      // switch off this channel
      temp = 0;
      ret = dd_sy4527_lParam_set (info, 1, channel, "Pw", &temp);
      // Set value
      ret = dd_sy4527_fParam_set (info, 1, channel, "V0Set", &value);
    }
  else
    {
      // Set Value
      ret = dd_sy4527_fParam_set (info, 1, channel, "V0Set", &value);
      // switch on this channel
      temp = 1;
      ret = dd_sy4527_lParam_set (info, 1, channel, "Pw", &temp);
    }
  return ret == 0 ? FE_SUCCESS : 0;
}

/*----------------------------------------------------------------------------*/
INT dd_sy4527_chState_set (DDSY4527_INFO * info, WORD channel, DWORD *pvalue)
{
  CAENHVRESULT ret;

  ret = dd_sy4527_lParam_set (info, 1, channel, "Pw", pvalue);

  return ret == 0 ? FE_SUCCESS : 0;
}
  
/*----------------------------------------------------------------------------*/
INT dd_sy4527_chState_get (DDSY4527_INFO * info, WORD channel, DWORD *pvalue)
{
  CAENHVRESULT ret;
  
  ret = dd_sy4527_lParam_get (info, 1, channel, "Pw", pvalue);

  return ret == 0 ? FE_SUCCESS : 0;
}

/*----------------------------------------------------------------------------*/
INT dd_sy4527_crateMap_get (DDSY4527_INFO * info, WORD channel, INT *dummy)
{
  unsigned short NrOfSlot, *NrOfChList, *SerNumList;
  char *ModelList, *DescriptionList;
  unsigned char *FmwRelMinList, *FmwRelMaxList;
 
  CAENHV_GetCrateMap(info->handle, &NrOfSlot, &NrOfChList, &ModelList, &DescriptionList, &SerNumList, &FmwRelMinList, &FmwRelMaxList);
  *dummy = 0;
  int i = 0;
  //cm_msg (MINFO, "cratemap", "dummy start: %d", *dummy);
  //slots packed from most significant bit down (ie slot 0 in bits 31 and 30, slot 1 in 29 and 28....)
  //00 -> empty slot, 01 -> 12chan card, 10 -> 24chan card, 11->48chan card 
  for(i=0; i<NrOfSlot; i++){
    if(NrOfChList[i] == 12)
      *dummy = *dummy | (1 << (30 - 2*i));
    else if(NrOfChList[i] == 24)
      *dummy = *dummy | (2 << (30 - 2*i));
    else if(NrOfChList[i] == 48)
      *dummy = *dummy | (3 << (30 - 2*i));
  }
  //lowest 2 bits == 10 -> 6 slot crate, == 11 -> 12 slot crate, == anything else -> 16 slot crate (in the last slot, must be either empty or 12chan == 00 or 01)
  if(NrOfSlot == 6){
    *dummy = *dummy | 2;
  }
  else if(NrOfSlot == 12){
    *dummy = *dummy | 3;
  }

    //cm_msg (MINFO, "cratemap", "%lu", *dummy);

  return FE_SUCCESS;
}
/*----------------------------------------------------------------------------*/

INT dd_sy4527_chStatus_get (DDSY4527_INFO * info, WORD channel, DWORD *pvalue)
{
  CAENHVRESULT ret;

  ret = dd_sy4527_lParam_get (info, 1, channel, "Status", pvalue);

  //cm_msg (MINFO, "dd_sy4527", "ChStatus: %d", *pvalue);

  return ret == 0 ? FE_SUCCESS : 0;
 
}

/*----------------------------------------------------------------------------*/

INT dd_sy4527_temperature_get (DDSY4527_INFO * info, WORD channel, float *pvalue)
{
  //cm_msg(MINFO, "dd_sy4527", info->dd_sy4527_settings.ip);

  CAENHVRESULT ret;

  ret = dd_sy4527_fBoard_get (info, 1, channel, "Temp", pvalue);
  
  return ret == 0 ? FE_SUCCESS : 0;

}
    
/*----------------------------------------------------------------------------*/

INT dd_sy4527_ramp_set (DDSY4527_INFO * info, INT cmd, WORD channel, float *pvalue)
{
  CAENHVRESULT ret;

  if (cmd == CMD_SET_RAMPUP)
    ret = dd_sy4527_fParam_set (info, 1, channel, "RUp", pvalue);
  if (cmd == CMD_SET_RAMPDOWN)
    ret = dd_sy4527_fParam_set (info, 1, channel, "RDWn", pvalue);
  return ret == 0 ? FE_SUCCESS : 0;
}

/*----------------------------------------------------------------------------*/
INT dd_sy4527_ramp_get (DDSY4527_INFO * info, INT cmd, WORD channel, float *pvalue)
{
  CAENHVRESULT ret;

  if (cmd == CMD_GET_RAMPUP)
    ret = dd_sy4527_fParam_get (info, 1, channel, "RUp", pvalue);
  if (cmd == CMD_GET_RAMPDOWN)
    ret = dd_sy4527_fParam_get (info, 1, channel, "RDWn", pvalue);
  return ret == 0 ? FE_SUCCESS : 0;
}
/*----------------------------------------------------------------------------*/
INT dd_sy4527_current_limit_set (DDSY4527_INFO * info, WORD channel, float *pvalue)
{
  CAENHVRESULT ret;

  //ret = dd_sy4527_fParam_set (info, 1, channel, "I0Set", pvalue);
  //return ret == 0 ? FE_SUCCESS : 0;

  WORD islot, ch;
  // Find out what slot we need to talk to.
  get_slot (info, channel, &ch, &islot);
  //how many channels are in this slot?
  int nChan = howBig(info, islot);
 
  int isPrimary = isFirst(info, channel);

  //do the normal thing for 12 channel cards and channel 0 of 48 chan cards;
  //return an error code for non-primary channels of 48 chan cards.
  if(nChan == 12 || isPrimary == 1 ){
    ret = dd_sy4527_fParam_set (info, 1, channel, "I0Set", pvalue);
    return ret == 0 ? FE_SUCCESS : 0;
  } else {
    return FE_SUCCESS;
  }

}

/*----------------------------------------------------------------------------*/
INT dd_sy4527_current_limit_get (DDSY4527_INFO * info, WORD channel, float *pvalue)
{
  CAENHVRESULT ret;

  //ret = dd_sy4527_fParam_get (info, 1, channel, "I0Set", pvalue);
  //return ret == 0 ? FE_SUCCESS : 0;

  WORD islot, ch;
  // Find out what slot we need to talk to.
  get_slot (info, channel, &ch, &islot);
  //how many channels are in this slot?
  int nChan = howBig(info, islot);

  int isPrimary = isFirst(info, channel);

  //do the normal thing for 12 and 24 channel cards and channel 0 of 48 chan cards;
  //return an error code for non-primary channels of 48 chan cards.
  if(nChan == 12 || nChan == 24 || isPrimary == 1 ){
    ret = dd_sy4527_fParam_get (info, 1, channel, "I0Set", pvalue);
    return ret == 0 ? FE_SUCCESS : 0;
  } else {
    *pvalue = -9999;
    return FE_SUCCESS; 
  }

}

/*----------------------------------------------------------------------------*/
INT dd_sy4527_voltage_limit_set (DDSY4527_INFO * info, WORD channel, float *pvalue)
{
  CAENHVRESULT ret;

  //ret = dd_sy4527_fParam_set (info, 1, channel, "SVMax", pvalue);
  //return ret == 0 ? FE_SUCCESS : 0;

  WORD islot, ch;
  // Find out what slot we need to talk to.
  get_slot (info, channel, &ch, &islot);
  //how many channels are in this slot?
  int nChan = howBig(info, islot);
 
  int isPrimary = isFirst(info, channel);

  //do the normal thing for 12 channel cards and channel 0 of 48 chan cards;
  //return an error code for non-primary channels of 48 chan cards.
  if(nChan == 12 || isPrimary == 1 ){
    ret = dd_sy4527_fParam_set (info, 1, channel, "SVMax", pvalue);
    return ret == 0 ? FE_SUCCESS : 0;
  } else {
    return FE_SUCCESS;
  }

}

/*----------------------------------------------------------------------------*/
INT dd_sy4527_voltage_limit_get (DDSY4527_INFO * info, WORD channel, float *pvalue)
{
  CAENHVRESULT ret;

  //ret = dd_sy4527_fParam_get (info, 1, channel, "SVMax", pvalue);
  //return ret == 0 ? FE_SUCCESS : 0;

  WORD islot, ch;
  // Find out what slot we need to talk to.
  get_slot (info, channel, &ch, &islot);
  //how many channels are in this slot?
  int nChan = howBig(info, islot);

  int isPrimary = isFirst(info, channel);  

  //do the normal thing for 12 and 24 channel cards and channel 0 of 48 chan cards;
  //return an error code for non-primary channels of 48 chan cards.
  if(nChan == 12 || nChan == 24 || isPrimary == 1 ){   
    ret = dd_sy4527_fParam_get (info, 1, channel, "SVMax", pvalue);
    return ret == 0 ? FE_SUCCESS : 0;
  } else {
    *pvalue = -9999;
    return FE_SUCCESS;
  }

}

/*----------------------------------------------------------------------------*/
INT dd_sy4527_trip_time_set (DDSY4527_INFO * info, WORD channel, float *pvalue)
{
  CAENHVRESULT ret;
  //ret = dd_sy4527_fParam_set (info, 1, channel, "Trip", pvalue);
  //return ret == 0 ? FE_SUCCESS : 0;

  WORD islot, ch;
  // Find out what slot we need to talk to.
  get_slot (info, channel, &ch, &islot);
  //how many channels are in this slot?
  int nChan = howBig(info, islot);
   
  //do the normal thing for 12 channel cards:
  if(nChan == 12){
    ret = dd_sy4527_fParam_set (info, 1, channel, "Trip", pvalue);
    return ret == 0 ? FE_SUCCESS : 0;
  } else {
    return FE_SUCCESS;
  }

}

/*----------------------------------------------------------------------------*/
INT dd_sy4527_trip_time_get (DDSY4527_INFO * info, WORD channel, float *pvalue)
{
  CAENHVRESULT ret;

  //ret = dd_sy4527_fParam_get (info, 1, channel, "Trip", pvalue);
  //return ret == 0 ? FE_SUCCESS : 0;

  WORD islot, ch;
  // Find out what slot we need to talk to.
  get_slot (info, channel, &ch, &islot);
  //how many channels are in this slot?
  int nChan = howBig(info, islot);

  //do the normal thing for 12 and 24 channel cards
  if(nChan == 12 || nChan == 24){
    ret = dd_sy4527_fParam_get (info, 1, channel, "Trip", pvalue);
    return ret == 0 ? FE_SUCCESS : 0;
  } else {
    *pvalue = -9999;
    return FE_SUCCESS;
  }
}

/*---- device driver entry point -----------------------------------*/
INT dd_sy4527 (INT cmd, ...)
{
  va_list argptr;
  HNDLE hKey;
  WORD channel, status, icmd;
  DWORD flags;
  DWORD state, *pstate;
  double *pdouble;
  unsigned short *NrOfChList;
  char *label;
  float value, *pvalue;
  void *info;
  INT (*bd) (INT cmd, ...);
  INT *pint;

  va_start (argptr, cmd);
  status = FE_SUCCESS;

  switch (cmd)
  {
  case CMD_INIT:
    hKey = va_arg (argptr, HNDLE);
    info = va_arg (argptr, void *);
    channel = (WORD) va_arg (argptr, INT);
    flags = va_arg (argptr, DWORD);
    bd =  va_arg (argptr, INT (*)(INT, ...));
    status = dd_sy4527_init (hKey, (void **)info, channel, bd);
    break;

  case CMD_EXIT:
    info = va_arg (argptr, void *);
    status = dd_sy4527_exit ((DDSY4527_INFO *) info);
    break;

  case CMD_GET_LABEL:  // name
    info = va_arg (argptr, void *);
    channel = (WORD) va_arg (argptr, INT);
    label = (char *) va_arg (argptr, char *);
    status = dd_sy4527_Label_get ((DDSY4527_INFO *) info, channel, label);
    break;

  case CMD_SET_LABEL:  // name
    info = va_arg (argptr, void *);
    channel = (WORD) va_arg (argptr, INT);
    label = (char *) va_arg (argptr, char *);
    status = dd_sy4527_Label_set ((DDSY4527_INFO *) info, channel, label);
    break;

  case CMD_GET_DEMAND:  // set voltage read back
    info = va_arg (argptr, void *);
    channel = (WORD) va_arg (argptr, INT);
    pvalue = va_arg (argptr, float *);
    status = dd_sy4527_demand_get ((DDSY4527_INFO *) info, channel, pvalue);
    break;

  case CMD_SET:  // voltage
    info = va_arg (argptr, void *);
    channel = (WORD) va_arg (argptr, INT);
    value = (float) va_arg (argptr, double);	// floats are passed as double
    status = dd_sy4527_set ((DDSY4527_INFO *) info, channel, value);
    break;

  case CMD_GET:  //voltage
    info = va_arg (argptr, void *);
    channel = (WORD) va_arg (argptr, INT);
    pvalue = va_arg(argptr, float *);
    status = dd_sy4527_get ((DDSY4527_INFO *) info, channel, pvalue);
    break;

  case CMD_GET_CURRENT:
    info = va_arg (argptr, void *);
    channel = (WORD) va_arg (argptr, INT);
    pvalue = va_arg(argptr, float *);
    status = dd_sy4527_current_get ((DDSY4527_INFO *) info, channel, pvalue);
    break;

  case CMD_SET_CHSTATE:
    info = va_arg (argptr, void *);
    channel = (WORD) va_arg(argptr, INT);
    state = (DWORD)va_arg(argptr, double);
    status = dd_sy4527_chState_set ((DDSY4527_INFO *) info, channel, &state);
    break;

  case CMD_GET_CHSTATE:
    info = va_arg (argptr, void *);
    channel = (WORD) va_arg (argptr, INT);
    pstate = (DWORD *) va_arg (argptr, DWORD *);
    status = dd_sy4527_chState_get ((DDSY4527_INFO *) info, channel, pstate);
    break;

  case CMD_GET_CRATEMAP:
    info = va_arg (argptr, void *);
    channel = (WORD) va_arg (argptr, INT);
    pint = (INT *) va_arg (argptr, INT *);
    status = dd_sy4527_crateMap_get ((DDSY4527_INFO *) info, channel, pint);
    break;

  case CMD_GET_STATUS:
    info = va_arg (argptr, void *);
    channel = (WORD) va_arg (argptr, INT);
    pstate = (DWORD *) va_arg (argptr, DWORD *);
    status = dd_sy4527_chStatus_get ((DDSY4527_INFO *) info, channel, pstate);
    break;

  case CMD_GET_TEMPERATURE:
    info = va_arg (argptr, void *);
    channel = (WORD) va_arg (argptr, INT);
    pvalue = (float *)va_arg(argptr, float *);
    status = dd_sy4527_temperature_get ((DDSY4527_INFO *) info, channel, pvalue);
    break;

  case CMD_SET_RAMPUP:
  case CMD_SET_RAMPDOWN:
    info = va_arg (argptr, void *);
    icmd = cmd;
    channel = (WORD) va_arg (argptr, INT);
    value = (float) va_arg(argptr, double);	// floats are passed as double
    status = dd_sy4527_ramp_set ((DDSY4527_INFO *) info, icmd, channel, &value);
    break;

  case CMD_GET_RAMPUP:
  case CMD_GET_RAMPDOWN:
    info = va_arg (argptr, void *);
    icmd = cmd;
    channel = (WORD) va_arg (argptr, INT);
    pvalue = va_arg (argptr, float *);
    status = dd_sy4527_ramp_get ((DDSY4527_INFO *) info, icmd, channel, pvalue);
    break;

  case CMD_SET_CURRENT_LIMIT:
    info = va_arg (argptr, void *);
    channel = (WORD) va_arg(argptr, INT);
    value = (float)va_arg(argptr, double);	// floats are passed as double
    status = dd_sy4527_current_limit_set ((DDSY4527_INFO *)info, channel, &value);
    break;

  case CMD_GET_CURRENT_LIMIT:
    info = va_arg (argptr, void *);
    channel = (WORD) va_arg (argptr, INT);
    pvalue = (float *) va_arg (argptr, float *);
    status = dd_sy4527_current_limit_get ((DDSY4527_INFO *)info, channel, pvalue);
    break;

  case CMD_SET_VOLTAGE_LIMIT:
    info = va_arg (argptr, void *);
    channel = (WORD) va_arg (argptr, INT);
    value = (float) va_arg (argptr, double);
    status = dd_sy4527_voltage_limit_set ((DDSY4527_INFO *)info, channel, &value);
    break;

  case CMD_GET_VOLTAGE_LIMIT:
    info = va_arg (argptr, void *);
    channel = (WORD) va_arg (argptr, INT);
    pvalue = va_arg (argptr, float *);
    status = dd_sy4527_voltage_limit_get ((DDSY4527_INFO *)info, channel, pvalue);
    break;

  case CMD_SET_TRIP_TIME:
    info = va_arg (argptr, void *);
    channel = (WORD) va_arg (argptr, INT);
    value = (float) va_arg (argptr, double);
    status = dd_sy4527_trip_time_set ((DDSY4527_INFO *)info, channel, &value);
    break;

  case CMD_GET_TRIP_TIME:
    info = va_arg (argptr, void *);
    channel = (WORD) va_arg (argptr, INT);
    pvalue = va_arg (argptr, float *);
    status = dd_sy4527_trip_time_get ((DDSY4527_INFO *)info, channel, pvalue);
    break;

  default:
    break;
  }

  va_end (argptr);

  return status;
}

/*------------------------------------------------------------------*/

/* emacs
 * Local Variables:
 * mode:C
 * mode:font-lock
 * tab-width: 8
 * c-basic-offset: 2
 * End:
 */
