//****************************************************************************
// Program: Data Probe (HexView)
// Version: 1.5
// Date:    1992-07-14
// Author:  Rohin Gosling
//
// Description:
//
//   HexView implementation: the 16-bytes-per-row hex display and its two
//   editing engines. Each data row renders the 8-digit address (dark-gray
//   ':'), the 16 two-nibble hex bytes with the dark-gray mid-row '-'
//   divider, the 16-character view (value-0 bytes as a dark-gray centered
//   dot, everything else as its glyph), and the relative-position margin
//   column. The cursor row shows red with light-red text; the active
//   editor's cursor cell is inverted (black on white), its counterpart in
//   the other column white.
//
//   Editing: Tab toggles the hex editor and the character editor; Ins
//   toggles insert/overwrite (launch default overwrite). Structure
//   moves in bytes, values edit in nibbles: the hex editor's
//   nibble cursor accepts 0-9 a-f - overwriting in place, or in insert
//   mode starting a fresh byte on a high-nibble keystroke and
//   completing it on the low - while Space steps a whole byte forward
//   in overwrite mode and pushes in a zero byte in insert mode, Delete
//   removes the byte under the cursor, and Backspace
//   removes the byte before it in insert mode or zeroes the previous
//   nibble in overwrite mode. Typing at the file's end appends, the
//   half-open last byte presenting zero-padded until its low digit
//   arrives. The character editor writes any printable byte with
//   byte-level insert/overwrite, Delete, and Backspace under the same
//   rules. Shift+navigation extends a byte selection: Delete zeroes
//   it, Backspace removes it (shrinking the file). The CUA clipboard
//   combos move byte ranges: Ctrl+Ins copies, Shift+Del cuts, and
//   Shift+Ins pastes over the bytes at the cursor. Every cursor move
//   and edit fires OnChange so the application can refresh its status
//   fields.
//
//   Chunked paging: the displayed data may be a window over a larger
//   file (SetWindow positions it). Addresses, the margin track, and
//   navigation work in absolute file coordinates; a cursor move beyond
//   the window fires OnPageRequest so the application can slide the
//   window, and the margin maps every chunk boundary across the file
//   (SetChunkSize) - the scaled positions where paging pauses fall.
//
//****************************************************************************

#include <stdio.h>
#include <stddef.h>
#include <alloc.h>

#include "mtext.h"
#include "mapp.h"
#include "hexview.h"

//----------------------------------------------------------------------------
// Row Layout Constants
//----------------------------------------------------------------------------

#define HEX_BYTES_PER_ROW       HEX_VIEW_BYTES_PER_ROW   // Bytes displayed per data row (the public row model).

#define HEX_COLUMN_ADDRESS      1       // 8-digit address + ':' at column 9.
#define HEX_COLUMN_BYTES        12      // First hex byte pair; byte i at 12 + i*3.
#define HEX_COLUMN_DIVIDER      35      // The '-' divider between bytes 7 and 8.
#define HEX_COLUMN_CHARACTERS   61      // 16-character view.
#define HEX_COLUMN_MARGIN       79      // Relative-position margin column.

#define HEX_ZERO_BYTE_GLYPH     0xFA    // Centered dot for value-0 bytes.

#define HEX_MARGIN_MAX_ROWS     64      // Upper bound on the margin's per-row seam map (screen rows <= 50).

//----------------------------------------------------------------------------
// Function: HexView::HexView
//
// Description:
//
//   Initializes the focusable hex-editor workspace component in light
//   gray on black (the workspace colors), with no data, the cursor at
//   offset 0, and the divider and margin enabled.
//
// Arguments:
//
//   - new_name   : Component name.
//   - new_x      : Absolute column.
//   - new_y      : Absolute row.
//   - new_width  : Width in cells.
//   - new_height : Height in cells (one data row per cell row).
//
// Returns:
//
//   - None
//
//----------------------------------------------------------------------------

HexView::HexView ( void ) : Component ()
{
	// The editing state: no data, the cursor at the origin, the hex editor active.

	data              = NULL;
	data_length       = 0;
	data_capacity     = 0;
	nibble_length     = 0;
	cursor_offset     = 0;
	cursor_nibble     = 0;
	top_offset        = 0;
	hex_editor_active = true;
	insert_mode       = false;
	selection_active  = false;
	selection_anchor  = 0;
	margin_enabled    = true;
	divider_enabled   = true;
	address_hex        = true;
	word_little_endian = true;
	focusable         = true;

	// The chunked-paging window state: a whole-file (unwindowed) view.

	window_base               = 0;
	window_tail               = 0;
	chunk_size                = 0;
	modified                  = false;
	page_request_offset       = 0;
	page_request_nibble       = 0;
	on_page_request           = NULL;
	on_page_request_user_data = NULL;

	// The clipboard: allocated lazily on the first copy or cut.

	clipboard          = NULL;
	clipboard_length   = 0;
	clipboard_capacity = 0;

	// The workspace colors.

	foreground = COLOR_LIGHT_GRAY;
	background = COLOR_BLACK;
}

HexView::HexView ( const char *new_name, int new_x, int new_y, int new_width, int new_height ) : Component ( new_name, new_x, new_y, new_width, new_height )
{
	// The editing state: no data, the cursor at the origin, the hex editor active.

	data              = NULL;
	data_length       = 0;
	data_capacity     = 0;
	nibble_length     = 0;
	cursor_offset     = 0;
	cursor_nibble     = 0;
	top_offset        = 0;
	hex_editor_active = true;
	insert_mode       = false;
	selection_active  = false;
	selection_anchor  = 0;
	margin_enabled    = true;
	divider_enabled   = true;
	address_hex        = true;
	word_little_endian = true;
	focusable         = true;

	// The chunked-paging window state: a whole-file (unwindowed) view.

	window_base               = 0;
	window_tail               = 0;
	chunk_size                = 0;
	modified                  = false;
	page_request_offset       = 0;
	page_request_nibble       = 0;
	on_page_request           = NULL;
	on_page_request_user_data = NULL;

	// The clipboard: allocated lazily on the first copy or cut.

	clipboard          = NULL;
	clipboard_length   = 0;
	clipboard_capacity = 0;

	// The workspace colors.

	foreground = COLOR_LIGHT_GRAY;
	background = COLOR_BLACK;
}

//----------------------------------------------------------------------------
// Function: HexView::~HexView
//
// Description:
//
//   Frees the clipboard buffer (the only resource the view owns; the data
//   buffer belongs to the application's file-buffer engine).
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

HexView::~HexView ( void )
{
	// Free the clipboard buffer, if it was ever allocated.

	if ( clipboard != NULL )
	{
		farfree ( clipboard );

		clipboard          = NULL;
		clipboard_length   = 0;
		clipboard_capacity = 0;
	}
}

//----------------------------------------------------------------------------
// Function: HexView::SetData
//
// Description:
//
//   Installs the data buffer to display and edit, and resets the cursor,
//   view, selection, and nibble length to the start.
//
// Arguments:
//
//   - new_data          : The buffer to display and edit.
//   - new_data_length   : The buffer's logical length in bytes.
//   - new_data_capacity : The buffer's physical size in bytes - the
//                         ceiling insert-mode growth may reach.
//
// Returns:
//
//   - None
//
//----------------------------------------------------------------------------

void HexView::SetData ( BYTE far *new_data, unsigned long new_data_length, unsigned long new_data_capacity )
{
	// Install the buffer and reset the cursor, view, selection, and window state.

	data             = new_data;
	data_length      = new_data_length;
	data_capacity    = new_data_capacity;
	nibble_length    = new_data_length*2;
	cursor_offset    = 0;
	cursor_nibble    = 0;
	top_offset       = 0;
	selection_active = false;
	window_base      = 0;
	window_tail      = 0;
	chunk_size       = 0;
	modified         = false;
}

//----------------------------------------------------------------------------
// Function: HexView::GetCursorByte / GetCursorWord
//
// Description:
//
//   The data under the cursor for the status fields: the byte at the
//   cursor, and the little-endian 16-bit word at the cursor (the final
//   byte of the buffer yields just that byte).
//
// Arguments:
//
//   - None
//
// Returns:
//
//   - The byte / little-endian word at the cursor; 0 for an empty
//     buffer.
//
//----------------------------------------------------------------------------

BYTE HexView::GetCursorByte ( void )
{
	// An empty buffer (or the cursor on the append slot) has no byte.

	if ( ( data == NULL ) || ( cursor_offset >= data_length ) ) return 0;

	// The byte under the cursor.

	return data [ cursor_offset ];
}

WORD HexView::GetCursorWord ( void )
{
	BYTE first_byte;    // The byte at the cursor (the lower file address).
	BYTE second_byte;   // The following byte (the higher file address).

	// An empty buffer (or the cursor on the append slot) has no word.

	if ( ( data == NULL ) || ( cursor_offset >= data_length ) ) return 0;

	// The cursor byte and the byte after it (zero past the file's end).

	first_byte  = data [ cursor_offset ];
	second_byte = ( cursor_offset + 1 < data_length ) ? data [ cursor_offset + 1 ] : 0;

	// The two bytes read as a word in the configured order:
	// little-endian makes the cursor byte the low half of the word,
	// big-endian the high half.

	if ( word_little_endian ) return ( WORD ) first_byte  | ( ( WORD ) second_byte << 8 );
	else                      return ( WORD ) second_byte | ( ( WORD ) first_byte  << 8 );
}

