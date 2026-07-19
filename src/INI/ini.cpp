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

#include <STDIO.H>
#include <STRING.H>
#include <ALLOC.H>
#include "ini.h"

//----------------------------------------------------------------------------
// Method: INI::INI ( Constructor )
//
// Description:
//
//   Default constructor for the INI file reader class. Initialises the
//   internal data pointers to NULL and the entry count to zero. The
//   object remains in an uninitialized state until Load is called.
//
// Parameters:
//
//   None.
//
// Return Values:
//
//   None.
//
//----------------------------------------------------------------------------

INI::INI ()
{
	section_names = NULL;		// Section names
	key_names     = NULL;		// Key names
	key_values    = NULL;		// Values
	entry_count   = 0;			// Entries.
};

//----------------------------------------------------------------------------
// Method: INI::~INI ( Destructor )
//
// Description:
//
//   Destructor for the INI file reader class. Releases all far-heap
//   memory allocated during Load.
//
//   - Deallocates each per-entry string buffer.
//
//   - Deallocates the top-level pointer arrays.
//
// Parameters:
//
//   None.
//
// Return Values:
//
//   None.
//
//----------------------------------------------------------------------------

INI::~INI ()
{
	// Only deallocate if memory was previously allocated by Load.

	if ( section_names != NULL )
	{
		// Deallocate each per-entry string buffer.

		for ( long i = 0; i < INI_MAX_ENTRIES; i++ )
		{
			farfree ( section_names [ i ] );
			farfree ( key_names     [ i ] );
			farfree ( key_values    [ i ] );
		}

		// Deallocate the top-level pointer arrays.

		farfree ( section_names );
		farfree ( key_names );
		farfree ( key_values );
	}
};

//----------------------------------------------------------------------------
// Method: INI::AllocateMemory
//
// Description:
//
//   Allocates far-heap memory for the parallel arrays if not already
//   allocated. On first call, allocates the top-level pointer arrays
//   and per-entry string buffers, and resets the entry count to zero.
//   Subsequent calls return immediately without modifying state.
//
// Parameters:
//
//   None.
//
// Return Values:
//
//   result (int):
//   - Returns 0 on success or if memory was already allocated.
//   - Returns -1 if a far-heap allocation fails.
//
//----------------------------------------------------------------------------

int INI::AllocateMemory ()
{
	// If memory has already been allocated, do nothing.

	if ( section_names != NULL )
	{
		return 0;
	}

	// Allocate the top-level pointer arrays.

	section_names = (char **) farmalloc ( INI_MAX_ENTRIES * sizeof ( char * ) );
	key_names     = (char **) farmalloc ( INI_MAX_ENTRIES * sizeof ( char * ) );
	key_values    = (char **) farmalloc ( INI_MAX_ENTRIES * sizeof ( char * ) );

	// Verify that all top-level pointer arrays were allocated.

	if ( section_names == NULL || key_names == NULL || key_values == NULL )
	{
		return -1;
	}

	// Allocate per-entry string buffers for each of the three parallel arrays.

	for ( long i = 0; i < INI_MAX_ENTRIES; i++ )
	{
		section_names [ i ] = (char *) farmalloc ( INI_MAX_STRING_SIZE );
		key_names     [ i ] = (char *) farmalloc ( INI_MAX_STRING_SIZE );
		key_values    [ i ] = (char *) farmalloc ( INI_MAX_STRING_SIZE );

		// Verify that all per-entry string buffers were allocated.

		if ( section_names [ i ] == NULL || key_names [ i ] == NULL || key_values [ i ] == NULL )
		{
			return -1;
		}
	}

	// Reset entry count.

	entry_count = 0;

	return 0;
};

//----------------------------------------------------------------------------
// Method: INI::TrimWhitespace
//
// Description:
//
//   Removes leading and trailing whitespace characters from a string
//   in place. Whitespace includes spaces, tabs, carriage returns, and
//   newline characters.
//
// Parameters:
//
//   string (char *): The string to trim.
//
// Return Values:
//
//   None.
//
//----------------------------------------------------------------------------

