//****************************************************************************
// Program: MicroText (Text Mode Library)
// Version: 1.0
// Date:    1992-07-13
// Author:  Rohin Gosling
//
// Description:
//
//   Low-level text-mode screen library for C and C++ programs.
//
//   - Models the 80-column text screen and every off-screen buffer as a 
//     TEXTBUFFER, a rectangular grid of hardware text cells laid out exactly 
//     like video  memory at segment 0xB800.
//
//   - Provides text-mode set/query, double-buffered screen flipping, 
//     character and string plotting with clipping and transparency, box-art 
//     primitives with automatic box-drawing union, block save/restore, 
//     shadow casting, and attribute, cursor, and keyboard helpers.
//
//   - A character cell is a 16-bit word. The low-byte is the character code 
//     and the high-byte is the attribute, where,
//
//       attribute = (background << 4) | foreground. 
//
//   - Fills use the REP STOSW idiom (one word per cell); the screen flip uses 
//     REP MOVSW.
//
//   - Every plotting function clips to the destination buffer's width/height, 
//     so out-of-range coordinates are silently trimmed rather than corrupting 
//     memory. The row stride is width * 2 bytes.
//
//   Compiled with the Borland C++ 3.1 command-line driver, using the large 
//   memory model for far pointer and farmalloc support:
//
//     bcc -c -ml -2 mtext.c
//
//****************************************************************************

#include <alloc.h>
#include <stddef.h>
#include <string.h>
#include <dos.h>

#include "mtext.h"

//----------------------------------------------------------------------------
// File-Scope Constants
//----------------------------------------------------------------------------

#define SCREEN_COLUMN_COUNT  80    // Physical text screen width in columns.

//----------------------------------------------------------------------------
// Mode and Screen Functions
//----------------------------------------------------------------------------

//----------------------------------------------------------------------------
// Function: SetTextMode
//
// Description:
//
//   Sets the video adapter to 80-column text mode (BIOS mode 3) with a
//   configurable number of text rows.
//
//   - 25 rows: Standard VGA text mode with the default 8x16 ROM font.
//              (400 scan lines / 16 pixels per character = 25 rows.)
//
//   - 43 rows: EGA-compatible extended text mode. Selects 350 vertical
//              scan lines via INT 10h AX=1201h BL=30h, then loads the 8x8
//              ROM font via INT 10h AX=1112h.
//              (350 scan lines / 8 pixels per character = 43 rows.)
//
//   - 50 rows: VGA extended text mode. Uses 400 vertical scan lines and
//              loads the 8x8 ROM font via INT 10h AX=1112h.
//              (400 scan lines / 8 pixels per character = 50 rows.)
//
//   Any value other than 43 or 50 defaults to 25 rows.
//
//   The BIOS scan-line selection persists in the BIOS Data Area and is
//   applied on every subsequent mode set, so the scan-line count is
//   programmed explicitly on every call: 350 lines for 43 rows, 400 lines
//   for 25 and 50 rows. Without the explicit 400-line selection, setting
//   50-row mode after 43-row mode would inherit 350 scan lines and yield
//   43 rows again.
//
//   Attribute blinking is disabled (INT 10h AX=1003h BL=00h) after every
//   mode set, so attribute bit 7 selects a bright background color rather
//   than blinking text and all 16 background colors are available.
//
// Arguments:
//
//   - rows : Number of text rows. Use the constants TEXT_MODE_25_ROWS (25),
//            TEXT_MODE_43_ROWS (43), or TEXT_MODE_50_ROWS (50).
//
// Returns:
//
//   - None
//
//----------------------------------------------------------------------------

void SetTextMode ( BYTE rows )
{
	asm {

		// Select the vertical scan-line count for the next mode set.
		// 43-row mode runs on 350 scan lines (EGA-style); 25- and 50-row
		// modes run on the VGA default of 400 scan lines.

		CMP		BYTE PTR [rows], 43
		JNE		SETTEXTMODE_SCANLINES400

		MOV		AX, 0x1201			// AH=12h BL=30h AL=01h: 350 scan lines.
		MOV		BL, 0x30
		INT		0x10
		JMP		SETTEXTMODE_SETMODE

	} SETTEXTMODE_SCANLINES400: asm {

		MOV		AX, 0x1202			// AH=12h BL=30h AL=02h: 400 scan lines.
		MOV		BL, 0x30
		INT		0x10

	} SETTEXTMODE_SETMODE: asm {

		// Set 80-column color text mode (BIOS mode 3).

		MOV		AX, 0x0003
		INT		0x10

		// For 25-row mode the default ROM font stands. For 43 or 50 rows,
		// load the 8x8 ROM font to double the visible row count.

		CMP		BYTE PTR [rows], 43
		JE		SETTEXTMODE_LOADFONT
		CMP		BYTE PTR [rows], 50
		JE		SETTEXTMODE_LOADFONT
		JMP		SETTEXTMODE_SETBLINK

	} SETTEXTMODE_LOADFONT: asm {

		// Load the 8x8 ROM font into the active character generator.
		// This halves the character cell height, raising the visible row
		// count to 43 (at 350 scan lines) or 50 (at 400 scan lines).

		MOV		AX, 0x1112
		MOV		BL, 0x00
		INT		0x10

	} SETTEXTMODE_SETBLINK: asm {

		// Disable attribute blinking so attribute bit 7 selects a bright
		// background color instead of blinking text.

		MOV		AX, 0x1003
		MOV		BL, 0x00
		INT		0x10
	}
}

//----------------------------------------------------------------------------
// Function: GetTextMode
//
// Description:
//
//   Returns the number of text rows for the current video mode.
//
//   Queries the BIOS via INT 10h AH=0Fh to determine the active video
//   mode. If the mode is a standard 80-column text mode (mode 2 or 3),
//   the row count is read from the BIOS Data Area at 0040:0084h, which
//   stores (rows - 1). If the current mode is not a text mode, the
//   function returns 0.
//
// Arguments:
//
//   - None
//
// Returns:
//
//   - The number of text rows (25, 43, or 50) if a text mode is active,
//     or 0 if the current video mode is not a text mode.
//
//----------------------------------------------------------------------------

BYTE GetTextMode ( void )
{
	BYTE text_rows;

	asm {

		// Query the current video mode. INT 10h AH=0Fh returns the
		// active mode number in AL.

		MOV		AH, 0x0F
		INT		0x10

		// Check for 80-column text modes (mode 2 = monochrome,
		// mode 3 = color).

		CMP		AL, 0x03
		JE		GETTEXTMODE_READROWS
		CMP		AL, 0x02
		JE		GETTEXTMODE_READROWS

		// Not a text mode. Return 0.

		XOR		AL, AL
		JMP		GETTEXTMODE_STORE

	} GETTEXTMODE_READROWS: asm {

		// Read the row count from the BIOS Data Area.
		// Address 0040:0084h holds (rows - 1).

		PUSH	ES
		MOV		AX, 0x0040
		MOV		ES, AX
		MOV		AL, ES:[0x0084]
		INC		AL
		POP		ES

	} GETTEXTMODE_STORE: asm {

		MOV		[text_rows], AL
	}

	return text_rows;
}

//----------------------------------------------------------------------------
// Function: FlipScreenBuffer
//
// Description:
//
//   Copies the source buffer to physical text video memory at segment
//   0xB800, clipped to 80 columns by the active mode's row count. This is
//   the frame-presentation step that makes rendering flicker-free: all
//   drawing composits into an off-screen buffer, then one flip presents
//   the finished frame.
//
//   A screen-wide source (width = 80, the designed use) is presented with
//   a single REP MOVSW block transfer. A source of any other width falls
//   back to a row-by-row copy, clipping each row to the screen width.
//
//   If the adapter is not in a text mode, or the source has no cells,
//   the call is a no-op.
//
// Arguments:
//
//   - source : The buffer to present. Normally 80 columns wide by the
//              active mode's row count.
//
// Returns:
//
//   - None
//
//----------------------------------------------------------------------------