//----------------------------------------------------------------------------
// Function: HexView::GetSelectedLength
//
// Description:
//
//   The selection block's size for the status field: the count of
//   highlighted bytes (GetSelectionBlock), which is the run between the
//   anchor and the cursor excluding the byte under the cursor.
//
// Arguments:
//
//   - None
//
// Returns:
//
//   - The highlighted byte count; 0 when nothing is highlighted.
//
//----------------------------------------------------------------------------

unsigned long HexView::GetSelectedLength ( void )
{
	// Local variables.

	unsigned long block_low;
	unsigned long block_count;

	// The highlighted byte count, zero when nothing is highlighted.

	return GetSelectionBlock ( &block_low, &block_count ) ? block_count : 0;
}

//----------------------------------------------------------------------------
// Function: HexView::GetSelectionRange
//
// Description:
//
//   The highlighted selection block in absolute file coordinates - the
//   window-relative block from GetSelectionBlock lifted by the window base,
//   so a caller (e.g. Export) can read the exact highlighted bytes through
//   the file buffer whatever chunk is loaded.
//
// Arguments:
//
//   - absolute_start : Out: the block's first byte as an absolute file offset.
//   - length         : Out: the block's byte count.
//
// Returns:
//
//   - true with the range set when a non-empty block is highlighted; false
//     (both outputs zero) otherwise.
//
//----------------------------------------------------------------------------

bool HexView::GetSelectionRange ( unsigned long *absolute_start, unsigned long *length )
{
	// Local variables.

	unsigned long block_low;
	unsigned long block_count;

	// Nothing highlighted: report an empty range.

	if ( !GetSelectionBlock ( &block_low, &block_count ) )
	{
		*absolute_start = 0;
		*length         = 0;

		return false;
	}

	// Lift the window-relative block by the window base.

	*absolute_start = window_base + block_low;
	*length         = block_count;

	// A non-empty block is highlighted.

	return true;
}

//----------------------------------------------------------------------------
// Function: HexView::GetCursorNibblePosition / SetCursorNibblePosition
//
// Description:
//
//   The cursor's byte offset and nibble half mapped to and from a single
//   nibble index (byte offset * 2 + nibble half) - the coordinate the
//   hex editor's movement and editing arithmetic works in.
//
// Arguments:
//
//   - nibble_position : The nibble index to place the cursor at.
//
// Returns:
//
//   - GetCursorNibblePosition: the cursor's nibble index.
//
//----------------------------------------------------------------------------

unsigned long HexView::GetCursorNibblePosition ( void )
{
	return cursor_offset*2 + ( unsigned long ) cursor_nibble;
}

void HexView::SetCursorNibblePosition ( unsigned long nibble_position )
{
	cursor_offset = nibble_position / 2;
	cursor_nibble = ( int ) ( nibble_position & 1 );
}

//----------------------------------------------------------------------------
// Function: HexView::GetNibble / SetNibble
//
// Description:
//
//   Nibble-granular access over the byte buffer: nibble index i lives in
//   byte i/2, the even index in the byte's high half and the odd index
//   in its low half - matching the left-to-right hex-digit order on
//   screen.
//
// Arguments:
//
//   - nibble_index : The nibble's index (byte offset * 2 + 0 or 1).
//   - value        : The nibble value to write (low 4 bits).
//
// Returns:
//
//   - GetNibble: the nibble's value in the low 4 bits.
//
//----------------------------------------------------------------------------

BYTE HexView::GetNibble ( unsigned long nibble_index )
{
	// Local variables.

	BYTE byte_value;

	// Even indices read the byte's high half, odd its low half.

	byte_value = data [ nibble_index >> 1 ];

	return ( nibble_index & 1 ) ? ( BYTE ) ( byte_value & 0x0F ) : ( BYTE ) ( byte_value >> 4 );
}

void HexView::SetNibble ( unsigned long nibble_index, BYTE value )
{
	// Local variables.

	BYTE far *cell;

	// Write the nibble into its half of the byte, leaving the other half untouched.

	cell = &data [ nibble_index >> 1 ];

	if ( nibble_index & 1 ) *cell = ( BYTE ) ( ( *cell & 0xF0 ) | ( value & 0x0F ) );
	else                    *cell = ( BYTE ) ( ( *cell & 0x0F ) | ( value << 4 ) );

	modified = true;
}

//----------------------------------------------------------------------------
// Function: HexView::InsertNibble / DeleteNibble
//
// Description:
//
//   The nibble-shifting structural edits - kept as the substrate for
//   the nibble-granularity Settings toggle; the default
//   byte-granular key handlers use InsertByte / DeleteByte instead.
//   InsertNibble pushes everything from the insertion point one nibble
//   down-file and writes the new nibble (refused when the grown length
//   would pass the physical buffer). DeleteNibble pulls everything
//   after the deleted nibble one nibble up-file. The logical length is
//   tracked in nibbles (an odd count rounds up to a whole byte whose
//   spare low nibble is kept zero).
//
// Arguments:
//
//   - nibble_index : The nibble position to insert at / delete.
//   - value        : The nibble value to insert (low 4 bits).
//
// Returns:
//
//   - InsertNibble: true if the nibble was inserted, false if the grown
//     length would pass the physical buffer's capacity.
//
//----------------------------------------------------------------------------

bool HexView::InsertNibble ( unsigned long nibble_index, BYTE value )
{
	// Local variables.

	unsigned long new_nibble_length;
	unsigned long i;

	// Refuse growth past the physical buffer.

	new_nibble_length = nibble_length + 1;

	if ( ( new_nibble_length + 1 ) / 2 > data_capacity ) return false;

	// Push everything from the insertion point one nibble down-file.

	for ( i = nibble_length; i > nibble_index; i-- )
	{
		SetNibble ( i, GetNibble ( i - 1 ) );
	}

	// Write the new nibble.

	SetNibble ( nibble_index, value );

	// An odd length rounds up to a whole byte; keep its spare low nibble zero.

	if ( new_nibble_length & 1 ) SetNibble ( new_nibble_length, 0 );

	// Adopt the grown length.

	nibble_length = new_nibble_length;
	data_length   = ( nibble_length + 1 ) / 2;

	// The nibble is inserted.

	return true;
}

void HexView::DeleteNibble ( unsigned long nibble_index )
{
	// Local variables.

	unsigned long i;

	// Nothing to delete outside the data.

	if ( ( nibble_length == 0 ) || ( nibble_index >= nibble_length ) ) return;

	// Pull everything after the deleted nibble one nibble up-file.

	for ( i = nibble_index; i + 1 < nibble_length; i++ )
	{
		SetNibble ( i, GetNibble ( i + 1 ) );
	}

	// Shrink the length and zero the vacated trailing nibble.

	nibble_length--;

	SetNibble ( nibble_length, 0 );

	data_length = ( nibble_length + 1 ) / 2;
}

//----------------------------------------------------------------------------
// Function: HexView::InsertByte / DeleteByte
//
// Description:
//
//   The byte-shifting edits of the character editor. InsertByte pushes
//   everything from the insertion point one byte down-file and writes
//   the new byte (refused at the physical buffer's capacity); DeleteByte
//   pulls everything after the deleted byte one byte up-file.
//
// Arguments:
//
//   - byte_offset : The byte position to insert at / delete.
//   - value       : The byte value to insert.
//
// Returns:
//
//   - InsertByte: true if the byte was inserted, false if the buffer is
//     already at its physical capacity.
//
//----------------------------------------------------------------------------

bool HexView::InsertByte ( unsigned long byte_offset, BYTE value )
{
	// Local variables.

	unsigned long i;

	// Refuse growth past the physical buffer.

	if ( data_length >= data_capacity ) return false;

	// Push everything from the insertion point one byte down-file.

	for ( i = data_length; i > byte_offset; i-- )
	{
		data [ i ] = data [ i - 1 ];
	}

	// Write the new byte and adopt the grown length.

	data [ byte_offset ] = value;

	nibble_length += 2;
	data_length    = ( nibble_length + 1 ) / 2;
	modified       = true;

	// The byte is inserted.

	return true;
}

void HexView::DeleteByte ( unsigned long byte_offset )
{
	// Local variables.

	unsigned long i;

	// Nothing to delete outside the data.

	if ( ( data_length == 0 ) || ( byte_offset >= data_length ) ) return;

	// Pull everything after the deleted byte one byte up-file.

	for ( i = byte_offset; i + 1 < data_length; i++ )
	{
		data [ i ] = data [ i + 1 ];
	}

	// Zero the vacated last byte and adopt the shrunken length.

	data [ data_length - 1 ] = 0;

	nibble_length = ( nibble_length >= 2 ) ? nibble_length - 2 : 0;
	data_length   = ( nibble_length + 1 ) / 2;
	modified      = true;
}

//----------------------------------------------------------------------------
// Function: HexView::GetSelectionBlock
//
// Description:
//
//   The highlighted selection block - the exact bytes the user sees in the
//   selection colours, which every selection operation (copy, cut, delete,
//   the Selected status) acts on. The block runs from the anchor toward the
//   cursor, INCLUDING the anchor byte and EXCLUDING the byte under the
//   cursor: that byte is drawn as the cursor caret, not as a selected
//   byte, so excluding it here keeps the copied/cut/deleted bytes
//   identical to the highlighted ones. A selection that has not yet
//   crossed off its anchor byte (or an empty view) has no highlighted
//   bytes.
//
// Arguments:
//
//   - block_low   : Out: the block's first byte offset.
//   - block_count : Out: the block's byte count.
//
// Returns:
//
//   - true with block_low / block_count set when a non-empty block is
//     highlighted; false (count 0) otherwise.
//
//----------------------------------------------------------------------------

