//****************************************************************************
// Program: Data Probe
// Version: 1.5
// Date:    1992-07-14
// Author:  Rohin Gosling
//
// Description:
//
//   Data Probe is a light weight HEX editor for MS-DOS, built on the 
//   MicroApp text mode visual UI framework.
//
//   - This module owns main() and assembles the application. The application frame 
//     (title bar, menu bar, status bars), the exact File / Edit / Help menu 
//     tree, and t$$he HexView workspace component.
//
//   - This version carries the full HexView editing engine plus real file
//     input/output through the FileBuffer paging engine:
//
//     - File > New / Open / Save / Save As drive the MicroApp file dialogs, 
//       small files edit whole-in-RAM, and larger files page through a 
//       sliding chunk window over a work-file copy.
//
//     - The application glue here re-attaches the view whenever the window 
//       slides (OnPageRequest), reports edits back to the buffer (NoteEdit), 
//       reflows a window whose in-RAM growth nears the physical buffer, and 
//       keeps the title bar showing the open file's name.
//
//   - Paging slides the window with a screenful of overlap and anchors the
//     cursor to the screen row it already occupied, so a crossing shows a
//     brief pause and the data scrolling on beneath a stationary cursor row, 
//     and reversing direction runs back over still-loaded data without 
//     paging again.
//
//   - The Edit menu adds the CUA clipboard (Cut / Copy / Paste, mirrored by
//     the HexView's Ctrl+Ins / Shift+Del / Shift+Ins keys) and the Find /
//     Replace dialogs:
//
//     - They parse a text or hex byte-pair pattern, search the whole file 
//       through the paging engine (sequentially paging chunks for a large 
//       file), and splice replacements in through the same flush path that 
//       carries an interactive edit into the file.
//
// Compilation:
//
//   - Borland C++ 3.1, large memory model. Built by BUILD.BAT from the 
//     build\ directory (see the canonical recipe there).
//
//****************************************************************************

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <process.h>
#include <dir.h>

#include "mtext.h"
#include "mapp.h"
#include "dprobe.h"
#include "hexview.h"
#include "filebuf.h"
#include "ini.h"

//----------------------------------------------------------------------------
// File-Scope State
//----------------------------------------------------------------------------

// Component references for the status, title, and paging handlers, set
// by main before Run.

static HexView   *hex_view_pointer;
static StatusBar *status_bar_pointer;
static TitleBar  *title_bar_pointer;

// The file buffer and chunked paging engine. Its destructor closes and
// deletes the work file after main returns.

static FileBuffer file_buffer;

// The composed title bar text ("Data Probe - FILE.EXT").

static char title_text [ 96 ];

// The application frame, kept for the live Settings row switch (Relayout).

static ApplicationPanel *application_frame_pointer;

//----------------------------------------------------------------------------
// Settings Model
//----------------------------------------------------------------------------

// Runtime settings with compile-time defaults. The defaults are compiled
// into the .EXE and persist to CONFIG.INI beside the executable (see
// Settings Persistence below), which overlays them at startup.

struct SETTINGS
{
	BYTE          rows;                  // Screen rows: 25 / 43 / 50 (43 and 50 are device-dependent).
	bool          address_hex;           // Address column base: true = hex, false = decimal.
	bool          divider_enabled;       // Mid-row '-' hex divider on/off.
	bool          margin_enabled;        // Relative-position margin on/off.
	unsigned long buffer_size;           // Configured chunk / buffer size (1024 .. ceiling).
	bool          insert_default;        // Launch / New default insert mode (overwrite).
	bool          word_little_endian;    // Word status field byte order (little-endian).
	bool          show_about_on_launch;  // Show the About dialog when the application launches.
};

static SETTINGS settings =
{
	TEXT_MODE_25_ROWS,             // rows
	true,                          // address_hex
	true,                          // divider_enabled
	true,                          // margin_enabled
	FILE_BUFFER_DEFAULT_MAX,       // buffer_size
	DPROBE_INSERT_MODE_DEFAULT,    // insert_default (overwrite)
	true,                          // word_little_endian (little-endian)
	true                           // show_about_on_launch (shown at startup by default)
};

// The chosen printer device (Printer Setup); Print writes the text dump here.

static char printer_device [ 16 ] = "LPT1";

//----------------------------------------------------------------------------
// Settings Persistence (CONFIG.INI)
//----------------------------------------------------------------------------
//
// Settings persist to CONFIG.INI in the executable's own directory (resolved
// from argv[0] at startup - see main). The compile-time SETTINGS defaults
// above are the authoritative fallback: LoadSettings overlays only the keys
// actually present and valid in the file, so a missing file, a missing key,
// or a garbled value each leaves that field at its compiled default. A
// missing file is re-created from the defaults, so the app is always left
// with a valid, hand-editable CONFIG.INI.

#define CONFIG_SECTION_DISPLAY  "Display"
#define CONFIG_SECTION_EDITOR   "Editor"
#define CONFIG_SECTION_STARTUP  "Startup"

static char config_path [ MAXPATH ] = "CONFIG.INI";   // Full path; set from argv[0] in main.

//----------------------------------------------------------------------------
// Function: ConfigParseBool
//
// Description:
//
//   Interprets an INI value string as a boolean. Accepts On/Off, Yes/No,
//   and 1/0 (case-insensitive). A NULL pointer (absent key) or an
//   unrecognized value returns default_value, so a missing or garbled entry
//   keeps the compiled default.
//
// Arguments:
//
//   - value         : The INI value string, or NULL if the key was absent.
//   - default_value : The value returned when value is NULL / unrecognized.
//
// Returns:
//
//   - The parsed boolean, or default_value.
//
//----------------------------------------------------------------------------

static bool ConfigParseBool ( const char *value, bool default_value )
{
	// An absent key keeps the compiled default.

	if ( value == NULL ) return default_value;

	// Accept the three truthy and three falsy spellings, case-insensitively.

	if ( stricmp ( value, "On"  ) == 0 ) return true;
	if ( stricmp ( value, "Yes" ) == 0 ) return true;
	if ( stricmp ( value, "1"   ) == 0 ) return true;
	if ( stricmp ( value, "Off" ) == 0 ) return false;
	if ( stricmp ( value, "No"  ) == 0 ) return false;
	if ( stricmp ( value, "0"   ) == 0 ) return false;

	// An unrecognized value keeps the compiled default.

	return default_value;
}

//----------------------------------------------------------------------------
// Function: LoadSettings
//
// Description:
//
//   Overlays CONFIG.INI onto the current (compiled-default) settings. Each
//   key is applied only when present and valid; anything missing or garbled
//   leaves that field untouched, so the compiled defaults always stand in.
//   The buffer size is adopted as-is here and clamped later by
//   FileBuffer::Allocate against free memory.
//
// Arguments:
//
//   - None
//
// Returns:
//
//   - true if CONFIG.INI was read, false if it was missing / unreadable
//     (settings then keep their compiled defaults).
//
//----------------------------------------------------------------------------

static bool LoadSettings ( void )
{
	// Local variables.

	INI   ini;
	char *value;

	// A missing / unreadable file keeps every compiled default.

	if ( ini.Load ( config_path ) != 0 ) return false;

	// Display.

	value = ini.GetValue ( CONFIG_SECTION_DISPLAY, "Rows" );
	if ( value != NULL )
	{
		int requested = atoi ( value );

		if      ( requested == TEXT_MODE_43_ROWS ) settings.rows = TEXT_MODE_43_ROWS;
		else if ( requested == TEXT_MODE_50_ROWS ) settings.rows = TEXT_MODE_50_ROWS;
		else if ( requested == TEXT_MODE_25_ROWS ) settings.rows = TEXT_MODE_25_ROWS;
	}

	value = ini.GetValue ( CONFIG_SECTION_DISPLAY, "AddressBase" );
	if ( value != NULL )
	{
		if      ( stricmp ( value, "Hex"     ) == 0 ) settings.address_hex = true;
		else if ( stricmp ( value, "Decimal" ) == 0 ) settings.address_hex = false;
	}

	value = ini.GetValue ( CONFIG_SECTION_DISPLAY, "WordOrder" );
	if ( value != NULL )
	{
		if      ( stricmp ( value, "Little" ) == 0 ) settings.word_little_endian = true;
		else if ( stricmp ( value, "Big"    ) == 0 ) settings.word_little_endian = false;
	}

	settings.divider_enabled = ConfigParseBool ( ini.GetValue ( CONFIG_SECTION_DISPLAY, "HexDivider"     ), settings.divider_enabled );
	settings.margin_enabled  = ConfigParseBool ( ini.GetValue ( CONFIG_SECTION_DISPLAY, "RelativeMargin" ), settings.margin_enabled );

	// Editor.

	settings.insert_default = ConfigParseBool ( ini.GetValue ( CONFIG_SECTION_EDITOR, "InsertByDefault" ), settings.insert_default );

	value = ini.GetValue ( CONFIG_SECTION_EDITOR, "BufferSize" );
	if ( value != NULL )
	{
		unsigned long requested = strtoul ( value, NULL, 10 );

		if ( requested != 0 ) settings.buffer_size = requested;
	}

	// Startup.

	settings.show_about_on_launch = ConfigParseBool ( ini.GetValue ( CONFIG_SECTION_STARTUP, "ShowAbout" ), settings.show_about_on_launch );

	return true;
}

//----------------------------------------------------------------------------
// Function: SaveSettings
//
// Description:
//
//   Writes the current settings to CONFIG.INI in the executable's directory,
//   creating the file if it does not exist. Values are stored in a readable
//   form (row count, Hex/Decimal, Little/Big, On/Off) so the file can be
//   hand-edited. A write failure (e.g. read-only media) is ignored - the app
//   keeps running on the in-memory settings.
//
// Arguments:
//
//   - None
//
// Returns:
//
//   - None
//
//----------------------------------------------------------------------------

static void SaveSettings ( void )
{
	// Local variables.

	INI  ini;
	char number [ 16 ];

	// Display. The first SetValue triggers the INI's lazy far-heap allocation;
	// bail before Save (which would truncate CONFIG.INI) if it fails.

	if ( ini.SetValue ( CONFIG_SECTION_DISPLAY, "Rows", ultoa ( ( unsigned long ) settings.rows, number, 10 ) ) != 0 ) return;
	ini.SetValue ( CONFIG_SECTION_DISPLAY, "AddressBase",    settings.address_hex        ? "Hex"    : "Decimal" );
	ini.SetValue ( CONFIG_SECTION_DISPLAY, "WordOrder",      settings.word_little_endian ? "Little" : "Big" );
	ini.SetValue ( CONFIG_SECTION_DISPLAY, "HexDivider",     settings.divider_enabled    ? "On"     : "Off" );
	ini.SetValue ( CONFIG_SECTION_DISPLAY, "RelativeMargin", settings.margin_enabled     ? "On"     : "Off" );

	// Editor.

	ini.SetValue ( CONFIG_SECTION_EDITOR, "InsertByDefault", settings.insert_default ? "On" : "Off" );
	ini.SetValue ( CONFIG_SECTION_EDITOR, "BufferSize",      ultoa ( settings.buffer_size, number, 10 ) );

	// Startup.

	ini.SetValue ( CONFIG_SECTION_STARTUP, "ShowAbout", settings.show_about_on_launch ? "On" : "Off" );

	ini.Save ( config_path );
}

//----------------------------------------------------------------------------
// Function: SyncEditState
//
// Description:
//
//   Reports a pending HexView edit back to the file buffer: NoteEdit
//   tracks the chunk's new length and marks the chunk and file dirty.
//   Called before status refreshes, paging, and saves so the buffer's
//   bookkeeping always reflects the last keystroke.
//
// Arguments:
//
//   - None
//
// Returns:
//
//   - None
//
//----------------------------------------------------------------------------

static void SyncEditState ( void )
{
	// Report a pending edit's new length once, then clear the view's flag.

	if ( hex_view_pointer->GetModified () )
	{
		file_buffer.NoteEdit ( hex_view_pointer->GetDataLength () );

		hex_view_pointer->ClearModified ();
	}
}

//----------------------------------------------------------------------------
// Function: ReattachHexView
//
// Description:
//
//   Points the hex view at the file buffer's loaded window and restores
//   the cursor: SetData installs the buffer and window length, SetWindow
//   places the window at its file offset with the file's tail beyond it
//   (driving the absolute addresses and the margin's seam cues), and the
//   cursor lands at its absolute position within the new window.
//
//   With screen_row >= 0 the cursor is anchored to that view row, so the
//   highlight bar stays visually stationary across an overlap page - the
//   user sees a brief pause and the data scroll on beneath an unmoved
//   cursor row, not a jump.
//
// Arguments:
//
//   - absolute_offset : The absolute file byte offset for the cursor.
//   - cursor_nibble   : The cursor's nibble half (0 or 1).
//   - screen_row      : The view row to anchor the cursor to, or -1 to
//                       simply scroll the cursor into view.
//
// Returns:
//
//   - None
//
//----------------------------------------------------------------------------

static void ReattachHexView ( unsigned long absolute_offset, int cursor_nibble, int screen_row )
{
	// Local variables.

	unsigned long window_base;
	unsigned long window_offset;

	// Attach the view to the loaded chunk and place the window in the file.

	window_base = file_buffer.GetChunkOffset ();

	hex_view_pointer->SetData ( file_buffer.GetBuffer (), file_buffer.GetChunkLength (), file_buffer.GetBufferSize () );

	hex_view_pointer->SetWindow ( window_base,
	                              file_buffer.GetFileLength () - window_base - file_buffer.GetChunkLength () );

	// The margin's chunk-boundary map: the chunk grid when the file is
	// paged, none for a whole-file load.

	hex_view_pointer->SetChunkSize ( file_buffer.GetChunked () ? file_buffer.GetChunkSize () : 0 );

	// The insert/overwrite mode is the caller's to set (New / Open pick the
	// configured default; paging and saves preserve it) - overwrite mode
	// can grow an empty or ended file at its end, so it is never forced.

	// Land the cursor at its absolute position within the new window.

	window_offset = ( absolute_offset > window_base ) ? absolute_offset - window_base : 0;

	if ( screen_row >= 0 ) hex_view_pointer->SetCursorPositionAnchored ( window_offset, cursor_nibble, screen_row );
	else                   hex_view_pointer->SetCursorPosition         ( window_offset, cursor_nibble );
}

//----------------------------------------------------------------------------
// Function: UpdateTitleBar
//
// Description:
//
//   Sets the title bar to "Data Probe - FILE.EXT" for a named file, or
//   the bare program title for an unnamed buffer.
//
// Arguments:
//
//   - None
//
// Returns:
//
//   - None
//
//----------------------------------------------------------------------------

static void UpdateTitleBar ( void )
{
	// Local variables.

	const char *path;

	// Compose "Data Probe - FILE.EXT", or the bare title for an unnamed buffer.

	path = file_buffer.GetFilePath ();

	if ( path [ 0 ] != '\0' ) sprintf ( title_text, "%s - %s", DPROBE_TITLE, path );
	else                      strcpy  ( title_text, DPROBE_TITLE );

	title_bar_pointer->SetText ( title_text );
}

//----------------------------------------------------------------------------
// Function: ShowFileError / ConfirmDiscard
//
// Description:
//
//   ShowFileError reports a failed file operation in a MessageBox.
//   ConfirmDiscard guards New and Open: with unsaved changes pending it
//   asks before the file state is discarded (Esc / No keeps editing).
//
// Arguments:
//
//   - message : ShowFileError: the error text to show.
//
// Returns:
//
//   - ConfirmDiscard: true when it is safe to discard the buffer.
//
//----------------------------------------------------------------------------

static void ShowFileError ( const char *message )
{
	MessageBox message_box ( "file-error-box", "Data Probe", message );

	message_box.RunModal ( Application::GetInstance () );
}

static bool ConfirmDiscard ( void )
{
	// A clean buffer needs no confirmation.

	if ( !file_buffer.GetFileDirty () ) return true;

	// Ask before unsaved changes are discarded.

	ConfirmationBox confirmation ( "discard-confirm", "Data Probe", "Discard unsaved changes?" );

	// Yes discards; No / Esc keeps editing.

	return confirmation.RunModal ( Application::GetInstance () ) == DIALOG_RESULT_YES;
}

//----------------------------------------------------------------------------
// Function: OnHexViewChange
//
// Description:
//
//   OnChange handler for the hex view, fired on every cursor move, edit,
//   and mode toggle: reports pending edits to the file buffer (reflowing
//   the chunk when in-RAM growth nears the physical buffer), then
//   refreshes both status rows - the hex row (row max-1) and the decimal
//   row (row max) - with the live Size / Offset / Selected / Byte / Word
//   values (word little-endian, size and offset absolute over the whole
//   file) and the insert-mode indicator.
//
// Arguments:
//
//   - sender    : The hex view.
//   - user_data : Unused.
//
// Returns:
//
//   - None
//
//----------------------------------------------------------------------------

