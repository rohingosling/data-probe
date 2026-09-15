//****************************************************************************
// Program: Data Probe
// Version: 1.5
// Date:    1992-07-14
// Author:  Rohin Gosling
//
// Description:
//
//   Application-level declarations for Data Probe.
//
//****************************************************************************

#ifndef _D_PROBE
#define _D_PROBE

#include "mtext.h"
#include "mapp.h"

//----------------------------------------------------------------------------
// Program Identity
//----------------------------------------------------------------------------

#define DPROBE_TITLE    "Data Probe"
#define DPROBE_VERSION  "Version 1.5"

//----------------------------------------------------------------------------
// Defaults
//----------------------------------------------------------------------------

// Files open in overwrite mode - empty files included; the append slot
// grows a file at its end in either mode.

#define DPROBE_INSERT_MODE_DEFAULT  false

//----------------------------------------------------------------------------
// Class: DataProbePanel
//
// Description:
//
//   Data Probe's application frame: the standard ApplicationPanel plus
//   the application's global Esc key, which opens the exit confirmation
//   (Yes default; Yes exits to DOS), and the status bar's NumLock
//   indicator, refreshed from the BIOS on every frame.
//
//----------------------------------------------------------------------------

class DataProbePanel : public ApplicationPanel
{
public:

	// Constructors

	DataProbePanel ( const char *name, int rows );

	// Methods

	virtual void Draw      ( TEXTBUFFER *text_buffer );
	virtual bool HandleKey ( KEY_EVENT &key_event );
};

//----------------------------------------------------------------------------

#endif

//----------------------------------------------------------------------------
