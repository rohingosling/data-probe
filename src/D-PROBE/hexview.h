//****************************************************************************
// Program: Data Probe (HexView)
// Version: 1.5
// Date:    1992-07-14
// Author:  Rohin Gosling
//
// Description:
//
//   HexView is Data Probe's workspace component: the 16-bytes-per-row
//   hex display with its address, byte, character, and relative-position
//   margin columns, plus the two editing engines - the nibble-level hex
//   editor and the character editor (Tab toggles) - with insert and
//   overwrite modes, nibble/byte delete and backspace shifting, and a
//   Shift+navigation selection block.
//
//   Row template (80 columns):
//
//     AAAAAAAA:   HH HH HH HH HH HH HH HH-HH HH HH HH HH HH HH HH   CC...CC M
//
//   The view fires OnChange whenever the cursor moves, so the
//   application can refresh its status fields (offset, byte, word).
//   For files larger than the working buffer the view is a window over
//   the file (SetWindow): addresses and navigation are absolute, and a
//   move beyond the window fires OnPageRequest for the application's
//   chunked paging engine.
//
//****************************************************************************

#ifndef _HEX_VIEW
#define _HEX_VIEW

#include "mtext.h"
#include "mapp.h"

//----------------------------------------------------------------------------
// Row Model
//----------------------------------------------------------------------------

#define HEX_VIEW_BYTES_PER_ROW  16      // Bytes displayed per data row.

//----------------------------------------------------------------------------
// Class: HexView
//
// Description:
//
//   The hex-editor workspace component (a MicroApp Component subclass),
//   displaying 16 bytes per row over an in-RAM data buffer.
//
//----------------------------------------------------------------------------

class HexView : public Component
{
	// Data Members

protected:

	BYTE far      *data;               // The displayed data buffer.
	unsigned long  data_length;        // Logical data length in bytes ( ( nibble_length + 1 ) / 2 ).
	unsigned long  data_capacity;      // Physical buffer size in bytes (the growth ceiling for inserts).
	unsigned long  nibble_length;      // Logical data length in nibbles (odd after a lone nibble insert).
	unsigned long  cursor_offset;      // Cursor position as a byte offset.
	int            cursor_nibble;      // Hex-editor sub-position: 0 = high nibble, 1 = low nibble.
	unsigned long  top_offset;         // Offset of the first visible row (multiple of 16).
	bool           hex_editor_active;  // true = hex editor, false = character editor (Tab toggles).
	bool           insert_mode;        // false = overwrite (the launch default).
	bool           selection_active;   // A Shift+navigation selection block is active.
	unsigned long  selection_anchor;   // Byte offset where the Shift+navigation selection began.
	bool           margin_enabled;     // Relative-position margin column on/off.
	bool           divider_enabled;    // Mid-row '-' divider on/off.
	bool           address_hex;        // Address column base: true = hex, false = decimal (Settings).
	bool           word_little_endian; // Word status field byte order: true = LE, false = BE (Settings).

	// Chunked-paging window state (all zero when the data is the whole
	// file): the window is the loaded chunk; addresses, the margin, and
	// navigation work in absolute file coordinates, movement beyond the
	// window fires OnPageRequest so the application can page, and the
	// margin maps every chunk boundary across the file (the seams where
	// paging pauses).

	unsigned long  window_base;         // Absolute file offset of data [ 0 ].
	unsigned long  window_tail;         // File bytes beyond the window's end.
	unsigned long  chunk_size;          // Chunk grid for the margin's boundary map (0 = whole file, no seams).
	bool           modified;            // Data mutated since the last ClearModified.
	unsigned long  page_request_offset; // Absolute byte offset of the last page request.
	int            page_request_nibble; // The nibble half the cursor wants after paging.
	EventHandler   on_page_request;     // Fired when the cursor moves beyond the window.
	void          *on_page_request_user_data;

	// Clipboard (CUA: Ctrl+Ins copy, Shift+Del cut, Shift+Ins paste; the
	// paste follows the edit mode). A far buffer farmalloc-ed on the first
	// copy/cut, holding the copied byte range for a later paste. Persists
	// across paging and file changes for the component's lifetime; freed
	// by the destructor.

	BYTE far      *clipboard;           // The copied/cut bytes.
	unsigned long  clipboard_length;    // Bytes currently held.
	unsigned long  clipboard_capacity;  // Allocated clipboard size.

	// Accessors

public:

	unsigned long GetDataLength     ( void ) { return data_length; }
	unsigned long GetCursorOffset   ( void ) { return cursor_offset; }
	int           GetCursorNibble   ( void ) { return cursor_nibble; }
	bool          GetInsertMode     ( void ) { return insert_mode; }
	bool          GetHexEditorActive( void ) { return hex_editor_active; }