void FlipScreenBuffer ( TEXTBUFFER *source )
{
	BYTE far *source_cells;
	int       screen_rows;
	int       copy_width;
	int       copy_height;
	unsigned  word_count;
	unsigned  source_row_skip;
	unsigned  screen_row_skip;

	// Clip the copy region to the physical screen: 80 columns by the
	// active mode's row count.

	screen_rows = ( int ) GetTextMode ();

	if ( screen_rows == 0 )      return;
	if ( source->cells == NULL ) return;

	copy_width = source->width;
	if ( copy_width > SCREEN_COLUMN_COUNT ) copy_width = SCREEN_COLUMN_COUNT;

	copy_height = source->height;
	if ( copy_height > screen_rows ) copy_height = screen_rows;

	if ( ( copy_width < 1 ) || ( copy_height < 1 ) ) return;

	source_cells = source->cells;

	if ( source->width == SCREEN_COLUMN_COUNT )
	{
		// Screen-wide source: present the frame with a single REP MOVSW.

		word_count = ( unsigned ) ( copy_width*copy_height );

		asm {

			PUSH	DS
			PUSH	ES

			MOV		AX, 0xB800			// Text video memory segment.
			MOV		ES, AX
			XOR		DI, DI
			MOV		CX, [word_count]
			LDS		SI, [source_cells]
			REP		MOVSW

			POP		ES
			POP		DS
		}
	}
	else
	{
		// Source narrower or wider than the screen: copy row by row,
		// clipping each row to the screen width. The skip counts step
		// each pointer past the cells excluded from the copy.

		source_row_skip = ( unsigned ) ( ( source->width - copy_width )*2 );
		screen_row_skip = ( unsigned ) ( ( SCREEN_COLUMN_COUNT - copy_width )*2 );

		asm {

			PUSH	DS
			PUSH	ES

			MOV		AX, 0xB800			// Text video memory segment.
			MOV		ES, AX
			XOR		DI, DI
			MOV		BX, [copy_height]
			LDS		SI, [source_cells]

		} FLIPSCREENBUFFER_ROW: asm {

			MOV		CX, [copy_width]
			REP		MOVSW
			ADD		SI, [source_row_skip]
			ADD		DI, [screen_row_skip]
			DEC		BX
			JNZ		FLIPSCREENBUFFER_ROW

		} asm {

			POP		ES
			POP		DS
		}
	}
}

//----------------------------------------------------------------------------
// Buffer Lifecycle Functions
//----------------------------------------------------------------------------

//----------------------------------------------------------------------------
// Function: CreateTextBuffer
//
// Description:
//
//   Creates an off-screen text buffer: farmalloc-s width * height * 2
//   bytes for the cell grid and initializes the returned struct's
//   dimensions and default foreground/background colors.
//
//   The cell contents are not initialized; clear the buffer with
//   ClearTextBuffer before its first use. The cell grid must fit a single
//   far segment, so width * height * 2 must not exceed 65 535 bytes
//   (an 80 x 50 screen buffer is 8 000 bytes).
//
//   If the allocation fails, the returned buffer has cells = NULL and
//   zero width and height.
//
// Arguments:
//
//   - width      : Buffer width in columns.
//   - height     : Buffer height in rows.
//   - foreground : Default foreground color (used by transparency).
//   - background : Default background color (used by transparency).
//
// Returns:
//
//   - The initialized TEXTBUFFER, by value. cells = NULL on allocation
//     failure.
//
//----------------------------------------------------------------------------

TEXTBUFFER CreateTextBuffer ( int width, int height, BYTE foreground, BYTE background )
{
	TEXTBUFFER text_buffer;

	text_buffer.cells      = ( BYTE far * ) farmalloc ( ( unsigned long ) width * ( unsigned long ) height * 2UL );
	text_buffer.width      = width;
	text_buffer.height     = height;
	text_buffer.foreground = foreground;
	text_buffer.background = background;

	if ( text_buffer.cells == NULL )
	{
		text_buffer.width  = 0;
		text_buffer.height = 0;
	}

	return text_buffer;
}

//----------------------------------------------------------------------------
// Function: DestroyTextBuffer
//
// Description:
//
//   Releases a buffer created with CreateTextBuffer: farfree-s the cell
//   grid and resets the struct to an empty state (cells = NULL, zero
//   width and height). Safe to call on a buffer that is already empty.
//
// Arguments:
//
//   - text_buffer : The buffer to destroy.
//
// Returns:
//
//   - None
//
//----------------------------------------------------------------------------

void DestroyTextBuffer ( TEXTBUFFER *text_buffer )
{
	if ( text_buffer->cells != NULL )
	{
		farfree ( ( void far * ) text_buffer->cells );
	}

	text_buffer->cells  = NULL;
	text_buffer->width  = 0;
	text_buffer->height = 0;
}

//----------------------------------------------------------------------------
// Function: ClearTextBuffer
//
// Description:
//
//   Fills every cell of the buffer with the given character and the
//   attribute (background << 4) | foreground, using a single REP STOSW
//   word fill.
//
// Arguments:
//
//   - text_buffer : The buffer to fill.
//   - character   : The character code to store in every cell.
//   - foreground  : Foreground color of the fill attribute.
//   - background  : Background color of the fill attribute.
//
// Returns:
//
//   - None
//
//----------------------------------------------------------------------------

void ClearTextBuffer ( TEXTBUFFER *text_buffer, char character, BYTE foreground, BYTE background )
{
	BYTE far *destination_cells;
	unsigned  word_count;
	unsigned  fill_word;

	if ( text_buffer->cells == NULL ) return;

	// A cell word is the character in the low byte and the attribute in
	// the high byte, matching the 0xB800 layout.

	destination_cells = text_buffer->cells;
	word_count        = ( unsigned ) ( text_buffer->width * text_buffer->height );
	fill_word         = ( unsigned ) ( ( ( unsigned ) MakeAttribute ( foreground, background ) << 8 ) | ( BYTE ) character );

	asm {

		PUSH	ES

		MOV		AX, [fill_word]
		MOV		CX, [word_count]
		LES		DI, [destination_cells]
		REP		STOSW

		POP		ES
	}
}

//----------------------------------------------------------------------------
// Function: CopyTextBuffer
//
// Description:
//
//   Copies the full source cell grid into the destination with a single
//   REP MOVSW block transfer. The destination must be at least the source
//   size; buffers of identical dimensions are the designed use (the copy
//   is linear, so differing widths would shear the rows).
//
// Arguments:
//
//   - destination_text_buffer : The buffer to copy into.
//   - source_text_buffer      : The buffer to copy from.
//
// Returns:
//
//   - None
//
//----------------------------------------------------------------------------

void CopyTextBuffer ( TEXTBUFFER *destination_text_buffer, TEXTBUFFER *source_text_buffer )
{
	BYTE far *source_cells;
	BYTE far *destination_cells;
	unsigned  word_count;

	if ( source_text_buffer->cells == NULL )      return;
	if ( destination_text_buffer->cells == NULL ) return;

	source_cells      = source_text_buffer->cells;
	destination_cells = destination_text_buffer->cells;
	word_count        = ( unsigned ) ( source_text_buffer->width * source_text_buffer->height );

	asm {

		PUSH	DS
		PUSH	ES

		MOV		CX, [word_count]
		LES		DI, [destination_cells]
		LDS		SI, [source_cells]
		REP		MOVSW

		POP		ES
		POP		DS
	}
}

//----------------------------------------------------------------------------
// File-Scope Box-Art Tables and Helpers
//----------------------------------------------------------------------------

