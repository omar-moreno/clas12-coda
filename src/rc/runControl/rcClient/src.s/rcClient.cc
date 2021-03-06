//-----------------------------------------------------------------------------
// Copyright (c) 1991,1992 Southeastern Universities Research Association,
//                         Continuous Electron Beam Accelerator Facility
//
// This software was developed under a United States Government license
// described in the NOTICE file included as part of this distribution.
//
//-----------------------------------------------------------------------------
//
// Description:
//      CODA RunControl Server Client Interface
//
// Author:  Jie Chen
//
// Revision History:
//   $Log: rcClient.cc,v $
//   Revision 1.14  1999/11/22 19:06:52  rwm
//   Fixed warnings by casting to (daqNetData &).
//
//   Revision 1.13  1999/02/25 14:29:44  rwm
//   Common limits defined in rcServer header file.
//
//   Revision 1.12  1998/11/24 14:52:11  timmer
//   Graham's earlier change for DYN_ATTR3,4
//
//   Revision 1.11  1998/09/17 19:18:32  rwm
//   Only print if _CODA_DEBUG is set.
//
//   Revision 1.10  1998/06/18 12:21:55  heyes
//   new GUI ready I think
//
//   Revision 1.9  1998/06/02 19:51:42  heyes
//   fixed rcServer
//
//   Revision 1.8  1998/05/28 17:47:11  heyes
//   new GUI look
//
//   Revision 1.7  1997/10/15 16:18:33  heyes
//   embed tools in runcontrol
//
//   Revision 1.6  1997/10/01 13:14:15  heyes
//   all in
//
//   Revision 1.5  1997/08/25 16:02:04  heyes
//   fix some display problems
//
//   Revision 1.4  1996/12/04 18:32:37  chen
//   port to 1.4 on hp and ultrix
//
//   Revision 1.3  1996/11/04 16:14:57  chen
//   add options for monitoring components
//
//   Revision 1.2  1996/10/31 18:09:26  chen
//   server message to database as an option
//
//   Revision 1.1.1.1  1996/10/11 13:39:32  chen
//   run control source
//
//
#undef _CODA_DEBUG

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <pwd.h>



/*sergey*/
#include <Xm/XmP.h>
#ifdef USE_CREG
#include <codaRegistry.h>
#endif
/*sergey*/



#include <rcServerLocater.h>
#include <rcMsg.h>
#include "daqRunLimits.h"
#include <daqCompBootStruct.h>
#include <daqMonitorStruct.h>
#include "rcClient.h"

#if defined (_CODA_2_0_T) || defined (_CODA_2_0)
#include <rcSvcInfoFinder.h>
#endif

#ifdef solaris
extern "C" int gethostname (char*, int);
#endif
extern "C" void setCompState(char *comp,int state);



const int NUM_CONN_RETRIES = 2; /*sergey: was 40*/

const int DEFAULT_SYNC_TIMEOUT = 4;
const int DEFAULT_SYNC_NUM_RETRIES = 5;
const int CODA_CMDCBK_TABLE_SIZE = 256;




static codaRcCallback *statCbk = NULL;

rcClient::rcClient (void)
:Event_Handler (), reactor_ (), toServer_ (), conServer_ (), exptname_ (0),
 connected_ (0), varDeleted_ (0), lock_ (0), discCbkList_ (),
 cmdCbkTable_ (CODA_CMDCBK_TABLE_SIZE), 
 dataManager_ ()
{
#ifdef _TRACE_OBJECTS
  printf ("    Create rcClient Class Object\n");
#endif
  // get user name and process id number
  struct passwd* pwsd = 0;
  if ((pwsd = getpwuid (getuid()) ) != 0) {
    username_ = new char[::strlen(pwsd->pw_name) + 1];
    ::strcpy (username_,pwsd->pw_name);
  }
  else {
    char buff[80];
    ::sprintf (buff, "userid_%d",getuid ());
    username_ = new char[::strlen (buff) + 1];
    ::strcpy (username_, buff);
  }
  pid_ = ::getpid ();

  // get X window display name
  char host[128];
  int  len = sizeof (host);
  if (::gethostname (host, len) != 0)
    strcpy (host, "unknown");

  char* tmp = ::getenv ("DISPLAY");
  char  fullname[128];
  if (!tmp) {
    strcat (host, ":0.0");
    strcpy (fullname, host);
  }
  else {
    if (::strcmp (tmp, ":0.0") == 0) {
      ::strcpy (fullname, host);
      ::strcat (fullname, tmp);
    }
    else
      strcpy (fullname, tmp);
  }
  disp_ = new char[::strlen (fullname) + 1];
  ::strcpy (disp_, fullname);
  
  infoRegistered_ = 0;
}

rcClient::~rcClient (void)
{
#ifdef _TRACE_OBJECTS
  printf ("    Delete rcClient Class Object\n");
#endif
  if (exptname_) {
    delete []exptname_;
    exptname_ = 0;
  }
  delete []username_;
  delete []disp_;
  infoRegistered_ = 0;

  // delete everything from disconnect callback list
  codaSlistIterator ite (discCbkList_);
  codaRcCallback* cbk = 0;
  for (ite.init(); !ite; ++ite)
  {
    cbk = (codaRcCallback *) ite ();
    delete cbk;
  }
  discCbkList_.deleteAllValues ();

  // remove all variables
  deleteAllVariables ();
}

int
rcClient::connected (void) const
{
  return connected_;
}