	unsigned long GetWindowBase       ( void ) { return window_base; }
	unsigned long GetFileLength       ( void ) { return window_base + data_length + window_tail; }
	unsigned long GetAbsoluteOffset   ( void ) { return window_base + cursor_offset; }
	bool          GetModified         ( void ) { return modified; }
	unsigned long GetPageRequestOffset( void ) { return page_request_offset; }
	int           GetPageRequestNibble( void ) { return page_request_nibble; }

	void ClearModified ( void ) { modified = false; }

	BYTE GetCursorByte      ( void );  // The byte under the cursor (0 for an empty buffer).
	WORD GetCursorWord      ( void );  // The word at the cursor, in the configured byte order (Settings).
	unsigned long GetSelectedLength     ( void );   // Highlighted byte count; 0 when nothing is highlighted.
	unsigned long GetFindContinueOffset ( void );   // Absolute offset just past the highlighted block (else the cursor): where a find resumes.
	bool          GetSelectionRange     ( unsigned long *absolute_start, unsigned long *length );   // Highlighted block in absolute file coords; false when nothing is highlighted.

	// Mutators

	void SetData           ( BYTE far *new_data, unsigned long new_data_length, unsigned long new_data_capacity );
	void SetMarginEnabled     ( bool new_margin_enabled )        { margin_enabled = new_margin_enabled; }
	void SetDividerEnabled    ( bool new_divider_enabled )       { divider_enabled = new_divider_enabled; }
	void SetAddressHex        ( bool new_address_hex )           { address_hex = new_address_hex; }
	void SetWordLittleEndian  ( bool new_word_little_endian )    { word_little_endian = new_word_little_endian; }

	void SetWindow     ( unsigned long new_window_base, unsigned long new_window_tail )  { window_base = new_window_base; window_tail = new_window_tail; }
	void SetChunkSize  ( unsigned long new_chunk_size )                                  { chunk_size = new_chunk_size; }
	void SetInsertMode ( bool new_insert_mode )                                          { insert_mode = new_insert_mode; }

	void SetCursorPosition         ( unsigned long new_cursor_offset, int new_cursor_nibble );                   // Window-relative; clamps and scrolls.
	void SetCursorPositionAnchored ( unsigned long new_cursor_offset, int new_cursor_nibble, int screen_row );   // As above, holding the cursor on a given view row.

	void SelectRange ( unsigned long start_offset, unsigned long length );            // Window-relative; highlights a found match.

	int  GetCursorScreenRow ( void );                                                // The view row the cursor currently sits on.

	void SetOnPageRequest ( EventHandler handler, void *user_data )  { on_page_request = handler; on_page_request_user_data = user_data; }

	// Clipboard Operations

	bool CopySelection  ( void );   // Copy the selection (or the cursor byte) into the clipboard.
	bool CutSelection   ( void );   // Copy, then remove (shrinking the file).
	bool PasteClipboard ( void );   // Write the clipboard at the cursor - insert mode inserts, overwrite mode overwrites in place (growing at the file's end).

	// Constructors / Destructor

	HexView ( void );
	HexView ( const char *name, int x, int y, int width, int height );

	virtual ~HexView ( void );

	// Methods

	virtual void Draw      ( TEXTBUFFER *text_buffer );
	virtual bool HandleKey ( KEY_EVENT &key_event );

	// Editing-Engine Helpers

protected:

	unsigned long GetCursorNibblePosition ( void );                                  // cursor_offset / cursor_nibble as one nibble index.
	void          SetCursorNibblePosition ( unsigned long nibble_position );         // The inverse mapping.

	BYTE GetNibble       ( unsigned long nibble_index );
	void SetNibble       ( unsigned long nibble_index, BYTE value );
	bool InsertNibble    ( unsigned long nibble_index, BYTE value );                 // false when refused at capacity.
	void DeleteNibble    ( unsigned long nibble_index );
	bool InsertByte      ( unsigned long byte_offset, BYTE value );                  // false when refused at capacity.
	void DeleteByte      ( unsigned long byte_offset );
	bool GetSelectionBlock ( unsigned long *block_low, unsigned long *block_count ); // The highlighted bytes (anchor inclusive, cursor exclusive); false when none.
	void DeleteSelection ( bool zero_only );
	void ClampCursor     ( void );
	void ScrollToCursor  ( void );
	void FinishKeyAction ( void );                                                   // ClampCursor + ScrollToCursor + FireChange.
	void FirePageRequest ( unsigned long absolute_offset, int nibble );             // Ask the application to page the window.

	bool          GetAppendSlotActive ( void );                                     // The append slot at the file's end (both modes): one slot past the last byte.
	unsigned long GetCursorLimit      ( void );                                     // Highest reachable window-relative byte offset.

	bool EnsureClipboard ( void );                                                  // Lazily allocate the clipboard to the buffer's capacity.

	bool HandleNavigationKey ( KEY_EVENT &key_event );
	bool HandleHexEditorKey  ( KEY_EVENT &key_event );
	bool HandleCharacterKey  ( KEY_EVENT &key_event );
};

//----------------------------------------------------------------------------

#endif

//----------------------------------------------------------------------------