bool HexView::GetSelectionBlock ( unsigned long *block_low, unsigned long *block_count )
{
	// Local variables.

	unsigned long low;
	unsigned long high;

	// Default to an empty block.

	*block_low   = 0;
	*block_count = 0;

	// No active selection, or nothing highlighted yet.

	if ( !selection_active || ( data_length == 0 ) ) return false;
	if ( cursor_offset == selection_anchor )         return false;   // Nothing highlighted yet.

	// Normalize the block to low..high, excluding the byte under the cursor.

	if ( cursor_offset > selection_anchor )
	{
		low  = selection_anchor;        // Forward: anchor .. cursor - 1.
		high = cursor_offset - 1;
	}
	else
	{
		low  = cursor_offset + 1;       // Backward: cursor + 1 .. anchor.
		high = selection_anchor;
	}

	// Clamp to the data; an inverted block is empty.

	if ( high > data_length - 1 ) high = data_length - 1;
	if ( low > high )             return false;

	// Report the block.

	*block_low   = low;
	*block_count = high - low + 1;

	return true;
}

//----------------------------------------------------------------------------
// Function: HexView::DeleteSelection
//
// Description:
//
//   The selection-block operations over the highlighted bytes
//   (GetSelectionBlock): Delete zeroes them (the file size is unchanged);
//   Backspace removes them, pulling the data after the block up-file and
//   shrinking the file by the block size, with the cursor left at the
//   block's start. Both clear the selection. A selection with nothing
//   highlighted is simply cleared.
//
// Arguments:
//
//   - zero_only : true zeroes the block (Delete); false removes it
//                 (Backspace).
//
// Returns:
//
//   - None
//
//----------------------------------------------------------------------------

void HexView::DeleteSelection ( bool zero_only )
{
	// Local variables.

	unsigned long block_low;
	unsigned long block_count;
	unsigned long i;

	// Nothing highlighted: just clear the selection.

	if ( !GetSelectionBlock ( &block_low, &block_count ) )
	{
		selection_active = false;
		return;
	}

	// Delete zeroes the block in place; Backspace removes it, shrinking the file.

	if ( zero_only )
	{
		for ( i = 0; i < block_count; i++ )
		{
			data [ block_low + i ] = 0;
		}
	}
	else
	{
		for ( i = block_low + block_count; i < data_length; i++ )
		{
			data [ i - block_count ] = data [ i ];
		}

		nibble_length = ( nibble_length >= block_count*2 ) ? nibble_length - block_count*2 : 0;
		data_length   = ( nibble_length + 1 ) / 2;
		cursor_offset = block_low;
		cursor_nibble = 0;
	}

	// The block operation always ends the selection.

	selection_active = false;
	modified         = true;
}

//----------------------------------------------------------------------------
// Function: HexView::ClampCursor / ScrollToCursor / FinishKeyAction
//
// Description:
//
//   ClampCursor pins the cursor inside the file after a move or a
//   size-changing edit. The cursor ranges over the byte-rounded extent -
//   every nibble of every whole byte, including the zero pad nibble of a
//   rounded-off last byte - so the low nibble of the last byte is always
//   reachable. The append slot is the one exception: its ghost cell
//   holds no byte, so the slot is a single cursor position and the
//   nibble half pins to the high one - without this, movement could
//   rest the cursor invisibly on the ghost's low nibble (the ghost
//   draws as one cell) and typing there would fill a byte low-half
//   first. ScrollToCursor moves the view so the cursor's row stays
//   visible. FinishKeyAction is the shared epilogue of every consumed
//   cursor move and edit: clamp, scroll, and fire OnChange so the
//   application refreshes its status fields.
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

void HexView::ClampCursor ( void )
{
	// An empty view pins the cursor to the origin.

	if ( data_length == 0 )
	{
		cursor_offset = 0;
		cursor_nibble = 0;
		return;
	}

	// Pin the cursor to the highest reachable offset.

	if ( cursor_offset > GetCursorLimit () ) cursor_offset = GetCursorLimit ();

	// The append slot is a single cursor position: its ghost cell holds
	// no byte (and draws as one cell), so there is no low nibble to sit
	// on - movement that steps or lands there pins back to the high
	// nibble. After the clamp above, an offset at data_length can only
	// be the active append slot.

	if ( cursor_offset >= data_length ) cursor_nibble = 0;
}

//----------------------------------------------------------------------------
// Function: HexView::GetAppendSlotActive / GetCursorLimit
//
// Description:
//
//   The append slot: the cursor may sit one byte PAST the last byte, on
//   a ghost cell that holds no data. Without it, editing could only ever
//   overwrite or push data down-file, so nothing could be added after the
//   final byte and the file could not grow at its end. Typing a hex digit
//   or a character there (or Space) appends.
//
//   The slot is offered at the file's end in BOTH insert and overwrite
//   modes, so typing past the last byte grows the file either way (an
//   overwrite edit still replaces in place everywhere BEFORE the end;
//   only at the end does it extend). It is present whenever the file's
//   end is loaded (window_tail == 0), and unconditionally when the view
//   holds NO data at all - an empty file, or a window emptied by editing -
//   the one case where the slot is the only place the cursor can be and
//   the editor must always present a cell to type into (see the ghost row
//   in Draw). It is absent at the buffer's capacity, and (for a non-empty
//   view) when the file's end lies outside the loaded window.
//
//   GetCursorLimit is the highest window-relative byte offset the cursor
//   may occupy: the last byte, or the append slot when it is active.
//
// Arguments:
//
//   - None
//
// Returns:
//
//   - GetAppendSlotActive: true when the append slot exists.
//   - GetCursorLimit: the highest reachable window-relative byte offset.
//
//----------------------------------------------------------------------------

bool HexView::GetAppendSlotActive ( void )
{
	// No slot at the physical buffer's capacity.

	if ( data_length >= data_capacity ) return false;

	// No data in the view at all: the slot is the only place the cursor
	// can be - the editor must always present a cell to type into.

	if ( data_length == 0 ) return true;

	// The file's end is loaded: the slot sits one byte past it in both
	// insert and overwrite modes, so typing there grows the file either
	// way.

	return window_tail == 0;
}

unsigned long HexView::GetCursorLimit ( void )
{
	// An empty view: offset 0 is the only position.

	if ( data_length == 0 ) return 0;

	// The last byte, or the append slot when it is active.

	return GetAppendSlotActive () ? data_length : data_length - 1;
}

void HexView::ScrollToCursor ( void )
{
	// Scroll up to the cursor's row, or down until its row is the last visible.

	if ( cursor_offset < top_offset )
	{
		top_offset = ( cursor_offset / HEX_BYTES_PER_ROW )*HEX_BYTES_PER_ROW;
	}

	if ( cursor_offset >= top_offset + ( unsigned long ) height*HEX_BYTES_PER_ROW )
	{
		top_offset = ( cursor_offset / HEX_BYTES_PER_ROW - ( unsigned long ) ( height - 1 ) )*HEX_BYTES_PER_ROW;
	}
}

void HexView::FinishKeyAction ( void )
{
	// The shared epilogue: clamp, scroll, and notify.

	ClampCursor    ();
	ScrollToCursor ();
	FireChange     ();
}

//----------------------------------------------------------------------------
// Function: HexView::SetCursorPosition / SetCursorPositionAnchored /
//           GetCursorScreenRow
//
// Description:
//
//   Places the cursor at a window-relative byte offset and nibble half,
//   clamped to the data and scrolled into view - the application uses
//   this to restore the cursor after re-attaching a freshly paged
//   window. The anchored variant additionally scrolls the view so the
//   cursor sits on the given screen row (clamped to the view and the
//   data), keeping the highlight bar visually stationary across a page;
//   GetCursorScreenRow reads the row to anchor to before the page.
//
// Arguments:
//
//   - new_cursor_offset : The window-relative byte offset.
//   - new_cursor_nibble : 0 = high nibble, 1 = low nibble.
//   - screen_row        : The view row the cursor should sit on.
//
// Returns:
//
//   - GetCursorScreenRow: the view row the cursor currently sits on.
//
//----------------------------------------------------------------------------

void HexView::SetCursorPosition ( unsigned long new_cursor_offset, int new_cursor_nibble )
{
	// Place the cursor, then clamp and scroll it into view.

	cursor_offset = new_cursor_offset;
	cursor_nibble = new_cursor_nibble;

	ClampCursor    ();
	ScrollToCursor ();
}