static void OnHexViewChange ( Component *sender, void *user_data )
{
	// Local variables.

	char          value [ 16 ];
	unsigned long absolute_offset;
	int           cursor_nibble;
	int           anchor_row;

	( void ) sender;
	( void ) user_data;

	// Report any pending edit before the status reads the buffer's state.

	SyncEditState ();

	// Restore the window's insert headroom when in-RAM growth nears the
	// physical buffer: flush (splicing the growth into the work file)
	// and reload at the nominal window size, holding the cursor on its
	// screen row so the reflow is invisible beyond its brief pause.

	if ( file_buffer.GetChunked ()
	  && ( file_buffer.GetChunkLength () + FILE_BUFFER_REFLOW_MARGIN >= file_buffer.GetBufferSize () ) )
	{
		absolute_offset = hex_view_pointer->GetAbsoluteOffset ();
		cursor_nibble   = hex_view_pointer->GetCursorNibble ();
		anchor_row      = hex_view_pointer->GetCursorScreenRow ();

		if ( file_buffer.ReflowChunk ( absolute_offset ) == FILE_IO_OK )
		{
			ReattachHexView ( absolute_offset, cursor_nibble, anchor_row );
		}
	}

	// Hex status row.

	sprintf ( value, "%08lX", hex_view_pointer->GetFileLength () );
	status_bar_pointer->SetValue ( 0, 0, value );

	sprintf ( value, "%08lX", hex_view_pointer->GetAbsoluteOffset () );
	status_bar_pointer->SetValue ( 0, 1, value );

	sprintf ( value, "%04lX", hex_view_pointer->GetSelectedLength () );
	status_bar_pointer->SetValue ( 0, 2, value );

	sprintf ( value, "%02X", hex_view_pointer->GetCursorByte () );
	status_bar_pointer->SetValue ( 0, 3, value );

	sprintf ( value, "%04X", hex_view_pointer->GetCursorWord () );
	status_bar_pointer->SetValue ( 0, 4, value );

	status_bar_pointer->SetField ( 0, 5, hex_view_pointer->GetInsertMode () ? "[INS]" : "[   ]", "" );

	// Decimal status row.

	sprintf ( value, "%lu", hex_view_pointer->GetFileLength () );
	status_bar_pointer->SetValue ( 1, 0, value );

	sprintf ( value, "%lu", hex_view_pointer->GetAbsoluteOffset () );
	status_bar_pointer->SetValue ( 1, 1, value );

	sprintf ( value, "%lu", hex_view_pointer->GetSelectedLength () );
	status_bar_pointer->SetValue ( 1, 2, value );

	sprintf ( value, "%u", hex_view_pointer->GetCursorByte () );
	status_bar_pointer->SetValue ( 1, 3, value );

	sprintf ( value, "%u", hex_view_pointer->GetCursorWord () );
	status_bar_pointer->SetValue ( 1, 4, value );
}

//----------------------------------------------------------------------------
// Function: OnHexViewPageRequest
//
// Description:
//
//   OnPageRequest handler for the hex view, fired when the cursor moves
//   beyond the loaded window: syncs any pending edit, slides the window
//   to the requested offset (flushing the dirty chunk first - the brief
//   pause of chunked paging), and re-attaches the view there.
//
//   The window slides with a screenful of overlap: the new window keeps
//   the rows the user was just looking at on the approach side, so the
//   cursor row can be anchored where it already sat (no jump), and
//   reversing direction scrolls straight back over loaded data without
//   paging again. The next seam is a screenful away, on the far side of
//   what is now on screen.
//
// Arguments:
//
//   - sender    : The hex view.
//   - user_data : Unused.
//
// Returns:
//
//   - None
//
//----------------------------------------------------------------------------

static void OnHexViewPageRequest ( Component *sender, void *user_data )
{
	// Local variables.

	unsigned long target_offset;
	unsigned long context_bytes;
	int           anchor_row;

	( void ) sender;
	( void ) user_data;

	// Report any pending edit before the window slides.

	SyncEditState ();

	// The wanted position, a screenful of overlap, and the cursor's row to anchor.

	target_offset = hex_view_pointer->GetPageRequestOffset ();
	context_bytes = ( unsigned long ) hex_view_pointer->GetHeight ()*HEX_VIEW_BYTES_PER_ROW;
	anchor_row    = hex_view_pointer->GetCursorScreenRow ();

	if ( file_buffer.PageTo ( target_offset, context_bytes ) != FILE_IO_OK )
	{
		ShowFileError ( "Cannot page the file." );

		return;
	}

	// Re-attach the view at the new window, holding the cursor's row.

	ReattachHexView ( target_offset, hex_view_pointer->GetPageRequestNibble (), anchor_row );
}

//----------------------------------------------------------------------------
// Function: RunExitConfirmation
//
// Description:
//
//   Opens the exit ConfirmationBox (Yes holds the default focus; Esc =
//   No) and quits the application on Yes. With unsaved changes pending
//   the message says so.
//
// Arguments:
//
//   - None
//
// Returns:
//
//   - None
//
//----------------------------------------------------------------------------

static void RunExitConfirmation ( void )
{
	// Local variables.

	const char *message;

	// Say so when unsaved changes would be discarded.

	message = file_buffer.GetFileDirty ()
	        ? "Discard unsaved changes and exit to DOS?"
	        : "Exit to DOS?";

	ConfirmationBox confirmation ( "exit-confirm", "Exit", message );

	if ( confirmation.RunModal ( Application::GetInstance () ) == DIALOG_RESULT_YES )
	{
		Application::GetInstance ()->Quit ();
	}
}

//----------------------------------------------------------------------------
// Dialog Runner Prototypes
//----------------------------------------------------------------------------

// The application frame's Ctrl+G / Ctrl+F / Ctrl+R shortcuts open these
// dialogs, which are defined further down beside the search machinery they
// drive. Everything else in this file is defined before it is used.

static void RunGoto    ( void );
static void RunFind    ( void );
static void RunReplace ( void );

//----------------------------------------------------------------------------
// Function: NumLockIndicatorText
//
// Description:
//
//   The status bar's NumLock indicator text for the keyboard's current
//   state, read from the BIOS shift-state byte. The lock keys never reach
//   the keyboard buffer, so the state can only be polled, never received
//   as a key event.
//
// Arguments:
//
//   - None
//
// Returns:
//
//   - "[NUM]" while NumLock is on, "[   ]" while it is off.
//
//----------------------------------------------------------------------------

static const char *NumLockIndicatorText ( void )
{
	return ( ( GetShiftState () & SHIFT_STATE_NUM_LOCK ) != 0 ) ? "[NUM]" : "[   ]";
}

//----------------------------------------------------------------------------
// Function: DataProbePanel::DataProbePanel / Draw / HandleKey
//
// Description:
//
//   Data Probe's application frame: the standard ApplicationPanel, plus the
//   application's global Esc key, which opens the exit confirmation
//   instead of quitting outright, and the Ctrl+G / Ctrl+F / Ctrl+R
//   shortcuts, which open the Goto / Find / Replace dialogs directly.
//   F10 (the menu toggle) comes from the ApplicationPanel base.
//
//   The shortcuts reach the frame only from the main editor: keys arrive
//   at the focused HexView first and bubble here unclaimed (a Ctrl combo
//   carries a control ASCII, which neither the hex editor's digit filter
//   nor the character editor's printable-only test accepts), while menus
//   and dialogs read the keyboard in their own modal loops and never
//   dispatch through the component tree at all.
//
//   Draw refreshes the NumLock indicator from the BIOS before drawing the
//   tree, so the field always reflects the keyboard as of the frame being
//   rendered. The run loop re-renders on a shift-state change, so a
//   NumLock press repaints the indicator without waiting for a keystroke.
//
// Arguments:
//
//   - new_name    : Component name.
//   - new_rows    : Screen rows.
//   - text_buffer : The back buffer to draw into.
//   - key_event   : The key event to handle.
//
// Returns:
//
//   - HandleKey: true if the key was consumed, false to bubble it.
//
//----------------------------------------------------------------------------

DataProbePanel::DataProbePanel ( const char *new_name, int new_rows ) : ApplicationPanel ( new_name, new_rows )
{
}

void DataProbePanel::Draw ( TEXTBUFFER *text_buffer )
{
	// Track the keyboard's NumLock state, then draw the frame.

	GetStatusBar ()->SetField ( 1, 5, NumLockIndicatorText (), "" );

	ApplicationPanel::Draw ( text_buffer );
}

bool DataProbePanel::HandleKey ( KEY_EVENT &key_event )
{
	// Esc opens the exit confirmation.

	if ( key_event.scan_code == KEY_ESC )
	{
		RunExitConfirmation ();

		return true;
	}

	// Ctrl+G / Ctrl+F / Ctrl+R open the Goto / Find / Replace dialogs - the
	// combinations the Edit menu advertises in its shortcut column.

	if ( ( GetShiftState () & SHIFT_STATE_CTRL ) != 0 )
	{
		if ( key_event.scan_code == KEY_G )
		{
			RunGoto ();

			return true;
		}

		if ( key_event.scan_code == KEY_F )
		{
			RunFind ();

			return true;
		}

		if ( key_event.scan_code == KEY_R )
		{
			RunReplace ();

			return true;
		}
	}

	// Everything else (F10, the menu key) falls to the base.

	return ApplicationPanel::HandleKey ( key_event );
}

//----------------------------------------------------------------------------
// Function: OnMenuExit
//
// Description:
//
//   File > Exit: runs the exit confirmation.
//
// Arguments:
//
//   - sender    : The menu item.
//   - user_data : Unused.
//
// Returns:
//
//   - None
//
//----------------------------------------------------------------------------

static void OnMenuExit ( Component *sender, void *user_data )
{
	( void ) sender;
	( void ) user_data;

	RunExitConfirmation ();
}

//----------------------------------------------------------------------------
// Function: OnMenuNew / OnMenuOpen
//
// Description:
//
//   File > New and File > Open. Both first confirm the discard of any
//   unsaved changes. New starts a blank, unnamed, unwritten buffer with
//   no dialog - an empty file, which the hex view presents as its
//   minimum appearance (one ghost row carrying the cursor); nothing
//   touches the disk until Save names it (falling through to Save As).
//   Open loads the chosen file - whole into the buffer when it fits, or
//   through the chunked paging engine's work-file copy when it is
//   larger - and re-attaches the view at offset 0 with the title bar
//   showing the file's name.
//
// Arguments:
//
//   - sender    : The menu item.
//   - user_data : Unused.
//
// Returns:
//
//   - None
//
//----------------------------------------------------------------------------

static void OnMenuNew ( Component *sender, void *user_data )
{
	( void ) sender;
	( void ) user_data;

	// Unsaved changes must be confirmed away first.

	if ( !ConfirmDiscard () ) return;

	// Start the blank, unnamed buffer.

	file_buffer.NewFile ( "" );

	// A new file starts in the configured insert default (overwrite);
	// overwrite mode grows it at its end as hex/characters are typed.

	hex_view_pointer->SetInsertMode ( settings.insert_default );

	// Present the empty buffer: view, title, and status.

	ReattachHexView ( 0, 0, -1 );
	UpdateTitleBar  ();
	OnHexViewChange ( NULL, NULL );
}

static void OnMenuOpen ( Component *sender, void *user_data )
{
	// Local variables.

	char name [ 64 ];

	( void ) sender;
	( void ) user_data;

	// Unsaved changes must be confirmed away first.

	if ( !ConfirmDiscard () ) return;

	// Run the Open dialog; the block releases the dialog's stack before the load.

	{
		FileOpenDialog dialog;

		if ( dialog.RunModal ( Application::GetInstance () ) != DIALOG_RESULT_OK ) return;

		strcpy ( name, ( const char * ) dialog.GetFileName () );
	}

	// An empty name is a cancel.

	if ( name [ 0 ] == '\0' ) return;

	if ( file_buffer.Open ( name ) != FILE_IO_OK )
	{
		// A failed open discards the previous file state, so the view
		// re-attaches to the now-empty buffer either way.

		ShowFileError ( "Cannot open file." );
	}

	// A file opens in the configured insert default (overwrite); an
	// empty file grows at its end through the append slot in either mode.

	hex_view_pointer->SetInsertMode ( settings.insert_default );

	// Present the opened file: view, title, and status.

	ReattachHexView ( 0, 0, -1 );
	UpdateTitleBar  ();
	OnHexViewChange ( NULL, NULL );
}

//----------------------------------------------------------------------------
// Function: RunSaveCommand / OnMenuSave / OnMenuSaveAs
//
// Description:
//
//   File > Save and File > Save As. Save writes straight back to the
//   file's path; an unnamed buffer (and always Save As) asks for the
//   name through the FileSaveAsDialog first. A chunked Save flushes the
//   dirty chunk and copies the work file over the original, reloading
//   the buffer on the chunk grid - so the view re-attaches at the cursor
//   after any successful save.
//
// Arguments:
//
//   - always_ask_name : true = Save As (always run the name dialog).
//   - sender          : The menu item.
//   - user_data       : Unused.
//
// Returns:
//
//   - None
//
//----------------------------------------------------------------------------

static void RunSaveCommand ( bool always_ask_name )
{
	// Local variables.

	char          name [ 64 ];
	unsigned long absolute_offset;
	int           cursor_nibble;
	int           anchor_row;
	int           result;

	// Report any pending edit, and remember the cursor to restore after the save.

	SyncEditState ();

	absolute_offset = hex_view_pointer->GetAbsoluteOffset ();
	cursor_nibble   = hex_view_pointer->GetCursorNibble ();
	anchor_row      = hex_view_pointer->GetCursorScreenRow ();

	// Save As, or a Save on an unnamed buffer, asks for the name first.

	if ( always_ask_name || ( file_buffer.GetFilePath () [ 0 ] == '\0' ) )
	{
		FileSaveAsDialog dialog;

		// Seed the Name box with the current file name so Save As opens on the
		// existing name (the file list otherwise pre-selects the first entry).

		if ( file_buffer.GetFilePath () [ 0 ] != '\0' )
		{
			dialog.SetFileName ( file_buffer.GetFilePath () );
		}

		if ( dialog.RunModal ( Application::GetInstance () ) != DIALOG_RESULT_OK ) return;

		strcpy ( name, ( const char * ) dialog.GetFileName () );

		if ( name [ 0 ] == '\0' ) return;

		result = file_buffer.SaveAs ( name );
	}
	else
	{
		result = file_buffer.Save ();
	}

	if ( result != FILE_IO_OK )
	{
		ShowFileError ( "Cannot save file." );

		return;
	}

	// Re-attach at the cursor (a chunked save reloaded the buffer) and refresh.

	ReattachHexView ( absolute_offset, cursor_nibble, anchor_row );
	UpdateTitleBar  ();
	OnHexViewChange ( NULL, NULL );
}

static void OnMenuSave ( Component *sender, void *user_data )
{
	( void ) sender;
	( void ) user_data;

	RunSaveCommand ( false );
}

static void OnMenuSaveAs ( Component *sender, void *user_data )
{
	( void ) sender;
	( void ) user_data;

	RunSaveCommand ( true );
}

//****************************************************************************
// Clipboard, Goto, Find, and Replace
//****************************************************************************

//----------------------------------------------------------------------------
// Search State and Constants
//----------------------------------------------------------------------------

#define GOTO_ADDRESS_CAPACITY   16          // Maximum Goto address field length (10 decimal digits fit with room to spare).
#define FIND_PATTERN_CAPACITY   64          // Maximum search / replacement length in bytes.
#define REPLACE_RESULT_NEXT     1           // ReplaceDialog: substitute the next match.
#define REPLACE_RESULT_ALL      2           // ReplaceDialog: substitute every match.
#define REPLACE_ALL_LIMIT       0x7FFFFFFFUL   // Backstop against a non-terminating Replace All.

// The last search / replacement fields and mode, so the dialogs re-open
// pre-filled and a Find can be repeated with a single keystroke.

static char last_find_text    [ FIND_PATTERN_CAPACITY ];
static char last_replace_text [ FIND_PATTERN_CAPACITY ];
static bool last_search_hex = false;

//----------------------------------------------------------------------------
// Function: HexDigit / ParsePattern
//
// Description:
//
//   HexDigit maps a hex character to its 0-15 value (or -1). ParsePattern
//   turns a Find/Replace field into a byte pattern: in text mode each
//   character is one byte; in hex mode the field is pairs of hex digits
//   (spaces ignored), each pair a byte. A trailing lone hex nibble or a
//   non-hex character is malformed.
//
// Arguments:
//
//   - character : HexDigit: the character to map.
//   - text      : The field text to parse.
//   - hex       : true = parse as hex byte pairs, false = literal text.
//   - pattern   : Output byte buffer.
//   - capacity  : The output buffer's capacity in bytes.
//
// Returns:
//
//   - HexDigit: the digit value 0-15, or -1.
//   - ParsePattern: the byte count (0 for an empty field), or -1 when a
//     hex field is malformed.
//
//----------------------------------------------------------------------------