void
rcClient::createAllVariables (void)
{
  version_           = new rccDaqData (exptname_, "version",           "unknown");
  startTime_         = new rccDaqData (exptname_, "startTime",         "      ");
  startTimeBin_      = new rccDaqData (exptname_, "startTimeBin",      0);
  endTime_           = new rccDaqData (exptname_, "endTime",           "      ");
  endTimeBin_        = new rccDaqData (exptname_, "endTimeBin",        0);
  currTime_          = new rccDaqData (exptname_, "currentTime",       "       ");
  currTimeBin_       = new rccDaqData (exptname_, "timeBin",           0);
  runNumber_         = new rccDaqData (exptname_, "runNumber",         0);
  status_            = new rccDaqData (exptname_, "status",            0);
  nevents_           = new rccDaqData (exptname_, "nevents",           0);
  nlongs_            = new rccDaqData (exptname_, "nlongs",            0);

  database_          = new rccDaqData (exptname_, "database",          "unknown");

  dataFile_          = new rccDaqData (exptname_, "dataFile",          "unknown");
  confFile_          = new rccDaqData (exptname_, "confFile",          "unknown"); /*sergey*/

  rcsMsgToDbase_     = new rccDaqData (exptname_, "rcsMsgToDbase",     0);
  logFileDescriptor_ = new rccDaqData (exptname_, "logFileDescriptor", "unknown");
  tokenInterval_     = new rccDaqData (exptname_, "tokenInterval",     0);

  allRunTypes_       = new rccDaqData (exptname_, "allRunTypes",       "unknown");
  runType_           = new rccDaqData (exptname_, "runType",           "unknown");
  runTypeNum_        = new rccDaqData (exptname_, "runTypeNum",        0);
  exptName_          = new rccDaqData (exptname_, "exptName",          exptname_);
  exptId_            = new rccDaqData (exptname_, "exptId",            0);
  hostName_          = new rccDaqData (exptname_, "hostName",          "unknown");
  autoIncrement_     = new rccDaqData (exptname_, "autoIncrement",     1);
  eventLimit_        = new rccDaqData (exptname_, "eventLimit",        0);
  dataLimit_         = new rccDaqData (exptname_, "dataLimit",         0);
  updateInterval_    = new rccDaqData (exptname_, "updateInterval",    1);
  compnames_         = new rccDaqData (exptname_, "components",        "unknown");
  compstates_        = new rccDaqData (exptname_, "compstates",        "unknown");
  clientList_        = new rccDaqData (exptname_, "clientList",        "unknown");
  master_            = new rccDaqData (exptname_, "master",            "unknown");
  controlDisp_       = new rccDaqData (exptname_, "controlDisplay",    "unknown");
  online_            = new rccDaqData (exptname_, "online",            1);
  runMsg_            = new rccDaqData (exptname_, "runMessage",        "       ");

  /* create data structure boot infor which has all information
     about auto boot flag of components */
  daqCompBootStruct bootInfo;
  compBootInfo_ = new rccDaqData (exptname_, "compBootInfo", &bootInfo);

  /* create daq run monitoring parameters structure */
  daqMonitorStruct monitorInfo;
  monitorParms_ = new rccDaqData (exptname_, "monitorParms", &monitorInfo);




  /* connect everything to dataManager */

  currTime_->connect (dataManager_);
  currTimeBin_->connect (dataManager_);
  startTime_->connect (dataManager_);
  startTimeBin_->connect (dataManager_);
  endTime_->connect (dataManager_);
  endTimeBin_->connect (dataManager_);
  version_->connect (dataManager_);
  runNumber_->connect (dataManager_);
  status_->connect (dataManager_);
  nevents_->connect (dataManager_);
  nlongs_->connect (dataManager_);

  database_->connect (dataManager_);

  dataFile_->connect (dataManager_);
  confFile_->connect (dataManager_); /*sergey*/

  rcsMsgToDbase_->connect (dataManager_);
  logFileDescriptor_->connect (dataManager_);
  tokenInterval_->connect (dataManager_);  

  allRunTypes_->connect (dataManager_);
  runType_->connect (dataManager_);
  runTypeNum_->connect (dataManager_);
  exptName_->connect (dataManager_);
  exptId_->connect (dataManager_);
  hostName_->connect (dataManager_);
  autoIncrement_->connect (dataManager_);
  eventLimit_->connect (dataManager_);
  dataLimit_->connect (dataManager_);
  updateInterval_->connect (dataManager_);
  compnames_->connect (dataManager_);
  compstates_->connect (dataManager_);
  clientList_->connect (dataManager_);
  master_->connect (dataManager_);
  controlDisp_->connect (dataManager_);
  online_->connect (dataManager_);
  compBootInfo_->connect (dataManager_);
  monitorParms_->connect (dataManager_);
  runMsg_->connect (dataManager_);

  runNumber_->enableWrite ();
  runType_->enableWrite ();
  autoIncrement_->enableWrite ();
  eventLimit_->enableWrite ();
  dataLimit_->enableWrite ();
  updateInterval_->enableWrite ();
  online_->enableWrite ();

  logFileDescriptor_->enableWrite ();
  tokenInterval_->enableWrite ();
  rcsMsgToDbase_->enableWrite ();

  confFile_->enableWrite (); //sergey 

}

void
rcClient::deleteAllVariables (void)
{
  if (!varDeleted_) {
    dataManager_.freeAll ();
    dataManager_.cleanAll ();
    varDeleted_ = 1;
  }
}





int
rcClient::connect (char* database, char* session, char* msqld)
{
  char* host;
  unsigned short port;

  int mst;

  if ((mst = rcSvcInfoFinder::findRcServer (msqld, database, session,
					    host, port)) == CODA_FATAL) {
    fprintf (stderr, "Cannot connect to msql server, Quit. \n");
    ::exit (1);
  }
  else if (mst == CODA_ERROR)
  {
#ifdef _CODA_DEBUG
    fprintf (stderr, "rcClient::connect: RunControl server with session %s is not running\n",
	     session);
#endif
    return CODA_ERROR;
  }
  else
  {
    rcServerLocater svcl (DAFINDSERVER, host, session, port, NUM_CONN_RETRIES);
    unsigned short svcport = 0;
#ifdef _CODA_DEBUG
    printf("rcClient::connect: svcport=%d\n",svcport);
#endif
    if (svcl.locatingServer (svcport) == 0)
    {
#ifdef _CODA_DEBUG
      printf ("rcClient::connect: Try to connect to server at host %s and port %d\n", host, svcport);
#endif
      INET_Addr addr (svcport, host);

      if (conServer_.connect (toServer_, addr) != -1)
      {
	    connected_ = 1;
#ifdef _CODA_DEBUG
	    printf ("rcClient::connect: Connected to the server\n");
#endif
	    reactor_.register_handler (this, Event_Handler::READ_MASK);
      }
    }
    else
    {
      connected_ = 0;
    }
    // free host memory
    delete []host;
  }

  if (connected_)
  {
    // set exptname
    if (exptname_)
      delete []exptname_;
    exptname_ = new char[::strlen (session) + 1];
    ::strcpy (exptname_, session);

    createAllVariables ();
    varDeleted_ = 0;

    // send client user name and process id to the server
    // synchronous call
    infoRegistered_ = 0;
    return sendClientInfo ();
  }
  else
  {
    return CODA_ERROR;
  }
}








