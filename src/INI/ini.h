//****************************************************************************
// Program: INI File Library
// Version: 2.0
// Date:    1992-05-09
// Author:  Rohin Gosling
//
// Description
//
//   INI file reader/writer library that supports comments, sections,
//   and key-value pairs. Provides methods to load, query, manipulate,
//   and save standard Windows-style INI files.
//
// Change Log
//
//   Version 1.1
//
//   - New Feature: Load method to parse and load INI files.
//
//   - New Feature: GetValue method to query key-value pairs by
//     section and key.
//
//   - New Feature: AddSection method to register a new section at
//     runtime.
//
//   - New Feature: SetValue method to add or update key-value pairs
//     at runtime.
//
//   - New Feature: Save method to persist in-memory data to an INI
//     file on disk.
//
//   - Refactor: AllocateMemory private helper method to support lazy
//     memory allocation for the parallel arrays.
//
//   - Refactor: Load method updated to use AllocateMemory helper.
//
//****************************************************************************

#ifndef INI_H
#define INI_H

//----------------------------------------------------------------------------
// Constants
//----------------------------------------------------------------------------

#define INI_MAX_ENTRIES     32  	// Max key-value entries; reduced from 256 for Data Probe's small config (keeps the far-heap footprint small).
#define INI_MAX_STRING_SIZE 80		// Maximum length of a section name, key, or value string
#define INI_MAX_LINE_SIZE   120		// Maximum length of a single line read from the INI file

//----------------------------------------------------------------------------
// Class: INI
//----------------------------------------------------------------------------

class INI
{

	// Member variables.

	char **section_names;	// Array of section name strings, one per entry
	char **key_names;		// Array of key name strings, one per entry
	char **key_values;		// Array of value strings, one per entry
	long   entry_count;		// Number of key-value entries currently stored

	// Private methods.

	void TrimWhitespace ( char *string );
	void StripQuotes    ( char *string );
	int  AllocateMemory ();

public:

	// Constructor(s)

	INI ();

	// Destructor(s)

	~INI ();

	// Methods

	int   Load       ( const char *file_name );
	char *GetValue   ( const char *section, const char *key );
	int   AddSection ( const char *section );
	int   SetValue   ( const char *section, const char *key, const char *value );
	int   Save       ( const char *file_name );

};

//----------------------------------------------------------------------------
//
//----------------------------------------------------------------------------

#endif

//----------------------------------------------------------------------------
//
//----------------------------------------------------------------------------