void HexView::SetCursorPositionAnchored ( unsigned long new_cursor_offset, int new_cursor_nibble, int screen_row )
{
	// Local variables.

	unsigned long cursor_row;
	unsigned long data_rows;
	unsigned long top_row;
	unsigned long maximum_top_row;

	// Place and clamp the cursor first.

	cursor_offset = new_cursor_offset;
	cursor_nibble = new_cursor_nibble;

	ClampCursor ();

	// Clamp the anchor row to the view.

	if ( screen_row < 0 )       screen_row = 0;
	if ( screen_row >= height ) screen_row = height - 1;

	// Scroll so the cursor's row sits on screen_row, without scrolling
	// past the data's last screenful.

	cursor_row = cursor_offset / HEX_BYTES_PER_ROW;
	data_rows  = ( data_length + HEX_BYTES_PER_ROW - 1 ) / HEX_BYTES_PER_ROW;

	top_row = ( cursor_row > ( unsigned long ) screen_row ) ? cursor_row - ( unsigned long ) screen_row : 0;

	maximum_top_row = ( data_rows > ( unsigned long ) height ) ? data_rows - ( unsigned long ) height : 0;

	if ( top_row > maximum_top_row ) top_row = maximum_top_row;

	top_offset = top_row*HEX_BYTES_PER_ROW;

	ScrollToCursor ();
}

int HexView::GetCursorScreenRow ( void )
{
	// A cursor above the view maps to the first row.

	if ( cursor_offset < top_offset ) return 0;

	// The cursor's row relative to the view top.

	return ( int ) ( ( cursor_offset - top_offset ) / HEX_BYTES_PER_ROW );
}

//----------------------------------------------------------------------------
// Function: HexView::SelectRange
//
// Description:
//
//   Selects a window-relative byte range and places the cursor on its
//   first byte, scrolling it into view - the application uses this to
//   highlight a found match after paging its window into view. A zero
//   length (or a start past the data) clears the selection.
//
// Arguments:
//
//   - start_offset : The window-relative byte offset of the range start.
//   - length       : The range length in bytes.
//
// Returns:
//
//   - None
//
//----------------------------------------------------------------------------

void HexView::SelectRange ( unsigned long start_offset, unsigned long length )
{
	// Local variables.

	unsigned long selection_cursor;

	// An empty range (or one past the data) clears the selection.

	if ( ( length == 0 ) || ( data_length == 0 ) || ( start_offset >= data_length ) )
	{
		selection_active = false;

		return;
	}

	// Highlight [ start_offset, start_offset + length - 1 ]: the anchor sits
	// on the block's first byte and the cursor one byte PAST its last. Since
	// the cursor byte is excluded from the highlight (GetSelectionBlock), the
	// block then covers the whole range and the cursor rests just after it -
	// the natural spot to resume a find or keep editing.

	selection_cursor = start_offset + length;

	if ( selection_cursor > data_length ) selection_cursor = data_length;

	selection_anchor = start_offset;
	cursor_offset    = selection_cursor;
	cursor_nibble    = 0;
	selection_active = true;

	ScrollToCursor ();
}

//----------------------------------------------------------------------------
// Function: HexView::GetFindContinueOffset
//
// Description:
//
//   The absolute file offset a forward find resumes from: just past the
//   highlighted selection block (so a repeated find/replace steps beyond
//   the current match), or the cursor when nothing is highlighted. Computed
//   from the block rather than the cursor so it is correct however the
//   selection was built (forward, backward, or a find's own highlight).
//
// Arguments:
//
//   - None
//
// Returns:
//
//   - The absolute offset to resume searching from.
//
//----------------------------------------------------------------------------

unsigned long HexView::GetFindContinueOffset ( void )
{
	// Local variables.

	unsigned long block_low;
	unsigned long block_count;

	// Resume just past the highlighted block, in absolute coordinates.

	if ( GetSelectionBlock ( &block_low, &block_count ) )
	{
		return window_base + block_low + block_count;
	}

	// Nothing highlighted: resume at the cursor.

	return window_base + cursor_offset;
}

//----------------------------------------------------------------------------
// Function: HexView::EnsureClipboard / CopySelection / CutSelection /
//           PasteClipboard
//
// Description:
//
//   The CUA clipboard (Ctrl+Ins copy, Shift+Del cut, Shift+Ins paste).
//   EnsureClipboard lazily allocates the clipboard buffer to the data
//   buffer's capacity on the first copy or cut, so any in-window selection
//   fits.
//
//   CopySelection copies the highlighted selection block (GetSelectionBlock -
//   exactly the bytes shown selected, cursor byte excluded) - or, with
//   nothing highlighted, the single byte under the cursor - into the
//   clipboard, leaving the file unchanged. CutSelection copies, then removes
//   the same bytes (the block-remove that shrinks the file, or the single
//   cursor byte via the byte-delete). PasteClipboard follows the edit
//   mode: in insert mode it opens a gap and inserts the clipboard bytes,
//   shifting the rest of the file down; in overwrite mode it replaces
//   the bytes at the cursor in place, appending past the file's end (up to
//   the buffer's capacity) only where the window holds that end. All three
//   end in FinishKeyAction, so the status fields refresh and the
//   application's file-buffer bookkeeping picks up any size change.
//
// Arguments:
//
//   - None
//
// Returns:
//
//   - CopySelection / CutSelection: true when bytes were copied (a cut
//     also removed them); false when there was nothing to copy or the
//     clipboard could not be allocated.
//   - PasteClipboard: true when at least one byte was pasted; false when
//     the clipboard is empty.
//
//----------------------------------------------------------------------------

bool HexView::EnsureClipboard ( void )
{
	// No data buffer sized yet: nothing to size the clipboard against.

	if ( data_capacity == 0 ) return false;

	// The existing clipboard is already large enough.

	if ( ( clipboard != NULL ) && ( clipboard_capacity >= data_capacity ) ) return true;

	// Re-allocate at the (grown) data capacity.

	if ( clipboard != NULL ) farfree ( clipboard );

	clipboard          = ( BYTE far * ) farmalloc ( data_capacity );
	clipboard_capacity = ( clipboard != NULL ) ? data_capacity : 0;
	clipboard_length   = 0;

	// Report whether a clipboard exists.

	return clipboard != NULL;
}

bool HexView::CopySelection ( void )
{
	// Local variables.

	unsigned long block_low;
	unsigned long block_count;
	unsigned long i;

	// Nothing to copy from an empty view.

	if ( ( data == NULL ) || ( data_length == 0 ) ) return false;

	// The highlighted block, or - with nothing highlighted - the single byte
	// under the cursor. A cursor sitting on the append slot (past the last
	// byte) has no byte to copy.

	if ( !GetSelectionBlock ( &block_low, &block_count ) )
	{
		if ( cursor_offset >= data_length ) return false;

		block_low   = cursor_offset;
		block_count = 1;
	}

	// The clipboard must exist and fit the block.

	if ( !EnsureClipboard () || ( block_count > clipboard_capacity ) ) return false;

	// Copy the block's bytes.

	for ( i = 0; i < block_count; i++ )
	{
		clipboard [ i ] = data [ block_low + i ];
	}

	// The clipboard now holds the block.

	clipboard_length = block_count;

	return true;
}

bool HexView::CutSelection ( void )
{
	// Local variables.

	unsigned long block_low;
	unsigned long block_count;

	// The copy must land before anything is removed.

	if ( !CopySelection () ) return false;

	// Remove exactly what was copied: the highlighted block (shrinking the
	// file), or the single byte under the cursor when nothing is highlighted.

	if ( GetSelectionBlock ( &block_low, &block_count ) )
	{
		DeleteSelection ( false );
	}
	else
	{
		selection_active = false;

		DeleteByte ( cursor_offset );

		cursor_nibble = 0;
	}

	// Refresh the status and bookkeeping after the removal.

	FinishKeyAction ();

	// The bytes were copied and removed.

	return true;
}

bool HexView::PasteClipboard ( void )
{
	// Local variables.

	unsigned long i;
	unsigned long target;
	unsigned long bytes_written;

	// Nothing to paste without data and a non-empty clipboard.

	if ( ( data == NULL ) || ( clipboard == NULL ) || ( clipboard_length == 0 ) ) return false;

	// A paste supersedes the selection: clear it and count the bytes written.

	selection_active = false;
	bytes_written    = 0;

	if ( insert_mode )
	{
		// Insert paste: open a gap by shifting the rest of the file down and
		// drop the clipboard bytes in at the cursor, growing the file. Each
		// InsertByte shifts within the loaded window and grows the chunk (a
		// chunked file splices the growth into the work file on the next
		// flush); the run stops if the buffer's capacity is reached.

		for ( i = 0; i < clipboard_length; i++ )
		{
			if ( !InsertByte ( cursor_offset + i, clipboard [ i ] ) ) break;

			bytes_written++;
		}
	}
	else
	{
		// Overwrite paste: each clipboard byte replaces the byte at the cursor
		// and beyond. Where the run passes the last byte it appends, growing
		// the file up to the buffer's capacity - but only when the window
		// holds the file's end (window_tail == 0); appending into an unloaded
		// chunk tail would splice bytes in ahead of it rather than overwrite
		// it, so the paste stops at the loaded data instead.

		for ( i = 0; i < clipboard_length; i++ )
		{
			target = cursor_offset + i;

			if ( target < data_length )
			{
				data [ target ] = clipboard [ i ];
			}
			else if ( ( data_length < data_capacity ) && ( window_tail == 0 ) )
			{
				data [ data_length ] = clipboard [ i ];
				data_length++;
				nibble_length = data_length*2;
			}
			else
			{
				break;
			}

			bytes_written++;
		}
	}

	// Land the cursor after the pasted run.

	if ( bytes_written > 0 )
	{
		cursor_offset += bytes_written;
		cursor_nibble  = 0;
		modified       = true;
	}

	FinishKeyAction ();

	// Report whether at least one byte was pasted.

	return bytes_written > 0;
}