void INI::TrimWhitespace ( char *string )
{
	// Initialise local variables.

	char *start  = string;	// Pointer to the first non-whitespace character
	char *end    = NULL;	// Pointer to the last non-whitespace character
	long  length = 0;		// Length of the trimmed string
	long  i      = 0;		// Loop counter

	// Find first non-whitespace character.

	while ( *start == ' ' || *start == '\t' ) start++;

	// Handle empty or all-whitespace string.

	if ( *start == '\0' )
	{
		string [ 0 ] = '\0';
		return;
	}

	// Find last non-whitespace character.

	end = string + strlen ( string ) - 1;

	while ( end > start && ( *end == ' ' || *end == '\t' || *end == '\n' || *end == '\r' ) )
	{
		end--;
	}

	// Copy trimmed string back to original buffer.

	length = end - start + 1;

	for ( i = 0; i < length; i++ )
	{
		string [ i ] = start [ i ];
	}

	// Null-terminate the trimmed string.

	string [ length ] = '\0';
};

//----------------------------------------------------------------------------
// Method: INI::StripQuotes
//
// Description:
//
//   Removes surrounding double-quote characters from a string in
//   place.
//
//   - If the string begins and ends with a double-quote character, both
//     quotes are removed and the inner content is preserved.
//
//   - If the string is not quoted, no modification is made.
//
// Parameters:
//
//   string (char *): The string to strip.
//
// Return Values:
//
//   None.
//
//----------------------------------------------------------------------------

void INI::StripQuotes ( char *string )
{
	// Initialise local variables.

	long length = strlen ( string );	// Length of the input string
	long i      = 0;					// Loop counter

	// Check if the string is enclosed in double quotes.

	if ( length >= 2 && string [ 0 ] == '"' && string [ length - 1 ] == '"' )
	{
		// Shift the inner content one position left to overwrite the opening quote.

		for ( i = 0; i < length - 2; i++ )
		{
			string [ i ] = string [ i + 1 ];
		}

		// Null-terminate at the position of the former closing quote.

		string [ length - 2 ] = '\0';
	}
};

//----------------------------------------------------------------------------
// Method: INI::Load
//
// Description:
//
//   Loads and parses an INI file. Allocates far-heap memory for up to
//   INI_MAX_ENTRIES entries, where each entry stores a section name,
//   key, and value. The parser supports:
//
//   - Line comments beginning with ;
//   - Section headers in the form [section]
//   - Key-value pairs in the form key = value
//   - Quoted values, where surrounding double quotes are stripped
//
//   Key-value pairs appearing before any section header are ignored.
//
// Parameters:
//
//   file_name (const char *): Path to the INI file to load.
//
// Return Values:
//
//   result (int):
//   - Returns 0 on success.
//   - Returns -1 if the file cannot be opened.
//
//----------------------------------------------------------------------------