static int HexDigit ( char character )
{
	if ( ( character >= '0' ) && ( character <= '9' ) ) return character - '0';
	if ( ( character >= 'a' ) && ( character <= 'f' ) ) return character - 'a' + 10;
	if ( ( character >= 'A' ) && ( character <= 'F' ) ) return character - 'A' + 10;

	return -1;
}

static int ParsePattern ( const char *text, bool hex, BYTE *pattern, int capacity )
{
	// Local variables.

	int length;
	int high_nibble;
	int digit;
	int i;

	length = 0;

	// Text mode: each character is one byte.

	if ( !hex )
	{
		for ( i = 0; ( text [ i ] != '\0' ) && ( length < capacity ); i++ )
		{
			pattern [ length++ ] = ( BYTE ) text [ i ];
		}

		// The literal byte count.

		return length;
	}

	// Hex mode: accumulate digit pairs, ignoring spacing between bytes.

	high_nibble = -1;

	for ( i = 0; text [ i ] != '\0'; i++ )
	{
		if ( ( text [ i ] == ' ' ) || ( text [ i ] == '\t' ) ) continue;

		digit = HexDigit ( text [ i ] );

		if ( digit < 0 ) return -1;

		if ( high_nibble < 0 )
		{
			high_nibble = digit;
		}
		else
		{
			if ( length >= capacity ) return length;

			pattern [ length++ ] = ( BYTE ) ( ( high_nibble << 4 ) | digit );
			high_nibble          = -1;
		}
	}

	if ( high_nibble >= 0 ) return -1;      // A trailing lone nibble is malformed.

	// The parsed byte count.

	return length;
}

//----------------------------------------------------------------------------
// Function: DialogGotoHandler / DialogFindHandler /
//           DialogReplaceNextHandler / DialogReplaceAllHandler /
//           DialogCancelHandler
//
// Description:
//
//   Button handlers for the Goto, Find, and Replace dialogs: each closes
//   the owning dialog (passed as user_data) with its own result code.
//
// Arguments:
//
//   - sender    : The button.
//   - user_data : The owning Dialog.
//
// Returns:
//
//   - None
//
//----------------------------------------------------------------------------

static void DialogGotoHandler ( Component *sender, void *user_data )
{
	( void ) sender;

	( ( Dialog * ) user_data )->Close ( DIALOG_RESULT_OK );
}

static void DialogFindHandler ( Component *sender, void *user_data )
{
	( void ) sender;

	( ( Dialog * ) user_data )->Close ( DIALOG_RESULT_OK );
}

static void DialogReplaceNextHandler ( Component *sender, void *user_data )
{
	( void ) sender;

	( ( Dialog * ) user_data )->Close ( REPLACE_RESULT_NEXT );
}

static void DialogReplaceAllHandler ( Component *sender, void *user_data )
{
	( void ) sender;

	( ( Dialog * ) user_data )->Close ( REPLACE_RESULT_ALL );
}

static void DialogCancelHandler ( Component *sender, void *user_data )
{
	( void ) sender;

	( ( Dialog * ) user_data )->Close ( DIALOG_RESULT_CANCEL );
}

//----------------------------------------------------------------------------
// Class: HexTextBox
//
// Description:
//
//   A dialog text box that, while its linked Type radio is set to Hex,
//   limits typed input to upper-case hex digits: a hex digit (0-9 a-f A-F)
//   is upper-cased and inserted, any other printable character is dropped,
//   and editing / navigation keys pass through unchanged. In text mode (or
//   with no linked radio) it is a plain TextBox. Used for the Find and
//   Replace search fields so a hex byte-pair pattern is entered cleanly.
//
//----------------------------------------------------------------------------

class HexTextBox : public TextBox
{
	// Data Members

protected:

	RadioButton *hex_mode_radio;    // Input is limited to hex digits while this radio is selected.

	// Mutators

public:

	void SetHexModeRadio ( RadioButton *new_hex_mode_radio ) { hex_mode_radio = new_hex_mode_radio; }

	// Constructors

	HexTextBox ( const char *name, int x, int y, int width, int height, unsigned edit_buffer_size, bool multi_line );

	// Methods

	virtual bool HandleKey ( KEY_EVENT &key_event );
};

HexTextBox::HexTextBox
(
	const char *new_name,
	int         new_x,
	int         new_y,
	int         new_width,
	int         new_height,
	unsigned    new_edit_buffer_size,
	bool        new_multi_line
)
: TextBox ( new_name, new_x, new_y, new_width, new_height, new_edit_buffer_size, new_multi_line )
{
	hex_mode_radio = NULL;
}

bool HexTextBox::HandleKey ( KEY_EVENT &key_event )
{
	// Local variables.

	int  digit_value;
	char upper_case;

	// In hex mode keep only hex digits, upper-cased; drop any other
	// printable character. Editing and navigation keys fall through to the
	// base text box unchanged.

	if ( ( hex_mode_radio != NULL ) && hex_mode_radio->IsSelected () && ( key_event.ascii >= 32 ) && ( key_event.ascii != 127 ) )
	{
		digit_value = HexDigit ( ( char ) key_event.ascii );

		if ( digit_value < 0 ) return true;   // Consume and ignore a non-hex character.

		upper_case = ( digit_value < 10 ) ? ( char ) ( '0' + digit_value ) : ( char ) ( 'A' + digit_value - 10 );

		key_event.ascii = ( BYTE ) upper_case;
		key_event.key   = ( WORD ) ( ( key_event.key & 0xFF00 ) | ( BYTE ) upper_case );
	}

	// Everything else edits normally.

	return TextBox::HandleKey ( key_event );
}

//----------------------------------------------------------------------------
// Class: GotoDialog
//
// Description:
//
//   The Goto dialog: an Address field, a Base = Hex / Decimal selector,
//   and Goto / Cancel buttons. Enter anywhere is the Goto default.
//
//   Both the field's seed and the selector start from the current state -
//   the address seeds with the cursor's own offset and the base with the
//   configured address number system - so the common case (nudge the
//   address already on screen) needs no retyping. The selector picks the
//   base the field is parsed in, and while it reads Hex the field
//   live-filters typed input to upper-case hex digits, exactly as the Find
//   dialog's search field does in Hex mode.
//
//----------------------------------------------------------------------------

class GotoDialog : public Dialog
{
	// Data Members

protected:

	HexTextBox       address_box;
	Label            base_label;
	RadioButtonGroup base_group;
	RadioButton      base_hex;
	RadioButton      base_decimal;
	Button           goto_button;
	Button           cancel_button;

	// Accessors

public:

	bool            IsHex          ( void ) { return base_hex.IsSelected (); }
	const char far *GetAddressText ( void ) { return address_box.GetEditText (); }

	// Constructors

	GotoDialog ( void );

	// Methods

	virtual bool HandleKey ( KEY_EVENT &key_event );
};

GotoDialog::GotoDialog ( void )
: Dialog        ( "goto-dialog",      18, 7, 44, 12, "Goto" ),
  address_box   ( "goto-address",     0, 0, 40, 3, GOTO_ADDRESS_CAPACITY, false ),
  base_label    ( "goto-base-label",  0, 0, "Base:" ),
  base_group    ( "goto-base-group",  0, 0, 40, 1 ),
  base_hex      ( "goto-base-hex",    0, 0, "Hex" ),
  base_decimal  ( "goto-base-dec",    0, 0, "Decimal" ),
  goto_button   ( "goto-do",          0, 0, 12, "Goto" ),
  cancel_button ( "goto-cancel",      0, 0, 10, "Cancel" )
{
	// Local variables.

	Application *application;
	char         seed_text [ GOTO_ADDRESS_CAPACITY ];
	int          rows;

	// Center the dialog for the actual row count.

	application = Application::GetInstance ();
	rows        = ( application != NULL ) ? ( int ) application->GetRows () : 25;

	x = ( 80 - width ) / 2;
	y = ( rows - height ) / 2;

	// Place the controls relative to the dialog.

	// The two base radios stack vertically under one another, sharing a
	// column beside the label - the Settings dialog's arrangement.

	address_box.SetPosition   ( x + 2,  y + 1 );
	base_label.SetPosition    ( x + 2,  y + 5 );
	base_hex.SetPosition      ( x + 11, y + 5 );
	base_decimal.SetPosition  ( x + 11, y + 6 );
	goto_button.SetPosition   ( x + 7,  y + 9 );
	cancel_button.SetPosition ( x + 27, y + 9 );

	// The address box: black text on the light-gray dialog, a white frame /
	// label while focused and dark-gray while not, and hex-only input while
	// the Base is Hex.

	address_box.SetText                     ( "Address" );
	address_box.SetColors                   ( COLOR_BLACK, COLOR_LIGHT_GRAY );
	address_box.SetSelectedBorderForeground ( COLOR_WHITE );
	address_box.SetHexModeRadio             ( &base_hex );

	// Seed the field with the cursor's current offset, formatted in the
	// configured address base - the base the selector opens on.

	if ( settings.address_hex ) sprintf ( seed_text, "%lX", hex_view_pointer->GetAbsoluteOffset () );
	else                        sprintf ( seed_text, "%lu", hex_view_pointer->GetAbsoluteOffset () );

	address_box.SetEditText ( seed_text );

	// Labels and radios read black on the light-gray dialog.

	base_label.SetColors   ( COLOR_BLACK, COLOR_LIGHT_GRAY );
	base_hex.SetColors     ( COLOR_BLACK, COLOR_LIGHT_GRAY );
	base_decimal.SetColors ( COLOR_BLACK, COLOR_LIGHT_GRAY );

	// Group the radios and seed them from the configured address base.

	base_group.AddRadioButton ( &base_hex );
	base_group.AddRadioButton ( &base_decimal );

	base_group.SelectRadioButton ( settings.address_hex ? &base_hex : &base_decimal );

	// Wire the buttons and assemble the dialog.

	goto_button.SetOnActivate   ( DialogGotoHandler,   this );
	cancel_button.SetOnActivate ( DialogCancelHandler, this );

	AddComponent ( &address_box );
	AddComponent ( &base_label );
	AddComponent ( &base_group );
	AddComponent ( &goto_button );
	AddComponent ( &cancel_button );
}

bool GotoDialog::HandleKey ( KEY_EVENT &key_event )
{
	// Enter is the Goto default.

	if ( key_event.scan_code == KEY_ENTER )
	{
		Close ( DIALOG_RESULT_OK );

		return true;
	}

	return Dialog::HandleKey ( key_event );
}

//----------------------------------------------------------------------------
// Class: FindDialog
//
// Description:
//
//   The Find dialog: a search field, a Text / Hex type
//   selector (so the field reads as a literal string or a byte-pair
//   sequence), a From = Start / Cursor origin selector, and Find / Cancel
//   buttons. Enter anywhere is the Find default. The search string echoes
//   the last search so a find can be repeated quickly.
//
//----------------------------------------------------------------------------

class FindDialog : public Dialog
{
	// Data Members

protected:

	HexTextBox       search_box;
	Label            type_label;
	Label            from_label;
	RadioButtonGroup type_group;
	RadioButton      type_text;
	RadioButton      type_hex;
	RadioButtonGroup from_group;
	RadioButton      from_start;
	RadioButton      from_cursor;
	Button           find_button;
	Button           cancel_button;

	// Accessors

public:

	bool            IsHex         ( void ) { return type_hex.IsSelected (); }
	bool            FromStart     ( void ) { return from_start.IsSelected (); }
	const char far *GetSearchText ( void ) { return search_box.GetEditText (); }

	// Constructors

	FindDialog ( void );

	// Methods

	virtual bool HandleKey ( KEY_EVENT &key_event );
};

FindDialog::FindDialog ( void )
: Dialog        ( "find-dialog",      15, 6, 50, 15, "Find" ),
  search_box    ( "find-search",      0, 0, 46, 3, FIND_PATTERN_CAPACITY, false ),
  type_label    ( "find-type-label",  0, 0, "Type:" ),
  from_label    ( "find-from-label",  0, 0, "From:" ),
  type_group    ( "find-type-group",  0, 0, 46, 1 ),
  type_text     ( "find-type-text",   0, 0, "Text" ),
  type_hex      ( "find-type-hex",     0, 0, "Hex" ),
  from_group    ( "find-from-group",  0, 0, 46, 1 ),
  from_start    ( "find-from-start",  0, 0, "Start" ),
  from_cursor   ( "find-from-cursor", 0, 0, "Cursor" ),
  find_button   ( "find-do",          0, 0, 12, "Find" ),
  cancel_button ( "find-cancel",      0, 0, 10, "Cancel" )
{
	// Local variables.

	Application *application;
	int          rows;

	// Center the dialog for the actual row count.

	application = Application::GetInstance ();
	rows        = ( application != NULL ) ? ( int ) application->GetRows () : 25;

	x = ( 80 - width ) / 2;
	y = ( rows - height ) / 2;

	// Place the controls relative to the dialog.

	// Each radio group stacks vertically under its own label, sharing a
	// column beside it - the Settings dialog's arrangement.

	search_box.SetPosition    ( x + 2,  y + 1 );
	type_label.SetPosition    ( x + 2,  y + 5 );
	type_text.SetPosition     ( x + 11, y + 5 );
	type_hex.SetPosition      ( x + 11, y + 6 );
	from_label.SetPosition    ( x + 2,  y + 8 );
	from_start.SetPosition    ( x + 11, y + 8 );
	from_cursor.SetPosition   ( x + 11, y + 9 );
	find_button.SetPosition   ( x + 9,  y + 12 );
	cancel_button.SetPosition ( x + 29, y + 12 );

	// The search box: black text on the light-gray dialog, a white frame /
	// label while focused and dark-gray while not, and hex-only input while
	// the Type is Hex.

	search_box.SetText                    ( "Search" );
	search_box.SetColors                  ( COLOR_BLACK, COLOR_LIGHT_GRAY );
	search_box.SetSelectedBorderForeground ( COLOR_WHITE );
	search_box.SetHexModeRadio            ( &type_hex );
	search_box.SetEditText                ( last_find_text );

	// Labels and radios read black on the light-gray dialog.

	type_label.SetColors  ( COLOR_BLACK, COLOR_LIGHT_GRAY );
	from_label.SetColors  ( COLOR_BLACK, COLOR_LIGHT_GRAY );
	type_text.SetColors   ( COLOR_BLACK, COLOR_LIGHT_GRAY );
	type_hex.SetColors    ( COLOR_BLACK, COLOR_LIGHT_GRAY );
	from_start.SetColors  ( COLOR_BLACK, COLOR_LIGHT_GRAY );
	from_cursor.SetColors ( COLOR_BLACK, COLOR_LIGHT_GRAY );

	// Group the radios and seed them from the last search.

	type_group.AddRadioButton ( &type_text );
	type_group.AddRadioButton ( &type_hex );
	from_group.AddRadioButton ( &from_start );
	from_group.AddRadioButton ( &from_cursor );

	if ( last_search_hex ) type_group.SelectRadioButton ( &type_hex );
	else                   type_group.SelectRadioButton ( &type_text );

	from_group.SelectRadioButton ( &from_cursor );

	// Wire the buttons and assemble the dialog.

	find_button.SetOnActivate   ( DialogFindHandler,   this );
	cancel_button.SetOnActivate ( DialogCancelHandler, this );

	AddComponent ( &search_box );
	AddComponent ( &type_label );
	AddComponent ( &from_label );
	AddComponent ( &type_group );
	AddComponent ( &from_group );
	AddComponent ( &find_button );
	AddComponent ( &cancel_button );
}

bool FindDialog::HandleKey ( KEY_EVENT &key_event )
{
	// Enter is the Find default.

	if ( key_event.scan_code == KEY_ENTER )
	{
		Close ( DIALOG_RESULT_OK );

		return true;
	}

	return Dialog::HandleKey ( key_event );
}

//----------------------------------------------------------------------------
// Class: ReplaceDialog
//
// Description:
//
//   The Replace dialog: a Find field, a Replace-with field, a
//   Text / Hex type selector shared by both fields, and Replace Next /
//   Replace All / Cancel buttons. Enter is the Replace Next default.
//   Replace Next searches forward from the cursor; Replace All sweeps the
//   whole file from the start.
//
//----------------------------------------------------------------------------

class ReplaceDialog : public Dialog
{
	// Data Members

protected:

	HexTextBox       find_box;
	HexTextBox       replace_box;
	Label            type_label;
	RadioButtonGroup type_group;
	RadioButton      type_text;
	RadioButton      type_hex;
	Button           next_button;
	Button           all_button;
	Button           cancel_button;