//----------------------------------------------------------------------------
// Function: HexView::FirePageRequest
//
// Description:
//
//   Records the absolute file position the cursor wants and fires
//   OnPageRequest, asking the application to slide the chunk window
//   there. The handler is expected to page the chunk in, re-attach the
//   view (SetData / SetWindow), and place the cursor (SetCursorPosition)
//   before returning. A no-op when no handler is installed.
//
// Arguments:
//
//   - absolute_offset : The absolute file byte offset the cursor wants.
//   - nibble          : The nibble half the cursor wants (0 or 1).
//
// Returns:
//
//   - None
//
//----------------------------------------------------------------------------

void HexView::FirePageRequest ( unsigned long absolute_offset, int nibble )
{
	// No handler installed: the request has nowhere to go.

	if ( on_page_request == NULL ) return;

	// Record the wanted position and ask the application to page.

	page_request_offset = absolute_offset;
	page_request_nibble = nibble;

	on_page_request ( this, on_page_request_user_data );
}

//----------------------------------------------------------------------------
// Function: HexView::Draw
//
// Description:
//
//   Renders the visible data rows from top_offset. Per row: the 8-digit
//   hex address with a dark-gray ':', the 16 hex byte pairs with the
//   dark-gray '-' divider after the 8th byte (when enabled), the
//   16-character view (value-0 bytes as a dark-gray centered dot), and
//   the relative-position margin column (when enabled).
//
//   Colors: light gray on black by default; separators, zero glyphs, and
//   the margin in dark gray; the cursor row is a navy highlight bar with
//   light-blue text. The active editor's cursor cell is inverted (black
//   on white) - in the hex editor the active nibble digit, in the
//   character editor the character cell - and the counterpart cells in
//   the other column are white. Selected bytes (outside the cursor cell)
//   show light red on red in both columns.
//
//   The append slot one byte past the file's end (see
//   GetAppendSlotActive) draws as a ghost cell - blank in the hex
//   column, a dot in the character column - and only while the cursor
//   is on it, so no row is ever conjured up for data that is not there.
//   A row lying wholly past the last byte is a ghost row and dots its
//   whole character column: that is the editor's minimum appearance for
//   an empty file (address, cursor, divider, dots, and the margin
//   track), the cue that it is ready for input.
//
//   The margin is a fixed track, independent of scrolling: the top
//   corner sits on the view's first row, the bottom corner on the
//   track's last row (the view's last row, or the data's last row for
//   data shorter than the view), and the cursor dash slides between
//   them proportionally to the cursor's line in the whole file. In
//   chunked mode a seam cue marks EVERY chunk boundary across the file
//   (at each boundary's scaled file position), so the margin is a
//   complete map of where paging pauses fall. Coinciding glyphs merge
//   per the margin's own table (dash + top corner = top tee, dash +
//   bottom corner = bottom tee, seam + dash = cross; the seam cue's
//   arms subsume a coinciding corner's).
//
//   In chunked mode the view shows the loaded window: addresses are
//   absolute file offsets (window_base + row offset), while the data,
//   cursor, and selection are window-relative.
//
// Arguments:
//
//   - text_buffer : The back buffer to draw into.
//
// Returns:
//
//   - None
//
//----------------------------------------------------------------------------