// A box-drawing glyph is modeled as a set of arms (strokes running from the
// cell center to each edge) plus a stroke weight per axis. The code page 437
// box-art set carries every arm combination of two or more arms, in all four
// single/double axis-weight combinations - 40 glyphs in total - so any union
// of two box glyphs lands back on a real glyph.

#define BOX_ARM_UP           0x01
#define BOX_ARM_DOWN         0x02
#define BOX_ARM_LEFT         0x04
#define BOX_ARM_RIGHT        0x08

#define BOX_WEIGHT_NONE      0
#define BOX_WEIGHT_SINGLE    1
#define BOX_WEIGHT_DOUBLE    2

typedef struct
{
	BYTE character;      // Code page 437 box-drawing character code.
	BYTE arms;           // BOX_ARM_* presence bits.
	BYTE h_weight;       // Horizontal stroke weight (BOX_WEIGHT_*).
	BYTE v_weight;       // Vertical stroke weight (BOX_WEIGHT_*).
} BOX_GLYPH;

static const BOX_GLYPH box_glyph_table [] =
{
	// Lines.

	{ 0xC4, BOX_ARM_LEFT | BOX_ARM_RIGHT,                             BOX_WEIGHT_SINGLE, BOX_WEIGHT_NONE   },
	{ 0xCD, BOX_ARM_LEFT | BOX_ARM_RIGHT,                             BOX_WEIGHT_DOUBLE, BOX_WEIGHT_NONE   },
	{ 0xB3, BOX_ARM_UP   | BOX_ARM_DOWN,                              BOX_WEIGHT_NONE,   BOX_WEIGHT_SINGLE },
	{ 0xBA, BOX_ARM_UP   | BOX_ARM_DOWN,                              BOX_WEIGHT_NONE,   BOX_WEIGHT_DOUBLE },

	// Corners: down-right, down-left, up-right, up-left.

	{ 0xDA, BOX_ARM_DOWN | BOX_ARM_RIGHT,                             BOX_WEIGHT_SINGLE, BOX_WEIGHT_SINGLE },
	{ 0xD5, BOX_ARM_DOWN | BOX_ARM_RIGHT,                             BOX_WEIGHT_DOUBLE, BOX_WEIGHT_SINGLE },
	{ 0xD6, BOX_ARM_DOWN | BOX_ARM_RIGHT,                             BOX_WEIGHT_SINGLE, BOX_WEIGHT_DOUBLE },
	{ 0xC9, BOX_ARM_DOWN | BOX_ARM_RIGHT,                             BOX_WEIGHT_DOUBLE, BOX_WEIGHT_DOUBLE },
	{ 0xBF, BOX_ARM_DOWN | BOX_ARM_LEFT,                              BOX_WEIGHT_SINGLE, BOX_WEIGHT_SINGLE },
	{ 0xB8, BOX_ARM_DOWN | BOX_ARM_LEFT,                              BOX_WEIGHT_DOUBLE, BOX_WEIGHT_SINGLE },
	{ 0xB7, BOX_ARM_DOWN | BOX_ARM_LEFT,                              BOX_WEIGHT_SINGLE, BOX_WEIGHT_DOUBLE },
	{ 0xBB, BOX_ARM_DOWN | BOX_ARM_LEFT,                              BOX_WEIGHT_DOUBLE, BOX_WEIGHT_DOUBLE },
	{ 0xC0, BOX_ARM_UP   | BOX_ARM_RIGHT,                             BOX_WEIGHT_SINGLE, BOX_WEIGHT_SINGLE },
	{ 0xD4, BOX_ARM_UP   | BOX_ARM_RIGHT,                             BOX_WEIGHT_DOUBLE, BOX_WEIGHT_SINGLE },
	{ 0xD3, BOX_ARM_UP   | BOX_ARM_RIGHT,                             BOX_WEIGHT_SINGLE, BOX_WEIGHT_DOUBLE },
	{ 0xC8, BOX_ARM_UP   | BOX_ARM_RIGHT,                             BOX_WEIGHT_DOUBLE, BOX_WEIGHT_DOUBLE },
	{ 0xD9, BOX_ARM_UP   | BOX_ARM_LEFT,                              BOX_WEIGHT_SINGLE, BOX_WEIGHT_SINGLE },
	{ 0xBE, BOX_ARM_UP   | BOX_ARM_LEFT,                              BOX_WEIGHT_DOUBLE, BOX_WEIGHT_SINGLE },
	{ 0xBD, BOX_ARM_UP   | BOX_ARM_LEFT,                              BOX_WEIGHT_SINGLE, BOX_WEIGHT_DOUBLE },
	{ 0xBC, BOX_ARM_UP   | BOX_ARM_LEFT,                              BOX_WEIGHT_DOUBLE, BOX_WEIGHT_DOUBLE },

	// Tees: right, left, down, up.

	{ 0xC3, BOX_ARM_UP   | BOX_ARM_DOWN | BOX_ARM_RIGHT,              BOX_WEIGHT_SINGLE, BOX_WEIGHT_SINGLE },
	{ 0xC6, BOX_ARM_UP   | BOX_ARM_DOWN | BOX_ARM_RIGHT,              BOX_WEIGHT_DOUBLE, BOX_WEIGHT_SINGLE },
	{ 0xC7, BOX_ARM_UP   | BOX_ARM_DOWN | BOX_ARM_RIGHT,              BOX_WEIGHT_SINGLE, BOX_WEIGHT_DOUBLE },
	{ 0xCC, BOX_ARM_UP   | BOX_ARM_DOWN | BOX_ARM_RIGHT,              BOX_WEIGHT_DOUBLE, BOX_WEIGHT_DOUBLE },
	{ 0xB4, BOX_ARM_UP   | BOX_ARM_DOWN | BOX_ARM_LEFT,               BOX_WEIGHT_SINGLE, BOX_WEIGHT_SINGLE },
	{ 0xB5, BOX_ARM_UP   | BOX_ARM_DOWN | BOX_ARM_LEFT,               BOX_WEIGHT_DOUBLE, BOX_WEIGHT_SINGLE },
	{ 0xB6, BOX_ARM_UP   | BOX_ARM_DOWN | BOX_ARM_LEFT,               BOX_WEIGHT_SINGLE, BOX_WEIGHT_DOUBLE },
	{ 0xB9, BOX_ARM_UP   | BOX_ARM_DOWN | BOX_ARM_LEFT,               BOX_WEIGHT_DOUBLE, BOX_WEIGHT_DOUBLE },
	{ 0xC2, BOX_ARM_DOWN | BOX_ARM_LEFT | BOX_ARM_RIGHT,              BOX_WEIGHT_SINGLE, BOX_WEIGHT_SINGLE },
	{ 0xD1, BOX_ARM_DOWN | BOX_ARM_LEFT | BOX_ARM_RIGHT,              BOX_WEIGHT_DOUBLE, BOX_WEIGHT_SINGLE },
	{ 0xD2, BOX_ARM_DOWN | BOX_ARM_LEFT | BOX_ARM_RIGHT,              BOX_WEIGHT_SINGLE, BOX_WEIGHT_DOUBLE },
	{ 0xCB, BOX_ARM_DOWN | BOX_ARM_LEFT | BOX_ARM_RIGHT,              BOX_WEIGHT_DOUBLE, BOX_WEIGHT_DOUBLE },
	{ 0xC1, BOX_ARM_UP   | BOX_ARM_LEFT | BOX_ARM_RIGHT,              BOX_WEIGHT_SINGLE, BOX_WEIGHT_SINGLE },
	{ 0xCF, BOX_ARM_UP   | BOX_ARM_LEFT | BOX_ARM_RIGHT,              BOX_WEIGHT_DOUBLE, BOX_WEIGHT_SINGLE },
	{ 0xD0, BOX_ARM_UP   | BOX_ARM_LEFT | BOX_ARM_RIGHT,              BOX_WEIGHT_SINGLE, BOX_WEIGHT_DOUBLE },
	{ 0xCA, BOX_ARM_UP   | BOX_ARM_LEFT | BOX_ARM_RIGHT,              BOX_WEIGHT_DOUBLE, BOX_WEIGHT_DOUBLE },

	// Crosses.

	{ 0xC5, BOX_ARM_UP | BOX_ARM_DOWN | BOX_ARM_LEFT | BOX_ARM_RIGHT, BOX_WEIGHT_SINGLE, BOX_WEIGHT_SINGLE },
	{ 0xD8, BOX_ARM_UP | BOX_ARM_DOWN | BOX_ARM_LEFT | BOX_ARM_RIGHT, BOX_WEIGHT_DOUBLE, BOX_WEIGHT_SINGLE },
	{ 0xD7, BOX_ARM_UP | BOX_ARM_DOWN | BOX_ARM_LEFT | BOX_ARM_RIGHT, BOX_WEIGHT_SINGLE, BOX_WEIGHT_DOUBLE },
	{ 0xCE, BOX_ARM_UP | BOX_ARM_DOWN | BOX_ARM_LEFT | BOX_ARM_RIGHT, BOX_WEIGHT_DOUBLE, BOX_WEIGHT_DOUBLE }
};