	// Accessors

public:

	bool            IsHex          ( void ) { return type_hex.IsSelected (); }
	const char far *GetFindText    ( void ) { return find_box.GetEditText (); }
	const char far *GetReplaceText ( void ) { return replace_box.GetEditText (); }

	// Constructors

	ReplaceDialog ( void );

	// Methods

	virtual bool HandleKey ( KEY_EVENT &key_event );
};

ReplaceDialog::ReplaceDialog ( void )
: Dialog        ( "replace-dialog",     13, 5, 54, 16, "Replace" ),
  find_box      ( "replace-find",        0, 0, 50, 3, FIND_PATTERN_CAPACITY, false ),
  replace_box   ( "replace-with",        0, 0, 50, 3, FIND_PATTERN_CAPACITY, false ),
  type_label    ( "replace-type-label",  0, 0, "Type:" ),
  type_group    ( "replace-type-group",  0, 0, 50, 1 ),
  type_text     ( "replace-type-text",   0, 0, "Text" ),
  type_hex      ( "replace-type-hex",     0, 0, "Hex" ),
  next_button   ( "replace-next",        0, 0, 14, "Replace Next" ),
  all_button    ( "replace-all",         0, 0, 13, "Replace All" ),
  cancel_button ( "replace-cancel",      0, 0, 10, "Cancel" )
{
	// Local variables.

	Application *application;
	int          rows;

	// Center the dialog for the actual row count.

	application = Application::GetInstance ();
	rows        = ( application != NULL ) ? ( int ) application->GetRows () : 25;

	x = ( 80 - width ) / 2;
	y = ( rows - height ) / 2;

	// Place the controls relative to the dialog.

	// The type radios stack vertically under one another, sharing a column
	// beside the label - the Settings dialog's arrangement.

	find_box.SetPosition      ( x + 2,  y + 1 );
	replace_box.SetPosition   ( x + 2,  y + 5 );
	type_label.SetPosition    ( x + 2,  y + 9 );
	type_text.SetPosition     ( x + 11, y + 9 );
	type_hex.SetPosition      ( x + 11, y + 10 );
	next_button.SetPosition   ( x + 3,  y + 13 );
	all_button.SetPosition    ( x + 20, y + 13 );
	cancel_button.SetPosition ( x + 36, y + 13 );

	// Both fields: black text on the light-gray dialog, a white frame / label
	// while focused and dark-gray while not, and hex-only input while the
	// Type is Hex.

	find_box.SetText                    ( "Find" );
	find_box.SetColors                  ( COLOR_BLACK, COLOR_LIGHT_GRAY );
	find_box.SetSelectedBorderForeground ( COLOR_WHITE );
	find_box.SetHexModeRadio            ( &type_hex );
	find_box.SetEditText                ( last_find_text );

	replace_box.SetText                    ( "Replace with" );
	replace_box.SetColors                  ( COLOR_BLACK, COLOR_LIGHT_GRAY );
	replace_box.SetSelectedBorderForeground ( COLOR_WHITE );
	replace_box.SetHexModeRadio            ( &type_hex );
	replace_box.SetEditText                ( last_replace_text );

	// Labels and radios read black on the light-gray dialog.

	type_label.SetColors ( COLOR_BLACK, COLOR_LIGHT_GRAY );
	type_text.SetColors  ( COLOR_BLACK, COLOR_LIGHT_GRAY );
	type_hex.SetColors   ( COLOR_BLACK, COLOR_LIGHT_GRAY );

	// Group the type radios and seed them from the last search.

	type_group.AddRadioButton ( &type_text );
	type_group.AddRadioButton ( &type_hex );

	if ( last_search_hex ) type_group.SelectRadioButton ( &type_hex );
	else                   type_group.SelectRadioButton ( &type_text );

	// Wire the buttons and assemble the dialog.

	next_button.SetOnActivate   ( DialogReplaceNextHandler, this );
	all_button.SetOnActivate    ( DialogReplaceAllHandler,  this );
	cancel_button.SetOnActivate ( DialogCancelHandler,      this );

	AddComponent ( &find_box );
	AddComponent ( &replace_box );
	AddComponent ( &type_label );
	AddComponent ( &type_group );
	AddComponent ( &next_button );
	AddComponent ( &all_button );
	AddComponent ( &cancel_button );
}

bool ReplaceDialog::HandleKey ( KEY_EVENT &key_event )
{
	// Enter is the Replace Next default.

	if ( key_event.scan_code == KEY_ENTER )
	{
		Close ( REPLACE_RESULT_NEXT );

		return true;
	}

	return Dialog::HandleKey ( key_event );
}

//----------------------------------------------------------------------------
// Function: ShowNotFound / SearchStartOffset
//
// Description:
//
//   ShowNotFound reports an unmatched search in a MessageBox.
//   SearchStartOffset is the absolute offset a find-next begins at: just
//   past the current match (the highlighted selection block) so a repeated
//   find/replace steps forward rather than re-finding in place, or the
//   cursor when nothing is highlighted.
//
// Arguments:
//
//   - title : ShowNotFound: the dialog title (Find / Replace).
//
// Returns:
//
//   - SearchStartOffset: the absolute file offset to search from.
//
//----------------------------------------------------------------------------

static void ShowNotFound ( const char *title )
{
	MessageBox message_box ( "search-none", title, "Pattern not found." );

	message_box.RunModal ( Application::GetInstance () );
}

static unsigned long SearchStartOffset ( void )
{
	return hex_view_pointer->GetFindContinueOffset ();
}

//----------------------------------------------------------------------------
// Function: ParseAddress
//
// Description:
//
//   Parses a Goto address field strictly in the chosen base: surrounding
//   whitespace is tolerated, but every remaining character must be a digit
//   of that base, and at least one digit must be present. Nothing else is
//   accepted - no sign, no "0x" prefix, no trailing units - so a slip of
//   the finger is reported rather than silently reinterpreted.
//
//   A field too long for the range saturates at ULONG_MAX and is left to
//   the caller's clamp, which pins any over-large target to the last byte.
//
// Arguments:
//
//   - address_text : The field text to parse.
//   - hex          : true = base 16, false = base 10.
//   - address      : Receives the parsed address on success.
//
// Returns:
//
//   - true if the field parsed, false if it was empty or malformed.
//
//----------------------------------------------------------------------------

static bool ParseAddress ( const char *address_text, bool hex, unsigned long *address )
{
	// Local variables.

	const char *scan;
	const char *digits;
	int         digit_count;

	// Skip any leading whitespace.

	scan = address_text;

	while ( ( *scan == ' ' ) || ( *scan == '\t' ) ) scan++;

	// Count the run of digits of the chosen base.

	digits      = scan;
	digit_count = 0;

	while ( *scan != '\0' )
	{
		if ( hex )
		{
			if ( HexDigit ( *scan ) < 0 ) break;
		}
		else
		{
			if ( ( *scan < '0' ) || ( *scan > '9' ) ) break;
		}

		digit_count++;
		scan++;
	}

	// Trailing whitespace is tolerated; anything else ends the run early and is malformed.

	while ( ( *scan == ' ' ) || ( *scan == '\t' ) ) scan++;

	if ( ( digit_count == 0 ) || ( *scan != '\0' ) ) return false;

	// Convert the validated run in its base.

	*address = strtoul ( digits, ( char ** ) 0, hex ? 16 : 10 );

	return true;
}

//----------------------------------------------------------------------------
// Function: RunGoto
//
// Description:
//
//   Runs the Goto dialog and, on Goto, jumps the cursor to the parsed
//   absolute file offset. An empty or malformed field is reported and
//   moves nothing.
//
//   A target at or past the file's end clamps to the last byte rather than
//   the append slot, deliberately matching what Shift+End does, and an
//   empty file pins to offset 0 (its ghost append slot, the only position
//   there is). A target outside the loaded window pages its chunk in
//   through the same machinery Find uses, and the landing is presented the
//   way a jump should be: the target row centred in the view where the
//   file allows it, any selection dropped, and the cursor on the byte's
//   high nibble.
//
// Arguments:
//
//   - None
//
// Returns:
//
//   - None
//
//----------------------------------------------------------------------------

static void RunGoto ( void )
{
	// Local variables.

	GotoDialog    dialog;
	char          address_text [ GOTO_ADDRESS_CAPACITY ];
	bool          hex;
	unsigned long target_offset;
	unsigned long file_length;
	unsigned long context_bytes;

	// Run the dialog; anything but Goto is a cancel.

	if ( dialog.RunModal ( Application::GetInstance () ) != DIALOG_RESULT_OK ) return;

	// Capture the field and the base it is to be read in.

	hex = dialog.IsHex ();

	strncpy ( address_text, ( const char * ) dialog.GetAddressText (), GOTO_ADDRESS_CAPACITY - 1 );

	address_text [ GOTO_ADDRESS_CAPACITY - 1 ] = '\0';

	// An empty or malformed address is reported, and no jump occurs.

	if ( !ParseAddress ( address_text, hex, &target_offset ) )
	{
		ShowFileError ( hex ? "Enter a hexadecimal address." : "Enter a decimal address." );

		return;
	}

	// Report any pending edit before the file's extent is read.

	SyncEditState ();

	// Clamp the target into the file.

	file_length = hex_view_pointer->GetFileLength ();

	if      ( file_length == 0 )             target_offset = 0;
	else if ( target_offset >= file_length ) target_offset = file_length - 1;

	// Page the containing chunk in when the target lies outside the loaded window.

	context_bytes = ( unsigned long ) hex_view_pointer->GetHeight ()*HEX_VIEW_BYTES_PER_ROW;

	if ( file_buffer.PageTo ( target_offset, context_bytes ) != FILE_IO_OK )
	{
		ShowFileError ( "Cannot page the file." );

		return;
	}

	// Land on the target's high nibble, its row centred in the view.

	ReattachHexView ( target_offset, 0, hex_view_pointer->GetHeight () / 2 );

	// A jump is not a selection: an empty range clears any highlight the
	// cursor left behind, without disturbing the landing position.

	hex_view_pointer->SelectRange ( 0, 0 );

	// Refresh both status rows for the new position.

	OnHexViewChange ( NULL, NULL );
}

//----------------------------------------------------------------------------
// Function: RunFind
//
// Description:
//
//   Runs the Find dialog and, on Find, searches from the chosen origin
//   (Start or the cursor) for the parsed pattern, paging across chunks for
//   a large file. A match is paged into view and highlighted as a
//   selection; a miss is reported. The searched field is remembered for
//   the next dialog.
//
// Arguments:
//
//   - None
//
// Returns:
//
//   - None
//
//----------------------------------------------------------------------------

static void RunFind ( void )
{
	// Local variables.

	FindDialog    dialog;
	BYTE          pattern [ FIND_PATTERN_CAPACITY ];
	char          search_text [ FIND_PATTERN_CAPACITY ];
	bool          hex;
	int           pattern_length;
	bool          found;
	unsigned long start_offset;
	unsigned long match_offset;
	unsigned long context_bytes;

	// Run the dialog; anything but Find is a cancel.

	if ( dialog.RunModal ( Application::GetInstance () ) != DIALOG_RESULT_OK ) return;

	// Capture the field and mode, remembered for the next dialog.

	hex = dialog.IsHex ();

	strncpy ( search_text, ( const char * ) dialog.GetSearchText (), FIND_PATTERN_CAPACITY - 1 );

	search_text [ FIND_PATTERN_CAPACITY - 1 ] = '\0';

	strcpy ( last_find_text, search_text );

	last_search_hex = hex;

	// Parse the field into a byte pattern.

	pattern_length = ParsePattern ( search_text, hex, pattern, FIND_PATTERN_CAPACITY );

	if ( pattern_length <= 0 )
	{
		ShowFileError ( hex ? "Enter an even number of hex digits." : "Enter text to find." );

		return;
	}

	// Sync the buffer, then search from the chosen origin.

	SyncEditState ();

	start_offset = dialog.FromStart () ? 0UL : SearchStartOffset ();

	if ( file_buffer.FindPattern ( pattern, ( unsigned ) pattern_length, start_offset, &found, &match_offset ) != FILE_IO_OK )
	{
		ShowFileError ( "Cannot search the file." );

		return;
	}

	if ( !found )
	{
		ShowNotFound ( "Find" );

		return;
	}

	// Page the match into view and highlight it.

	context_bytes = ( unsigned long ) hex_view_pointer->GetHeight ()*HEX_VIEW_BYTES_PER_ROW;

	if ( file_buffer.PageTo ( match_offset, context_bytes ) != FILE_IO_OK )
	{
		ShowFileError ( "Cannot page the file." );

		return;
	}

	ReattachHexView ( match_offset, 0, -1 );

	hex_view_pointer->SelectRange ( match_offset - file_buffer.GetChunkOffset (), ( unsigned long ) pattern_length );

	OnHexViewChange ( NULL, NULL );
}

//----------------------------------------------------------------------------
// Function: DoReplaceNext / DoReplaceAll
//
// Description:
//
//   The two Replace actions over a parsed find pattern and replacement.
//   DoReplaceNext finds the next match forward from the cursor, splices
//   the replacement in through the paging layer, and highlights it.
//   DoReplaceAll sweeps the whole file from the start, replacing every
//   match and continuing past each replacement (so a replacement that
//   contains the pattern is not re-matched), reflowing the chunk when its
//   in-RAM growth nears the buffer. Both report the outcome.
//
// Arguments:
//
//   - find_pattern : The bytes to find.
//   - find_length  : The find pattern's length.
//   - replacement  : The replacement bytes.
//   - replace_length : The replacement's length (0 deletes the match).
//
// Returns:
//
//   - None
//
//----------------------------------------------------------------------------

static void DoReplaceNext ( const BYTE far *find_pattern, int find_length, const BYTE far *replacement, int replace_length )
{
	// Local variables.

	bool          found;
	unsigned long start_offset;
	unsigned long match_offset;
	unsigned long context_bytes;
	unsigned long window_offset;

	// Sync the buffer, then find the next match forward from the cursor.

	SyncEditState ();

	start_offset = SearchStartOffset ();

	if ( file_buffer.FindPattern ( find_pattern, ( unsigned ) find_length, start_offset, &found, &match_offset ) != FILE_IO_OK )
	{
		ShowFileError ( "Cannot search the file." );

		return;
	}

	if ( !found )
	{
		ShowNotFound ( "Replace" );

		return;
	}

	// Page the match into view.

	context_bytes = ( unsigned long ) hex_view_pointer->GetHeight ()*HEX_VIEW_BYTES_PER_ROW;

	if ( file_buffer.PageTo ( match_offset, context_bytes ) != FILE_IO_OK )
	{
		ShowFileError ( "Cannot page the file." );

		return;
	}

	// Splice the replacement in through the loaded window.

	window_offset = match_offset - file_buffer.GetChunkOffset ();

	if ( !file_buffer.ReplaceInChunk ( window_offset, ( unsigned long ) find_length, replacement, ( unsigned long ) replace_length ) )
	{
		ShowFileError ( "The replacement does not fit the buffer." );

		return;
	}

	// Highlight the replacement and refresh.

	ReattachHexView ( match_offset, 0, -1 );

	hex_view_pointer->SelectRange ( match_offset - file_buffer.GetChunkOffset (), ( unsigned long ) replace_length );

	OnHexViewChange ( NULL, NULL );
}