void HexView::Draw ( TEXTBUFFER *text_buffer )
{
	char           text [ 16 ];
	unsigned long  row_offset;
	unsigned long  byte_offset;
	unsigned long  display_length;
	unsigned long  margin_length;
	unsigned long  total_data_rows;
	unsigned long  file_length_total;
	unsigned long  boundary;
	unsigned long  remaining_cells;
	unsigned long  selection_low;
	unsigned long  selection_high;
	int            bottom_margin_row;
	int            scaled_row;
	int            boundary_row;
	int            row_cell_count;
	char           seam_on_row [ HEX_MARGIN_MAX_ROWS ];
	BYTE           row_foreground;
	BYTE           row_background;
	BYTE           separator_foreground;
	BYTE           data_byte;
	char           character;
	char           margin_glyph;
	bool           ghost_visible;
	bool           margin_top;
	bool           margin_bottom;
	bool           margin_cursor;
	bool           margin_boundary;
	bool           cursor_row;
	bool           selected;
	int            row;
	int            i;

	// Hidden views, or views with no data attached, draw nothing.

	if ( !visible )    return;
	if ( data == NULL ) return;

	// The active selection block, normalized to low/high byte offsets.

	selection_low  = ( selection_anchor < cursor_offset ) ? selection_anchor : cursor_offset;
	selection_high = ( selection_anchor > cursor_offset ) ? selection_anchor : cursor_offset;

	// The ghost cell - the append slot past the last byte - is drawn only
	// while the cursor actually sits on it, so a row is never conjured up
	// for data that is not there. An empty file therefore always shows a
	// single ghost row (the cursor has nowhere else to be), and typing
	// after the last byte of a full row opens the next row under the
	// cursor rather than ahead of it.

	ghost_visible  = GetAppendSlotActive () && ( cursor_offset == data_length );
	display_length = data_length + ( ghost_visible ? 1UL : 0UL );

	// The margin track: the top corner is fixed on the view's first row;
	// the bottom corner is fixed on the track's last row (the view's
	// last row, or the data's last row for data shorter than the view,
	// but never fewer than two rows - a track needs two distinct ends);
	// the cursor dash slides between them proportionally to the cursor's
	// LINE within the file's lines (not the byte offset within the
	// line). The track spans the WHOLE file - in chunked mode the file
	// is longer than the loaded window, so the lines and the cursor's
	// line are computed in absolute file coordinates - and it counts the
	// ghost cell, so the dash stays coherent when the cursor sits past
	// the last byte.

	file_length_total = GetFileLength ();
	margin_length     = file_length_total + ( ghost_visible ? 1UL : 0UL );

	total_data_rows   = ( margin_length + HEX_BYTES_PER_ROW - 1 ) / HEX_BYTES_PER_ROW;
	bottom_margin_row = ( total_data_rows > ( unsigned long ) height ) ? height - 1 : ( int ) total_data_rows - 1;

	if ( bottom_margin_row < 1 )          bottom_margin_row = 1;
	if ( bottom_margin_row > height - 1 ) bottom_margin_row = height - 1;

	scaled_row = ( total_data_rows > 1 )
	           ? ( int ) ( ( ( ( window_base + cursor_offset ) / HEX_BYTES_PER_ROW )*( unsigned long ) bottom_margin_row ) / ( total_data_rows - 1 ) )
	           : 0;

	// The chunk-boundary map: a seam cue for EVERY chunk boundary across
	// the whole file, not just the loaded window's edges, so the margin
	// is a complete map of where paging pauses fall. The boundaries are
	// anchored to the fixed chunk grid (multiples of chunk_size); with
	// overlap paging the actual pause when scrolling down lands on or
	// just before each grid line (a screenful earlier when scrolling
	// up), so the marks are a stable nominal reference rather than a
	// per-direction exact position. seam_on_row [ r ] flags the margin
	// rows a boundary scales onto; whole-file loads (chunk_size 0) have
	// none.

	for ( row = 0; row < HEX_MARGIN_MAX_ROWS; row++ ) seam_on_row [ row ] = 0;

	if ( ( chunk_size > 0 ) && ( total_data_rows > 1 ) )
	{
		for ( boundary = chunk_size; boundary < file_length_total; boundary += chunk_size )
		{
			boundary_row = ( int ) ( ( ( boundary / HEX_BYTES_PER_ROW )*( unsigned long ) bottom_margin_row ) / ( total_data_rows - 1 ) );

			if ( boundary_row >= bottom_margin_row ) break;      // Boundaries are monotonic; the rest fall on or past the bottom corner.

			if ( ( boundary_row >= 0 ) && ( boundary_row < HEX_MARGIN_MAX_ROWS ) ) seam_on_row [ boundary_row ] = 1;
		}
	}

	// Draw each visible data row from the top of the scroll window.

	for ( row = 0; row < height; row++ )
	{
		row_offset = top_offset + ( unsigned long ) row*HEX_BYTES_PER_ROW;

		if ( row_offset >= display_length ) break;

		// Row colors: the cursor's row is a navy highlight bar with light-
		// blue text; selected/active cells on it show white (below).

		cursor_row = ( cursor_offset >= row_offset ) && ( cursor_offset < row_offset + HEX_BYTES_PER_ROW );

		row_foreground       = cursor_row ? COLOR_LIGHT_BLUE : foreground;
		row_background       = cursor_row ? COLOR_BLUE       : background;
		separator_foreground = COLOR_DARK_GRAY;

		// Clear the row: the cursor row's highlight bar runs from the
		// left edge to one cell past the character column; everything
		// beyond it (the margin area) stays in the base colors.

		for ( i = 0; i < width; i++ )
		{
			if ( i < HEX_COLUMN_CHARACTERS + HEX_BYTES_PER_ROW + 1 )
			{
				PutCharacter ( text_buffer, x + i, y + row, ' ', row_foreground, row_background, 0, 0 );
			}
			else
			{
				PutCharacter ( text_buffer, x + i, y + row, ' ', foreground, background, 0, 0 );
			}
		}

		// Address column: 8 digits (absolute file offsets, so a paged chunk
		// shows its true position) and a dark-gray ':'. Hex by default, or
		// zero-padded decimal when the address base is set to decimal
		// (Settings).

		if ( address_hex ) sprintf ( text, "%08lX", window_base + row_offset );
		else               sprintf ( text, "%08lu", window_base + row_offset );

		PutText      ( text_buffer, x + HEX_COLUMN_ADDRESS, y + row, text, row_foreground, row_background, 0, 0 );
		PutCharacter ( text_buffer, x + HEX_COLUMN_ADDRESS + 8, y + row, ':', separator_foreground, row_background, 0, 0 );

		// The cells this row draws: its bytes, plus the ghost cell when
		// the cursor sits on it. A row entirely past the last byte is a
		// ghost row - it exists only to carry the cursor (an empty file's
		// minimum appearance) - and shows the empty-cell dots across its
		// whole character column.

		if ( row_offset >= data_length )
		{
			row_cell_count = HEX_BYTES_PER_ROW;
		}
		else
		{
			// Clamp in unsigned long BEFORE the cast: data_length - row_offset
			// can exceed a 16-bit int (a full 32 KB chunk overflows on row 0,
			// wrapping negative and drawing no bytes), so cast only the
			// already-clamped, <= 16 value.

			remaining_cells = data_length - row_offset;

			if ( ghost_visible ) remaining_cells++;

			row_cell_count = ( remaining_cells > ( unsigned long ) HEX_BYTES_PER_ROW ) ? HEX_BYTES_PER_ROW : ( int ) remaining_cells;
		}

		// Byte and character columns.

		for ( i = 0; i < row_cell_count; i++ )
		{
			byte_offset = row_offset + ( unsigned long ) i;

			// Ghost cells carry no byte: the hex column stays blank and
			// the character column shows the empty-cell dot, so the row
			// still reads as a row. The cursor inverts its cell in the
			// active editor and shows white in the other, exactly as it
			// does over real data.

			if ( byte_offset >= data_length )
			{
				if ( byte_offset == cursor_offset )
				{
					PutCharacter ( text_buffer, x + HEX_COLUMN_BYTES + i*3, y + row, ' ',
					               hex_editor_active ? COLOR_BLACK : COLOR_WHITE,
					               hex_editor_active ? COLOR_WHITE : row_background, 0, 0 );

					PutCharacter ( text_buffer, x + HEX_COLUMN_CHARACTERS + i, y + row, ( char ) HEX_ZERO_BYTE_GLYPH,
					               hex_editor_active ? COLOR_WHITE : COLOR_BLACK,
					               hex_editor_active ? row_background : COLOR_WHITE, 0, 0 );
				}
				else
				{
					PutCharacter ( text_buffer, x + HEX_COLUMN_CHARACTERS + i, y + row, ( char ) HEX_ZERO_BYTE_GLYPH,
					               separator_foreground, row_background, 0, 0 );
				}

				continue;
			}

			data_byte = data [ byte_offset ];
			selected  = selection_active && ( byte_offset >= selection_low ) && ( byte_offset <= selection_high );
			character = ( data_byte == 0 ) ? ( char ) HEX_ZERO_BYTE_GLYPH : ( char ) data_byte;

			sprintf ( text, "%02X", data_byte );

			// The hex byte pair: the cursor byte shows its active nibble
			// inverted when the hex editor is active (white otherwise);
			// selected bytes show light red on red.

			if ( byte_offset == cursor_offset )
			{
				if ( hex_editor_active )
				{
					PutCharacter ( text_buffer, x + HEX_COLUMN_BYTES + i*3,     y + row, text [ 0 ],
					               ( cursor_nibble == 0 ) ? COLOR_BLACK : COLOR_WHITE,
					               ( cursor_nibble == 0 ) ? COLOR_WHITE : row_background, 0, 0 );

					PutCharacter ( text_buffer, x + HEX_COLUMN_BYTES + i*3 + 1, y + row, text [ 1 ],
					               ( cursor_nibble == 1 ) ? COLOR_BLACK : COLOR_WHITE,
					               ( cursor_nibble == 1 ) ? COLOR_WHITE : row_background, 0, 0 );
				}
				else
				{
					PutText ( text_buffer, x + HEX_COLUMN_BYTES + i*3, y + row, text, COLOR_WHITE, row_background, 0, 0 );
				}
			}
			else if ( selected )
			{
				PutText ( text_buffer, x + HEX_COLUMN_BYTES + i*3, y + row, text, COLOR_LIGHT_RED, COLOR_RED, 0, 0 );
			}
			else
			{
				PutText ( text_buffer, x + HEX_COLUMN_BYTES + i*3, y + row, text, row_foreground, row_background, 0, 0 );
			}

			// The character view: inverted at the cursor when the
			// character editor is active (white otherwise); selected
			// bytes light red on red; value-0 bytes as a dark-gray dot.

			if ( byte_offset == cursor_offset )
			{
				if ( !hex_editor_active )
				{
					PutCharacter ( text_buffer, x + HEX_COLUMN_CHARACTERS + i, y + row, character, COLOR_BLACK, COLOR_WHITE, 0, 0 );
				}
				else
				{
					PutCharacter ( text_buffer, x + HEX_COLUMN_CHARACTERS + i, y + row, character, COLOR_WHITE, row_background, 0, 0 );
				}
			}
			else if ( selected )
			{
				PutCharacter ( text_buffer, x + HEX_COLUMN_CHARACTERS + i, y + row, character, COLOR_LIGHT_RED, COLOR_RED, 0, 0 );
			}
			else
			{
				PutCharacter ( text_buffer, x + HEX_COLUMN_CHARACTERS + i, y + row, character,
				               ( data_byte == 0 ) ? separator_foreground : row_foreground, row_background, 0, 0 );
			}
		}

		// Mid-row divider between bytes 7 and 8.

		if ( divider_enabled )
		{
			PutCharacter ( text_buffer, x + HEX_COLUMN_DIVIDER, y + row, '-', separator_foreground, row_background, 0, 0 );
		}

	}

	// Relative-position margin: the fixed track corners, the sliding
	// cursor dash, and a seam cue at every chunk boundary, merged per the
	// margin's own table where they coincide. The track is drawn in its
	// own pass so it stays complete even when the loaded window's data
	// ends above the view's last row (a short final window of a longer
	// file), and so an empty file still shows the two-row track of its
	// minimum appearance.

	if ( margin_enabled && ( margin_length > 0 ) )
	{
		for ( row = 0; ( row <= bottom_margin_row ) && ( row < height ); row++ )
		{
			margin_top      = ( row == 0 );
			margin_bottom   = ( row == bottom_margin_row );
			margin_cursor   = ( row == scaled_row );
			margin_boundary = ( row < HEX_MARGIN_MAX_ROWS ) && ( seam_on_row [ row ] != 0 );

			margin_glyph = '\0';

			if      ( margin_boundary && margin_cursor )             margin_glyph = ( char ) 0xC5;   // Seam + dash merge to a cross.
			else if ( margin_top && margin_bottom && margin_cursor ) margin_glyph = ( char ) 0xC5;   // All three merge to a cross.
			else if ( margin_boundary )                              margin_glyph = ( char ) 0xB4;   // The chunk-boundary cue; its arms subsume a coinciding corner's.
			else if ( margin_top && margin_bottom )                  margin_glyph = ( char ) 0xB4;   // Single-row track: both corners.
			else if ( margin_top && margin_cursor )                  margin_glyph = ( char ) 0xC2;   // Dash + top corner    = top tee.
			else if ( margin_bottom && margin_cursor )               margin_glyph = ( char ) 0xC1;   // Dash + bottom corner = bottom tee.
			else if ( margin_top )                                   margin_glyph = ( char ) 0xBF;   // Top-of-track corner.
			else if ( margin_bottom )                                margin_glyph = ( char ) 0xD9;   // Bottom-of-track corner.
			else if ( margin_cursor )                                margin_glyph = ( char ) 0xC4;   // The cursor-position dash.

			if ( margin_glyph != '\0' )
			{
				PutCharacter ( text_buffer, x + HEX_COLUMN_MARGIN, y + row, margin_glyph, separator_foreground, background, 0, 0 );
			}
		}
	}
}

//----------------------------------------------------------------------------
// Function: HexDigitValue
//
// Description:
//
//   Maps a typed character to its hex-digit value for the hex editor's
//   input filter.
//
// Arguments:
//
//   - character : The typed ASCII character.
//
// Returns:
//
//   - The digit value 0-15, or -1 if the character is not 0-9 a-f A-F.
//
//----------------------------------------------------------------------------

static int HexDigitValue ( char character )
{
	// Map the three accepted digit ranges.

	if ( ( character >= '0' ) && ( character <= '9' ) ) return character - '0';
	if ( ( character >= 'a' ) && ( character <= 'f' ) ) return character - 'a' + 10;
	if ( ( character >= 'A' ) && ( character <= 'F' ) ) return character - 'A' + 10;

	// Not a hex digit.

	return -1;
}

//----------------------------------------------------------------------------
// Function: HexView::HandleKey
//
// Description:
//
//   The editing-engine dispatcher. The CUA clipboard combos are caught
//   first, distinguished by the shift/ctrl state (GetShiftState): Ctrl+Ins
//   copies, Shift+Ins pastes (overwriting), Shift+Del cuts - the scan code
//   names the key, the modifier picks the action, so a plain Ins still
//   toggles insert/overwrite and a plain Del still deletes in the editor.
//   Tab toggles the hex and character editors; the navigation keys move
//   the cursor (Shift extends the selection); everything else goes to the
//   active editor. Esc and F10 are left to bubble to the application.
//
// Arguments:
//
//   - key_event : The key event to handle.
//
// Returns:
//
//   - true if the key was consumed, false to bubble it.
//
//----------------------------------------------------------------------------