int INI::Load ( const char *file_name )
{
	// Initialise local variables.

	FILE *file   = NULL;	// File handle for the INI file
	char *equals = NULL;	// Position of the '=' delimiter in the current line
	char *key    = NULL;	// Pointer to the key portion of a key-value pair
	char *value  = NULL;	// Pointer to the value portion of a key-value pair
	char *end    = NULL;	// Position of the ']' delimiter in a section header

	// Line buffer and current section name accumulator.

	char  line            [ INI_MAX_LINE_SIZE   ];
	char  current_section [ INI_MAX_STRING_SIZE ];

	// Allocate memory for entries if not already allocated.

	if ( AllocateMemory () != 0 )
	{
		return -1;
	}

	// Reset entry count and clear the current section name.

	entry_count           = 0;
	current_section [ 0 ] = '\0';

	// Open file.

	file = fopen ( file_name, "r" );

	if ( file == NULL )
	{
		return -1;
	}

	// Parse file line by line.

	while ( fgets ( line, INI_MAX_LINE_SIZE, file ) != NULL )
	{
		TrimWhitespace ( line );

		// Skip empty lines and comments.

		if ( line [ 0 ] == '\0' || line [ 0 ] == ';' )
		{
			continue;
		}

		// Parse section header.

		if ( line [ 0 ] == '[' )
		{
			// Locate the closing bracket of the section header.

			end = strchr ( line, ']' );

			// Extract and store the section name if the closing bracket was found.

			if ( end != NULL )
			{
				*end = '\0';
				strcpy         ( current_section, line + 1 );
				TrimWhitespace ( current_section           );
			}

			// Advance to the next line after processing the section header.

			continue;
		}

		// Skip key-value pairs outside of a section.

		if ( current_section [ 0 ] == '\0' )
		{
			// Key-value pairs are only valid within a named section.

			continue;
		}

		// Parse key-value pair.

		equals = strchr ( line, '=' );

		if ( equals != NULL && entry_count < INI_MAX_ENTRIES )
		{
			// Split the line at the '=' delimiter into key and value substrings.

			*equals = '\0';
			key     = line;
			value   = equals + 1;

			// Clean up whitespace and remove any surrounding quotes from the value.

			TrimWhitespace ( key );
			TrimWhitespace ( value );
			StripQuotes    ( value );

			// Store the parsed entry in the parallel arrays.

			strcpy ( section_names [ entry_count ], current_section );
			strcpy ( key_names     [ entry_count ], key             );
			strcpy ( key_values    [ entry_count ], value           );

			// Advance the entry count to the next available slot.

			entry_count++;
		}
	}

	// Close file.

	fclose ( file );

	return 0;
};

//----------------------------------------------------------------------------
// Method: INI::GetValue
//
// Description:
//
//   Retrieves the value associated with a given section and key. The
//   search is case-sensitive. If the section and key combination is
//   not found, NULL is returned.
//
// Parameters:
//
//   section (const char *): The section name to search for.
//
//   key (const char *): The key name to search for within the section.
//
// Return Values:
//
//   value (char *):
//   - A pointer to the value string if found.
//   - NULL if the section and key combination does not exist.
//
//----------------------------------------------------------------------------

char *INI::GetValue ( const char *section, const char *key )
{
	// Linear search through all stored entries for a matching section and key.

	for ( long i = 0; i < entry_count; i++ )
	{
		// Compare both the section name and key name against the query.

		if ( strcmp ( section_names [ i ], section ) == 0 && strcmp ( key_names [ i ], key ) == 0 )
		{
			// Return a pointer to the matching value string.

			return key_values [ i ];
		}
	}

	// No matching section and key combination was found.

	return NULL;
};

//----------------------------------------------------------------------------
// Method: INI::AddSection
//
// Description:
//
//   Validates that a section can receive entries. If the section
//   already exists among the stored entries, the call succeeds
//   immediately. If the section does not yet exist, the method
//   verifies that capacity remains for at least one new entry.
//
//   This method does not create a physical entry in the parallel
//   arrays. Section headers appear in saved files only when
//   key-value pairs have been added to the section via SetValue.
//
// Parameters:
//
//   section (const char *): The section name to register.
//
// Return Values:
//
//   result (int):
//   - Returns 0 on success.
//   - Returns -1 if the entry table is full and the section does
//     not already exist.
//
//----------------------------------------------------------------------------

int INI::AddSection ( const char *section )
{
	// Ensure memory is allocated.

	if ( AllocateMemory () != 0 )
	{
		return -1;
	}

	// Check if the section already exists in any entry.

	for ( long i = 0; i < entry_count; i++ )
	{
		if ( strcmp ( section_names [ i ], section ) == 0 )
		{
			return 0;
		}
	}

	// Verify that there is capacity for at least one more entry.

	if ( entry_count >= INI_MAX_ENTRIES )
	{
		return -1;
	}

	return 0;
};