static void DoReplaceAll ( const BYTE far *find_pattern, int find_length, const BYTE far *replacement, int replace_length )
{
	// Local variables.

	char          message [ 48 ];
	bool          found;
	bool          stopped_early;
	unsigned long offset;
	unsigned long match_offset;
	unsigned long context_bytes;
	unsigned long window_offset;
	unsigned long replaced_count;

	// Sync the buffer and sweep the whole file from the start.

	SyncEditState ();

	context_bytes  = ( unsigned long ) hex_view_pointer->GetHeight ()*HEX_VIEW_BYTES_PER_ROW;
	offset         = 0;
	replaced_count = 0;
	stopped_early  = false;

	// Find, page, and replace until no match remains.

	for ( ; ; )
	{
		if ( file_buffer.FindPattern ( find_pattern, ( unsigned ) find_length, offset, &found, &match_offset ) != FILE_IO_OK )
		{
			stopped_early = true;

			break;
		}

		if ( !found ) break;

		if ( file_buffer.PageTo ( match_offset, context_bytes ) != FILE_IO_OK )
		{
			stopped_early = true;

			break;
		}

		// Restore insert headroom before a replacement whose growth would
		// otherwise near the physical buffer (Replace All can accumulate
		// growth within one chunk).

		if ( file_buffer.GetChunked ()
		  && ( file_buffer.GetChunkLength () + FILE_BUFFER_REFLOW_MARGIN + ( unsigned long ) replace_length >= file_buffer.GetBufferSize () ) )
		{
			if ( ( file_buffer.ReflowChunk ( match_offset ) != FILE_IO_OK )
			  || ( file_buffer.PageTo ( match_offset, context_bytes ) != FILE_IO_OK ) )
			{
				stopped_early = true;

				break;
			}
		}

		window_offset = match_offset - file_buffer.GetChunkOffset ();

		if ( !file_buffer.ReplaceInChunk ( window_offset, ( unsigned long ) find_length, replacement, ( unsigned long ) replace_length ) )
		{
			stopped_early = true;      // The buffer is full (a small file grew past its headroom).

			break;
		}

		// Continue past the replacement so it is never re-matched.

		replaced_count++;
		offset = match_offset + ( unsigned long ) replace_length;

		if ( replaced_count >= REPLACE_ALL_LIMIT )
		{
			stopped_early = true;

			break;
		}
	}

	if ( stopped_early && ( replaced_count == 0 ) )
	{
		// Nothing was replaced, but a failed page or reflow may have moved
		// the loaded window - re-attach the view to the buffer's real state
		// before reporting.

		ReattachHexView ( file_buffer.GetChunkOffset (), 0, -1 );

		OnHexViewChange ( NULL, NULL );

		ShowFileError ( "Cannot complete the replacement." );

		return;
	}

	// Commit the final replacement (a chunked page-back flushes it) and show
	// the file from the start.

	if ( file_buffer.PageTo ( 0, context_bytes ) != FILE_IO_OK )
	{
		// The replacements up to here are applied and flushed - re-attach
		// the view to the window the buffer actually holds and report the
		// partial count.

		ReattachHexView ( file_buffer.GetChunkOffset (), 0, -1 );

		OnHexViewChange ( NULL, NULL );

		sprintf ( message, "Replaced %lu, but cannot page the file.", replaced_count );

		ShowFileError ( message );

		return;
	}

	ReattachHexView ( 0, 0, -1 );

	OnHexViewChange ( NULL, NULL );

	sprintf ( message, stopped_early ? "Replaced %lu, then stopped." : "Replaced %lu occurrence(s).", replaced_count );

	{
		MessageBox message_box ( "replace-done", "Replace All", message );

		message_box.RunModal ( Application::GetInstance () );
	}
}

//----------------------------------------------------------------------------
// Function: RunReplace
//
// Description:
//
//   Runs the Replace dialog and dispatches to Replace Next or Replace All
//   over the parsed find pattern and replacement (both remembered for the
//   next dialog). A malformed hex field is reported rather than acted on.
//
// Arguments:
//
//   - None
//
// Returns:
//
//   - None
//
//----------------------------------------------------------------------------

static void RunReplace ( void )
{
	// Local variables.

	ReplaceDialog dialog;
	BYTE          find_pattern [ FIND_PATTERN_CAPACITY ];
	BYTE          replacement  [ FIND_PATTERN_CAPACITY ];
	char          find_text    [ FIND_PATTERN_CAPACITY ];
	char          replace_text [ FIND_PATTERN_CAPACITY ];
	bool          hex;
	int           find_length;
	int           replace_length;
	int           result;

	// Run the dialog; only the two Replace actions proceed.

	result = dialog.RunModal ( Application::GetInstance () );

	if ( ( result != REPLACE_RESULT_NEXT ) && ( result != REPLACE_RESULT_ALL ) ) return;

	// Capture the fields and mode, remembered for the next dialog.

	hex = dialog.IsHex ();

	strncpy ( find_text,    ( const char * ) dialog.GetFindText (),    FIND_PATTERN_CAPACITY - 1 );
	strncpy ( replace_text, ( const char * ) dialog.GetReplaceText (), FIND_PATTERN_CAPACITY - 1 );

	find_text    [ FIND_PATTERN_CAPACITY - 1 ] = '\0';
	replace_text [ FIND_PATTERN_CAPACITY - 1 ] = '\0';

	strcpy ( last_find_text,    find_text );
	strcpy ( last_replace_text, replace_text );

	last_search_hex = hex;

	// Parse both fields; an empty replacement deletes the match.

	find_length    = ParsePattern ( find_text,    hex, find_pattern, FIND_PATTERN_CAPACITY );
	replace_length = ParsePattern ( replace_text, hex, replacement,  FIND_PATTERN_CAPACITY );

	if ( find_length <= 0 )
	{
		ShowFileError ( hex ? "Enter an even number of hex digits to find." : "Enter text to find." );

		return;
	}

	if ( replace_length < 0 )
	{
		ShowFileError ( "The replacement hex is malformed." );

		return;
	}

	// Dispatch to the chosen action.

	if ( result == REPLACE_RESULT_NEXT ) DoReplaceNext ( find_pattern, find_length, replacement, replace_length );
	else                                 DoReplaceAll  ( find_pattern, find_length, replacement, replace_length );
}

//----------------------------------------------------------------------------
// Function: OnMenuCut / OnMenuCopy / OnMenuPaste / OnMenuGoto /
//           OnMenuFind / OnMenuReplace
//
// Description:
//
//   Edit-menu handlers. Cut / Copy / Paste drive the HexView clipboard
//   (the same operations as the Ctrl+Ins / Shift+Del / Shift+Ins keys);
//   Goto, Find, and Replace open their dialogs (the same dialogs as the
//   Ctrl+G / Ctrl+F / Ctrl+R keys).
//
// Arguments:
//
//   - sender    : The menu item.
//   - user_data : Unused.
//
// Returns:
//
//   - None
//
//----------------------------------------------------------------------------

static void OnMenuCut ( Component *sender, void *user_data )
{
	( void ) sender;
	( void ) user_data;

	hex_view_pointer->CutSelection ();
}

static void OnMenuCopy ( Component *sender, void *user_data )
{
	( void ) sender;
	( void ) user_data;

	hex_view_pointer->CopySelection ();
}

static void OnMenuPaste ( Component *sender, void *user_data )
{
	( void ) sender;
	( void ) user_data;

	hex_view_pointer->PasteClipboard ();
}

static void OnMenuGoto ( Component *sender, void *user_data )
{
	( void ) sender;
	( void ) user_data;

	RunGoto ();
}

static void OnMenuFind ( Component *sender, void *user_data )
{
	( void ) sender;
	( void ) user_data;

	RunFind ();
}

static void OnMenuReplace ( Component *sender, void *user_data )
{
	( void ) sender;
	( void ) user_data;

	RunReplace ();
}

//****************************************************************************
// Export, Print, DOS Shell, Settings, Manual, About
//****************************************************************************

//----------------------------------------------------------------------------
// Function: DialogOkHandler
//
// Description:
//
//   A shared button handler that closes its owning dialog (passed as
//   user_data) with DIALOG_RESULT_OK - the Ok / Export / Close action for
//   these dialogs, the counterpart of DialogCancelHandler.
//
// Arguments:
//
//   - sender    : The button.
//   - user_data : The owning Dialog.
//
// Returns:
//
//   - None
//
//----------------------------------------------------------------------------

static void DialogOkHandler ( Component *sender, void *user_data )
{
	( void ) sender;

	( ( Dialog * ) user_data )->Close ( DIALOG_RESULT_OK );
}

//----------------------------------------------------------------------------
// Function: ApplyViewSettings / ApplyRows / ApplyBufferSize
//
// Description:
//
//   Push the settings model into the running editor. ApplyViewSettings
//   sets the live HexView options that only affect rendering (margin,
//   divider, address base, word byte-order) - a fresh frame shows them.
//   ApplyRows performs the live text-mode switch: Application::SetRows
//   re-sets the mode and rebuilds the back buffer (falling back to 25 rows
//   if a device-dependent 43/50-row mode is not honoured), the application frame is
//   re-laid-out, the cursor is re-scrolled into the resized view, and a
//   frame is drawn. ApplyBufferSize re-allocates the working buffer at a
//   new size and reloads the current file through it - reloading discards
//   unsaved edits, so it confirms first when the file is dirty.
//
// Arguments:
//
//   - new_rows : ApplyRows: the requested row count.
//   - new_size : ApplyBufferSize: the requested buffer / chunk size.
//
// Returns:
//
//   - None
//
//----------------------------------------------------------------------------

static void ApplyViewSettings ( void )
{
	hex_view_pointer->SetMarginEnabled    ( settings.margin_enabled );
	hex_view_pointer->SetDividerEnabled   ( settings.divider_enabled );
	hex_view_pointer->SetAddressHex       ( settings.address_hex );
	hex_view_pointer->SetWordLittleEndian ( settings.word_little_endian );
}

static void ApplyRows ( BYTE new_rows )
{
	// Local variables.

	Application  *application;
	unsigned long absolute_offset;
	int           cursor_nibble;

	// Remember the cursor, then switch the text mode and re-lay-out the frame.

	application     = Application::GetInstance ();
	absolute_offset = hex_view_pointer->GetAbsoluteOffset ();
	cursor_nibble   = hex_view_pointer->GetCursorNibble ();

	application->SetRows     ( new_rows );
	application_frame_pointer->Relayout ( ( int ) application->GetRows () );

	// Re-scroll the cursor within the resized view, then paint the new mode.

	ReattachHexView ( absolute_offset, cursor_nibble, -1 );

	application->RenderFrame ();
}

static void ApplyBufferSize ( unsigned long new_size )
{
	// Local variables.

	char name [ FILE_BUFFER_PATH_SIZE ];
	bool had_name;

	// Reloading the file discards unsaved edits: confirm first.

	if ( file_buffer.GetFileDirty () )
	{
		ConfirmationBox confirmation ( "buffer-resize", "Settings",
		                               "Change the buffer size and discard unsaved changes?" );

		if ( confirmation.RunModal ( Application::GetInstance () ) != DIALOG_RESULT_YES ) return;
	}

	// Remember the open file so it can reload through the new buffer.

	strcpy ( name, file_buffer.GetFilePath () );

	had_name = ( name [ 0 ] != '\0' );

	// Release the old buffer and allocate at the new size.

	file_buffer.Close   ();
	file_buffer.Release ();

	if ( !file_buffer.Allocate ( new_size ) )
	{
		// The far memory was just freed, so this should not fail; keep a
		// live minimum buffer if it somehow does.

		file_buffer.Allocate ( FILE_BUFFER_MINIMUM_CHUNK );

		ShowFileError ( "Not enough memory for that buffer size." );
	}

	// Reload the file, or stay on a blank buffer.

	if ( had_name )
	{
		if ( file_buffer.Open ( name ) != FILE_IO_OK )
		{
			file_buffer.NewFile ( "" );

			ShowFileError ( "Cannot reload the file at the new buffer size." );
		}
	}
	else
	{
		file_buffer.NewFile ( "" );
	}

	// Present the reloaded state: view, title, and status.

	hex_view_pointer->SetInsertMode ( settings.insert_default );

	ReattachHexView ( 0, 0, -1 );
	UpdateTitleBar  ();
	OnHexViewChange ( NULL, NULL );
}

//----------------------------------------------------------------------------
// Class: SettingsDialog
//
// Description:
//
//   The Settings dialog: screen Rows (25 / 43 / 50), address
//   number base (Hex / Decimal), Word byte-order (Little / Big), the Hex
//   divider / Relative margin / Insert-default toggles, and the working
//   Buffer size. Radios and check boxes seed from the current settings;
//   the accessors report the chosen values back to RunSettings, which
//   applies the deltas. Enter is the Ok default.
//
//----------------------------------------------------------------------------

class SettingsDialog : public Dialog
{
	// Data Members

protected:

	Label            rows_label;
	RadioButtonGroup rows_group;
	RadioButton      rows_25;
	RadioButton      rows_43;
	RadioButton      rows_50;
	Label            address_label;
	RadioButtonGroup address_group;
	RadioButton      address_hex_radio;
	RadioButton      address_dec_radio;
	Label            word_label;
	RadioButtonGroup word_group;
	RadioButton      word_le_radio;
	RadioButton      word_be_radio;
	CheckBox         divider_check;
	CheckBox         margin_check;
	CheckBox         insert_check;
	CheckBox         about_check;
	Label            buffer_label;
	TextBox          buffer_box;
	Button           ok_button;
	Button           cancel_button;

	// Accessors

public:

	BYTE          GetRows            ( void );
	bool          IsAddressHex       ( void ) { return address_hex_radio.IsSelected (); }
	bool          IsWordLittleEndian ( void ) { return word_le_radio.IsSelected (); }
	bool          IsDividerOn        ( void ) { return divider_check.IsChecked (); }
	bool          IsMarginOn         ( void ) { return margin_check.IsChecked (); }
	bool          IsInsertDefault    ( void ) { return insert_check.IsChecked (); }
	bool          IsAboutOnLaunch    ( void ) { return about_check.IsChecked (); }
	unsigned long GetBufferSize      ( void );

	// Constructors

	SettingsDialog ( void );

	// Methods

	virtual bool HandleKey ( KEY_EVENT &key_event );
};

SettingsDialog::SettingsDialog ( void )
: Dialog            ( "settings-dialog",      14, 3, 52, 23, "Settings" ),
  rows_label        ( "settings-rows-label",   0, 0, "Rows:" ),
  rows_group        ( "settings-rows-group",   0, 0, 48, 1 ),
  rows_25           ( "settings-rows-25",      0, 0, "25" ),
  rows_43           ( "settings-rows-43",      0, 0, "43" ),
  rows_50           ( "settings-rows-50",      0, 0, "50" ),
  address_label     ( "settings-addr-label",   0, 0, "Address:" ),
  address_group     ( "settings-addr-group",   0, 0, 48, 1 ),
  address_hex_radio ( "settings-addr-hex",     0, 0, "Hex" ),
  address_dec_radio ( "settings-addr-dec",     0, 0, "Decimal" ),
  word_label        ( "settings-word-label",   0, 0, "Word order:" ),
  word_group        ( "settings-word-group",   0, 0, 48, 1 ),
  word_le_radio     ( "settings-word-le",      0, 0, "Little Endian" ),
  word_be_radio     ( "settings-word-be",      0, 0, "Big Endian" ),
  divider_check     ( "settings-divider",      0, 0, "Hex divider" ),
  margin_check      ( "settings-margin",       0, 0, "Relative-position margin" ),
  insert_check      ( "settings-insert",       0, 0, "Insert mode by default" ),
  about_check       ( "settings-about",        0, 0, "Show About dialog at launch" ),
  buffer_label      ( "settings-buffer-label", 0, 0, "Buffer size (bytes):" ),
  buffer_box        ( "settings-buffer",       0, 0, 20, 3, 16, false ),
  ok_button         ( "settings-ok",           0, 0, 10, "Ok" ),
  cancel_button     ( "settings-cancel",       0, 0, 10, "Cancel" )
{
	// Local variables.

	char buffer_text [ 16 ];

	// Center the dialog for the actual row count.

	x = ( 80 - width ) / 2;
	y = ( ( ( Application::GetInstance () != NULL ) ? ( int ) Application::GetInstance ()->GetRows () : 25 ) - height ) / 2;

	// Place the controls relative to the dialog.

	rows_label.SetPosition        ( x + 2,  y + 2  );
	rows_25.SetPosition           ( x + 14, y + 2  );
	rows_43.SetPosition           ( x + 14, y + 3  );
	rows_50.SetPosition           ( x + 14, y + 4  );

	address_label.SetPosition     ( x + 2,  y + 6  );
	address_hex_radio.SetPosition ( x + 14, y + 6  );
	address_dec_radio.SetPosition ( x + 14, y + 7  );

	word_label.SetPosition        ( x + 2,  y + 9  );
	word_le_radio.SetPosition     ( x + 14, y + 9  );
	word_be_radio.SetPosition     ( x + 14, y + 10 );

	divider_check.SetPosition     ( x + 2,  y + 12 );
	margin_check.SetPosition      ( x + 2,  y + 13 );
	insert_check.SetPosition      ( x + 2,  y + 14 );
	about_check.SetPosition       ( x + 2,  y + 15 );

	buffer_label.SetPosition      ( x + 2,  y + 18 );
	buffer_box.SetPosition        ( x + 23, y + 17 );

	ok_button.SetPosition         ( x + 10, y + 21 );
	cancel_button.SetPosition     ( x + 30, y + 21 );

	// Black-on-light-gray labels, radios, and check boxes (dialog convention).

	rows_label.SetColors        ( COLOR_BLACK, COLOR_LIGHT_GRAY );
	rows_25.SetColors           ( COLOR_BLACK, COLOR_LIGHT_GRAY );
	rows_43.SetColors           ( COLOR_BLACK, COLOR_LIGHT_GRAY );
	rows_50.SetColors           ( COLOR_BLACK, COLOR_LIGHT_GRAY );
	address_label.SetColors     ( COLOR_BLACK, COLOR_LIGHT_GRAY );
	address_hex_radio.SetColors ( COLOR_BLACK, COLOR_LIGHT_GRAY );
	address_dec_radio.SetColors ( COLOR_BLACK, COLOR_LIGHT_GRAY );
	word_label.SetColors        ( COLOR_BLACK, COLOR_LIGHT_GRAY );
	word_le_radio.SetColors     ( COLOR_BLACK, COLOR_LIGHT_GRAY );
	word_be_radio.SetColors     ( COLOR_BLACK, COLOR_LIGHT_GRAY );
	divider_check.SetColors     ( COLOR_BLACK, COLOR_LIGHT_GRAY );
	margin_check.SetColors      ( COLOR_BLACK, COLOR_LIGHT_GRAY );
	insert_check.SetColors      ( COLOR_BLACK, COLOR_LIGHT_GRAY );
	about_check.SetColors       ( COLOR_BLACK, COLOR_LIGHT_GRAY );
	buffer_label.SetColors      ( COLOR_BLACK, COLOR_LIGHT_GRAY );

	buffer_box.SetText                     ( "" );
	buffer_box.SetColors                   ( COLOR_BLACK, COLOR_LIGHT_GRAY );
	buffer_box.SetSelectedBorderForeground ( COLOR_WHITE );

	// Group the radios.

	rows_group.AddRadioButton    ( &rows_25 );
	rows_group.AddRadioButton    ( &rows_43 );
	rows_group.AddRadioButton    ( &rows_50 );
	address_group.AddRadioButton ( &address_hex_radio );
	address_group.AddRadioButton ( &address_dec_radio );
	word_group.AddRadioButton    ( &word_le_radio );
	word_group.AddRadioButton    ( &word_be_radio );

	// Seed every control from the current settings.

	if      ( settings.rows == TEXT_MODE_43_ROWS ) rows_group.SelectRadioButton ( &rows_43 );
	else if ( settings.rows == TEXT_MODE_50_ROWS ) rows_group.SelectRadioButton ( &rows_50 );
	else                                           rows_group.SelectRadioButton ( &rows_25 );

	address_group.SelectRadioButton ( settings.address_hex        ? &address_hex_radio : &address_dec_radio );
	word_group.SelectRadioButton    ( settings.word_little_endian ? &word_le_radio     : &word_be_radio );

	divider_check.SetChecked ( settings.divider_enabled );
	margin_check.SetChecked  ( settings.margin_enabled );
	insert_check.SetChecked  ( settings.insert_default );
	about_check.SetChecked   ( settings.show_about_on_launch );

	// Seed the buffer-size field.

	sprintf ( buffer_text, "%lu", settings.buffer_size );

	buffer_box.SetEditText ( buffer_text );

	// Wire the buttons and assemble the dialog.

	ok_button.SetOnActivate     ( DialogOkHandler,     this );
	cancel_button.SetOnActivate ( DialogCancelHandler, this );

	AddComponent ( &rows_label );
	AddComponent ( &rows_group );
	AddComponent ( &address_label );
	AddComponent ( &address_group );
	AddComponent ( &word_label );
	AddComponent ( &word_group );
	AddComponent ( &divider_check );
	AddComponent ( &margin_check );
	AddComponent ( &insert_check );
	AddComponent ( &about_check );
	AddComponent ( &buffer_label );
	AddComponent ( &buffer_box );
	AddComponent ( &ok_button );
	AddComponent ( &cancel_button );
}