#define BOX_GLYPH_COUNT  ( sizeof ( box_glyph_table ) / sizeof ( box_glyph_table [ 0 ] ) )

//----------------------------------------------------------------------------
// Function: FindBoxGlyphByCharacter
//
// Description:
//
//   Looks up a character code in the box-glyph table.
//
// Arguments:
//
//   - character : The character code to look up.
//
// Returns:
//
//   - A pointer to the glyph's table entry, or NULL if the character is
//     not a box-drawing glyph.
//
//----------------------------------------------------------------------------

static const BOX_GLYPH *FindBoxGlyphByCharacter ( BYTE character )
{
	int i;

	for ( i = 0; i < BOX_GLYPH_COUNT; i++ )
	{
		if ( box_glyph_table [ i ].character == character ) return &box_glyph_table [ i ];
	}

	return NULL;
}

//----------------------------------------------------------------------------
// Function: FindBoxCharacter
//
// Description:
//
//   Finds the character code of the box glyph with the given arm set and
//   axis weights. The weights must be normalized: BOX_WEIGHT_NONE on an
//   axis with no arms, single or double on an axis with arms.
//
// Arguments:
//
//   - arms     : BOX_ARM_* presence bits.
//   - h_weight : Normalized horizontal stroke weight.
//   - v_weight : Normalized vertical stroke weight.
//
// Returns:
//
//   - The matching character code, or 0 if no glyph matches.
//
//----------------------------------------------------------------------------

static BYTE FindBoxCharacter ( BYTE arms, BYTE h_weight, BYTE v_weight )
{
	int i;

	for ( i = 0; i < BOX_GLYPH_COUNT; i++ )
	{
		if ( ( box_glyph_table [ i ].arms == arms ) &&
		     ( box_glyph_table [ i ].h_weight == h_weight ) &&
		     ( box_glyph_table [ i ].v_weight == v_weight ) )
		{
			return box_glyph_table [ i ].character;
		}
	}

	return 0;
}

//----------------------------------------------------------------------------
// Function: ResolveAttribute
//
// Description:
//
//   Applies the transparency rules to a plotting call's color pair: when
//   a transparency flag is set, the buffer's own default color replaces
//   the passed color. Returns the packed attribute byte.
//
// Arguments:
//
//   - text_buffer                      : The destination buffer (supplies the defaults).
//   - foreground                       : The foreground color passed in the call.
//   - background                       : The background color passed in the call.
//   - foreground_transparency_enabled  : Nonzero to substitute the buffer's default foreground.
//   - background_transparency_enabled  : Nonzero to substitute the buffer's default background.
//
// Returns:
//
//   - The resolved attribute byte.
//
//----------------------------------------------------------------------------

static BYTE ResolveAttribute
(
	TEXTBUFFER *text_buffer,
	BYTE        foreground,
	BYTE        background,
	BYTE        foreground_transparency_enabled,
	BYTE        background_transparency_enabled
)
{
	if ( foreground_transparency_enabled ) foreground = text_buffer->foreground;
	if ( background_transparency_enabled ) background = text_buffer->background;

	return MakeAttribute ( foreground, background );
}

//----------------------------------------------------------------------------
// Function: PlotBoxCell
//
// Description:
//
//   Writes one box-art cell. With union_enabled set, the new glyph merges
//   with any box glyph already in the cell: the arm sets are unioned and
//   each axis takes the heavier stroke weight, so crossing and touching
//   lines join instead of overwriting ('-' + '|' yields '+', a single
//   line meeting a double frame grows the matching mixed tee, and so on).
//   With union_enabled clear, the new glyph simply overwrites the cell
//   (the rectangle behavior: a panel border covers whatever is beneath
//   it). A non-box character in the cell is always overwritten. Clips to
//   the buffer.
//
// Arguments:
//
//   - text_buffer   : The buffer to plot into.
//   - x             : Cell column.
//   - y             : Cell row.
//   - arms          : BOX_ARM_* presence bits of the new glyph.
//   - h_weight      : Horizontal stroke weight of the new glyph.
//   - v_weight      : Vertical stroke weight of the new glyph.
//   - union_enabled : Nonzero to merge with an existing box glyph.
//   - attribute     : Attribute byte to store with the glyph.
//
// Returns:
//
//   - None
//
//----------------------------------------------------------------------------

static void PlotBoxCell
(
	TEXTBUFFER *text_buffer,
	int         x,
	int         y,
	BYTE        arms,
	BYTE        h_weight,
	BYTE        v_weight,
	BYTE        union_enabled,
	BYTE        attribute
)
{
	BYTE far        *cell;
	const BOX_GLYPH *existing_glyph;
	BYTE             character;

	if ( ( x < 0 ) || ( x >= text_buffer->width ) )  return;
	if ( ( y < 0 ) || ( y >= text_buffer->height ) ) return;

	cell = text_buffer->cells + ( ( unsigned ) y * text_buffer->width + x )*2;

	// Union with an existing box glyph: merge the arm sets and take the
	// heavier stroke weight on each axis.

	if ( union_enabled )
	{
		existing_glyph = FindBoxGlyphByCharacter ( cell [ 0 ] );

		if ( existing_glyph != NULL )
		{
			arms |= existing_glyph->arms;

			if ( existing_glyph->h_weight > h_weight ) h_weight = existing_glyph->h_weight;
			if ( existing_glyph->v_weight > v_weight ) v_weight = existing_glyph->v_weight;
		}
	}

	// Normalize the axis weights: no weight on an armless axis, and never
	// less than single on an axis that carries arms.

	if ( ( arms & ( BOX_ARM_LEFT | BOX_ARM_RIGHT ) ) == 0 ) h_weight = BOX_WEIGHT_NONE;
	else if ( h_weight == BOX_WEIGHT_NONE )                 h_weight = BOX_WEIGHT_SINGLE;

	if ( ( arms & ( BOX_ARM_UP | BOX_ARM_DOWN ) ) == 0 )    v_weight = BOX_WEIGHT_NONE;
	else if ( v_weight == BOX_WEIGHT_NONE )                 v_weight = BOX_WEIGHT_SINGLE;

	character = FindBoxCharacter ( arms, h_weight, v_weight );

	if ( character == 0 ) return;

	cell [ 0 ] = character;
	cell [ 1 ] = attribute;
}

//----------------------------------------------------------------------------
// Cell and String Plotting
//----------------------------------------------------------------------------