int
rcClient::disconnect (void)
{
  static int64_t opcode = DADISCONNECT;
  daqNetData data (exptname_, "command", (int)DADISCONNECT);
  rcMsg msg(opcode,data,0);
  int n = toServer_ << msg;
  connected_ = 0;        // reset connection flag
  lock_ = 0;
  infoRegistered_ = 0;   // reset register information flag
  if (n == -1)
    return CODA_ERROR;
  else {
    // register timer to close socket later
    Time_Value dtv (2.0);
    if (reactor_.schedule_timer (this, (const void *)&opcode, dtv) != -1)
      return CODA_SUCCESS;
    else
      return CODA_ERROR;
  }
}

void
rcClient::killServer (void)
{
  static int64_t opcode = DAZAP;
  daqNetData data (exptname_, "command", (int)DAZAP);
  rcMsg msg(opcode,data,0);
  int n = toServer_ << msg;
  connected_ = 0;        // reset connection flag
  infoRegistered_ = 0;   // reset register information flag
  // register timer to close socket later
  Time_Value dtv (2.0);
  reactor_.schedule_timer (this, (const void *)&opcode, dtv);
}

int
rcClient::disconnectCallback (rcCallback callback, void* arg)
{
  codaRcCallback *cbk = new codaRcCallback (callback, arg);
  codaSlistIterator ite (discCbkList_);
  codaRcCallback *tcbk = 0;

  int error = 0;
  for (ite.init(); !ite; ++ite)
  {
    tcbk = (codaRcCallback *) ite ();
    if (*tcbk == *cbk)
    {
      error = 1;
      break;
    }
  }
  if (!error)
  {
    discCbkList_.add ((void *)cbk);
    return CODA_SUCCESS;
  }
  else
  {
    delete cbk;
    return CODA_ERROR;
  }
}

int
rcClient::handle_timeout (const Time_Value&, const void* arg)
{
  int64_t opcode = *(int64_t *)arg;
  int status = 0;

  switch (opcode){
  case DADISCONNECT:
    // delete all variables
    deleteAllVariables ();
    // close socket connection
    status = reactor_.remove_handler (this, Event_Handler::READ_MASK);
    break;
  case DAZAP:
    // delete all variables
    deleteAllVariables ();
    // close socket connection
    status = reactor_.remove_handler (this, Event_Handler::READ_MASK);
    break;
  default:
    status = -1;
    break;
  }
  return status;
}

int
rcClient::getFd (void) const
{
  return get_handle ();
}

int
rcClient::get_handle (void) const
{
  return toServer_.get_handle ();
}

int
rcClient::handle_input (int)
{
  int n, status = 0;
  int64_t type, size;
  daqNetData data;
  rcMsg recver (DAUNKNOWN, data);

  /* lock the client to prevent recursive calls */
  rcClientLocker locker (this);

  n = toServer_ >> recver;
#ifdef _CODA_DEBUG
  printf ("rcClient::handle_input: %d bytes received from server side\n", n);
#endif
  switch (n)
  {
    case -1:
    case 0:
#ifdef _CODA_DEBUG
      printf ("rcClient::handle_input: Client to Server Connection is Broken\n");
#endif
      status = -1;
      break;

    default:
      switch (recver.type ())
      {
        case DAGET_VAL:
#ifdef _CODA_DEBUG
          printf ("rcClient::handle_input:  get value callback\n");
#endif
          // get value callback
          status = getValCbkFromServer (recver);
          break;
        case DASET_VAL:
#ifdef _CODA_DEBUG
          printf ("rcClient::handle_input:  set value callback\n");
#endif
          // set value callback
          status = setValCbkFromServer (recver);
          break;
        case DAMONITOR_VAL_ON:
#ifdef _CODA_DEBUG
          printf ("rcClient::handle_input: DAMONITOR_VAL_ON\n");
#endif
          status = monitoredValFromServer (recver);
          break;
        case DAMONITOR_VAL_OFF:
#ifdef _CODA_DEBUG
          printf ("rcClient::handle_input: DAMONITOR_VAL_OFF\n");
#endif
          status = monitorOffFromServer (recver);
          break;
        case DAADD_VARS:
#ifdef _CODA_DEBUG
          printf ("rcClient::handle_input: DAADD_VARS\n");
#endif
          status = addDynamicVars (recver);
          break;
        case DAREMOVE_VARS:
#ifdef _CODA_DEBUG
          printf ("rcClient::handle_input: DAREMOVE_VARS\n");
#endif
          status = removeDynamicVars (recver);
          break;
        case DAADD_ANALOG_VARS:
#ifdef _CODA_DEBUG
          printf ("rcClient::handle_input:  15\n");
#endif
          status = addAnaLogVars  (recver);
          break;
        case DAREMOVE_ANALOG_VARS:
#ifdef _CODA_DEBUG
          printf ("rcClient::handle_input:  16\n");
#endif
          status = removeAnaLogVars (recver);
          break;
        default:
#ifdef _CODA_DEBUG
          printf ("rcClient::handle_input: calls commandCbkFromServer\n");
#endif
	      /* command callback */
          status = commandCbkFromServer (recver);
          break;
      }
  }

#ifdef _CODA_DEBUG
  printf ("rcClient::handle_input: returns status=%d\n",status);
#endif

  return(status);
}
    
int
rcClient::handle_close (int, Reactor_Mask)
{
  // remove all callbacks associated with this server
  //  removeCallbacks ();
  deleteAllVariables ();
  // call all disconnect callbacks
  callAllDiscCbks ();
  return toServer_.close ();
}

int
rcClient::pendIO (double seconds)
{
  if (lock_)
    return CODA_INVALID_CALL;

  Time_Value dtv (seconds);
  int nfound = 0;
  if ((nfound = reactor_.handle_events (dtv)) > 0)
    return CODA_SUCCESS;
  else if (nfound == 0)
    return CODA_WARNING;
  else
    return CODA_ERROR;
}

int
rcClient::pendIO (void)
{
  if (lock_)
    return CODA_INVALID_CALL;

  int nfound;
  if ((nfound = reactor_.handle_events ()) > 0)
    return CODA_SUCCESS;
  else if (nfound == 0)
    return CODA_WARNING;
  else
    return CODA_ERROR;
}

void
rcClient::callAllDiscCbks (void)
{
  codaSlistIterator ite (discCbkList_);
  codaRcCallback* cbk = 0;
  rcCallback callback = 0;
  void *arg = 0;

  for (ite.init (); !ite; ++ite)
  {
    cbk = (codaRcCallback *)ite ();
    callback = cbk->callbackFunction ();
    arg = cbk->userarg ();
    (*callback)(CODA_SUCCESS, arg, (daqNetData *)0);
  }
}