BYTE SettingsDialog::GetRows ( void )
{
	// The selected row-count radio; 25 is the fallback.

	if ( rows_43.IsSelected () ) return TEXT_MODE_43_ROWS;
	if ( rows_50.IsSelected () ) return TEXT_MODE_50_ROWS;

	return TEXT_MODE_25_ROWS;
}

unsigned long SettingsDialog::GetBufferSize ( void )
{
	// Local variables.

	unsigned long value;

	// Parse the field and clamp it to the allowed chunk range.

	value = strtoul ( ( const char * ) buffer_box.GetEditText (), NULL, 10 );

	if ( value < FILE_BUFFER_MINIMUM_CHUNK ) value = FILE_BUFFER_MINIMUM_CHUNK;
	if ( value > FILE_BUFFER_CHUNK_CEILING ) value = FILE_BUFFER_CHUNK_CEILING;

	// The clamped size.

	return value;
}

bool SettingsDialog::HandleKey ( KEY_EVENT &key_event )
{
	// Enter is the Ok default.

	if ( key_event.scan_code == KEY_ENTER )
	{
		Close ( DIALOG_RESULT_OK );

		return true;
	}

	return Dialog::HandleKey ( key_event );
}

//----------------------------------------------------------------------------
// Function: RunSettings
//
// Description:
//
//   Runs the Settings dialog and applies the chosen values. The rendering
//   options (address base, divider, margin, word order) and the insert
//   default take effect immediately; a changed Rows value drives a live
//   text-mode switch; a changed Buffer size re-allocates and reloads. The
//   settings model is then reconciled with what the engine actually
//   adopted (Rows may have fallen back, the buffer size is clamped to free
//   memory), and the status rows refresh.
//
// Arguments:
//
//   - None
//
// Returns:
//
//   - None
//
//----------------------------------------------------------------------------

static void RunSettings ( void )
{
	// Local variables.

	BYTE          previous_rows;
	unsigned long previous_buffer_size;
	BYTE          new_rows;
	bool          new_address_hex;
	bool          new_divider;
	bool          new_margin;
	bool          new_word_le;
	bool          new_insert_default;
	bool          new_show_about;
	unsigned long new_buffer_size;

	// Collect the chosen values, then let the (large) dialog leave the stack
	// before applying - a buffer-size change may open a nested confirmation.

	{
		SettingsDialog dialog;

		if ( dialog.RunModal ( Application::GetInstance () ) != DIALOG_RESULT_OK ) return;

		new_rows           = dialog.GetRows ();
		new_address_hex    = dialog.IsAddressHex ();
		new_divider        = dialog.IsDividerOn ();
		new_margin         = dialog.IsMarginOn ();
		new_word_le        = dialog.IsWordLittleEndian ();
		new_insert_default = dialog.IsInsertDefault ();
		new_show_about     = dialog.IsAboutOnLaunch ();
		new_buffer_size    = dialog.GetBufferSize ();
	}

	previous_rows        = settings.rows;
	previous_buffer_size = settings.buffer_size;

	// The live rendering options and the insert default take effect at once.

	settings.address_hex        = new_address_hex;
	settings.divider_enabled    = new_divider;
	settings.margin_enabled     = new_margin;
	settings.word_little_endian = new_word_le;
	settings.insert_default     = new_insert_default;

	ApplyViewSettings ();

	// The About-on-launch preference takes effect at the next startup; store it.

	settings.show_about_on_launch = new_show_about;

	// Rows: a live text-mode switch (rebuild the back buffer + relayout).

	if ( new_rows != previous_rows ) ApplyRows ( new_rows );

	// Buffer size: a live re-allocate that reloads the current file.

	if ( new_buffer_size != previous_buffer_size ) ApplyBufferSize ( new_buffer_size );

	// Reconcile with what the engine actually adopted (rows may have fallen
	// back; the buffer size is clamped to free memory).

	settings.rows        = Application::GetInstance ()->GetRows ();
	settings.buffer_size = file_buffer.GetChunkSize ();

	// Persist the reconciled settings to CONFIG.INI (created if absent).

	SaveSettings ();

	OnHexViewChange ( NULL, NULL );
}

//----------------------------------------------------------------------------
// Function: WriteHexDump
//
// Description:
//
//   Writes a text hex dump of a byte range mirroring the on-screen layout -
//   "address:  16 hex bytes (with the mid-row '-' divider)  16 printable
//   characters" per row, CR/LF terminated - to a file or the printer device
//   (Export and Print). The bytes come through
//   FileBuffer::ReadAt, so the whole file (or a selection) is dumped
//   whatever chunk is loaded. A printer dump ends with a form feed.
//
// Arguments:
//
//   - destination : The output file path or printer device name.
//   - start       : The absolute file offset to dump from.
//   - length      : The byte count to dump.
//   - to_printer  : true to append a trailing form feed (a printer dump).
//
// Returns:
//
//   - FILE_IO_OK, or FILE_IO_ERROR_OPEN / FILE_IO_ERROR_WRITE on failure.
//
//----------------------------------------------------------------------------

static int WriteHexDump ( const char *destination, unsigned long start, unsigned long length, bool to_printer )
{
	// Local variables.

	FILE          *output;
	BYTE           row_bytes [ 16 ];
	char           line [ 96 ];
	char          *p;
	unsigned long  offset;
	unsigned long  remaining;
	unsigned       want;
	unsigned       got;
	unsigned       i;

	// Open the output file or printer device.

	output = fopen ( destination, "wb" );

	if ( output == NULL ) return FILE_IO_ERROR_OPEN;

	// Dump 16-byte rows until the range is exhausted.

	offset    = start;
	remaining = length;

	while ( remaining > 0 )
	{
		want = ( remaining > 16UL ) ? 16 : ( unsigned ) remaining;
		got  = file_buffer.ReadAt ( offset, row_bytes, want );

		if ( got == 0 ) break;

		// Compose the row into the line buffer.

		p = line;

		// Address + the ':' terminator, then two spaces to the hex column.

		sprintf ( p, "%08lX:  ", offset );
		p += 11;

		for ( i = 0; i < 16; i++ )
		{
			if ( i < got )
			{
				sprintf ( p, "%02X", row_bytes [ i ] );
				p += 2;
			}
			else
			{
				*p++ = ' ';
				*p++ = ' ';
			}

			// The separator after this byte: the mid-row '-' divider after
			// the 8th byte, a space between the other bytes, none after the
			// last.

			if      ( i == 7 )  *p++ = '-';
			else if ( i < 15 )  *p++ = ' ';
		}

		// Two spaces from the last hex column to the character column.

		*p++ = ' ';
		*p++ = ' ';

		// The printable-character column; non-printables read as '.'.

		for ( i = 0; i < got; i++ )
		{
			*p++ = ( ( row_bytes [ i ] >= 32 ) && ( row_bytes [ i ] < 127 ) ) ? ( char ) row_bytes [ i ] : '.';
		}

		*p++ = '\r';
		*p++ = '\n';
		*p   = '\0';

		// A failed write abandons the dump.

		if ( fputs ( line, output ) == EOF )
		{
			fclose ( output );

			return FILE_IO_ERROR_WRITE;
		}

		offset    += got;
		remaining -= got;
	}

	if ( to_printer ) fputc ( 0x0C, output );      // Form feed.

	fclose ( output );

	// The dump is written.

	return FILE_IO_OK;
}

//----------------------------------------------------------------------------
// Function: DumpRange
//
// Description:
//
//   Resolves the byte range a dump covers - the highlighted selection when
//   selection_only is set (reporting an error and failing if nothing is
//   highlighted), otherwise the whole file - after syncing any pending
//   edit so the bytes are current.
//
// Arguments:
//
//   - selection_only : true to dump only the highlighted selection.
//   - start          : Out: the absolute start offset.
//   - length         : Out: the byte count.
//   - error_title    : The dialog title for a "nothing selected" error.
//
// Returns:
//
//   - true when a range was resolved; false (with an error shown) when
//     selection_only was requested but nothing is highlighted.
//
//----------------------------------------------------------------------------

static bool DumpRange ( bool selection_only, unsigned long *start, unsigned long *length, const char *error_title )
{
	// Sync any pending edit so the dumped bytes are current.

	SyncEditState ();

	if ( selection_only )
	{
		if ( !hex_view_pointer->GetSelectionRange ( start, length ) )
		{
			MessageBox message_box ( "dump-no-selection", error_title, "Nothing is selected." );

			message_box.RunModal ( Application::GetInstance () );

			return false;
		}

		return true;
	}

	// The whole file.

	*start  = 0;
	*length = file_buffer.GetFileLength ();

	return true;
}

//----------------------------------------------------------------------------
// Class: ExportDialog
//
// Description:
//
//   The Export to Text dialog: a file-name field and a
//   "Selection only" toggle, with Export / Cancel buttons. Enter is the
//   Export default.
//
//----------------------------------------------------------------------------

class ExportDialog : public Dialog
{
	// Data Members

protected:

	TextBox  name_box;
	CheckBox selection_check;
	Button   export_button;
	Button   cancel_button;

	// Accessors

public:

	const char far *GetFileName   ( void ) { return name_box.GetEditText (); }
	bool            SelectionOnly ( void ) { return selection_check.IsChecked (); }

	// Constructors

	ExportDialog ( void );

	// Methods

	virtual bool HandleKey ( KEY_EVENT &key_event );
};

ExportDialog::ExportDialog ( void )
: Dialog          ( "export-dialog",  15, 7, 50, 11, "Export to Text" ),
  name_box        ( "export-name",     0, 0, 46, 3, 64, false ),
  selection_check ( "export-selection", 0, 0, "Selection only" ),
  export_button   ( "export-do",        0, 0, 10, "Export" ),
  cancel_button   ( "export-cancel",    0, 0, 10, "Cancel" )
{
	// Center the dialog for the actual row count.

	x = ( 80 - width ) / 2;
	y = ( ( ( Application::GetInstance () != NULL ) ? ( int ) Application::GetInstance ()->GetRows () : 25 ) - height ) / 2;

	// Place the controls relative to the dialog.

	name_box.SetPosition        ( x + 2,  y + 1 );
	selection_check.SetPosition ( x + 2,  y + 5 );
	export_button.SetPosition   ( x + 8,  y + 8 );
	cancel_button.SetPosition   ( x + 28, y + 8 );

	// The file-name field, seeded with a default name.

	name_box.SetText                     ( "File name" );
	name_box.SetColors                   ( COLOR_BLACK, COLOR_LIGHT_GRAY );
	name_box.SetSelectedBorderForeground ( COLOR_WHITE );
	name_box.SetEditText                 ( "DPROBE.TXT" );

	selection_check.SetColors ( COLOR_BLACK, COLOR_LIGHT_GRAY );

	// Wire the buttons and assemble the dialog.

	export_button.SetOnActivate ( DialogOkHandler,     this );
	cancel_button.SetOnActivate ( DialogCancelHandler, this );

	AddComponent ( &name_box );
	AddComponent ( &selection_check );
	AddComponent ( &export_button );
	AddComponent ( &cancel_button );
}

bool ExportDialog::HandleKey ( KEY_EVENT &key_event )
{
	// Enter is the Export default.

	if ( key_event.scan_code == KEY_ENTER )
	{
		Close ( DIALOG_RESULT_OK );

		return true;
	}

	return Dialog::HandleKey ( key_event );
}

//----------------------------------------------------------------------------
// Function: RunExport
//
// Description:
//
//   Runs the Export dialog and writes the text hex dump of the whole file
//   (or the selection) to the named file, reporting the outcome.
//
// Arguments:
//
//   - None
//
// Returns:
//
//   - None
//
//----------------------------------------------------------------------------

static void RunExport ( void )
{
	// Local variables.

	ExportDialog  dialog;
	char          name [ 64 ];
	unsigned long start;
	unsigned long length;

	// Run the dialog; anything but Export is a cancel.

	if ( dialog.RunModal ( Application::GetInstance () ) != DIALOG_RESULT_OK ) return;

	// An empty name is a cancel.

	strncpy ( name, ( const char * ) dialog.GetFileName (), sizeof ( name ) - 1 );

	name [ sizeof ( name ) - 1 ] = '\0';

	if ( name [ 0 ] == '\0' ) return;

	// Resolve the range and write the dump.

	if ( !DumpRange ( dialog.SelectionOnly (), &start, &length, "Export to Text" ) ) return;

	if ( WriteHexDump ( name, start, length, false ) != FILE_IO_OK )
	{
		ShowFileError ( "Cannot write the export file." );

		return;
	}

	// Report the completed export.

	{
		MessageBox message_box ( "export-done", "Export to Text", "Export complete." );

		message_box.RunModal ( Application::GetInstance () );
	}
}

//----------------------------------------------------------------------------
// Class: PrinterSetupDialog
//
// Description:
//
//   The Printer Setup dialog: choose the parallel printer
//   device (LPT1 / LPT2 / LPT3 / PRN) that Print writes to. Enter is the
//   Ok default.
//
//----------------------------------------------------------------------------

class PrinterSetupDialog : public Dialog
{
	// Data Members

protected:

	Label            device_label;
	RadioButtonGroup device_group;
	RadioButton      device_lpt1;
	RadioButton      device_lpt2;
	RadioButton      device_lpt3;
	RadioButton      device_prn;
	Button           ok_button;
	Button           cancel_button;

	// Accessors

public:

	const char *GetDevice ( void );

	// Constructors

	PrinterSetupDialog ( void );

	// Methods