//----------------------------------------------------------------------------
// Function: PutCharacter
//
// Description:
//
//   Writes a single cell at (x, y): the character byte plus the resolved
//   attribute, clipped to the buffer bounds (a fully out-of-range
//   coordinate is a no-op). If a transparency flag is set, the buffer's
//   default color replaces the corresponding passed color.
//
// Arguments:
//
//   - text_buffer                      : The buffer to plot into.
//   - x                                : Cell column.
//   - y                                : Cell row.
//   - character                        : The character code to write.
//   - foreground                       : Foreground color.
//   - background                       : Background color.
//   - foreground_transparency_enabled  : Nonzero to use the buffer's default foreground.
//   - background_transparency_enabled  : Nonzero to use the buffer's default background.
//
// Returns:
//
//   - None
//
//----------------------------------------------------------------------------

void PutCharacter
(
	TEXTBUFFER *text_buffer,
	int         x,
	int         y,
	char        character,
	BYTE        foreground,
	BYTE        background,
	BYTE        foreground_transparency_enabled,
	BYTE        background_transparency_enabled
)
{
	BYTE far *cell;

	if ( text_buffer->cells == NULL )                return;
	if ( ( x < 0 ) || ( x >= text_buffer->width ) )  return;
	if ( ( y < 0 ) || ( y >= text_buffer->height ) ) return;

	cell = text_buffer->cells + ( ( unsigned ) y * text_buffer->width + x )*2;

	cell [ 0 ] = ( BYTE ) character;
	cell [ 1 ] = ResolveAttribute ( text_buffer, foreground, background,
	                                foreground_transparency_enabled, background_transparency_enabled );
}

//----------------------------------------------------------------------------
// Function: PutText
//
// Description:
//
//   Writes a NUL-terminated string starting at (x, y), advancing one
//   column per character and clipping each cell to the buffer edges
//   (characters left of column 0 or past the right edge are trimmed; an
//   out-of-range row is a no-op). If a transparency flag is set, the
//   buffer's default color replaces the corresponding passed color.
//
// Arguments:
//
//   - text_buffer                      : The buffer to plot into.
//   - x                                : Column of the first character.
//   - y                                : Row to write on.
//   - s                                : The string to write.
//   - foreground                       : Foreground color.
//   - background                       : Background color.
//   - foreground_transparency_enabled  : Nonzero to use the buffer's default foreground.
//   - background_transparency_enabled  : Nonzero to use the buffer's default background.
//
// Returns:
//
//   - None
//
//----------------------------------------------------------------------------

void PutText
(
	TEXTBUFFER *text_buffer,
	int         x,
	int         y,
	const char *s,
	BYTE        foreground,
	BYTE        background,
	BYTE        foreground_transparency_enabled,
	BYTE        background_transparency_enabled
)
{
	BYTE far *cell;
	BYTE      attribute;
	int       i;

	if ( text_buffer->cells == NULL )                return;
	if ( s == NULL )                                 return;
	if ( ( y < 0 ) || ( y >= text_buffer->height ) ) return;

	attribute = ResolveAttribute ( text_buffer, foreground, background,
	                               foreground_transparency_enabled, background_transparency_enabled );

	for ( i = 0; s [ i ] != '\0'; i++ )
	{
		if ( x + i < 0 )                   continue;
		if ( x + i >= text_buffer->width ) break;

		cell = text_buffer->cells + ( ( unsigned ) y * text_buffer->width + ( x + i ) )*2;

		cell [ 0 ] = ( BYTE ) s [ i ];
		cell [ 1 ] = attribute;
	}
}

//----------------------------------------------------------------------------
// Box-Art Primitives
//----------------------------------------------------------------------------

//----------------------------------------------------------------------------
// Function: PutHorizontalAsciiLine
//
// Description:
//
//   Draws a run of length horizontal box-drawing characters from (x, y)
//   in the chosen line thickness ('-' single or '=' double), clipped to
//   the buffer and applying the transparency rules. Where a glyph lands
//   on an existing box-drawing glyph, the two are merged per the
//   box-union rules rather than overwritten.
//
//   With end_caps_enabled set (and length of at least 2), the two end
//   cells become right/left tee caps with single vertical arms, so a
//   separator tucks cleanly into a surrounding frame: the caps union with
//   the frame's verticals, growing the matching single or mixed tee.
//
// Arguments:
//
//   - text_buffer                      : The buffer to plot into.
//   - x                                : Column of the first cell.
//   - y                                : Row to draw on.
//   - length                           : Number of cells to draw.
//   - foreground                       : Foreground color.
//   - background                       : Background color.
//   - foreground_transparency_enabled  : Nonzero to use the buffer's default foreground.
//   - background_transparency_enabled  : Nonzero to use the buffer's default background.
//   - line_thickness                   : '-' for single box lines, '=' for double.
//   - end_caps_enabled                 : Nonzero to draw tee end caps.
//
// Returns:
//
//   - None
//
//----------------------------------------------------------------------------

void PutHorizontalAsciiLine
(
	TEXTBUFFER *text_buffer,
	int         x,
	int         y,
	int         length,
	BYTE        foreground,
	BYTE        background,
	BYTE        foreground_transparency_enabled,
	BYTE        background_transparency_enabled,
	char        line_thickness,
	BYTE        end_caps_enabled
)
{
	BYTE attribute;
	BYTE line_weight;
	BYTE arms;
	BYTE v_weight;
	int  i;

	if ( text_buffer->cells == NULL ) return;
	if ( length < 1 )                 return;

	attribute   = ResolveAttribute ( text_buffer, foreground, background,
	                                 foreground_transparency_enabled, background_transparency_enabled );
	line_weight = ( line_thickness == '=' ) ? BOX_WEIGHT_DOUBLE : BOX_WEIGHT_SINGLE;

	for ( i = 0; i < length; i++ )
	{
		arms     = BOX_ARM_LEFT | BOX_ARM_RIGHT;
		v_weight = BOX_WEIGHT_NONE;

		if ( end_caps_enabled && ( length >= 2 ) )
		{
			if ( i == 0 )          { arms = BOX_ARM_UP | BOX_ARM_DOWN | BOX_ARM_RIGHT; v_weight = BOX_WEIGHT_SINGLE; }
			if ( i == length - 1 ) { arms = BOX_ARM_UP | BOX_ARM_DOWN | BOX_ARM_LEFT;  v_weight = BOX_WEIGHT_SINGLE; }
		}

		PlotBoxCell ( text_buffer, x + i, y, arms, line_weight, v_weight, 1, attribute );
	}
}

//----------------------------------------------------------------------------
// Function: PutVerticalAsciiLine
//
// Description:
//
//   Draws a run of length vertical box-drawing characters from (x, y) in
//   the chosen line thickness ('-' single or '=' double), clipped to the
//   buffer and applying the transparency rules. Where a glyph lands on an
//   existing box-drawing glyph, the two are merged per the box-union
//   rules rather than overwritten.
//
//   With end_caps_enabled set (and length of at least 2), the two end
//   cells become down/up tee caps with single horizontal arms, so a
//   separator tucks cleanly into a surrounding frame: the caps union with
//   the frame's horizontals, growing the matching single or mixed tee.
//
// Arguments:
//
//   - text_buffer                      : The buffer to plot into.
//   - x                                : Column to draw on.
//   - y                                : Row of the first cell.
//   - length                           : Number of cells to draw.
//   - foreground                       : Foreground color.
//   - background                       : Background color.
//   - foreground_transparency_enabled  : Nonzero to use the buffer's default foreground.
//   - background_transparency_enabled  : Nonzero to use the buffer's default background.
//   - line_thickness                   : '-' for single box lines, '=' for double.
//   - end_caps_enabled                 : Nonzero to draw tee end caps.
//
// Returns:
//
//   - None
//
//----------------------------------------------------------------------------