int
rcClient::sendCmdCallback (int cmd, daqData& data,
			   rcCallback callback, void* arg)
{
  codaRcCallback* cbk = new codaRcCallback (callback, arg);
  rcMsg msg (cmd, (daqNetData &)data, (int64_t)cbk);

  int n = toServer_ << msg;
#ifdef _CODA_DEBUG
  printf ("--- rcClient::sendCmdCallback: send %d bytes command data to server\n", n);
#endif
  if (n > 0)
  {
    // register callback to the callback table
    cmdCbkTable_.add ((int64_t)cbk, (void *)cbk);
    return CODA_SUCCESS;
  }
  else
    return CODA_ERROR;
  // data will be freed inside msg
}


int
rcClient::sendClientInfo (void)
{
  char* temp[3];
  int  size = 80;

#ifdef _CODA_DEBUG
  printf("--- rcClient::sendClientInfo: username_ >%s< len=%d\n",username_,::strlen(username_));fflush(stdout);
#endif

  /* create a netData holding three char strings */
  temp[0] = new char[::strlen (username_) + 1];
  ::strcpy (temp[0], username_);
  temp[1] = new char[40];
  ::sprintf (temp[1], "%d", pid_);
  temp[2] = new char[::strlen (disp_) + 1];
  ::strcpy (temp[2], disp_);

#ifdef _CODA_DEBUG
  printf("rcClient::sendClientInfo: temp[0] >%s<\n",temp[0]);fflush(stdout);
  printf("rcClient::sendClientInfo: temp[1] >%s<\n",temp[1]);fflush(stdout);
  printf("rcClient::sendClientInfo: temp[2] >%s<\n",temp[2]);fflush(stdout);
#endif

  /* appropriate method from daqNetData.cc will be used, based on parameters types */
  daqNetData data ("RCS","command", temp, 3);

  /* free memory */
  delete []temp[0]; delete []temp[1]; delete []temp[2];

  /* set default callback for notification of server */
  codaRcCallback* cbk = new codaRcCallback (&(rcClient::regInfoCbk), (void *)this);

#ifdef _CODA_DEBUG
  printf("rcClient::sendClientInfo 11\n");fflush(stdout);
#endif

  rcMsg msg (DAREG_CLIENT_INFO, data, (int64_t)cbk);

#ifdef _CODA_DEBUG
  printf("rcClient::sendClientInfo 12\n");fflush(stdout);
#endif

#ifdef _CODA_DEBUG
  printf("rcClient::sendClientInfo 121\n");fflush(stdout);
#endif

  int n = toServer_ << msg;

#ifdef _CODA_DEBUG
  printf("rcClient::sendClientInfo 122\n");fflush(stdout);
#endif

#ifdef _CODA_DEBUG
  printf("rcClient::sendClientInfo: send %d bytes register client info data to server\n", n);fflush(stdout);
#endif
  if (n > 0)
  {
    /* register callback to the callback table */
    cmdCbkTable_.add ((int64_t)cbk, (void *)cbk);
    int i = 0;

    while (!infoRegistered_ && i < DEFAULT_SYNC_NUM_RETRIES)
    {
      pendIO (DEFAULT_SYNC_TIMEOUT);
      i++;
    }

    if (infoRegistered_)
      return CODA_SUCCESS;
    else
      return CODA_ERROR;
  }
  else
    return CODA_ERROR;
}
    

int
rcClient::getValueCallback (char* compname, 
			    char* attrname,
			    rcCallback callback,
			    void* arg)
{
#ifdef _CODA_DEBUG
  printf("rcClient::getValueCallback reached: compname >%s< attrname >%s<\n",compname,attrname);
#endif

  /* this data is not inside the table */
  if (!dataManager_.hasData (compname, attrname)) return CODA_ERROR;

  daqNetData data (compname, attrname, 0);
  codaRcCallback* cbk = new codaRcCallback (callback, arg);
  rcMsg msg (DAGET_VAL, data, (int64_t)cbk);

  int n = toServer_ << msg;
#ifdef _CODA_DEBUG
  printf ("rcClient::getValueCallback: send %d bytes data on getValueCallback to server\n", n);
#endif
  if (n > 0)
  {
    /* register this callback to this data variables */
    daqData *tdata = 0;
    dataManager_.findData (compname, attrname, tdata);
    if (tdata == 0) /* should never happen */
    {
      fprintf (stderr,"Fatal: Something is wrong at getValueCallback\n");
      return CODA_ERROR;
    }
    else
    {
      /* get callback list needs void* ugly */
      tdata->registerGetCbk ((void *)cbk);
#ifdef _CODA_DEBUG
      printf ("rcClient::getValueCallback: number of get value callback of %s %s is %d\n", 
	      tdata->compname(), tdata->attrname(),tdata->numGetCbks ());
#endif
      return CODA_SUCCESS;
    }
  }
  else
  {
    return CODA_ERROR;
  }
}

int
rcClient::setValueCallback (daqData& data,
			    rcCallback callback,
			    void* arg)
{
  // this data is not inside the table
  if (!dataManager_.hasData (data.compname(), data.attrname()))
    return CODA_ERROR;

  daqData *tdata = 0;
  dataManager_.findData (data.compname(), data.attrname(), tdata);
  if (!tdata->writable ()) {
    // this data is read only
    return CODA_ERROR;
  }

  codaRcCallback* cbk = new codaRcCallback (callback, arg);
  rcMsg msg (DASET_VAL, (daqNetData &)data, (int64_t)cbk);

  int n = toServer_ << msg;
#ifdef _CODA_DEBUG
  printf ("rcClient::setValueCallback: send %d bytes data on setValueCallback to server\n", n);
#endif
  if (n > 0)
  {
    // set callback list needs void*
    tdata->registerSetCbk ((void *)cbk);
#ifdef _CODA_DEBUG
    printf ("rcClient::setValueCallback: number of set value callback of %s %s is %d\n", 
	    tdata->compname(), tdata->attrname(),tdata->numSetCbks ());
#endif
    return CODA_SUCCESS;
  }
  else
    return CODA_ERROR;
}
 