	virtual bool HandleKey ( KEY_EVENT &key_event );
};

PrinterSetupDialog::PrinterSetupDialog ( void )
: Dialog        ( "printer-setup-dialog", 20, 7, 40, 11, "Printer Setup" ),
  device_label  ( "printer-label", 0, 0, "Printer:" ),
  device_group  ( "printer-group", 0, 0, 36, 1 ),
  device_lpt1   ( "printer-lpt1",  0, 0, "LPT1" ),
  device_lpt2   ( "printer-lpt2",  0, 0, "LPT2" ),
  device_lpt3   ( "printer-lpt3",  0, 0, "LPT3" ),
  device_prn    ( "printer-prn",   0, 0, "PRN" ),
  ok_button     ( "printer-ok",     0, 0, 10, "Ok" ),
  cancel_button ( "printer-cancel", 0, 0, 10, "Cancel" )
{
	// Center the dialog for the actual row count.

	x = ( 80 - width ) / 2;
	y = ( ( ( Application::GetInstance () != NULL ) ? ( int ) Application::GetInstance ()->GetRows () : 25 ) - height ) / 2;

	// Place the controls relative to the dialog.

	device_label.SetPosition  ( x + 2, y + 1 );
	device_lpt1.SetPosition   ( x + 4, y + 3 );
	device_lpt2.SetPosition   ( x + 4, y + 4 );
	device_lpt3.SetPosition   ( x + 4, y + 5 );
	device_prn.SetPosition    ( x + 4, y + 6 );
	ok_button.SetPosition     ( x + 6,  y + 8 );
	cancel_button.SetPosition ( x + 22, y + 8 );

	device_label.SetColors ( COLOR_BLACK, COLOR_LIGHT_GRAY );
	device_lpt1.SetColors  ( COLOR_BLACK, COLOR_LIGHT_GRAY );
	device_lpt2.SetColors  ( COLOR_BLACK, COLOR_LIGHT_GRAY );
	device_lpt3.SetColors  ( COLOR_BLACK, COLOR_LIGHT_GRAY );
	device_prn.SetColors   ( COLOR_BLACK, COLOR_LIGHT_GRAY );

	// Group the device radios and seed them from the configured device.

	device_group.AddRadioButton ( &device_lpt1 );
	device_group.AddRadioButton ( &device_lpt2 );
	device_group.AddRadioButton ( &device_lpt3 );
	device_group.AddRadioButton ( &device_prn );

	if      ( strcmp ( printer_device, "LPT2" ) == 0 ) device_group.SelectRadioButton ( &device_lpt2 );
	else if ( strcmp ( printer_device, "LPT3" ) == 0 ) device_group.SelectRadioButton ( &device_lpt3 );
	else if ( strcmp ( printer_device, "PRN"  ) == 0 ) device_group.SelectRadioButton ( &device_prn );
	else                                               device_group.SelectRadioButton ( &device_lpt1 );

	// Wire the buttons and assemble the dialog.

	ok_button.SetOnActivate     ( DialogOkHandler,     this );
	cancel_button.SetOnActivate ( DialogCancelHandler, this );

	AddComponent ( &device_label );
	AddComponent ( &device_group );
	AddComponent ( &ok_button );
	AddComponent ( &cancel_button );
}

const char *PrinterSetupDialog::GetDevice ( void )
{
	// The selected device; LPT1 is the fallback.

	if ( device_lpt2.IsSelected () ) return "LPT2";
	if ( device_lpt3.IsSelected () ) return "LPT3";
	if ( device_prn.IsSelected  () ) return "PRN";

	return "LPT1";
}

bool PrinterSetupDialog::HandleKey ( KEY_EVENT &key_event )
{
	// Enter is the Ok default.

	if ( key_event.scan_code == KEY_ENTER )
	{
		Close ( DIALOG_RESULT_OK );

		return true;
	}

	return Dialog::HandleKey ( key_event );
}

//----------------------------------------------------------------------------
// Class: PrintDialog
//
// Description:
//
//   The Print dialog: shows the configured printer device and
//   a "Selection only" toggle, with Print / Cancel buttons. Enter is the
//   Print default.
//
//----------------------------------------------------------------------------

class PrintDialog : public Dialog
{
	// Data Members

protected:

	Label    printer_label;
	CheckBox selection_check;
	Button   print_button;
	Button   cancel_button;

	// Accessors

public:

	bool SelectionOnly ( void ) { return selection_check.IsChecked (); }

	// Constructors

	PrintDialog ( void );

	// Methods

	virtual bool HandleKey ( KEY_EVENT &key_event );
};

PrintDialog::PrintDialog ( void )
: Dialog          ( "print-dialog", 20, 8, 40, 9, "Print" ),
  printer_label   ( "print-printer", 0, 0, "" ),
  selection_check ( "print-selection", 0, 0, "Selection only" ),
  print_button    ( "print-do",     0, 0, 10, "Print" ),
  cancel_button   ( "print-cancel", 0, 0, 10, "Cancel" )
{
	// Local variables.

	char printer_text [ 24 ];

	// Center the dialog for the actual row count.

	x = ( 80 - width ) / 2;
	y = ( ( ( Application::GetInstance () != NULL ) ? ( int ) Application::GetInstance ()->GetRows () : 25 ) - height ) / 2;

	// Place the controls relative to the dialog.

	printer_label.SetPosition   ( x + 2,  y + 1 );
	selection_check.SetPosition ( x + 2,  y + 3 );
	print_button.SetPosition    ( x + 6,  y + 6 );
	cancel_button.SetPosition   ( x + 22, y + 6 );

	// Show the configured printer device.

	sprintf ( printer_text, "Printer: %s", printer_device );

	printer_label.SetText   ( printer_text );
	printer_label.SetColors ( COLOR_BLACK, COLOR_LIGHT_GRAY );

	selection_check.SetColors ( COLOR_BLACK, COLOR_LIGHT_GRAY );

	// Wire the buttons and assemble the dialog.

	print_button.SetOnActivate  ( DialogOkHandler,     this );
	cancel_button.SetOnActivate ( DialogCancelHandler, this );

	AddComponent ( &printer_label );
	AddComponent ( &selection_check );
	AddComponent ( &print_button );
	AddComponent ( &cancel_button );
}

bool PrintDialog::HandleKey ( KEY_EVENT &key_event )
{
	// Enter is the Print default.

	if ( key_event.scan_code == KEY_ENTER )
	{
		Close ( DIALOG_RESULT_OK );

		return true;
	}

	return Dialog::HandleKey ( key_event );
}

//----------------------------------------------------------------------------
// Function: RunPrinterSetup / RunPrint
//
// Description:
//
//   RunPrinterSetup stores the chosen printer device. RunPrint runs the
//   Print dialog and sends the text hex dump of the whole file (or the
//   selection) to that device, ending with a form feed.
//
// Arguments:
//
//   - None
//
// Returns:
//
//   - None
//
//----------------------------------------------------------------------------

static void RunPrinterSetup ( void )
{
	PrinterSetupDialog dialog;

	// Run the dialog; Ok adopts the chosen device.

	if ( dialog.RunModal ( Application::GetInstance () ) != DIALOG_RESULT_OK ) return;

	strcpy ( printer_device, dialog.GetDevice () );
}

static void RunPrint ( void )
{
	// Local variables.

	PrintDialog   dialog;
	unsigned long start;
	unsigned long length;

	// Run the dialog; anything but Print is a cancel.

	if ( dialog.RunModal ( Application::GetInstance () ) != DIALOG_RESULT_OK ) return;

	// Resolve the range and send the dump to the printer.

	if ( !DumpRange ( dialog.SelectionOnly (), &start, &length, "Print" ) ) return;

	if ( WriteHexDump ( printer_device, start, length, true ) != FILE_IO_OK )
	{
		ShowFileError ( "Cannot print - the printer may not be ready." );

		return;
	}

	// Report the completed print.

	{
		MessageBox message_box ( "print-done", "Print", "Sent to the printer." );

		message_box.RunModal ( Application::GetInstance () );
	}
}

//----------------------------------------------------------------------------
// Function: RunDosShell
//
// Description:
//
//   Drops to a DOS command shell and returns cleanly. The INT 9
//   key-state hook is removed and the hardware cursor restored, a plain
//   25-row screen is set, COMMAND.COM (COMSPEC) is spawned and waited on,
//   then Data Probe's keyboard hook, cursor, text mode, and screen are
//   restored. A failed spawn is reported after the screen is back.
//
// Arguments:
//
//   - None
//
// Returns:
//
//   - None
//
//----------------------------------------------------------------------------

static void RunDosShell ( void )
{
	// Local variables.

	Application *application;
	char        *comspec;
	int          shell_result;

	// The shell to spawn: COMSPEC, or COMMAND.COM as the fallback.

	application = Application::GetInstance ();

	comspec = getenv ( "COMSPEC" );

	if ( comspec == NULL ) comspec = "COMMAND.COM";

	// Hand the screen and keyboard back to DOS for the shell.

	RemoveKeyboardHandler ();
	SetTextMode ( TEXT_MODE_25_ROWS );
	ShowCursor  ();

	printf ( "\r\nData Probe - type EXIT to return.\r\n\r\n" );

	shell_result = spawnlp ( P_WAIT, comspec, comspec, ( char * ) NULL );

	// Restore Data Probe's keyboard, text mode, cursor, and screen. The mode
	// set must precede HideCursor: a BIOS mode set reinitializes the video
	// hardware, restoring the default underline cursor at the home position,
	// so hiding the cursor first would simply be undone by it.

	InstallKeyboardHandler ();
	SetTextMode ( settings.rows );
	HideCursor  ();

	application->RenderFrame ();

	if ( shell_result == -1 ) ShowFileError ( "Cannot start the DOS shell." );
}

//----------------------------------------------------------------------------
// Class: AboutDialog
//
// Description:
//
//   The About dialog: the fixed program/version/release/author
//   block, verbatim, with an Ok button. Enter or Esc closes it.
//
//----------------------------------------------------------------------------

class AboutDialog : public Dialog
{
	// Data Members

protected:

	Label  title_line;
	Label  version_line;
	Label  release_line;
	Label  author_line;
	Button ok_button;

	// Constructors

public:

	AboutDialog ( void );

	// Methods

	virtual bool HandleKey ( KEY_EVENT &key_event );
};

AboutDialog::AboutDialog ( void )
: Dialog       ( "about-dialog", 25, 8, 30, 11, "About" ),
  title_line   ( "about-title",   0, 0, "Data Probe" ),
  version_line ( "about-version", 0, 0, "Version : 1.5" ),
  release_line ( "about-release", 0, 0, "Release : 1992" ),
  author_line  ( "about-author",  0, 0, " Author : Rohin Gosling" ),
  ok_button    ( "about-ok",      0, 0, 10, "Ok" )
{
	// Center the dialog for the actual row count.

	x = ( 80 - width ) / 2;
	y = ( ( ( Application::GetInstance () != NULL ) ? ( int ) Application::GetInstance ()->GetRows () : 25 ) - height ) / 2;

	// Place the fixed identity block and the Ok button.

	title_line.SetPosition   ( x + ( width - 10 ) / 2, y + 2 );
	version_line.SetPosition ( x + 4, y + 4 );
	release_line.SetPosition ( x + 4, y + 5 );
	author_line.SetPosition  ( x + 4, y + 6 );
	ok_button.SetPosition    ( x + ( width - 10 ) / 2, y + 8 );

	// Black-on-light-gray dialog text.

	title_line.SetColors   ( COLOR_BLACK, COLOR_LIGHT_GRAY );
	version_line.SetColors ( COLOR_BLACK, COLOR_LIGHT_GRAY );
	release_line.SetColors ( COLOR_BLACK, COLOR_LIGHT_GRAY );
	author_line.SetColors  ( COLOR_BLACK, COLOR_LIGHT_GRAY );

	// Wire the button and assemble the dialog.

	ok_button.SetOnActivate ( DialogOkHandler, this );

	AddComponent ( &title_line );
	AddComponent ( &version_line );
	AddComponent ( &release_line );
	AddComponent ( &author_line );
	AddComponent ( &ok_button );
}

bool AboutDialog::HandleKey ( KEY_EVENT &key_event )
{
	// Enter closes the dialog (Esc falls to the base).

	if ( key_event.scan_code == KEY_ENTER )
	{
		Close ( DIALOG_RESULT_OK );

		return true;
	}

	return Dialog::HandleKey ( key_event );
}

//----------------------------------------------------------------------------
// User Manual Text (embedded in the .EXE)
//----------------------------------------------------------------------------

static const char *manual_lines [] =
{
	"Data Probe (Version 1.5)",
	"------------------------",
	"",
	"Data Probe opens any file and lets you edit its",
	"bytes in place, in hex or as characters.",
	"",
	"- Files of any size can be loaded.",
	"",
	"- Large files are paged through a work file, so the",
	"  original is untouched until you Save.",
	"",
	"- Tab switches between the hex and character editors.",
	"",
	"- Ins toggles insert / overwrite mode. Type hex digits",
	"  or characters to edit the byte under the cursor.",
	"",
	"- The Space bar, Backspace, and Delete keys behave",
	"  differently depending on the insert state.",
	"  The insert state may be seen in the bottom right",
	"  as [   ] or [INS].",
	"",
	"Menus:",
	"",
	"F10 opens the menu bar and Esc closes it; anything",
	"without a key of its own lives there.",
	"",
	"File:",
	"",
	"  New            Start an empty, unnamed file.",
	"  Open...        Load a file from disk.",
	"  Save           Write the edits back to the file.",
	"  Save As...     Write the data out to a new name.",
	"  Export...      Write a text hex dump to a file.",
	"  Printer Setup  Choose the printer device.",
	"  Print...       Send a text hex dump to it.",
	"  DOS Shell      Run a shell; type EXIT to return.",
	"  Exit           Leave Data Probe.",
	"",
	"Export and Print cover the whole file, or just the",
	"selection. New, Open and Exit warn on unsaved work.",
	"",
	"Edit:",
	"",
	"  Cut            Copy the selection, then remove it.",
	"  Copy           Copy the selection.",
	"  Paste          Insert or overwrite, per Ins mode.",
	"  Goto...        Jump to a file offset.",
	"  Find...        Search for a byte pattern.",
	"  Replace...     Replace matches, one or all.",
	"  Settings...    Program settings.",
	"",
	"Program settings are kept in CONFIG.INI, in the same",
	"directory as DPROBE.EXE",
	"",
	"Help:",
	"",
	"  User Guide     This user guide.",
	"  About          Program info.",
	"",
	"Keyboard:",
	"",
	"  Tab             Switch hex / character editor",
	"  Ins             Insert / overwrite mode",
	"  Arrow keys      Move the cursor",
	"  PgUp / PgDn     Move one page",
	"  Home / End      Start / end of line",
	"  Shift+Home/End  Start / end of file",
	"  Shift+arrows    Select a block",
	"  Shift+PgUp/PgDn Extend the selection by a page",
	"  Ctrl+Ins        Copy",
	"  Shift+Del       Cut",
	"  Shift+Ins       Paste",
	"  Delete          Delete the byte or selection",
	"  Backspace       Delete back / remove selection",
	"  Space           Insert a zero byte, or in",
	"                  overwrite mode step one byte on",
	"  Ctrl+G          Goto an address",
	"  Ctrl+F          Find",
	"  Ctrl+R          Replace",
	"  F10             Open the menu bar",
	"  Esc             Exit Data Probe (asks first)",
	""
};

#define MANUAL_LINE_COUNT  ( ( int ) ( sizeof ( manual_lines ) / sizeof ( manual_lines [ 0 ] ) ) )

// ListBox::AddItem drops silently past LIST_ITEM_CAPACITY, so a guide that
// outgrows the list would simply lose its tail with no diagnostic. Size the
// array against the capacity here instead: a negative array bound is a
// compile error, so the overflow fails the build rather than the reader.

typedef char manual_fits_in_list_box [ ( MANUAL_LINE_COUNT <= LIST_ITEM_CAPACITY ) ? 1 : -1 ];

//----------------------------------------------------------------------------
// Class: ManualDialog
//
// Description:
//
//   The User Guide dialog: a scrolling viewer over the manual
//   text compiled into the executable, rendered as a ListBox so the arrow
//   keys and PgUp / PgDn scroll it. A Close button and Esc dismiss it.
//
//----------------------------------------------------------------------------

class ManualDialog : public Dialog
{
	// Data Members

protected:

	ListBox text_list;
	Button  close_button;

	// Constructors

public:

	ManualDialog ( void );
};

//----------------------------------------------------------------------------
// Function: ManualDialogHeight
//
// Description:
//
//   The User Guide dialog height for the active text mode, sized to cover
//   the hex data region: 19 rows in 25-row mode, 36 in 43-row mode, and 44
//   in 50-row mode. Centering places the dialog top at row 3 (the top of
//   the data area) in every mode, so the dialog blankets the hex view.
//
// Arguments:
//
//   - None
//
// Returns:
//
//   - The dialog height in rows for the current text mode.
//
//----------------------------------------------------------------------------