void PutVerticalAsciiLine
(
	TEXTBUFFER *text_buffer,
	int         x,
	int         y,
	int         length,
	BYTE        foreground,
	BYTE        background,
	BYTE        foreground_transparency_enabled,
	BYTE        background_transparency_enabled,
	char        line_thickness,
	BYTE        end_caps_enabled
)
{
	BYTE attribute;
	BYTE line_weight;
	BYTE arms;
	BYTE h_weight;
	int  i;

	if ( text_buffer->cells == NULL ) return;
	if ( length < 1 )                 return;

	attribute   = ResolveAttribute ( text_buffer, foreground, background,
	                                 foreground_transparency_enabled, background_transparency_enabled );
	line_weight = ( line_thickness == '=' ) ? BOX_WEIGHT_DOUBLE : BOX_WEIGHT_SINGLE;

	for ( i = 0; i < length; i++ )
	{
		arms     = BOX_ARM_UP | BOX_ARM_DOWN;
		h_weight = BOX_WEIGHT_NONE;

		if ( end_caps_enabled && ( length >= 2 ) )
		{
			if ( i == 0 )          { arms = BOX_ARM_LEFT | BOX_ARM_RIGHT | BOX_ARM_DOWN; h_weight = BOX_WEIGHT_SINGLE; }
			if ( i == length - 1 ) { arms = BOX_ARM_LEFT | BOX_ARM_RIGHT | BOX_ARM_UP;   h_weight = BOX_WEIGHT_SINGLE; }
		}

		PlotBoxCell ( text_buffer, x, y + i, arms, h_weight, line_weight, 1, attribute );
	}
}

//----------------------------------------------------------------------------
// Function: PutAsciiRectangle
//
// Description:
//
//   Draws a rectangle of width x height box-drawing characters with
//   independent horizontal and vertical line thicknesses, clipped to the
//   buffer and applying the transparency rules. Unlike the line
//   primitives, the rectangle does NOT union with existing box-drawing
//   glyphs - its border overwrites whatever lies beneath it, so a panel
//   or menu covers the content under it cleanly. With filled nonzero,
//   the interior is cleared to spaces in the resolved attribute.
//
//   A rectangle one cell wide or one cell tall degenerates to the
//   corresponding line (without end caps).
//
// Arguments:
//
//   - text_buffer                      : The buffer to plot into.
//   - x                                : Column of the top-left corner.
//   - y                                : Row of the top-left corner.
//   - width                            : Rectangle width in cells.
//   - height                           : Rectangle height in cells.
//   - foreground                       : Foreground color.
//   - background                       : Background color.
//   - foreground_transparency_enabled  : Nonzero to use the buffer's default foreground.
//   - background_transparency_enabled  : Nonzero to use the buffer's default background.
//   - horizontal_line_thickness        : '-' or '=' for the top and bottom edges.
//   - vertical_line_thickness          : '-' or '=' for the left and right edges.
//   - filled                           : Nonzero to clear the interior.
//
// Returns:
//
//   - None
//
//----------------------------------------------------------------------------

void PutAsciiRectangle
(
	TEXTBUFFER *text_buffer,
	int         x,
	int         y,
	int         width,
	int         height,
	BYTE        foreground,
	BYTE        background,
	BYTE        foreground_transparency_enabled,
	BYTE        background_transparency_enabled,
	char        horizontal_line_thickness,
	char        vertical_line_thickness,
	BYTE        filled
)
{
	BYTE far *cell;
	BYTE      attribute;
	BYTE      h_weight;
	BYTE      v_weight;
	int       i;
	int       j;

	if ( text_buffer->cells == NULL )       return;
	if ( ( width < 1 ) || ( height < 1 ) )  return;

	// Degenerate rectangles collapse to lines.

	if ( width == 1 )
	{
		PutVerticalAsciiLine ( text_buffer, x, y, height, foreground, background, foreground_transparency_enabled, background_transparency_enabled, vertical_line_thickness, 0 );
		return;
	}

	if ( height == 1 )
	{
		PutHorizontalAsciiLine ( text_buffer, x, y, width, foreground, background, foreground_transparency_enabled, background_transparency_enabled, horizontal_line_thickness, 0 );
		return;
	}

	attribute = ResolveAttribute ( text_buffer, foreground, background, foreground_transparency_enabled, background_transparency_enabled );
	h_weight  = ( horizontal_line_thickness == '=' ) ? BOX_WEIGHT_DOUBLE : BOX_WEIGHT_SINGLE;
	v_weight  = ( vertical_line_thickness   == '=' ) ? BOX_WEIGHT_DOUBLE : BOX_WEIGHT_SINGLE;

	// Corners and edges overwrite whatever lies beneath them (a panel
	// border covers the content under it - no union with the underlying
	// glyphs).

	PlotBoxCell ( text_buffer, x,             y,              BOX_ARM_DOWN | BOX_ARM_RIGHT, h_weight, v_weight, 0, attribute );
	PlotBoxCell ( text_buffer, x + width - 1, y,              BOX_ARM_DOWN | BOX_ARM_LEFT,  h_weight, v_weight, 0, attribute );
	PlotBoxCell ( text_buffer, x,             y + height - 1, BOX_ARM_UP   | BOX_ARM_RIGHT, h_weight, v_weight, 0, attribute );
	PlotBoxCell ( text_buffer, x + width - 1, y + height - 1, BOX_ARM_UP   | BOX_ARM_LEFT,  h_weight, v_weight, 0, attribute );

	for ( i = 1; i < width - 1; i++ )
	{
		PlotBoxCell ( text_buffer, x + i, y,              BOX_ARM_LEFT | BOX_ARM_RIGHT, h_weight, BOX_WEIGHT_NONE, 0, attribute );
		PlotBoxCell ( text_buffer, x + i, y + height - 1, BOX_ARM_LEFT | BOX_ARM_RIGHT, h_weight, BOX_WEIGHT_NONE, 0, attribute );
	}

	for ( j = 1; j < height - 1; j++ )
	{
		PlotBoxCell ( text_buffer, x,             y + j, BOX_ARM_UP | BOX_ARM_DOWN, BOX_WEIGHT_NONE, v_weight, 0, attribute );
		PlotBoxCell ( text_buffer, x + width - 1, y + j, BOX_ARM_UP | BOX_ARM_DOWN, BOX_WEIGHT_NONE, v_weight, 0, attribute );
	}

	// Interior fill.

	if ( filled )
	{
		for ( j = 1; j < height - 1; j++ )
		{
			if ( ( y + j < 0 ) || ( y + j >= text_buffer->height ) ) continue;

			for ( i = 1; i < width - 1; i++ )
			{
				if ( x + i < 0 )                   continue;
				if ( x + i >= text_buffer->width ) break;

				cell = text_buffer->cells + ( ( unsigned ) ( y + j ) * text_buffer->width + ( x + i ) )*2;

				cell [ 0 ] = ' ';
				cell [ 1 ] = attribute;
			}
		}
	}
}

//----------------------------------------------------------------------------
// Block Save-Restore and Shadow
//----------------------------------------------------------------------------

// Shadow darkening table, indexed by color: white steps down to light
// gray, light gray to dark gray, dark gray to black, every light color to
// its dark counterpart, and every dark color (and black) to black.

static const BYTE shadow_color_table [ 16 ] =
{
	COLOR_BLACK,        // Black         -> black
	COLOR_BLACK,        // Blue          -> black
	COLOR_BLACK,        // Green         -> black
	COLOR_BLACK,        // Cyan          -> black
	COLOR_BLACK,        // Red           -> black
	COLOR_BLACK,        // Magenta       -> black
	COLOR_BLACK,        // Brown         -> black
	COLOR_DARK_GRAY,    // Light gray    -> dark gray
	COLOR_BLACK,        // Dark gray     -> black
	COLOR_BLUE,         // Light blue    -> blue
	COLOR_GREEN,        // Light green   -> green
	COLOR_CYAN,         // Light cyan    -> cyan
	COLOR_RED,          // Light red     -> red
	COLOR_MAGENTA,      // Light magenta -> magenta
	COLOR_BROWN,        // Yellow        -> brown
	COLOR_LIGHT_GRAY    // White         -> light gray
};