int
rcClient::monitorOnCallback (char* compname, 
							 char* attrname,
							 rcCallback callback,
							 void* arg)
{
#ifdef _CODA_DEBUG
  printf("rcClient::monitorOnCallback reached, comp >%s< attr >%s<\n",compname,attrname);
#endif

  /* this data is not inside the table */

  if (!dataManager_.hasData (compname, attrname))
  {
    return CODA_ERROR;
  }

  codaRcCallback* cbk = new codaRcCallback (callback, arg);
    
  daqNetData data (compname, attrname, 0);
  
  rcMsg msg (DAMONITOR_VAL_ON, data, (int64_t)cbk);
  
  int n = toServer_ << msg;
#ifdef _CODA_DEBUG
  printf ("rcClient::monitorOnCallback: send %d bytes data on monitor Callback to server\n", n);
#endif
  if (n > 0)
  {
    // register this callback to this data variables
    daqData *tdata = 0;
    dataManager_.findData (compname, attrname, tdata);
    if (tdata == 0) /* should never happen */
	{
      fprintf (stderr,"Fatal: Soemthing is wrong at getValueCallback\n");
      return CODA_ERROR;
    }
    else
    {
      // monitor callback list needs void* 
      tdata->monitorOn ((void *)cbk);
#ifdef _CODA_DEBUG
      printf ("-- rcClient::monitorOnCallback: number of monitor on callback of %s %s is %d\n", 
	      tdata->compname(), tdata->attrname(),tdata->numMonitorCbks ());
#endif
      return CODA_SUCCESS;
    }
  }
  else
  {
    return CODA_ERROR;
  }
}


int
rcClient::monitorOffCallback (char* compname, 
			      char* attrname,
			      rcCallback moncallback,
			      void* monarg,
			      rcCallback callback,
			      void* arg)
{
#ifdef _CODA_DEBUG
  printf("rcClient::monitorOffCallback reached, comp >%s< attr >%s<\n",compname,attrname);
#endif

  if (callback == 0) return CODA_ERROR;

  /* client side does not have this data */
  if (!dataManager_.hasData (compname, attrname)) return CODA_ERROR;

  daqData* edata = 0;
  dataManager_.findData (compname, attrname, edata);

  /* on client side all daqData are rccDaqData, so it is safe to do this */
  rccDaqData* rccdata = (rccDaqData *)edata;
  codaRcCallback tcbk (moncallback, monarg);
  if (!rccdata->hasMonCallback (tcbk)) return CODA_ERROR;
  codaRcCallback *cbk = rccdata->monCallback (tcbk);

  /* nothing goes out for monitor off */
#ifdef Linux_x86_64
  daqNetData data (compname, attrname, (int64_t)cbk);
#else
  daqNetData data (compname, attrname, (int)cbk);
#endif

  /* all callback information is inside offcbk */
  codaRcCallback* offcbk = new codaRcCallback (callback, arg);
  rcMsg msg (DAMONITOR_VAL_OFF, data, (int64_t)offcbk);
  
  int n = toServer_ << msg;
#ifdef _CODA_DEBUG
  printf ("rcClient::monitorOffCallback: send %d bytes data on monitor off to server\n", n);
#endif
  if (n > 0)
  {
    edata->registerMonOffCbk ((void *)offcbk);
#ifdef _CODA_DEBUG
    printf ("rcClient::monitorOffCallback: number of monitor off callback of %s %s is %d\n",
	    edata->compname(), edata->attrname(), edata->numMonOffCbks ());
#endif
    return CODA_SUCCESS;
  }
  else
  {
    return CODA_ERROR;
  }
}


int
rcClient::getValCbkFromServer (rcMsg& cmsg)
{
  int status = 0;
  int cbkSt = CODA_SUCCESS;
  // callback id actually is pointer of codaRcCallback pointer
  int64_t cbkId = cmsg.reqId (); 
  daqNetData& ndata = (daqNetData &)cmsg;
  daqData* sdata = 0;
  dataManager_.findData (ndata.name(), ndata.attribute(), sdata);

  if (sdata == 0)
  {
    fprintf (stderr, "getValueCallback cannot find right callback\n");
    status = -1;
  }
  else
  {
    if (sdata->hasGetCbk ((void *)cbkId))
    {
      codaRcCallback *cbk = (codaRcCallback *)cbkId;
      rcCallback func = cbk->callbackFunction ();
      void* arg = cbk->userarg ();
      (*func)(cbkSt, arg, &ndata);

      // callback id associated memory will be freed
      sdata->removeGetCbk ((void *)cbkId);

      // assign new data to the data inside data manager
      *sdata = ndata;
#ifdef _CODA_DEBUG
      printf ("rcClient::getValCbkFromServer: number of get callbacks for %s %s is %d\n",sdata->compname(),
	      sdata->attrname(), sdata->numGetCbks());
#endif
    }
    else
    {
      fprintf (stderr, "getValueCallback cannot find right callback\n");
      status = -1;
    }
  }

  return(status);
}


int
rcClient::setValCbkFromServer (rcMsg& cmsg)
{
  int status = 0;
  int cbkSt = CODA_SUCCESS;
  int64_t cbkId = cmsg.reqId ();
  daqNetData& ndata = (daqNetData &)cmsg;
  daqData* sdata = 0;
  dataManager_.findData (ndata.name(), ndata.attribute(), sdata);

  if (sdata == 0)
  {
    fprintf (stderr, "setValueCallback cannot find right callback\n");
    status = -1;
  }
  else
  {
    if (sdata->hasSetCbk ((void *)cbkId))
    {
      codaRcCallback *cbk = (codaRcCallback *)cbkId;
      rcCallback func = cbk->callbackFunction ();
      void* arg = cbk->userarg ();
      (*func)(cbkSt, arg, &ndata);

      // memory associated with cbkId will be freed
      sdata->removeSetCbk ((void *)cbkId);
#ifdef _CODA_DEBUG
      printf ("== rcClient::setValCbkFromServer: number of set callbacks for %s %s is %d\n",sdata->compname(),
	      sdata->attrname(), sdata->numSetCbks());
#endif
    }
    else
    {
      fprintf (stderr, "setValueCallback cannot find right callback\n");
      status = -1;
    }
  }
  return status;
}

