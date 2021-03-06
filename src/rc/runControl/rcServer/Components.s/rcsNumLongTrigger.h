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
//      Number of long words daqData Trigger Class
//
// Author:  
//      Jie Chen
//      CEBAF Data Acquisition Group
//
// Revision History:
//   $Log: rcsNumLongTrigger.h,v $
//   Revision 1.1.1.1  1996/10/11 13:39:20  chen
//   run control source
//
//
#ifndef _CODA_RCS_NUMLONG_TRIGGER_H
#define _CODA_RCS_NUMLONG_TRIGGER_H

#include <daqDataTrigger.h>

class daqRun;

class rcsNumLongTrigger: public daqDataTrigger
{
public:
  // constructor and destructor
  rcsNumLongTrigger  (daqRun* run);
  ~rcsNumLongTrigger (void);

  virtual void trigger (daqData* data);

private:
  daqRun* run_;
};
#endif