bool HexView::HandleKey ( KEY_EVENT &key_event )
{
	// Local variables.

	WORD shift_state;

	// No data attached: nothing to edit.

	if ( data == NULL ) return false;

	// Clipboard combos: the shift/ctrl state distinguishes Ctrl+Ins (copy)
	// and Shift+Ins (paste) from a plain Ins (insert-mode toggle), and
	// Shift+Del (cut) from a plain Del (handled as delete by the editors).

	if ( key_event.scan_code == KEY_INS )
	{
		shift_state = GetShiftState ();

		if      ( shift_state & SHIFT_STATE_CTRL )  CopySelection  ();
		else if ( shift_state & SHIFT_STATE_SHIFT ) PasteClipboard ();
		else
		{
			// The toggle changes no data and moves no cursor (the append
			// slot exists in both modes) - FinishKeyAction runs for its
			// OnChange, refreshing the [INS] status indicator.

			insert_mode = !insert_mode;

			FinishKeyAction ();
		}

		return true;
	}

	if ( ( key_event.scan_code == KEY_DELETE ) && ( GetShiftState () & SHIFT_STATE_SHIFT ) )
	{
		CutSelection ();

		return true;
	}

	// Tab toggles the hex and character editors.

	if ( key_event.scan_code == KEY_TAB )
	{
		hex_editor_active = !hex_editor_active;

		FireChange ();

		return true;
	}

	// Navigation first; anything else goes to the active editor.

	if ( HandleNavigationKey ( key_event ) ) return true;

	return hex_editor_active ? HandleHexEditorKey ( key_event ) : HandleCharacterKey ( key_event );
}

//----------------------------------------------------------------------------
// Function: HexView::HandleNavigationKey
//
// Description:
//
//   Cursor navigation for both editors: in the hex editor Left/Right
//   move nibble-by-nibble (skipping the display whitespace between
//   bytes, wrapping row-to-row); in the character editor they move
//   byte-by-byte. Up/Down move one row, PgUp/PgDn one screen, Home/End
//   jump to the start/end of the line - or of the whole file with Shift
//   held - all clamped to the data and scrolling the view to follow.
//   Movement works in absolute file coordinates: in chunked mode a move
//   beyond the loaded window fires OnPageRequest so the application can
//   page the adjacent chunk in and re-place the cursor.
//   Holding Shift extends the selection block from an anchor set where
//   the Shift navigation began; unshifted movement clears the selection.
//   Home and End repurpose Shift to pick the jump scope (line vs file),
//   so they always clear the selection instead of extending it.
//
// Arguments:
//
//   - key_event : The key event to handle.
//
// Returns:
//
//   - true if the key was a navigation key, false otherwise.
//
//----------------------------------------------------------------------------

bool HexView::HandleNavigationKey ( KEY_EVENT &key_event )
{
	// Local variables.

	unsigned long page_bytes;
	unsigned long absolute_offset;
	unsigned long absolute_total;
	unsigned long absolute_last;
	unsigned long absolute_limit;
	unsigned long append_slot;
	unsigned long window_limit;
	unsigned long nibble_position;
	unsigned long last_nibble;
	int           target_nibble;
	bool          shift_extend;

	// Only the eight navigation keys are handled here.

	switch ( key_event.scan_code )
	{
		case KEY_LEFT:
		case KEY_RIGHT:
		case KEY_UP:
		case KEY_DOWN:
		case KEY_PGUP:
		case KEY_PGDN:
		case KEY_HOME:
		case KEY_END:

			break;

		default:

			return false;
	}

	// Nothing to navigate in a totally empty file (the key is still consumed).

	absolute_total = window_base + data_length + window_tail;

	if ( absolute_total == 0 ) return true;

	// Shift extends the selection from an anchor at the cursor position
	// where the Shift navigation began; unshifted movement clears it.
	// Home/End use Shift for the jump scope instead (line vs file), so
	// they always clear the selection.

	shift_extend = ( GetShiftState () & SHIFT_STATE_SHIFT ) != 0;

	if ( ( key_event.scan_code == KEY_HOME ) || ( key_event.scan_code == KEY_END ) )
	{
		selection_active = false;
	}
	else
	{
		if ( shift_extend && !selection_active )
		{
			selection_anchor = cursor_offset;
			selection_active = true;
		}

		if ( !shift_extend ) selection_active = false;
	}

	// The movement arithmetic works in absolute file coordinates so a
	// move can land beyond the loaded window in chunked mode. The
	// movement limit runs one byte past the file's end when the append
	// slot is active (in both modes; see GetAppendSlotActive), so data
	// can be added after the last byte; End still lands on the last real
	// byte. A step onto the append slot's ghost low nibble is transient -
	// ClampCursor pins it back to the high one.

	append_slot     = GetAppendSlotActive () ? 1UL : 0UL;

	page_bytes      = ( unsigned long ) height*HEX_BYTES_PER_ROW;
	absolute_offset = window_base + cursor_offset;
	absolute_last   = absolute_total - 1;
	absolute_limit  = absolute_last + append_slot;
	last_nibble     = absolute_limit*2 + 1;   // The byte-rounded extent: the last real byte's low nibble is always reachable.
	nibble_position = absolute_offset*2 + ( unsigned long ) cursor_nibble;
	target_nibble   = cursor_nibble;

	// Apply the movement in absolute coordinates.

	switch ( key_event.scan_code )
	{
		case KEY_LEFT:

			if ( hex_editor_active )
			{
				if ( nibble_position > 0 ) nibble_position--;

				absolute_offset = nibble_position / 2;
				target_nibble   = ( int ) ( nibble_position & 1 );
			}
			else
			{
				if ( absolute_offset > 0 ) absolute_offset--;
			}
			break;

		case KEY_RIGHT:

			if ( hex_editor_active )
			{
				if ( nibble_position < last_nibble ) nibble_position++;

				absolute_offset = nibble_position / 2;
				target_nibble   = ( int ) ( nibble_position & 1 );
			}
			else
			{
				if ( absolute_offset < absolute_limit ) absolute_offset++;
			}
			break;

		case KEY_UP:

			if ( absolute_offset >= HEX_BYTES_PER_ROW ) absolute_offset -= HEX_BYTES_PER_ROW;
			break;

		case KEY_DOWN:

			if ( absolute_offset + HEX_BYTES_PER_ROW <= absolute_limit ) absolute_offset += HEX_BYTES_PER_ROW;
			break;

		case KEY_PGUP:

			absolute_offset = ( absolute_offset >= page_bytes ) ? absolute_offset - page_bytes : 0;
			break;

		case KEY_PGDN:

			absolute_offset = ( absolute_offset + page_bytes <= absolute_limit ) ? absolute_offset + page_bytes : absolute_limit;
			break;

		case KEY_HOME:

			// Home = start of the line; Shift+Home = start of the file.

			absolute_offset = shift_extend ? 0 : absolute_offset - ( absolute_offset % HEX_BYTES_PER_ROW );
			target_nibble   = 0;
			break;

		case KEY_END:

			// End = end of the line; Shift+End = end of the file. The
			// cursor lands on the last byte's first (high) nibble.

			if ( shift_extend )
			{
				absolute_offset = absolute_last;
			}
			else
			{
				absolute_offset = absolute_offset - ( absolute_offset % HEX_BYTES_PER_ROW ) + ( HEX_BYTES_PER_ROW - 1 );

				if ( absolute_offset > absolute_last ) absolute_offset = absolute_last;
			}

			target_nibble = 0;
			break;
	}

	// A target inside the loaded window (including the append slot at the
	// file's end) moves the cursor directly. Beyond it, the page request
	// asks the application to slide the window (the handler re-attaches
	// the view and places the cursor); with no handler installed the move
	// clamps to the window's edge.

	window_limit = window_base + GetCursorLimit ();

	if ( ( absolute_offset >= window_base ) && ( absolute_offset <= window_limit ) )
	{
		cursor_offset = absolute_offset - window_base;
		cursor_nibble = target_nibble;
	}
	else if ( on_page_request != NULL )
	{
		FirePageRequest ( absolute_offset, target_nibble );
	}
	else
	{
		cursor_offset = ( absolute_offset < window_base ) ? 0 : GetCursorLimit ();
		cursor_nibble = target_nibble;
	}

	FinishKeyAction ();

	return true;
}

//----------------------------------------------------------------------------
// Function: HexView::HandleHexEditorKey
//
// Description:
//
//   The hex editor: a nibble cursor for editing values, byte-granular
//   structural edits (structure moves in bytes, values edit in
//   nibbles). Only 0-9 a-f are accepted as input: overwrite mode
//   replaces the nibble under the cursor in place; insert mode inserts
//   a fresh byte on a high-nibble keystroke (carrying the digit in its
//   high half) and completes it in place on the following low-nibble
//   keystroke. Both advance one nibble and update the character column
//   live, and typing at the end of the file appends (the half-open last
//   byte presents zero-padded until its low digit arrives). Space in
//   overwrite mode steps a whole byte forward (to the next byte's high
//   nibble); in insert mode it inserts a zero byte.
//   Delete removes the byte under the cursor (or zeroes an active
//   selection). Backspace: insert mode removes the byte before the
//   cursor, dragging all down-file data with it (the file shrinks);
//   overwrite mode steps back one nibble and zeroes it in place (the
//   file size is unchanged). Backspace on an active selection removes
//   the block.
//
// Arguments:
//
//   - key_event : The key event to handle.
//
// Returns:
//
//   - true if the key was consumed, false to bubble it.
//
//----------------------------------------------------------------------------