//----------------------------------------------------------------------------
// Method: INI::SetValue
//
// Description:
//
//   Adds a new key-value pair or updates an existing one. If an
//   entry with the given section and key already exists, its value
//   is overwritten. Otherwise, a new entry is appended to the
//   parallel arrays.
//
//   Memory is allocated lazily on first use if neither Load nor
//   SetValue has been called before.
//
// Parameters:
//
//   section (const char *): The section name for the entry.
//
//   key (const char *): The key name for the entry.
//
//   value (const char *): The value string to store.
//
// Return Values:
//
//   result (int):
//   - Returns 0 on success.
//   - Returns -1 if the entry table is full and no existing entry
//     matches the given section and key.
//
//----------------------------------------------------------------------------

int INI::SetValue ( const char *section, const char *key, const char *value )
{
	// Ensure memory is allocated.

	if ( AllocateMemory () != 0 )
	{
		return -1;
	}

	// Search for an existing entry with the matching section and key.

	for ( long i = 0; i < entry_count; i++ )
	{
		if ( strcmp ( section_names [ i ], section ) == 0 && strcmp ( key_names [ i ], key ) == 0 )
		{
			// Update the existing entry's value.

			strcpy ( key_values [ i ], value );
			return 0;
		}
	}

	// No existing entry found. Add a new entry if capacity allows.

	if ( entry_count >= INI_MAX_ENTRIES )
	{
		return -1;
	}

	// Store the new entry in the parallel arrays.

	strcpy ( section_names [ entry_count ], section );
	strcpy ( key_names     [ entry_count ], key     );
	strcpy ( key_values    [ entry_count ], value   );

	// Advance the entry count.

	entry_count++;

	return 0;
};

//----------------------------------------------------------------------------
// Method: INI::Save
//
// Description:
//
//   Writes the in-memory entries to an INI file. Entries are
//   grouped by section so that each section header appears once,
//   followed by all key-value pairs belonging to that section.
//
//   Comments from the original file are not preserved, as they
//   are not stored in the in-memory representation.
//
// Parameters:
//
//   file_name (const char *): Path to the output INI file.
//
// Return Values:
//
//   result (int):
//   - Returns 0 on success.
//   - Returns -1 if the file cannot be opened for writing.
//
//----------------------------------------------------------------------------

int INI::Save ( const char *file_name )
{
	// Initialise local variables.

	FILE *file          = NULL;		// File handle for the output INI file
	long  i             = 0;		// Outer loop counter
	long  j             = 0;		// Inner loop counter
	int   first_section = 1;		// Flag to suppress blank line before first section

	// Per-entry write tracking flags.

	int written [ INI_MAX_ENTRIES ];

	// Open file for writing.

	file = fopen ( file_name, "w" );

	if ( file == NULL )
	{
		return -1;
	}

	// Initialise the written flags to zero (not yet written).

	for ( i = 0; i < entry_count; i++ )
	{
		written [ i ] = 0;
	}

	// Write entries grouped by section.

	for ( i = 0; i < entry_count; i++ )
	{
		// Skip entries that have already been written.

		if ( written [ i ] )
		{
			continue;
		}

		// Write a blank line before each section header, except the first.

		if ( !first_section )
		{
			fprintf ( file, "\n" );
		}

		first_section = 0;

		// Write the section header.

		fprintf ( file, "[%s]\n", section_names [ i ] );

		// Write all entries belonging to this section.

		for ( j = i; j < entry_count; j++ )
		{
			if ( strcmp ( section_names [ j ], section_names [ i ] ) == 0 )
			{
				fprintf ( file, "%s = %s\n", key_names [ j ], key_values [ j ] );
				written [ j ] = 1;
			}
		}
	}

	// Close file.

	fclose ( file );

	return 0;
};

//----------------------------------------------------------------------------
//
//----------------------------------------------------------------------------