int
rcClient::monitoredValFromServer (rcMsg& cmsg)
{
  int status = 0;
  int cbkSt = CODA_SUCCESS;
  int64_t cbkId = cmsg.reqId ();
  daqNetData& ndata = (daqNetData &)cmsg;
  daqData* sdata = 0;
  dataManager_.findData (ndata.name(), ndata.attribute(), sdata);

#ifdef _CODA_DEBUG
  printf("rcClient::monitoredValFromServer reached, cbkId=0x%x\n",cbkId);
#endif

  if (sdata == 0)
  {
    fprintf (stderr, "rcClient::monitoredValFromServer: monitor callback cannot find right callback\n");
    status = -1;
  }
  else
  {
    if (sdata->hasMonitorCbk ((void *)cbkId))
    {
      codaRcCallback *cbk = (codaRcCallback *)cbkId;
      rcCallback func = cbk->callbackFunction ();
      void* arg = cbk->userarg ();
      (*func)(cbkSt, arg, &ndata);

      /* assign new data to the data inside the data manager */
      *sdata = ndata;

#ifdef _CODA_DEBUG
      printf ("rcClient::monitoredValFromServer: here number of monitor callbacks for comp >%s< attr >%s< is %d %08x\n",
		sdata->compname(), sdata->attrname(), sdata->numMonitorCbks(), func);
#endif

#ifdef _CODA_DEBUG
      if (strcmp(sdata->attrname(),DYN_ATTR2)==0 || strcmp(sdata->attrname(),DYN_ATTR6)==0)
	    printf ("rcClient::monitoredValFromServer: here number of monitor callbacks for comp >%s< attr >%s< is %d %08x\n",
		  sdata->compname(), sdata->attrname(), sdata->numMonitorCbks(), func);
#endif
    }
    else
    {
      fprintf (stderr, "rcClient::monitoredValFromServer: monitor Callback cannot find right callback\n");
      status = -1;
    }
  }
  return(status);
}

int
rcClient::monitorOffFromServer (rcMsg& cmsg)
{
  int status = 0;
  int cbkst = CODA_SUCCESS;
  daqNetData& ndata = (daqNetData &)cmsg;

  /* the server side puts status or callback id which is casted codaRcCallback inside ndata */
  int64_t moncbk = (int64_t)ndata;

  /* the server side puts monitor off callback information in the request id */
  int64_t cbkId = cmsg.reqId ();
  daqData* sdata = 0;
  dataManager_.findData (ndata.name(), ndata.attribute(), sdata);

  assert (sdata != 0);

  if (moncbk != 0) /* server side removed monitor on callbacks successfully */
  {
    if (sdata->hasMonitorCbk ((void *)moncbk)) /* memory associated with this moncbk will be freed */
	{
      sdata->monitorOff ((void *)moncbk);
	}
  }
  else
  {
    cbkst = CODA_ERROR;
  }

  if (sdata->hasMonOffCbk ((void *)cbkId))
  {
    codaRcCallback *cbk = (codaRcCallback *)cbkId;
    rcCallback func = cbk->callbackFunction ();
    void* arg = cbk->userarg ();
    (*func)(cbkst, arg, &ndata);
    /* memory associated with this cbkId will be freed inside the following */
    sdata->removeMonOffCbk ((void *)cbkId);
#ifdef _CODA_DEBUG
    printf ("rcClient::monitorOffFromServer: number of monitor off callbacks for comp >%s< attr >%s< is %d\n",
	    sdata->compname(), sdata->attrname(),
	    sdata->numMonOffCbks());
#endif
  }
  else
  {
    status = -1;
  }

  return status;
}

int
rcClient::commandCbkFromServer (rcMsg& cmsg)
{
#ifdef _CODA_DEBUG
  printf ("rcClient::commandCbkFromServer reached\n");fflush(stdout);
#endif
  int64_t cbkId = cmsg.reqId ();
#ifdef _CODA_DEBUG
  printf ("rcClient::commandCbkFromServer 1\n");fflush(stdout);
#endif
  if (cmdCbkTable_.find (cbkId, (void *)cbkId))
  {
#ifdef _CODA_DEBUG
    printf ("rcClient::commandCbkFromServer 2\n");fflush(stdout);
#endif
    codaRcCallback *cbk = (codaRcCallback *)cbkId;
#ifdef _CODA_DEBUG
    printf ("rcClient::commandCbkFromServer 3\n");fflush(stdout);
#endif
    rcCallback func = cbk->callbackFunction ();
#ifdef _CODA_DEBUG
    printf ("rcClient::commandCbkFromServer 4\n");fflush(stdout);
#endif
    void* arg = cbk->userarg ();
#ifdef _CODA_DEBUG
    printf ("rcClient::commandCbkFromServer 5\n");fflush(stdout);
#endif

    /* check status */
    int st = (daqNetData)cmsg; /* status of remote execution of command */
#ifdef _CODA_DEBUG
    printf ("rcClient::commandCbkFromServer 6: st=%d\n",st);fflush(stdout);
#endif
    daqNetData& da = (daqNetData &)cmsg;
    (*func)(st, arg, &da);

    /* free memory associated with this command callback */
    cmdCbkTable_.remove (cbkId, (void *)cbkId);
    delete cbk;

#ifdef _CODA_DEBUG
    printf ("rcClient::commandCbkFromServer: Has Command callback number %d\n",numCmdCbks());fflush(stdout);
#endif
    return(0);
  }
  else
  {
#ifdef _CODA_DEBUG
    printf ("rcClient::commandCbkFromServer: ERROR\n");fflush(stdout);
#endif
    return(-1);
  }
}
void
rcClient::offCallback (int status, void* arg, daqNetData* data)
{
}






extern Display *MainDisplay;
/*
extern void *MainDisplay;
extern "C" void coda_Send(void *d, char *n, char *m);
*/



/* unlike attr6Callback below, called not every time portHandler updates, but only from transactions .. */

void
rcClient::attr2Callback (int status, void* arg, daqNetData* data)
{
  rcClient *obj = (rcClient *)arg; /*sergey*/

  int state = (int)(*data);
  char cmd[1000];

  if(status != 0) return;

#ifdef _CODA_DEBUG
  printf("********* rcClient::attr2Callback state=%d name >%s< attr >%s<\n",state,data->name(),data->attribute());fflush(stdout);
#endif





#if 0 /* do not need it here, do it from attr6Callback ... */
#ifdef USE_CREG
  sprintf(cmd,"s:%d %s",state,data->name());
#ifdef _CODA_DEBUG
  printf("!!!!!!!!! CEDIT 39: rcClient::attr2Callback: coda_Send() cmd=>%s<\n",cmd);
#endif
  if(MainDisplay) coda_Send(MainDisplay,"CEDIT",cmd);
  /*coda_Send(XtDisplay(obj->baseWidget()),"CEDIT",cmd);*/
#endif

#endif




}



/* sergey: called every time when component report it's status to portHandler (see rcServer/Network.s/portHandler.cc);
rcServer's daqRun::createDynamicVars() creates dvars including DYN_ATTR6 (which is "state") and sends message 'DAADD_VARS',
which will be recieved by rcClient::handle_input() and processed by rcClient::addDynamicVars() */

/* after all that every change in portHandler to status will trigger 'attr6Callback' call here !!!??? */