static int ManualDialogHeight ( void )
{
	// Local variables.

	BYTE mode_rows;

	// Pick the height that blankets the hex data region in the active mode.

	mode_rows = ( Application::GetInstance () != NULL ) ? Application::GetInstance ()->GetRows () : ( BYTE ) TEXT_MODE_25_ROWS;

	if ( mode_rows == TEXT_MODE_50_ROWS ) return 44;
	if ( mode_rows == TEXT_MODE_43_ROWS ) return 36;

	return 19;
}

ManualDialog::ManualDialog ( void )
: Dialog       ( "manual-dialog", 9, 3, 62, ManualDialogHeight (), "User Guide" ),
  text_list    ( "manual-list",  0, 0, 58, 13 ),
  close_button ( "manual-close", 0, 0, 10, "Close" )
{
	// Local variables.

	int i;

	// Center the dialog for the actual row count.

	x = ( 80 - width ) / 2;
	y = ( ( ( Application::GetInstance () != NULL ) ? ( int ) Application::GetInstance ()->GetRows () : 25 ) - height ) / 2;

	// The list fills the dialog from just below the top border down to just
	// above the Close button; the button sits just above the bottom border,
	// maximizing the vertical space for the guide text.

	text_list.SetPosition    ( x + 2, y + 1 );
	text_list.SetSize        ( 58, height - 3 );
	close_button.SetPosition ( x + ( width - 10 ) / 2, y + height - 2 );

	// Fill the list with the embedded manual text.

	for ( i = 0; i < MANUAL_LINE_COUNT; i++ ) text_list.AddItem ( manual_lines [ i ], NULL );

	text_list.SetColors                   ( COLOR_BLACK, COLOR_LIGHT_GRAY );
	text_list.SetSelectedBorderForeground ( COLOR_WHITE );

	// Wire the button and assemble the dialog.

	close_button.SetOnActivate ( DialogOkHandler, this );

	AddComponent ( &text_list );
	AddComponent ( &close_button );
}

//----------------------------------------------------------------------------
// Function: OnMenuExport / OnMenuPrinterSetup / OnMenuPrint /
//           OnMenuDosShell / OnMenuSettings / OnMenuUserGuide / OnMenuAbout
//
// Description:
//
//   The remaining File / Edit / Help menu handlers: each opens
//   its dialog or runs its action.
//
// Arguments:
//
//   - sender    : The menu item.
//   - user_data : Unused.
//
// Returns:
//
//   - None
//
//----------------------------------------------------------------------------

static void OnMenuExport ( Component *sender, void *user_data )
{
	( void ) sender;
	( void ) user_data;

	RunExport ();
}

static void OnMenuPrinterSetup ( Component *sender, void *user_data )
{
	( void ) sender;
	( void ) user_data;

	RunPrinterSetup ();
}

static void OnMenuPrint ( Component *sender, void *user_data )
{
	( void ) sender;
	( void ) user_data;

	RunPrint ();
}

static void OnMenuDosShell ( Component *sender, void *user_data )
{
	( void ) sender;
	( void ) user_data;

	RunDosShell ();
}

static void OnMenuSettings ( Component *sender, void *user_data )
{
	( void ) sender;
	( void ) user_data;

	RunSettings ();
}

static void OnMenuUserGuide ( Component *sender, void *user_data )
{
	ManualDialog dialog;

	( void ) sender;
	( void ) user_data;

	dialog.RunModal ( Application::GetInstance () );
}

static void OnMenuAbout ( Component *sender, void *user_data )
{
	AboutDialog dialog;

	( void ) sender;
	( void ) user_data;

	dialog.RunModal ( Application::GetInstance () );
}

//----------------------------------------------------------------------------
// Function: main
//
// Description:
//
//   Data Probe entry point, following the MicroApp consumer pattern: the
//   Application constructor sets the text mode, the application frame, menu tree,
//   status rows, and HexView workspace are assembled and installed as
//   the root, Run() drives the render / dispatch loop until Exit is
//   confirmed, and the Application destructor restores the text mode.
//
// Arguments:
//
//   - None
//
// Returns:
//
//   - 0 on completion.
//
//----------------------------------------------------------------------------

int main ( int argc, char *argv [] )
{
	( void ) argc;

	// Resolve CONFIG.INI in the executable's own directory (argv[0] carries
	// the full program path under DOS 3.0+) and overlay any persisted
	// settings onto the compiled defaults. A missing / unreadable file is
	// created from the defaults, so a valid CONFIG.INI exists from the first
	// run. This precedes the buffer allocation because a persisted Buffer
	// size feeds it.

	{
		char drive [ MAXDRIVE ];
		char dir   [ MAXDIR ];

		fnsplit ( argv [ 0 ], drive, dir, NULL, NULL );
		fnmerge ( config_path, drive, dir, "CONFIG", ".INI" );
	}

	if ( !LoadSettings () ) SaveSettings ();

	// Size and allocate the file working buffer from free far memory
	// (farcoreleft against the configured maximum) before
	// the video mode switches, so a failure can report plainly.

	if ( !file_buffer.Allocate ( settings.buffer_size ) )
	{
		printf ( "Data Probe: not enough memory for the file buffer.\n" );

		return 1;
	}

	settings.buffer_size = file_buffer.GetChunkSize ();   // The size actually adopted after farcoreleft clamping.

	// Launch in the persisted row mode. The Application confirms the mode it
	// actually set (43 / 50 are device-dependent and may fall back), so the
	// application frame and workspace below are built for that confirmed count and the
	// settings model is reconciled to match.

	Application application ( settings.rows );

	int launch_rows = ( int ) application.GetRows ();

	settings.rows = ( BYTE ) launch_rows;

	// The application frame: title bar (navy, white text), menu bar (light-gray,
	// black items, navy/white selection), black workspace, and the two
	// status rows (light-gray, dark-gray labels, black values).

	DataProbePanel application_frame ( "application-frame", launch_rows );

	application_frame.SetColors ( COLOR_LIGHT_GRAY, COLOR_BLACK );

	application_frame.GetTitleBar ()->SetText        ( DPROBE_TITLE );
	application_frame.GetTitleBar ()->SetVersionText ( DPROBE_VERSION );
	application_frame.GetTitleBar ()->SetColors      ( COLOR_WHITE, COLOR_BLUE );

	application_frame.GetMenuBar ()->SetColors ( COLOR_BLACK, COLOR_LIGHT_GRAY );

	application_frame.GetStatusBar ()->SetColors ( COLOR_DARK_GRAY, COLOR_LIGHT_GRAY );

	// The menu tree (File / Edit / Help).

	Menu file_menu ( "file-menu", "File" );
	Menu edit_menu ( "edit-menu", "Edit" );
	Menu help_menu ( "help-menu", "Help" );

	MenuItem file_new           ( "file-new",           "New" );
	MenuItem file_open          ( "file-open",          "Open..." );
	MenuItem file_save          ( "file-save",          "Save" );
	MenuItem file_save_as       ( "file-save-as",       "Save As..." );
	MenuItem file_separator_1   ( "file-separator-1",   "" );
	MenuItem file_export        ( "file-export",        "Export..." );
	MenuItem file_separator_2   ( "file-separator-2",   "" );
	MenuItem file_printer_setup ( "file-printer-setup", "Printer Setup..." );
	MenuItem file_print         ( "file-print",         "Print..." );
	MenuItem file_dos_shell     ( "file-dos-shell",     "DOS Shell" );
	MenuItem file_separator_3   ( "file-separator-3",   "" );
	MenuItem file_exit          ( "file-exit",          "Exit" );

	// The Edit items carry the display-only shortcut labels for the keys the
	// application itself dispatches (Settings... and every File / Help item
	// has no key, so none carries a label).

	MenuItem edit_cut         ( "edit-cut",         "Cut",         "Shift+Del" );
	MenuItem edit_copy        ( "edit-copy",        "Copy",        "Ctrl+Ins"  );
	MenuItem edit_paste       ( "edit-paste",       "Paste",       "Shift+Ins" );
	MenuItem edit_separator_1 ( "edit-separator-1", "" );
	MenuItem edit_goto        ( "edit-goto",        "Goto...",     "Ctrl+G"    );
	MenuItem edit_find        ( "edit-find",        "Find...",     "Ctrl+F"    );
	MenuItem edit_replace     ( "edit-replace",     "Replace...",  "Ctrl+R"    );
	MenuItem edit_separator_2 ( "edit-separator-2", "" );
	MenuItem edit_settings    ( "edit-settings",    "Settings..." );

	MenuItem help_user_guide ( "help-user-guide", "User Guide" );
	MenuItem help_separator  ( "help-separator",  "" );
	MenuItem help_about      ( "help-about",      "About" );

	// Mark the separators.

	file_separator_1.SetSeparator ( true );
	file_separator_2.SetSeparator ( true );
	file_separator_3.SetSeparator ( true );
	edit_separator_1.SetSeparator ( true );
	edit_separator_2.SetSeparator ( true );
	help_separator.SetSeparator   ( true );

	// Assemble the three menus in display order.

	file_menu.AddItem ( &file_new );
	file_menu.AddItem ( &file_open );
	file_menu.AddItem ( &file_save );
	file_menu.AddItem ( &file_save_as );
	file_menu.AddItem ( &file_separator_1 );
	file_menu.AddItem ( &file_export );
	file_menu.AddItem ( &file_separator_2 );
	file_menu.AddItem ( &file_printer_setup );
	file_menu.AddItem ( &file_print );
	file_menu.AddItem ( &file_dos_shell );
	file_menu.AddItem ( &file_separator_3 );
	file_menu.AddItem ( &file_exit );

	edit_menu.AddItem ( &edit_cut );
	edit_menu.AddItem ( &edit_copy );
	edit_menu.AddItem ( &edit_paste );
	edit_menu.AddItem ( &edit_separator_1 );
	edit_menu.AddItem ( &edit_goto );
	edit_menu.AddItem ( &edit_find );
	edit_menu.AddItem ( &edit_replace );
	edit_menu.AddItem ( &edit_separator_2 );
	edit_menu.AddItem ( &edit_settings );

	help_menu.AddItem ( &help_user_guide );
	help_menu.AddItem ( &help_separator );
	help_menu.AddItem ( &help_about );

	application_frame.GetMenuBar ()->AddMenu ( &file_menu );
	application_frame.GetMenuBar ()->AddMenu ( &edit_menu );
	application_frame.GetMenuBar ()->AddMenu ( &help_menu );

	// Every menu item is wired to its handler; Exit runs the confirmation.

	file_new.SetOnActivate           ( OnMenuNew,          NULL );
	file_open.SetOnActivate          ( OnMenuOpen,         NULL );
	file_save.SetOnActivate          ( OnMenuSave,         NULL );
	file_save_as.SetOnActivate       ( OnMenuSaveAs,       NULL );
	file_export.SetOnActivate        ( OnMenuExport,       NULL );
	file_printer_setup.SetOnActivate ( OnMenuPrinterSetup, NULL );
	file_print.SetOnActivate         ( OnMenuPrint,        NULL );
	file_dos_shell.SetOnActivate     ( OnMenuDosShell,     NULL );
	file_exit.SetOnActivate          ( OnMenuExit,         NULL );

	edit_cut.SetOnActivate      ( OnMenuCut,      NULL );
	edit_copy.SetOnActivate     ( OnMenuCopy,     NULL );
	edit_paste.SetOnActivate    ( OnMenuPaste,    NULL );
	edit_goto.SetOnActivate     ( OnMenuGoto,     NULL );
	edit_find.SetOnActivate     ( OnMenuFind,     NULL );
	edit_replace.SetOnActivate  ( OnMenuReplace,  NULL );
	edit_settings.SetOnActivate ( OnMenuSettings, NULL );

	help_user_guide.SetOnActivate ( OnMenuUserGuide, NULL );
	help_about.SetOnActivate      ( OnMenuAbout,     NULL );

	// The status rows: hex status (row max-1) and decimal status (row
	// max), with the insert-mode and num-lock indicator fields. The
	// column positions make room for the 8-digit fields; the hex view's
	// OnChange handler drives the values live.

	application_frame.GetStatusBar ()->SetColumnPosition ( 0, 1 );
	application_frame.GetStatusBar ()->SetColumnPosition ( 1, 16 );
	application_frame.GetStatusBar ()->SetColumnPosition ( 2, 33 );
	application_frame.GetStatusBar ()->SetColumnPosition ( 3, 48 );
	application_frame.GetStatusBar ()->SetColumnPosition ( 4, 57 );
	application_frame.GetStatusBar ()->SetColumnPosition ( 5, 74 );

	// Hex status row (labels + hex values). Decimal status row: the same
	// values in decimal, but the labels are dropped as redundant beneath
	// the row above - only a leading ':' is kept as a cue tying each
	// decimal value to the hex value over it. Each decimal label is a
	// run of spaces the width of the hex label it sits under, ending in
	// the ':', so the colons and values line up column-for-column.

	application_frame.GetStatusBar ()->SetField ( 0, 0, "Size:",     "0" );
	application_frame.GetStatusBar ()->SetField ( 0, 1, "Offset:",   "0" );
	application_frame.GetStatusBar ()->SetField ( 0, 2, "Selected:", "0" );
	application_frame.GetStatusBar ()->SetField ( 0, 3, "Byte:",     "0" );
	application_frame.GetStatusBar ()->SetField ( 0, 4, "Word:",     "0" );
	application_frame.GetStatusBar ()->SetField ( 0, 5, "[   ]",     "" );

	application_frame.GetStatusBar ()->SetField ( 1, 0, "    :",      "0" );   // under "Size:"
	application_frame.GetStatusBar ()->SetField ( 1, 1, "      :",    "0" );   // under "Offset:"
	application_frame.GetStatusBar ()->SetField ( 1, 2, "        :",  "0" );   // under "Selected:"
	application_frame.GetStatusBar ()->SetField ( 1, 3, "    :",      "0" );   // under "Byte:"
	application_frame.GetStatusBar ()->SetField ( 1, 4, "    :",      "0" );   // under "Word:"
	application_frame.GetStatusBar ()->SetField ( 1, 5, NumLockIndicatorText (), "" );   // Refreshed every frame by DataProbePanel::Draw.

	// Data values render black; the labels (and the ':' cues) keep the
	// bar's dark gray.

	application_frame.GetStatusBar ()->SetValueForeground ( COLOR_BLACK );

	// Two-tone indicators: the [ ] brackets stay in the
	// bar's dark-gray foreground; the 3-character INS / NUM interiors
	// show black.

	application_frame.GetStatusBar ()->SetHighlightForeground ( COLOR_BLACK );
	application_frame.GetStatusBar ()->SetFieldHighlight      ( 0, 5, 1, 3 );
	application_frame.GetStatusBar ()->SetFieldHighlight      ( 1, 5, 1, 3 );

	// The workspace: the HexView across the data rows, editing the file
	// buffer's loaded chunk. Launch starts on the same blank, unnamed,
	// empty buffer File > New gives; File > Open brings in a real file.

	HexView hex_view ( "hex-view", 0, 3, 80, launch_rows - 6 );

	file_buffer.NewFile ( "" );

	application_frame.SetWorkspace ( &hex_view );

	// Wire the live status fields, the paging engine, and the title,
	// and set their initial values.

	hex_view_pointer          = &hex_view;
	status_bar_pointer        = application_frame.GetStatusBar ();
	title_bar_pointer         = application_frame.GetTitleBar ();
	application_frame_pointer = &application_frame;

	hex_view.SetOnChange      ( OnHexViewChange,      NULL );
	hex_view.SetOnPageRequest ( OnHexViewPageRequest, NULL );

	// Apply the initial rendering settings (margin, divider, address base,
	// word order), then launch in the configured insert default: overwrite.
	// The blank launch buffer is filled by typing hex/characters that
	// append at its end, so overwrite is never forced to insert.

	ApplyViewSettings ();

	hex_view.SetInsertMode ( settings.insert_default );

	ReattachHexView ( 0, 0, -1 );
	UpdateTitleBar  ();
	OnHexViewChange ( &hex_view, NULL );

	// Run until Exit is confirmed; the destructor restores the mode.

	application.SetRoot ( &application_frame );

	// Show the About dialog on launch when the setting is enabled; render the
	// application frame first so the dialog floats over the assembled screen.

	if ( settings.show_about_on_launch )
	{
		AboutDialog about_dialog;

		application.RenderFrame ();
		about_dialog.RunModal ( &application );
	}

	application.Run     ();

	// Normal completion.

	return 0;
}

//----------------------------------------------------------------------------