//----------------------------------------------------------------------------
// Function: CopyCellBlock
//
// Description:
//
//   Copies a width x height rectangle of cells from one buffer to
//   another, row by row, clipping the rectangle to both buffers. The
//   shared engine behind GetTextBlock and PutTextBlock.
//
// Arguments:
//
//   - destination_text_buffer : The buffer to copy into.
//   - destination_x           : Destination column of the rectangle.
//   - destination_y           : Destination row of the rectangle.
//   - source_text_buffer      : The buffer to copy from.
//   - source_x                : Source column of the rectangle.
//   - source_y                : Source row of the rectangle.
//   - width                   : Rectangle width in cells.
//   - height                  : Rectangle height in cells.
//
// Returns:
//
//   - None
//
//----------------------------------------------------------------------------

static void CopyCellBlock
(
	TEXTBUFFER *destination_text_buffer,
	int         destination_x,
	int         destination_y,
	TEXTBUFFER *source_text_buffer,
	int         source_x,
	int         source_y,
	int         width,
	int         height
)
{
	BYTE far *destination_row;
	BYTE far *source_row;
	int       j;

	if ( destination_text_buffer->cells == NULL ) return;
	if ( source_text_buffer->cells == NULL )      return;

	// Clip the rectangle's left and top edges against both buffers.

	if ( source_x < 0 )      { width  += source_x;      destination_x -= source_x;      source_x      = 0; }
	if ( source_y < 0 )      { height += source_y;      destination_y -= source_y;      source_y      = 0; }
	if ( destination_x < 0 ) { width  += destination_x; source_x      -= destination_x; destination_x = 0; }
	if ( destination_y < 0 ) { height += destination_y; source_y      -= destination_y; destination_y = 0; }

	// Clip the rectangle's right and bottom edges against both buffers.

	if ( source_x + width > source_text_buffer->width )             width  = source_text_buffer->width - source_x;
	if ( destination_x + width > destination_text_buffer->width )   width  = destination_text_buffer->width - destination_x;
	if ( source_y + height > source_text_buffer->height )           height = source_text_buffer->height - source_y;
	if ( destination_y + height > destination_text_buffer->height ) height = destination_text_buffer->height - destination_y;

	if ( ( width < 1 ) || ( height < 1 ) ) return;

	for ( j = 0; j < height; j++ )
	{
		source_row      = source_text_buffer->cells      + ( ( unsigned ) ( source_y + j ) * source_text_buffer->width + source_x )*2;
		destination_row = destination_text_buffer->cells + ( ( unsigned ) ( destination_y + j ) * destination_text_buffer->width + destination_x )*2;

		memcpy ( destination_row, source_row, ( unsigned ) width*2 );
	}
}

//----------------------------------------------------------------------------
// Function: GetTextBlock
//
// Description:
//
//   Captures a width x height region of the source buffer starting at
//   (x, y) into the destination buffer, from the destination's top-left
//   corner - a snapshot of the region a floating element is about to
//   cover, taken so PutTextBlock can restore it on close. Clips to both
//   buffers.
//
// Arguments:
//
//   - destination_text_buffer : The buffer receiving the snapshot.
//   - source_text_buffer      : The buffer to capture from.
//   - x                       : Source column of the region.
//   - y                       : Source row of the region.
//   - width                   : Region width in cells.
//   - height                  : Region height in cells.
//
// Returns:
//
//   - None
//
//----------------------------------------------------------------------------

void GetTextBlock
(
	TEXTBUFFER *destination_text_buffer,
	TEXTBUFFER *source_text_buffer,
	int         x,
	int         y,
	int         width,
	int         height
)
{
	CopyCellBlock ( destination_text_buffer, 0, 0, source_text_buffer, x, y, width, height );
}

//----------------------------------------------------------------------------
// Function: PutTextBlock
//
// Description:
//
//   Writes a previously captured block (from the source buffer's top-left
//   corner) back into the destination buffer at (x, y), restoring what a
//   floating element covered. Clips to both buffers.
//
// Arguments:
//
//   - destination_text_buffer : The buffer to restore into.
//   - source_text_buffer      : The buffer holding the captured block.
//   - x                       : Destination column of the region.
//   - y                       : Destination row of the region.
//   - width                   : Region width in cells.
//   - height                  : Region height in cells.
//
// Returns:
//
//   - None
//
//----------------------------------------------------------------------------

void PutTextBlock
(
	TEXTBUFFER *destination_text_buffer,
	TEXTBUFFER *source_text_buffer,
	int         x,
	int         y,
	int         width,
	int         height
)
{
	CopyCellBlock ( destination_text_buffer, x, y, source_text_buffer, 0, 0, width, height );
}

//----------------------------------------------------------------------------
// Function: PutShadow
//
// Description:
//
//   Darkens the attributes of every cell in a width x height region per
//   the shadow rules: white -> light gray, light gray -> dark gray, dark
//   gray -> black, any light color -> its dark counterpart, and any dark
//   color -> black. The characters are left untouched.
//
//   foreground_shadow_enabled darkens the foreground nibble and
//   background_shadow_enabled darkens the background nibble, so a drop
//   shadow can dim only the background while text stays legible. Clips
//   to the buffer.
//
// Arguments:
//
//   - text_buffer                : The buffer to darken.
//   - x                          : Column of the region.
//   - y                          : Row of the region.
//   - width                      : Region width in cells.
//   - height                     : Region height in cells.
//   - foreground_shadow_enabled  : Nonzero to darken the foreground nibble.
//   - background_shadow_enabled  : Nonzero to darken the background nibble.
//
// Returns:
//
//   - None
//
//----------------------------------------------------------------------------

void PutShadow
(
	TEXTBUFFER *text_buffer,
	int         x,
	int         y,
	int         width,
	int         height,
	BYTE        foreground_shadow_enabled,
	BYTE        background_shadow_enabled
)
{
	BYTE far *cell;
	BYTE      foreground;
	BYTE      background;
	int       i;
	int       j;

	if ( text_buffer->cells == NULL ) return;

	// Clip the region to the buffer.

	if ( x < 0 ) { width  += x; x = 0; }
	if ( y < 0 ) { height += y; y = 0; }

	if ( x + width > text_buffer->width )   width  = text_buffer->width - x;
	if ( y + height > text_buffer->height ) height = text_buffer->height - y;

	if ( ( width < 1 ) || ( height < 1 ) ) return;

	for ( j = 0; j < height; j++ )
	{
		cell = text_buffer->cells + ( ( unsigned ) ( y + j ) * text_buffer->width + x )*2;

		for ( i = 0; i < width; i++ )
		{
			foreground = ( BYTE ) ( cell [ 1 ] & 0x0F );
			background = ( BYTE ) ( ( cell [ 1 ] >> 4 ) & 0x0F );

			if ( foreground_shadow_enabled ) foreground = shadow_color_table [ foreground ];
			if ( background_shadow_enabled ) background = shadow_color_table [ background ];

			cell [ 1 ]  = MakeAttribute ( foreground, background );
			cell       += 2;
		}
	}
}

//----------------------------------------------------------------------------
// Attribute, Cursor, and Keyboard Helpers
//----------------------------------------------------------------------------