void
rcClient::attr6Callback (int status, void* arg, daqNetData* data)
{
  rcClient *obj = (rcClient *)arg; /*sergey*/

  int state = (int)(*data);
  char cmd[1000];

#ifdef _CODA_DEBUG
  printf("********* rcClient::attr6Callback status=%d\n",status);fflush(stdout);
#endif

  if(status != 0) return;

#ifdef _CODA_DEBUG
  printf("********* rcClient::attr6Callback state=%d name >%s< attr >%s<\n",state,data->name(),data->attribute());fflush(stdout);
#endif

#ifdef USE_CREG
  sprintf(cmd,"s:%d %s",state,data->name());
#ifdef _CODA_DEBUG
  printf("!!!!!!!!! CEDIT 30: rcClient::attr6Callback: coda_Send() cmd=>%s<\n",cmd);
#endif
  if(MainDisplay) coda_Send(MainDisplay,"CEDIT",cmd);
  /*coda_Send(XtDisplay(obj->baseWidget()),"CEDIT",cmd);*/
#endif
}








int
rcClient::addDynamicVars (rcMsg& cmsg)
{
  daqNetData& ndata = (daqNetData &)cmsg;
  char *token;
  int i, j;

  /* assume no more than MAX_NUM_DYN_VARS components name coming back from server */
  int count = MAX_NUM_DYN_VARS;
  char *names[1000];
  char *name[1000];
  char *attr[1000];
  daqData* data = 0;

#ifdef _CODA_DEBUG
  printf (">> rcClient::addDynamicVars: add all variables >%s<\n", (char *)ndata);
  printf (">>>>> rcClient::addDynamicVars: count1=%d\n",count);
#endif

  if (ndata.getData (names, count) != CODA_ERROR)
  {

#ifdef _CODA_DEBUG
    printf (">>>>> rcClient::addDynamicVars: count2=%d\n",count);
#endif

	/*sergey: seems was ERROR: 'count' already counted every component 4 times (see daqRun.cc::createDynamicVars, sent
        from rcServer to us as DAADD_VARS) for DYN_ATTR2, DYN_ATTR3, DYN_ATTR4 and DYN_ATTR5); fixed it below
	*/

    for(i=0; i<count; i++)
    {

      /* spliting names[] into name[] and attr[]
         NOTE: names[] will be changed in process, only first part (before ':') left
         which makes names[] and name[] the same */
      token = strtok(names[i], ":"); /* get the first token */
      j = 0;
      while (token != NULL) /* walk through other tokens */
      {
        /*printf("%s\n",token);*/
        if(j==0) name[i] = token;
        else if(j==1) attr[i] = token;
        token = strtok(NULL, ":");
        j++;
      }
      if(j!=2)
	  {
        printf("ERROR in rcClient::addDynamicVars: the number of fields in names arrived from daqRun is %d, must be 2\n",j);
        printf("ERROR in rcClient::addDynamicVars: names[%d] >%s<\n",i,names[i]);
        exit(0);
	  }
#ifdef _CODA_DEBUG
      else
	  {
		printf("rcClient::addDynamicVars: names[%d] >%s< -> name[%d] >%s< attr[%d] >%s<\n",i,names[i],i,name[i],i,attr[i]);
	  }
#endif


#ifdef _CODA_DEBUG
      printf (">> rcClient::addDynamicVars: Add variable >%s< with attribute >%s<\n",name[i],attr[i]);
#endif

      daqData* ldata = new rccDaqData (name[i], attr[i], 0);
      /* delete if already here */
      if (ldata->connect (dataManager_) != CODA_SUCCESS) delete ldata;

	  /*sergey: was like that, now daqRun sends both name and attribute
      daqData* ldata = new rccDaqData (names[i], DYN_ATTR0, 0);
      daqData* edata = new rccDaqData (names[i], DYN_ATTR1, 0);
      daqData* sdata = new rccDaqData (names[i], DYN_ATTR2, 0);
      daqData* erdata = new rccDaqData (names[i], DYN_ATTR3, 0);
      daqData* drdata = new rccDaqData (names[i], DYN_ATTR4, 0);
      daqData* livetime = new rccDaqData (names[i], DYN_ATTR5, 0);
      if (ldata->connect (dataManager_) != CODA_SUCCESS) delete ldata;
      if (edata->connect (dataManager_) != CODA_SUCCESS) delete edata;
      if (sdata->connect (dataManager_) != CODA_SUCCESS) delete sdata;
      if (erdata->connect (dataManager_) != CODA_SUCCESS) delete erdata;
      if (drdata->connect (dataManager_) != CODA_SUCCESS) delete drdata;
      if (livetime->connect (dataManager_) != CODA_SUCCESS) delete livetime;
	  */

      /* only for DYN_ATTR2 and DYN_ATTR6, call 'monitorOnCallback()' */

      if(!strcmp(attr[i],DYN_ATTR2))
	  {
        monitorOffCallback (name[i], attr[i], (rcCallback)&(attr2Callback), (void *)this, (rcCallback)&(offCallback), (void *)this);
        if (monitorOnCallback (name[i], attr[i],(rcCallback)&(rcClient::attr2Callback),(void*)this) != CODA_SUCCESS)
        {
	      printf ("rcClient::addDynamicVars: ERROR: Cannot register monitor on name %s attr %s\n", name[i], attr[i]);
        }
#ifdef _CODA_DEBUG
        else
	    {
	      printf (">> rcClient::addDynamicVars: registered monitor on name %s attr %s\n", name[i], attr[i]);
	    }
#endif
	  }

      if(!strcmp(attr[i],DYN_ATTR6))
	  {
        monitorOffCallback (name[i], attr[i], (rcCallback)&(attr6Callback), (void *)this, (rcCallback)&(offCallback), (void *)this);
        if (monitorOnCallback (name[i], attr[i],(rcCallback)&(rcClient::attr6Callback),(void*)this) != CODA_SUCCESS)
        {
	      printf ("rcClient::addDynamicVars: ERROR: Cannot register monitor on name %s attr %s\n", name[i], attr[i]);
        }
#ifdef _CODA_DEBUG
        else
	    {
	      printf (">> rcClient::addDynamicVars: registered monitor on name %s attr %s\n", name[i], attr[i]);
	    }
#endif
	  }

      // free memory of names[i]
      delete []names[i];
    }
  }

#ifdef _CODA_DEBUG
  printf (">>>>> rcClient::addDynamicVars done\n");
#endif

  return(0);
}





