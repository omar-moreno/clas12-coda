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
//      RunControl Option Menu For Zooming capability
//
// Author:  
//      Jie Chen
//      CEBAF Data Acquisition Group
//
// Revision History:
//   $Log: rcZoomButton.h,v $
//   Revision 1.1.1.1  1996/10/11 13:39:27  chen
//   run control source
//
//
#ifndef _RC_ZOOM_BUTTON_H
#define _RC_ZOOM_BUTTON_H

#include <rcMenuComd.h>
#include <rcClientHandler.h>

class rcZoomButton: public rcMenuComd
{
public:
  rcZoomButton (char* name, int active,
		char* acc, char* acc_text,
		rcClientHandler& handler);
  virtual ~rcZoomButton (void);

  // class name
  virtual const char* className (void) const {return "rcZoomButton";}

protected:
  virtual void doit (void);
  virtual void undoit (void);
};

#endif