//----------------------------------------------------------------------------
// Function: MakeAttribute
//
// Description:
//
//   Packs a foreground/background color pair into the single hardware
//   attribute byte (background << 4) | foreground.
//
// Arguments:
//
//   - foreground : Foreground color (0-15).
//   - background : Background color (0-15).
//
// Returns:
//
//   - The packed attribute byte.
//
//----------------------------------------------------------------------------

BYTE MakeAttribute ( BYTE foreground, BYTE background )
{
	return ( BYTE ) ( ( background << 4 ) | foreground );
}

//----------------------------------------------------------------------------
// Function: ShowCursor
//
// Description:
//
//   Turns the hardware text cursor on via INT 10h AH=01h, restoring the
//   standard underline cursor shape (scan lines 6-7 of the character
//   cell).
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

void ShowCursor ( void )
{
	asm {

		MOV		AH, 0x01
		MOV		CX, 0x0607			// Underline cursor: start line 6, end line 7.
		INT		0x10
	}
}

//----------------------------------------------------------------------------
// Function: HideCursor
//
// Description:
//
//   Turns the hardware text cursor off via INT 10h AH=01h, setting bit 5
//   of the cursor start line (CH = 0x20), which disables the cursor.
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

void HideCursor ( void )
{
	asm {

		MOV		AH, 0x01
		MOV		CX, 0x2000			// Bit 5 of CH set: cursor disabled.
		INT		0x10
	}
}

//----------------------------------------------------------------------------
// Function: SetCursorPosition
//
// Description:
//
//   Moves the hardware text cursor to (x, y) on display page 0 via
//   INT 10h AH=02h.
//
// Arguments:
//
//   - x : Cursor column.
//   - y : Cursor row.
//
// Returns:
//
//   - None
//
//----------------------------------------------------------------------------

void SetCursorPosition ( int x, int y )
{
	asm {

		MOV		AH, 0x02
		MOV		BH, 0x00			// Display page 0.
		MOV		DL, BYTE PTR [x]	// Column.
		MOV		DH, BYTE PTR [y]	// Row.
		INT		0x10
	}
}

//----------------------------------------------------------------------------
// Function: KeyPressed
//
// Description:
//
//   Polls the BIOS keyboard buffer via INT 16h AH=01h, without removing
//   any keystroke from the buffer. Non-blocking.
//
// Arguments:
//
//   - None
//
// Returns:
//
//   - Nonzero if a keystroke is waiting in the keyboard buffer, 0 if not.
//
//----------------------------------------------------------------------------

int KeyPressed ( void )
{
	int key_waiting;

	asm {

		// INT 16h AH=01h returns ZF=1 if the keyboard buffer is empty,
		// ZF=0 if a keystroke is waiting. MOV does not affect the flags,
		// so AX may be preloaded before the branch.

		MOV		AH, 0x01
		INT		0x16

		MOV		AX, 0x0000
		JZ		KEYPRESSED_STORE
		MOV		AX, 0x0001

	} KEYPRESSED_STORE: asm {

		MOV		[key_waiting], AX
	}

	return key_waiting;
}

//----------------------------------------------------------------------------
// Function: ReadKey
//
// Description:
//
//   Reads one keystroke from the BIOS keyboard buffer via INT 16h AH=00h,
//   blocking until a key is available.
//
// Arguments:
//
//   - None
//
// Returns:
//
//   - The raw 16-bit BIOS key word (scan_code << 8) | ascii.
//
//----------------------------------------------------------------------------

WORD ReadKey ( void )
{
	WORD key_code;

	asm {

		MOV		AH, 0x00
		INT		0x16
		MOV		[key_code], AX
	}

	return key_code;
}

//----------------------------------------------------------------------------
// Function: PeekKey
//
// Description:
//
//   Returns the keystroke waiting at the head of the BIOS keyboard buffer
//   without removing it, via INT 16h AH=01h. Non-blocking.
//
// Arguments:
//
//   - None
//
// Returns:
//
//   - The waiting key word (scan_code << 8) | ascii, or 0 if the buffer
//     is empty.
//
//----------------------------------------------------------------------------

WORD PeekKey ( void )
{
	WORD key_code;

	asm {

		// INT 16h AH=01h returns ZF=0 with the waiting key in AX, or
		// ZF=1 (buffer empty), in which case AX is forced to 0.

		MOV		AH, 0x01
		INT		0x16
		JNZ		PEEKKEY_STORE
		XOR		AX, AX

	} PEEKKEY_STORE: asm {

		MOV		[key_code], AX
	}

	return key_code;
}

//----------------------------------------------------------------------------
// Function: GetShiftState
//
// Description:
//
//   Returns the BIOS shift-flag byte via INT 16h AH=02h, reporting which
//   modifier keys are held right now. Test the result against the
//   SHIFT_STATE_* masks (shift / ctrl / alt). Non-blocking; independent
//   of the keyboard buffer.
//
// Arguments:
//
//   - None
//
// Returns:
//
//   - The shift-flag byte (SHIFT_STATE_* masks) in the low byte; the
//     high byte is 0.
//
//----------------------------------------------------------------------------

WORD GetShiftState ( void )
{
	WORD shift_state;

	asm {

		MOV		AH, 0x02
		INT		0x16
		XOR		AH, AH
		MOV		[shift_state], AX
	}

	return shift_state;
}

//----------------------------------------------------------------------------
// Key-State Tracking (chained INT 9 handler)
//----------------------------------------------------------------------------

// The handler reads each raw scan code from keyboard port 0x60 and records
// it in the key-state table (bit 7 of the scan code distinguishes break
// from make), then chains to the previous INT 9 handler so the BIOS
// keyboard buffer, typematic repeat, and Ctrl-Alt-Del keep working.

static void interrupt ( *old_keyboard_handler ) ( ) = NULL;

static volatile BYTE key_state_table [ 128 ];

static int keyboard_handler_installed = 0;

static void interrupt NewKeyboardHandler ( )
{
	BYTE scan_code;

	scan_code = ( BYTE ) inportb ( 0x60 );

	if ( scan_code & 0x80 ) key_state_table [ scan_code & 0x7F ] = 0;
	else                    key_state_table [ scan_code & 0x7F ] = 1;

	_chain_intr ( old_keyboard_handler );
}

//----------------------------------------------------------------------------
// Function: InstallKeyboardHandler
//
// Description:
//
//   Hooks INT 9 with the key-state tracking handler (chaining to the
//   previous handler), clearing the key-state table first. Enables
//   KeyDown. Idempotent: a second install is ignored until the handler
//   is removed.
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

void InstallKeyboardHandler ( void )
{
	int i;

	if ( keyboard_handler_installed ) return;

	for ( i = 0; i < 128; i++ ) key_state_table [ i ] = 0;

	old_keyboard_handler = getvect ( 0x09 );

	setvect ( 0x09, NewKeyboardHandler );

	keyboard_handler_installed = 1;
}

//----------------------------------------------------------------------------
// Function: RemoveKeyboardHandler
//
// Description:
//
//   Restores the INT 9 handler that was active before
//   InstallKeyboardHandler. Must be called before the program exits.
//   Idempotent: a remove without an install is ignored.
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

void RemoveKeyboardHandler ( void )
{
	if ( !keyboard_handler_installed ) return;

	setvect ( 0x09, old_keyboard_handler );

	keyboard_handler_installed = 0;
}

//----------------------------------------------------------------------------
// Function: KeyDown
//
// Description:
//
//   Reports whether a key is physically held down right now, from the
//   key-state table maintained by the chained INT 9 handler. Returns 0
//   for every key while the handler is not installed.
//
// Arguments:
//
//   - scan_code : The key's scan code (bit 7 ignored).
//
// Returns:
//
//   - Nonzero while the key is held down, 0 otherwise.
//
//----------------------------------------------------------------------------

int KeyDown ( BYTE scan_code )
{
	return ( int ) key_state_table [ scan_code & 0x7F ];
}

//----------------------------------------------------------------------------