// not realy remove these variables, but notify these
// variables that remote data variables are no longer valid.
// Reconnection will be handled in the monitoredValFromServer
int
rcClient::removeDynamicVars (rcMsg& cmsg)
{
  daqNetData& ndata = (daqNetData &)cmsg;
  /* assume no more than MAX_NUM_DYN_VARS components name coming back from server */
  int count = MAX_NUM_DYN_VARS;
  char *names[1000];

  /* at client side all daqData are the type of rccDaqData */
  daqData* data = 0; 
  rccDaqData* rccdata = 0;

  if (ndata.getData (names, count) != CODA_ERROR)
  {
    for (int i = 0; i < count; i++)
    {
#ifdef _CODA_DEBUG
      printf ("rcClient::removeDynamicVars: Vars %s disconnected\n",names[i]);
#endif
      if (dataManager_.findData (names[i], DYN_ATTR0, data) == CODA_SUCCESS)
      {
	    rccdata = (rccDaqData *)data;
	    rccdata->notifyDisconnection ();
      }

      if (dataManager_.findData (names[i], DYN_ATTR1, data) == CODA_SUCCESS)
      {
	    rccdata = (rccDaqData *)data;
	    rccdata->notifyDisconnection ();
      }

      if (dataManager_.findData (names[i], DYN_ATTR2, data) == CODA_SUCCESS)
      {
	    rccdata = (rccDaqData *)data;
	    rccdata->notifyDisconnection ();
      }

      if (dataManager_.findData (names[i], DYN_ATTR3, data) == CODA_SUCCESS)
      {
	    rccdata = (rccDaqData *)data;
	    rccdata->notifyDisconnection ();
      }

      if (dataManager_.findData (names[i], DYN_ATTR4, data) == CODA_SUCCESS)
      {
	    rccdata = (rccDaqData *)data;
	    rccdata->notifyDisconnection ();
	  }

	  /*sergey*/
      if (dataManager_.findData (names[i], DYN_ATTR5, data) == CODA_SUCCESS)
      {
	    rccdata = (rccDaqData *)data;
	    rccdata->notifyDisconnection ();
      }

	  /*sergey*/
      if (dataManager_.findData (names[i], DYN_ATTR6, data) == CODA_SUCCESS)
      {
	    rccdata = (rccDaqData *)data;
	    rccdata->notifyDisconnection ();
      }



      // free memory of names[i]
      delete []names[i];
    }
  }
  return 0;
}

int
rcClient::addAnaLogVars (rcMsg& cmsg)
{
  daqNetData& ndata = (daqNetData &)cmsg;
  // assume no more than MAX_NUM_DYN_VARS components name coming back from
  // server: unsafe, but no way > MAX_NUM_DYN_VARS components
  int count = MAX_NUM_DYN_VARS;
  char *names[1000];
  daqData* data = 0;

#ifdef _CODA_DEBUG
  printf ("rcClient::addAnaLogVars: add all variables %s\n", (char *)ndata);
#endif
  if (ndata.getData (names, count) != CODA_ERROR)
  {
    for (int i = 0; i < count; i++)
    {
      daqData* ldata = new rccDaqData (names[i], DYN_ANA_LOG, "unknown");
#ifdef _CODA_DEBUG
      printf ("rcClient::addAnaLogVars: Add ANA Log variables %s %s\n",names[i], DYN_ANA_LOG);
#endif
      if (ldata->connect (dataManager_) == CODA_SUCCESS)
	ldata->enableWrite ();
      else // already there
	delete ldata;
      // free memory of names[i]
      delete []names[i];
    }
  }
  return 0;
}

// not realy remove these variables, but notify these
// variables that remote data variables are no longer valid.
// Reconnection will be handled in the monitoredValFromServer
int
rcClient::removeAnaLogVars (rcMsg& cmsg)
{
  daqNetData& ndata = (daqNetData &)cmsg;
  // assume no more than MAX_NUM_DYN_VARS components name coming back from
  // server
  int count = MAX_NUM_DYN_VARS;
  char *names[1000];

  // at client side all daqData are the type of rccDaqData
  daqData* data = 0; 
  rccDaqData* rccdata = 0;

  if (ndata.getData (names, count) != CODA_ERROR) {
    for (int i = 0; i < count; i++) {
#ifdef _CODA_DEBUG
      printf ("rcClient::removeAnaLogVars: Vars %s %s disconnected\n",names[i], DYN_ANA_LOG);
#endif
      if (dataManager_.findData (names[i], DYN_ANA_LOG, data)
	  == CODA_SUCCESS) {
	rccdata = (rccDaqData *)data;
	rccdata->notifyDisconnection ();
      }
      // free memory of names[i]
      delete []names[i];
    }
  }
  return 0;
}

int
rcClient::numCmdCbks (void)
{
  codaIntHashIterator ite (cmdCbkTable_);

  int count = 0;

  for (ite.init (); !ite; ++ite) count++;

  return(count);
}

void
rcClient::removeCallbacks (void)
{
  daqDataManagerIterator ite (dataManager_);
  rccDaqData *data = 0;

  for (ite.init (); !ite; ++ite) {
    data = (rccDaqData *)ite ();
    data->removeCallbacks ();
  }
}

void
rcClient::regInfoCbk (int status, void* arg, daqNetData* )
{
  if (status == CODA_SUCCESS)
  {
    rcClient* obj = (rcClient *)arg;
    obj->infoRegistered_ = 1;
#ifdef _CODA_DEBUG
    printf ("rcClient::regInfoCbk: Client Information registered to server, status=%d\n",status);
#endif
  }
  else
  {
#ifdef _CODA_DEBUG
    printf ("rcClient::regInfoCbk: ERROR: Client Information is NOT registered to server, status=%d\n",status);
#endif
  }
}

char*
rcClient::exptname (void) const
{
  return exptname_;
}

daqData*
rcClient::data (char* compname, char* attrname)
{
  daqData* edata = 0;
  dataManager_.findData (compname, attrname, edata);  
  return edata;
}

void
rcClient::lock (void)
{
  lock_ = 1;
}

void
rcClient::unlock (void)
{
  lock_ = 0;
}

rcClientLocker::rcClientLocker (rcClient* client)
:client_ (client)
{
#ifdef _TRACE_OBJECTS
  printf ("rcClientLocker::rcClientLocker: Create rcClientLocker Class Object\n");
#endif
  client_->lock ();
}

rcClientLocker::~rcClientLocker (void)
{
#ifdef _TRACE_OBJECTS
  printf ("rcClientLocker::~rcClientLocker: Delete rcClientLocker Class Object\n");
#endif
  client_->unlock ();
}
