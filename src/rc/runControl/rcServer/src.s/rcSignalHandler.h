//-----------------------------------------------------------------------------
// Copyright (c) 1994,1995 Southeastern Universities Research Association,
//                         Continuous Electron Beam Accelerator Facility
//
// This software was developed under a United States Government license
// described in the NOTICE file included as part of this distribution.
//
// CEBAF Data Acquisition Group, 12000 Jefferson Ave., Newport News, VA 23606
//       coda@cebaf.gov  Tel: (804) 249-7030     Fax: (804) 249-5800
//-----------------------------------------------------------------------------
//
// Description:
//      RunControl Server Signal Handler
//
// Author:  
//      Jie Chen
//      CEBAF Data Acquisition Group
//
// Revision History:
//   $Log: rcSignalHandler.h,v $
//   Revision 1.1.1.1  1996/10/11 13:39:14  chen
//   run control source
//
//
#ifndef _CODA_RC_SIGNAL_HANDLER_H
#define _CODA_RC_SIGNAL_HANDLER_H

#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include <signal.h>

extern sig_atomic_t finished;

class rcSignalHandler
{
public:
  // operations
  static int registerSignalHandlers (void);

protected:
  // all signal handlers
  static void signalFunc (int signo);

private:
  rcSignalHandler (void);
};
#endif