bool HexView::HandleHexEditorKey ( KEY_EVENT &key_event )
{
	// Local variables.

	unsigned long nibble_position;
	unsigned long block_low;
	unsigned long block_count;
	int           digit_value;
	bool          edit_applied;

	// The cursor as a single nibble index.

	nibble_position = GetCursorNibblePosition ();

	// Delete: zero an active selection, or remove the byte under the cursor.

	if ( key_event.scan_code == KEY_DELETE )
	{
		if ( GetSelectionBlock ( &block_low, &block_count ) )
		{
			DeleteSelection ( true );
		}
		else if ( data_length > 0 )
		{
			// Structural edits move whole bytes: the byte under the
			// cursor is removed and the data after it pulls up.

			selection_active = false;

			DeleteByte ( cursor_offset );

			cursor_nibble = 0;
		}

		FinishKeyAction ();

		return true;
	}

	// Backspace: remove an active selection, the byte before the cursor (insert
	// mode), or zero the previous nibble in place (overwrite mode).

	if ( key_event.scan_code == KEY_BACKSPACE )
	{
		if ( GetSelectionBlock ( &block_low, &block_count ) )
		{
			DeleteSelection ( false );
		}
		else if ( insert_mode )
		{
			// Structural edits move whole bytes: remove the byte
			// before the cursor, dragging all down-file data with it
			// (the file shrinks), and land on its slot's high nibble.
			// At the file's first byte there is no byte before the
			// cursor, so Backspace simply steps back to that byte's high
			// nibble rather than stalling on its low one.

			selection_active = false;

			if ( cursor_offset > 0 )
			{
				cursor_offset--;
				cursor_nibble = 0;

				DeleteByte ( cursor_offset );
			}
			else
			{
				cursor_nibble = 0;
			}
		}
		else if ( nibble_position > 0 )
		{
			// Overwrite mode edits values in place: step back one nibble
			// and zero it (the file size is unchanged). The one skipped
			// position is the zero pad nibble of a half-open last byte
			// (nibble_length itself, stepped onto from the append slot):
			// no data lives there and it already reads zero, so zeroing
			// would only spuriously dirty the file.

			selection_active = false;

			nibble_position--;

			if ( nibble_position < nibble_length ) SetNibble ( nibble_position, 0 );

			SetCursorNibblePosition ( nibble_position );
		}

		FinishKeyAction ();

		return true;
	}

	// Space: insert a zero byte (insert mode) or step a whole byte forward (overwrite).

	if ( key_event.scan_code == KEY_SPACE )
	{
		if ( insert_mode )
		{
			// Structural edits move whole bytes: push a zero byte
			// in at the cursor.

			if ( InsertByte ( cursor_offset, 0 ) ) cursor_nibble = 0;
		}
		else if ( ( data_length > 0 ) && ( cursor_offset < GetCursorLimit () ) )
		{
			// Overwrite mode: Space is otherwise inert, so it steps a
			// whole byte forward to the next byte's high nibble - a fast
			// byte advance to complement the arrows' nibble stepping.

			cursor_offset++;
			cursor_nibble = 0;
		}
		else if ( ( data_length > 0 ) && ( window_tail > 0 ) )
		{
			// The step crosses the window's end: page forward.

			FirePageRequest ( window_base + data_length, 0 );
		}

		selection_active = false;

		FinishKeyAction ();

		return true;
	}

	// A hex digit edits the nibble under the cursor.

	digit_value = HexDigitValue ( ( char ) key_event.ascii );

	if ( digit_value >= 0 )
	{
		edit_applied = false;

		if ( nibble_position >= nibble_length )
		{
			// The zero pad nibble of a rounded-off last byte (or the
			// first nibble of an empty file): writing here appends the
			// nibble to the logical length in both modes. A write that
			// starts a fresh byte zeroes the whole byte first - beyond the
			// data the buffer holds whatever farmalloc left there, so the
			// test is "is this byte past the current data", not merely
			// "is this the byte's high nibble" - defensive: ClampCursor
			// pins the cursor off the append slot's low nibble, but the
			// byte test stays correct for any caller that lands there.

			if ( nibble_position / 2 < data_capacity )
			{
				if ( nibble_position / 2 >= data_length ) data [ nibble_position / 2 ] = 0;

				SetNibble ( nibble_position, ( BYTE ) digit_value );

				nibble_length = nibble_position + 1;
				data_length   = ( nibble_length + 1 ) / 2;
				edit_applied  = true;
			}
		}
		else if ( insert_mode )
		{
			// Structural edits move whole bytes: a high-nibble
			// keystroke inserts a fresh byte carrying the digit in its
			// high half; the following low-nibble keystroke completes
			// the byte in place.

			if ( cursor_nibble == 0 )
			{
				edit_applied = InsertByte ( cursor_offset, ( BYTE ) ( digit_value << 4 ) );
			}
			else
			{
				SetNibble ( nibble_position, ( BYTE ) digit_value );

				edit_applied = true;
			}
		}
		else
		{
			SetNibble ( nibble_position, ( BYTE ) digit_value );

			edit_applied = true;
		}

		// Advance one nibble only when the write landed, staying inside
		// the byte-rounded extent (which includes the active append
		// slot, in both modes) - or paging forward when the advance
		// crosses the window's end in chunked mode.

		if ( edit_applied )
		{
			if ( nibble_position < GetCursorLimit ()*2 + 1 )
			{
				SetCursorNibblePosition ( nibble_position + 1 );
			}
			else if ( window_tail > 0 )
			{
				FirePageRequest ( window_base + data_length, 0 );
			}
		}

		selection_active = false;

		FinishKeyAction ();

		return true;
	}

	// Not a hex-editor key.

	return false;
}

//----------------------------------------------------------------------------
// Function: HexView::HandleCharacterKey
//
// Description:
//
//   The character editor: a normal text-editor cursor over the byte
//   buffer. Any printable character is written at the cursor - insert
//   mode pushes the data one byte down-file first, and an empty buffer
//   takes its first byte in either mode - updating the hex column live
//   and advancing one byte. Delete removes the byte under
//   the cursor (or zeroes an active selection). Backspace steps back one
//   byte: in insert mode it deletes that byte, dragging all down-file
//   data with it (the file shrinks); in overwrite mode it zeroes it in
//   place (the file size is unchanged). Backspace on an active selection
//   removes the block.
//
// Arguments:
//
//   - key_event : The key event to handle.
//
// Returns:
//
//   - true if the key was consumed, false to bubble it.
//
//----------------------------------------------------------------------------

bool HexView::HandleCharacterKey ( KEY_EVENT &key_event )
{
	// Local variables.

	unsigned long block_low;
	unsigned long block_count;

	// Delete: zero an active selection, or remove the byte under the cursor.

	if ( key_event.scan_code == KEY_DELETE )
	{
		if ( GetSelectionBlock ( &block_low, &block_count ) )
		{
			DeleteSelection ( true );
		}
		else if ( data_length > 0 )
		{
			selection_active = false;

			DeleteByte ( cursor_offset );
		}

		FinishKeyAction ();

		return true;
	}

	// Backspace: remove an active selection, or step back one byte and delete it
	// (insert mode) / zero it in place (overwrite mode).

	if ( key_event.scan_code == KEY_BACKSPACE )
	{
		if ( GetSelectionBlock ( &block_low, &block_count ) )
		{
			DeleteSelection ( false );
		}
		else if ( cursor_offset > 0 )
		{
			selection_active = false;

			cursor_offset--;

			// Insert mode drags all down-file data left with the delete;
			// overwrite mode zeroes the byte in place.

			if ( insert_mode )
			{
				DeleteByte ( cursor_offset );
			}
			else
			{
				data [ cursor_offset ] = 0;
				modified               = true;
			}
		}

		FinishKeyAction ();

		return true;
	}

	if ( key_event.ascii >= 32 )
	{
		// Insert mode, or the cursor on the append slot at the file's end
		// (an empty buffer, or one byte past the last byte in either mode),
		// grows the file by inserting/appending the byte; overwrite mode
		// within the data replaces in place. The cursor advances only when
		// the write landed.

		if ( insert_mode || ( cursor_offset >= data_length ) )
		{
			if ( InsertByte ( cursor_offset, ( BYTE ) key_event.ascii ) )
			{
				if ( cursor_offset < GetCursorLimit () ) cursor_offset++;
			}
		}
		else
		{
			data [ cursor_offset ] = ( BYTE ) key_event.ascii;
			modified               = true;

			if      ( cursor_offset < GetCursorLimit () ) cursor_offset++;
			else if ( window_tail > 0 )                   FirePageRequest ( window_base + data_length, 0 );
		}

		selection_active = false;

		FinishKeyAction ();

		return true;
	}

	// Not a character-editor key.

	return false;
}

//----------------------------------------------------------------------------
