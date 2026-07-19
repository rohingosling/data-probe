//****************************************************************************
// Program: MicroText Test Driver
// Version: 1.0
// Date:    1992-07-13
// Author:  Rohin Gosling
//
// Description:
//
//   Interactive test driver for the MicroText library. Runs nine stages,
//   each advanced with a key press:
//
//     1. Core pipeline  - Far-heap TEXTBUFFER, REP STOSW clear, and a
//                         single REP MOVSW flip to text video memory.
//     2. Colors         - MakeAttribute over all 16 foreground and all 16
//                         background colors (bright backgrounds must not
//                         blink).
//     3. Plotting       - PutCharacter / PutText with every transparency
//                         combination, plus left-, right-, and off-screen
//                         clipping.
//     4. Box art        - PutHorizontalAsciiLine / PutVerticalAsciiLine /
//                         PutAsciiRectangle: single and double thickness,
//                         crossing auto-unions, end caps into single and
//                         double frames, and a filled rectangle.
//     5. Copy           - CopyTextBuffer full-frame buffer-to-buffer copy,
//                         presented from the copy.
//     6. Block / shadow - GetTextBlock snapshot, PutShadow darkening over
//                         color bands, PutTextBlock restore with an
//                         automated byte-for-byte check.
//     7. Cursor         - ShowCursor / HideCursor / SetCursorPosition
//                         moving the hardware cursor between markers.
//     8. Text modes     - SetTextMode / GetTextMode across 43-, 50-, and
//                         25-row modes, plus a screen-clipping check with
//                         an oversized buffer.
//     9. Keyboard       - KeyPressed polling counter and blocking ReadKey
//                         echo as (scan << 8) | ascii. Esc exits.
//
//   The banner/label helpers write cells directly into TEXTBUFFER grids,
//   so the test also verifies the documented cell layout: character in
//   the low byte, attribute in the high byte, row stride width * 2.
//
//   Build with build.bat in this directory (results in BUILD.LOG), then
//   run TEST.EXE inside DOSBox.
//
//****************************************************************************

#include <stdio.h>
#include <stdlib.h>

#include "mtext.h"

//----------------------------------------------------------------------------
// Constants
//----------------------------------------------------------------------------

#define SCREEN_COLUMN_COUNT   80      // Physical text screen width in columns.

#define KEY_SCAN_ESC          0x01    // Esc scan code.

#define FILL_LIGHT_SHADE      0xB0    // Light-shade block character.
#define FILL_MEDIUM_SHADE     0xB1    // Medium-shade block character.
#define FILL_SOLID_BLOCK      0xDB    // Solid block character.

#define ECHO_LIST_ROW         9       // First row of the key-echo list.
#define ECHO_LIST_ROW_COUNT   12      // Rows in the key-echo list.

//----------------------------------------------------------------------------
// Function: FillTestCells
//
// Description:
//
//   Writes a run of identical cells directly into a buffer's cell grid:
//   count cells from (x, y), each holding the given character and
//   attribute. Clips to the buffer's right edge and ignores out-of-range
//   rows.
//
// Arguments:
//
//   - text_buffer : The buffer to write into.
//   - x           : Column of the first cell.
//   - y           : Row to write on.
//   - count       : Number of cells to write.
//   - character   : Character code to store in each cell.
//   - attribute   : Attribute byte to store in each cell.
//
// Returns:
//
//   - None
//
//----------------------------------------------------------------------------

static void FillTestCells ( TEXTBUFFER *text_buffer, int x, int y, int count, char character, BYTE attribute )
{
	BYTE far *cell;
	int       i;

	if ( text_buffer->cells == NULL )                return;
	if ( ( y < 0 ) || ( y >= text_buffer->height ) ) return;
	if ( x < 0 )                                     return;

	cell = text_buffer->cells + ( ( unsigned ) y * text_buffer->width + x )*2;

	for ( i = 0; i < count; i++ )
	{
		if ( x + i >= text_buffer->width ) break;

		cell [ 0 ] = ( BYTE ) character;
		cell [ 1 ] = attribute;
		cell      += 2;
	}
}

//----------------------------------------------------------------------------
// Function: PutTestText
//
// Description:
//
//   Writes a NUL-terminated string directly into a buffer's cell grid,
//   one cell per character starting at (x, y), all with the same
//   attribute. Clips to the buffer's right edge and ignores out-of-range
//   rows.
//
// Arguments:
//
//   - text_buffer : The buffer to write into.
//   - x           : Column of the first character.
//   - y           : Row to write on.
//   - text        : The string to write.
//   - attribute   : Attribute byte for every character cell.
//
// Returns:
//
//   - None
//
//----------------------------------------------------------------------------

static void PutTestText ( TEXTBUFFER *text_buffer, int x, int y, const char *text, BYTE attribute )
{
	BYTE far *cell;
	int       i;

	if ( text_buffer->cells == NULL )                return;
	if ( ( y < 0 ) || ( y >= text_buffer->height ) ) return;
	if ( x < 0 )                                     return;

	cell = text_buffer->cells + ( ( unsigned ) y * text_buffer->width + x )*2;

	for ( i = 0; text [ i ] != '\0'; i++ )
	{
		if ( x + i >= text_buffer->width ) break;

		cell [ 0 ] = ( BYTE ) text [ i ];
		cell [ 1 ] = attribute;
		cell      += 2;
	}
}

//----------------------------------------------------------------------------
// Function: PutTestBanner
//
// Description:
//
//   Fills a full row with spaces in the given attribute, then writes the
//   text over it starting at column 2 - a one-row banner or note line.
//
// Arguments:
//
//   - text_buffer : The buffer to write into.
//   - y           : Row to write on.
//   - text        : The banner text.
//   - attribute   : Attribute byte for the whole row.
//
// Returns:
//
//   - None
//
//----------------------------------------------------------------------------

static void PutTestBanner ( TEXTBUFFER *text_buffer, int y, const char *text, BYTE attribute )
{
	FillTestCells ( text_buffer, 0, y, text_buffer->width, ' ', attribute );
	PutTestText   ( text_buffer, 2, y, text, attribute );
}

//----------------------------------------------------------------------------
// Function: CreateTestBuffer
//
// Description:
//
//   Creates a text buffer via CreateTextBuffer with the test driver's
//   default colors. On allocation failure, restores 25-row text mode,
//   reports the failure on the console, and exits the program.
//
// Arguments:
//
//   - width  : Buffer width in columns.
//   - height : Buffer height in rows.
//
// Returns:
//
//   - The created TEXTBUFFER.
//
//----------------------------------------------------------------------------

static TEXTBUFFER CreateTestBuffer ( int width, int height )
{
	TEXTBUFFER text_buffer;

	text_buffer = CreateTextBuffer ( width, height, COLOR_LIGHT_GRAY, COLOR_BLACK );

	if ( text_buffer.cells == NULL )
	{
		SetTextMode ( TEXT_MODE_25_ROWS );
		printf ( "Out of far memory: CreateTextBuffer ( %d, %d ) failed.\n", width, height );
		exit ( 1 );
	}

	return text_buffer;
}

//----------------------------------------------------------------------------
// Function: RunPipelineStage
//
// Description:
//
//   Stage 1: proves the core pipeline end-to-end. The frame buffer is
//   cleared with a REP STOSW fill (light-shade blocks, blue on black),
//   labeled, and presented with a single REP MOVSW flip.
//
// Arguments:
//
//   - frame : The 80 x 25 frame buffer.
//
// Returns:
//
//   - None
//
//----------------------------------------------------------------------------

static void RunPipelineStage ( TEXTBUFFER *frame )
{
	ClearTextBuffer ( frame, ( char ) FILL_LIGHT_SHADE, COLOR_BLUE, COLOR_BLACK );

	PutTestBanner ( frame, 1, " MicroText Test - Stage 1 of 9 - Core Pipeline ",                        MakeAttribute ( COLOR_WHITE,      COLOR_BLUE  ) );
	PutTestBanner ( frame, 3, " This frame is an 80 x 25 far-heap TEXTBUFFER, cleared with a single ",  MakeAttribute ( COLOR_LIGHT_GRAY, COLOR_BLACK ) );
	PutTestBanner ( frame, 4, " REP STOSW fill and presented with a single REP MOVSW flip to 0xB800. ", MakeAttribute ( COLOR_LIGHT_GRAY, COLOR_BLACK ) );
	PutTestBanner ( frame, 6, " Press any key for the next stage ... ",                                 MakeAttribute ( COLOR_YELLOW,     COLOR_BLACK ) );

	FlipScreenBuffer ( frame );
	ReadKey ();
}

//----------------------------------------------------------------------------
// Function: RunColorStage
//
// Description:
//
//   Stage 2: renders one row per color - a background swatch (spaces with
//   background = color), a foreground swatch (solid blocks with
//   foreground = color), and a label - exercising MakeAttribute over all
//   16 colors in both nibbles. With blinking disabled by SetTextMode, the
//   bright-background rows (8-15) must show steady.
//
// Arguments:
//
//   - frame : The 80 x 25 frame buffer.
//
// Returns:
//
//   - None
//
//----------------------------------------------------------------------------

static void RunColorStage ( TEXTBUFFER *frame )
{
	static const char *color_names [ 16 ] =
	{
		"Black",      "Blue",          "Green",       "Cyan",
		"Red",        "Magenta",       "Brown",       "Light Gray",
		"Dark Gray",  "Light Blue",    "Light Green", "Light Cyan",
		"Light Red",  "Light Magenta", "Yellow",      "White"
	};

	char label [ 40 ];
	int  color;
	int  row;

	ClearTextBuffer ( frame, ' ', COLOR_LIGHT_GRAY, COLOR_BLACK );

	PutTestBanner ( frame,  1, " MicroText Test - Stage 2 of 9 - MakeAttribute and the 16 Colors ", MakeAttribute ( COLOR_WHITE, COLOR_BLUE ) );
	PutTestText   ( frame,  6, 3, "Background",                                                     MakeAttribute ( COLOR_LIGHT_GRAY, COLOR_BLACK ) );
	PutTestText   ( frame, 24, 3, "Foreground",                                                     MakeAttribute ( COLOR_LIGHT_GRAY, COLOR_BLACK ) );

	for ( color = 0; color < 16; color++ )
	{
		row = 4 + color;

		FillTestCells ( frame,  6, row, 14, ' ',                        MakeAttribute ( COLOR_WHITE, ( BYTE ) color ) );
		FillTestCells ( frame, 24, row, 14, ( char ) FILL_SOLID_BLOCK,  MakeAttribute ( ( BYTE ) color, COLOR_BLACK ) );

		sprintf     ( label, "%2d  %s", color, color_names [ color ] );
		PutTestText ( frame, 42, row, label, MakeAttribute ( COLOR_LIGHT_GRAY, COLOR_BLACK ) );
	}

	PutTestBanner ( frame, 21, " Rows 8-15: the bright backgrounds must show steady (no blinking). ", MakeAttribute ( COLOR_LIGHT_GRAY, COLOR_BLACK ) );
	PutTestBanner ( frame, 23, " Press any key for the next stage ... ",                              MakeAttribute ( COLOR_YELLOW,     COLOR_BLACK ) );

	FlipScreenBuffer ( frame );
	ReadKey ();
}

//----------------------------------------------------------------------------
// Function: RunPlottingStage
//
// Description:
//
//   Stage 3: PutCharacter and PutText. The frame is filled with a cyan-
//   on-blue shade so the buffer's defaults (light gray on black, set at
//   creation) are visibly distinct from the passed colors. Each row
//   exercises one transparency combination - the transparency rows pass
//   yellow on red, which must be replaced by the defaults - and the last
//   rows exercise left-, right-, and off-screen clipping.
//
// Arguments:
//
//   - frame : The 80 x 25 frame buffer.
//
// Returns:
//
//   - None
//
//----------------------------------------------------------------------------

static void RunPlottingStage ( TEXTBUFFER *frame )
{
	int i;

	ClearTextBuffer ( frame, ( char ) FILL_MEDIUM_SHADE, COLOR_CYAN, COLOR_BLUE );

	PutTestBanner ( frame, 1, " MicroText Test - Stage 3 of 9 - PutCharacter / PutText ", MakeAttribute ( COLOR_WHITE, COLOR_BLUE ) );

	// One row per transparency combination. The buffer defaults are
	// light gray on black; the passed colors are yellow on red where a
	// transparency flag must override them.

	PutText ( frame, 4,  4, "Opaque: WHITE on RED (both colors as passed)",                COLOR_WHITE,  COLOR_RED, 0, 0 );
	PutText ( frame, 4,  6, "Foreground transparency: default LIGHT GRAY on RED",          COLOR_YELLOW, COLOR_RED, 1, 0 );
	PutText ( frame, 4,  8, "Background transparency: WHITE on default BLACK",             COLOR_WHITE,  COLOR_RED, 0, 1 );
	PutText ( frame, 4, 10, "Both transparent: default LIGHT GRAY on default BLACK",       COLOR_YELLOW, COLOR_RED, 1, 1 );

	// PutCharacter row: ten single-cell plots.

	for ( i = 0; i < 10; i++ )
	{
		PutCharacter ( frame, 4 + i, 12, ( char ) ( 'A' + i ), COLOR_LIGHT_GREEN, COLOR_BLACK, 0, 0 );
	}

	PutText ( frame, 16, 12, "<- PutCharacter, one call per cell", COLOR_LIGHT_GREEN, COLOR_BLACK, 0, 0 );

	// Clipping: a string running off the right edge, a string starting
	// left of column 0, and fully out-of-range calls (all no-ops).

	PutText ( frame, 60, 14, "Right-clipped at column 79: 0123456789", COLOR_WHITE, COLOR_MAGENTA, 0, 0 );
	PutText ( frame, -4, 16, "XXXX<- the four X's are left-clipped",   COLOR_WHITE, COLOR_MAGENTA, 0, 0 );

	PutText      ( frame, 0, 999, "This row is out of range",  COLOR_WHITE, COLOR_RED, 0, 0 );
	PutCharacter ( frame, 999, 0, 'X',                         COLOR_WHITE, COLOR_RED, 0, 0 );

	PutTestBanner ( frame, 21, " The transparency rows pass yellow-on-red; the defaults must show instead. ", MakeAttribute ( COLOR_LIGHT_GRAY, COLOR_BLACK ) );
	PutTestBanner ( frame, 23, " Press any key for the next stage ... ",                                      MakeAttribute ( COLOR_YELLOW,     COLOR_BLACK ) );

	FlipScreenBuffer ( frame );
	ReadKey ();
}

//----------------------------------------------------------------------------
// Function: RunBoxArtStage
//
// Description:
//
//   Stage 4: box-art primitives. Draws a single-thickness frame and a
//   double-thickness frame, each crossed by capped separators (the caps
//   must union into the frame edges, growing single or mixed tees), a
//   filled rectangle overlapping the double frame (rectangles never
//   union: the box sits cleanly on top, overwriting the frame beneath
//   it), and four labeled free crossings covering every single/double
//   thickness combination.
//
// Arguments:
//
//   - frame : The 80 x 25 frame buffer.
//
// Returns:
//
//   - None
//
//----------------------------------------------------------------------------

static void RunBoxArtStage ( TEXTBUFFER *frame )
{
	ClearTextBuffer ( frame, ' ', COLOR_LIGHT_GRAY, COLOR_BLACK );

	PutTestBanner ( frame, 1, " MicroText Test - Stage 4 of 9 - Box-Art Lines, Rectangles, Unions ", MakeAttribute ( COLOR_WHITE, COLOR_BLUE ) );

	// Single-thickness frame with capped separators: the horizontal
	// separator caps must become left/right tees on the frame verticals,
	// the vertical separator caps must become down/up tees on the frame
	// horizontals, and the two separators must cross as a single cross.

	PutAsciiRectangle      ( frame,  3,  3, 22, 15, COLOR_LIGHT_GRAY, COLOR_BLACK, 0, 0, '-', '-', 0 );
	PutHorizontalAsciiLine ( frame,  3,  7, 22,     COLOR_LIGHT_GRAY, COLOR_BLACK, 0, 0, '-', 1 );
	PutVerticalAsciiLine   ( frame, 12,  3, 15,     COLOR_LIGHT_GRAY, COLOR_BLACK, 0, 0, '-', 1 );
	PutText                ( frame,  5,  5, "single",                 COLOR_LIGHT_CYAN, COLOR_BLACK, 0, 0 );

	// Double-thickness frame with capped single separators: the caps
	// must union into the double edges as mixed tees.

	PutAsciiRectangle      ( frame, 28,  3, 22, 15, COLOR_LIGHT_GRAY, COLOR_BLACK, 0, 0, '=', '=', 0 );
	PutHorizontalAsciiLine ( frame, 28,  7, 22,     COLOR_LIGHT_GRAY, COLOR_BLACK, 0, 0, '-', 1 );
	PutVerticalAsciiLine   ( frame, 38,  3, 15,     COLOR_LIGHT_GRAY, COLOR_BLACK, 0, 0, '-', 1 );
	PutText                ( frame, 30,  5, "double",                 COLOR_LIGHT_CYAN, COLOR_BLACK, 0, 0 );

	// Filled rectangle overlapping the double frame's right edge:
	// rectangles do not union - the box must sit cleanly ON TOP of the
	// double frame, its border and fill overwriting the covered segment.

	PutAsciiRectangle ( frame, 44, 10, 12, 5, COLOR_WHITE, COLOR_BLUE, 0, 0, '-', '-', 1 );
	PutText           ( frame, 45, 12, "filled",  COLOR_WHITE, COLOR_BLUE, 0, 0 );

	// Free crossings: every single/double thickness combination.

	PutHorizontalAsciiLine ( frame, 58,  4, 9, COLOR_LIGHT_GRAY, COLOR_BLACK, 0, 0, '-', 0 );
	PutVerticalAsciiLine   ( frame, 62,  3, 3, COLOR_LIGHT_GRAY, COLOR_BLACK, 0, 0, '-', 0 );
	PutText                ( frame, 69,  4, "s + s",  COLOR_LIGHT_CYAN, COLOR_BLACK, 0, 0 );

	PutHorizontalAsciiLine ( frame, 58,  8, 9, COLOR_LIGHT_GRAY, COLOR_BLACK, 0, 0, '=', 0 );
	PutVerticalAsciiLine   ( frame, 62,  7, 3, COLOR_LIGHT_GRAY, COLOR_BLACK, 0, 0, '-', 0 );
	PutText                ( frame, 69,  8, "d + s",  COLOR_LIGHT_CYAN, COLOR_BLACK, 0, 0 );

	PutHorizontalAsciiLine ( frame, 58, 12, 9, COLOR_LIGHT_GRAY, COLOR_BLACK, 0, 0, '-', 0 );
	PutVerticalAsciiLine   ( frame, 62, 11, 3, COLOR_LIGHT_GRAY, COLOR_BLACK, 0, 0, '=', 0 );
	PutText                ( frame, 69, 12, "s + d",  COLOR_LIGHT_CYAN, COLOR_BLACK, 0, 0 );

	PutHorizontalAsciiLine ( frame, 58, 16, 9, COLOR_LIGHT_GRAY, COLOR_BLACK, 0, 0, '=', 0 );
	PutVerticalAsciiLine   ( frame, 62, 15, 3, COLOR_LIGHT_GRAY, COLOR_BLACK, 0, 0, '=', 0 );
	PutText                ( frame, 69, 16, "d + d",  COLOR_LIGHT_CYAN, COLOR_BLACK, 0, 0 );

	PutTestBanner ( frame, 19, " Line crossings must auto-union; separator caps must tuck into frames. ",  MakeAttribute ( COLOR_LIGHT_GRAY, COLOR_BLACK ) );
	PutTestBanner ( frame, 20, " The filled box must sit cleanly ON TOP of the double frame (no union). ", MakeAttribute ( COLOR_LIGHT_GRAY, COLOR_BLACK ) );
	PutTestBanner ( frame, 22, " Press any key for the next stage ... ",                                   MakeAttribute ( COLOR_YELLOW,     COLOR_BLACK ) );

	FlipScreenBuffer ( frame );
	ReadKey ();
}

//----------------------------------------------------------------------------
// Function: RunCopyStage
//
// Description:
//
//   Stage 5: copies the stage 4 frame into a second buffer with
//   CopyTextBuffer, retitles the copy, and presents the copy. The screen
//   must show the stage 4 box-art image carried over intact, under the
//   stage 5 title.
//
// Arguments:
//
//   - frame : The 80 x 25 frame buffer, still holding the stage 4 image.
//
// Returns:
//
//   - None
//
//----------------------------------------------------------------------------

static void RunCopyStage ( TEXTBUFFER *frame )
{
	TEXTBUFFER copy_buffer;

	copy_buffer = CreateTestBuffer ( frame->width, frame->height );

	CopyTextBuffer ( &copy_buffer, frame );

	PutTestBanner ( &copy_buffer,  1, " MicroText Test - Stage 5 of 9 - CopyTextBuffer ",                      MakeAttribute ( COLOR_WHITE,      COLOR_GREEN ) );
	PutTestBanner ( &copy_buffer, 19, " This frame is a copy of stage 4, retitled and flipped as the copy. ",  MakeAttribute ( COLOR_LIGHT_GRAY, COLOR_BLACK ) );
	PutTestBanner ( &copy_buffer, 20, " The box art above must match stage 4 exactly. ",                       MakeAttribute ( COLOR_LIGHT_GRAY, COLOR_BLACK ) );
	PutTestBanner ( &copy_buffer, 22, " Press any key for the next stage ... ",                                MakeAttribute ( COLOR_YELLOW,     COLOR_BLACK ) );

	FlipScreenBuffer ( &copy_buffer );
	ReadKey ();

	DestroyTextBuffer ( &copy_buffer );
}

//----------------------------------------------------------------------------
// Function: RunBlockShadowStage
//
// Description:
//
//   Stage 6: block save/restore and shadow. Over a backdrop of 16 color
//   bands, the stage opens two shadowed overlays in turn (the floating-
//   element discipline: GetTextBlock snapshot, PutShadow, body,
//   PutTextBlock restore), verifying each restore byte for byte by
//   re-capturing the region and comparing against the snapshot. The
//   first overlay's shadow strips cross the middle bands; the second
//   overlay sits lower so its bottom shadow strip falls across the white
//   band, which the first shadow does not reach.
//
// Arguments:
//
//   - frame : The 80 x 25 frame buffer.
//
// Returns:
//
//   - None
//
//----------------------------------------------------------------------------

static void RunBlockShadowStage ( TEXTBUFFER *frame )
{
	TEXTBUFFER block_buffer;
	TEXTBUFFER verify_buffer;
	char       message [ 80 ];
	long       mismatch_count;
	unsigned   byte_count;
	unsigned   i;
	int        color;

	// Backdrop: color bands, so the shadow's per-color darkening is
	// directly visible.

	ClearTextBuffer ( frame, ' ', COLOR_LIGHT_GRAY, COLOR_BLACK );

	PutTestBanner ( frame, 1, " MicroText Test - Stage 6 of 9 - GetTextBlock / PutShadow / PutTextBlock ", MakeAttribute ( COLOR_WHITE, COLOR_BLUE ) );

	for ( color = 0; color < 16; color++ )
	{
		FillTestCells ( frame,  4, 4 + color, 30, ' ',                       MakeAttribute ( COLOR_WHITE, ( BYTE ) color ) );
		FillTestCells ( frame, 36, 4 + color, 24, ( char ) FILL_SOLID_BLOCK, MakeAttribute ( ( BYTE ) color, COLOR_BLACK ) );
	}

	PutTestBanner ( frame, 21, " A shadowed overlay is about to cover the bands. ", MakeAttribute ( COLOR_LIGHT_GRAY, COLOR_BLACK ) );
	PutTestBanner ( frame, 23, " Press any key to open the overlay ... ",           MakeAttribute ( COLOR_YELLOW,     COLOR_BLACK ) );

	FlipScreenBuffer ( frame );
	ReadKey ();

	// Floating-element discipline, overlay 1: save the region the
	// overlay and its shadow will cover, cast the shadow, then draw the
	// body over it. The text block is padded by two cells on every side.

	block_buffer = CreateTestBuffer ( 37, 11 );

	GetTextBlock ( &block_buffer, frame, 20, 7, 37, 11 );

	PutShadow         ( frame, 22, 8, 35, 10, 1, 1 );
	PutAsciiRectangle ( frame, 20, 7, 35, 10, COLOR_BLACK, COLOR_LIGHT_GRAY, 0, 0, '-', '-', 1 );
	PutText           ( frame, 23, 10, "Floating overlay",              COLOR_BLACK, COLOR_LIGHT_GRAY, 0, 0 );
	PutText           ( frame, 23, 12, "The shadow strip to the right", COLOR_BLACK, COLOR_LIGHT_GRAY, 0, 0 );
	PutText           ( frame, 23, 13, "and below darkens each band.",  COLOR_BLACK, COLOR_LIGHT_GRAY, 0, 0 );

	PutTestBanner ( frame, 23, " Check the shadow colors, then press any key to close ... ", MakeAttribute ( COLOR_YELLOW, COLOR_BLACK ) );

	FlipScreenBuffer ( frame );
	ReadKey ();

	// Restore the saved block, then verify the restore byte for byte by
	// re-capturing the region and comparing it against the snapshot.

	PutTextBlock ( frame, &block_buffer, 20, 7, 37, 11 );

	verify_buffer = CreateTestBuffer ( 37, 11 );

	GetTextBlock ( &verify_buffer, frame, 20, 7, 37, 11 );

	mismatch_count = 0;
	byte_count     = 37 * 11 * 2;

	for ( i = 0; i < byte_count; i++ )
	{
		if ( block_buffer.cells [ i ] != verify_buffer.cells [ i ] ) mismatch_count++;
	}

	DestroyTextBuffer ( &verify_buffer );
	DestroyTextBuffer ( &block_buffer );

	sprintf       ( message, " Overlay 1 closed - byte-for-byte restore check: %s (%ld mismatching bytes) ",
	                ( mismatch_count == 0 ) ? "OK" : "FAIL", mismatch_count );
	PutTestBanner ( frame, 21, message, MakeAttribute ( ( mismatch_count == 0 ) ? COLOR_LIGHT_GREEN : COLOR_LIGHT_RED, COLOR_BLACK ) );
	PutTestBanner ( frame, 23, " Press any key for the second, lower overlay ... ", MakeAttribute ( COLOR_YELLOW, COLOR_BLACK ) );

	FlipScreenBuffer ( frame );
	ReadKey ();

	// Overlay 2 sits lower, so its bottom shadow strip falls across the
	// white band (the last band), which overlay 1's shadow never reaches.

	block_buffer = CreateTestBuffer ( 36, 11 );

	GetTextBlock ( &block_buffer, frame, 20, 9, 36, 11 );

	PutShadow         ( frame, 22, 10, 34, 10, 1, 1 );
	PutAsciiRectangle ( frame, 20, 9, 34, 10, COLOR_BLACK, COLOR_LIGHT_GRAY, 0, 0, '-', '-', 1 );
	PutText           ( frame, 23, 12, "Floating overlay 2",           COLOR_BLACK, COLOR_LIGHT_GRAY, 0, 0 );
	PutText           ( frame, 23, 14, "The bottom shadow strip now",  COLOR_BLACK, COLOR_LIGHT_GRAY, 0, 0 );
	PutText           ( frame, 23, 15, "falls across the white band.", COLOR_BLACK, COLOR_LIGHT_GRAY, 0, 0 );

	PutTestBanner ( frame, 23, " Check the shadowed white band, then press any key to close ... ", MakeAttribute ( COLOR_YELLOW, COLOR_BLACK ) );

	FlipScreenBuffer ( frame );
	ReadKey ();

	PutTextBlock ( frame, &block_buffer, 20, 9, 36, 11 );

	verify_buffer = CreateTestBuffer ( 36, 11 );

	GetTextBlock ( &verify_buffer, frame, 20, 9, 36, 11 );

	mismatch_count = 0;
	byte_count     = 36 * 11 * 2;

	for ( i = 0; i < byte_count; i++ )
	{
		if ( block_buffer.cells [ i ] != verify_buffer.cells [ i ] ) mismatch_count++;
	}

	sprintf       ( message, " Overlay 2 closed - byte-for-byte restore check: %s (%ld mismatching bytes) ",
	                ( mismatch_count == 0 ) ? "OK" : "FAIL", mismatch_count );
	PutTestBanner ( frame, 21, message, MakeAttribute ( ( mismatch_count == 0 ) ? COLOR_LIGHT_GREEN : COLOR_LIGHT_RED, COLOR_BLACK ) );
	PutTestBanner ( frame, 23, " Press any key for the next stage ... ", MakeAttribute ( COLOR_YELLOW, COLOR_BLACK ) );

	FlipScreenBuffer ( frame );
	ReadKey ();

	DestroyTextBuffer ( &verify_buffer );
	DestroyTextBuffer ( &block_buffer );
}

//----------------------------------------------------------------------------
// Function: RunCursorStage
//
// Description:
//
//   Stage 7: cursor helpers. Steps the blinking hardware cursor through
//   two marked positions with ShowCursor / SetCursorPosition, hides it
//   with HideCursor, and shows it again - one key press per step. Ends
//   with the cursor hidden.
//
// Arguments:
//
//   - frame : The 80 x 25 frame buffer.
//
// Returns:
//
//   - None
//
//----------------------------------------------------------------------------

static void RunCursorStage ( TEXTBUFFER *frame )
{
	ClearTextBuffer ( frame, ' ', COLOR_LIGHT_GRAY, COLOR_BLACK );

	PutTestBanner ( frame, 1, " MicroText Test - Stage 7 of 9 - Cursor Helpers ",                    MakeAttribute ( COLOR_WHITE,      COLOR_BLUE  ) );
	PutTestBanner ( frame, 3, " The blinking hardware cursor must sit between the yellow markers. ", MakeAttribute ( COLOR_LIGHT_GRAY, COLOR_BLACK ) );

	// 1. Cursor visible at (20, 10).

	PutTestBanner ( frame, 5, " 1. Cursor visible at column 20, row 10 - press any key ... ", MakeAttribute ( COLOR_YELLOW, COLOR_BLACK ) );
	PutText       ( frame, 17, 10, "->", COLOR_YELLOW, COLOR_BLACK, 0, 0 );
	PutText       ( frame, 22, 10, "<-", COLOR_YELLOW, COLOR_BLACK, 0, 0 );

	FlipScreenBuffer  ( frame );
	ShowCursor        ();
	SetCursorPosition ( 20, 10 );
	ReadKey           ();

	// 2. Cursor moved to (60, 18).

	PutTestBanner ( frame, 5, " 2. Cursor moved to column 60, row 18 - press any key ... ", MakeAttribute ( COLOR_YELLOW, COLOR_BLACK ) );
	PutText       ( frame, 57, 18, "->", COLOR_YELLOW, COLOR_BLACK, 0, 0 );
	PutText       ( frame, 62, 18, "<-", COLOR_YELLOW, COLOR_BLACK, 0, 0 );

	FlipScreenBuffer  ( frame );
	SetCursorPosition ( 60, 18 );
	ReadKey           ();

	// 3. Cursor hidden.

	PutTestBanner ( frame, 5, " 3. Cursor hidden - nothing may blink - press any key ... ", MakeAttribute ( COLOR_YELLOW, COLOR_BLACK ) );

	FlipScreenBuffer ( frame );
	HideCursor       ();
	ReadKey          ();

	// 4. Cursor shown again.

	PutTestBanner ( frame, 5, " 4. Cursor shown again at column 20, row 10 - press any key ... ", MakeAttribute ( COLOR_YELLOW, COLOR_BLACK ) );

	FlipScreenBuffer  ( frame );
	ShowCursor        ();
	SetCursorPosition ( 20, 10 );
	ReadKey           ();

	HideCursor ();
}

//----------------------------------------------------------------------------
// Function: RunTextModeStage
//
// Description:
//
//   Stage 8: cycles 43-, 50-, and 25-row text modes. For each mode, the
//   requested row count is compared against GetTextMode, and a banner on
//   the last row proves the full height is addressable. Ends with a
//   clipping check: an 80 x 50 buffer flipped while in 25-row mode must
//   show only its top 25 rows.
//
//   Ends with the adapter back in 25-row mode.
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

static void RunTextModeStage ( void )
{
	static const BYTE mode_sequence [ 3 ] = { TEXT_MODE_43_ROWS, TEXT_MODE_50_ROWS, TEXT_MODE_25_ROWS };
	static const BYTE fill_colors   [ 3 ] = { COLOR_CYAN,        COLOR_GREEN,       COLOR_BLUE        };

	TEXTBUFFER mode_buffer;
	char       message [ 80 ];
	BYTE       requested_rows;
	BYTE       reported_rows;
	int        i;

	for ( i = 0; i < 3; i++ )
	{
		requested_rows = mode_sequence [ i ];

		SetTextMode ( requested_rows );
		HideCursor  ();
		reported_rows = GetTextMode ();

		mode_buffer = CreateTestBuffer ( SCREEN_COLUMN_COUNT, ( int ) reported_rows );

		ClearTextBuffer ( &mode_buffer, ( char ) FILL_LIGHT_SHADE, fill_colors [ i ], COLOR_BLACK );

		sprintf       ( message, " MicroText Test - Stage 8 of 9 - SetTextMode / GetTextMode (%d of 3) ", i + 1 );
		PutTestBanner ( &mode_buffer, 1, message, MakeAttribute ( COLOR_WHITE, COLOR_BLUE ) );

		sprintf       ( message, " Requested %2d rows - GetTextMode () reports %2d rows - %s ",
		                requested_rows, reported_rows, ( requested_rows == reported_rows ) ? "OK" : "MISMATCH" );
		PutTestBanner ( &mode_buffer, 3, message, MakeAttribute ( COLOR_LIGHT_GRAY, COLOR_BLACK ) );

		PutTestBanner ( &mode_buffer, 5, " Press any key ... ", MakeAttribute ( COLOR_YELLOW, COLOR_BLACK ) );

		sprintf       ( message, " Row %2d - the last visible row in %2d-row mode ", reported_rows - 1, reported_rows );
		PutTestBanner ( &mode_buffer, ( int ) reported_rows - 1, message, MakeAttribute ( COLOR_WHITE, COLOR_RED ) );

		FlipScreenBuffer ( &mode_buffer );
		ReadKey ();

		DestroyTextBuffer ( &mode_buffer );
	}

	// Clipping check: with the adapter in 25-row mode, flip a 50-row
	// buffer. FlipScreenBuffer must clip to the active mode's rows, so
	// only the top 25 rows of the buffer may appear.

	mode_buffer = CreateTestBuffer ( SCREEN_COLUMN_COUNT, TEXT_MODE_50_ROWS );

	ClearTextBuffer ( &mode_buffer, ( char ) FILL_MEDIUM_SHADE, COLOR_MAGENTA, COLOR_BLACK );

	PutTestBanner ( &mode_buffer,  1, " Clipping check: an 80 x 50 buffer flipped in 25-row mode ",   MakeAttribute ( COLOR_WHITE,      COLOR_BLUE  ) );
	PutTestBanner ( &mode_buffer,  3, " Only the top 25 rows of this buffer may appear on screen. ",  MakeAttribute ( COLOR_LIGHT_GRAY, COLOR_BLACK ) );
	PutTestBanner ( &mode_buffer,  5, " Press any key ... ",                                          MakeAttribute ( COLOR_YELLOW,     COLOR_BLACK ) );
	PutTestBanner ( &mode_buffer, 24, " Row 24 - the last row that may be visible ",                  MakeAttribute ( COLOR_WHITE,      COLOR_RED   ) );
	PutTestBanner ( &mode_buffer, 30, " Row 30 - MUST NOT BE VISIBLE ",                               MakeAttribute ( COLOR_WHITE,      COLOR_RED   ) );

	FlipScreenBuffer ( &mode_buffer );
	ReadKey ();

	DestroyTextBuffer ( &mode_buffer );
}

//----------------------------------------------------------------------------
// Function: RunKeyboardStage
//
// Description:
//
//   Stage 9: keyboard helpers. While no key is waiting, spins on
//   KeyPressed, incrementing a polling counter and reflipping the frame
//   every iteration - the counter proves the poll is non-blocking, and
//   the continuous full-frame reflip soak-tests the flip for flicker.
//   Each keystroke is then read with ReadKey and echoed into a rolling
//   list as the raw word (scan << 8) | ascii. Esc exits.
//
// Arguments:
//
//   - frame : The 80 x 25 frame buffer.
//
// Returns:
//
//   - None
//
//----------------------------------------------------------------------------

static void RunKeyboardStage ( TEXTBUFFER *frame )
{
	char  message [ 80 ];
	long  poll_count;
	WORD  key;
	BYTE  scan_code;
	BYTE  ascii;
	char  display_character;
	int   line_index;

	ClearTextBuffer ( frame, ' ', COLOR_LIGHT_GRAY, COLOR_BLACK );

	PutTestBanner ( frame, 1, " MicroText Test - Stage 9 of 9 - KeyPressed / ReadKey / KeyDown ",   MakeAttribute ( COLOR_WHITE,      COLOR_BLUE  ) );
	PutTestBanner ( frame, 3, " The polling counter spins while no key is waiting (KeyPressed). ",  MakeAttribute ( COLOR_LIGHT_GRAY, COLOR_BLACK ) );
	PutTestBanner ( frame, 4, " Press keys to echo them; each is read as (scan << 8) | ascii. ",    MakeAttribute ( COLOR_LIGHT_GRAY, COLOR_BLACK ) );
	PutTestBanner ( frame, 5, " Hold Enter/Space/arrows: the KeyDown line tracks the held keys. ",  MakeAttribute ( COLOR_LIGHT_GRAY, COLOR_BLACK ) );
	PutTestBanner ( frame, 6, " Press Esc to exit. ",                                               MakeAttribute ( COLOR_YELLOW,     COLOR_BLACK ) );

	// Hook the chained INT 9 key-state handler for KeyDown.

	InstallKeyboardHandler ();

	poll_count = 0;
	line_index = 0;

	for ( ;; )
	{
		// Poll until a keystroke is waiting, repainting and reflipping
		// the frame on every iteration.

		while ( !KeyPressed () )
		{
			poll_count++;

			sprintf       ( message, " Polling ... KeyPressed () calls with no key waiting: %ld ", poll_count );
			PutTestBanner ( frame, 7, message, MakeAttribute ( COLOR_LIGHT_CYAN, COLOR_BLACK ) );

			sprintf       ( message, " KeyDown: Enter[%c]  Space[%c]  Up[%c]  Down[%c]  Left[%c]  Right[%c] ",
			                KeyDown ( 0x1C ) ? 'X' : ' ', KeyDown ( 0x39 ) ? 'X' : ' ',
			                KeyDown ( 0x48 ) ? 'X' : ' ', KeyDown ( 0x50 ) ? 'X' : ' ',
			                KeyDown ( 0x4B ) ? 'X' : ' ', KeyDown ( 0x4D ) ? 'X' : ' ' );
			PutTestBanner ( frame, 8, message, MakeAttribute ( COLOR_LIGHT_GREEN, COLOR_BLACK ) );

			FlipScreenBuffer ( frame );
		}

		// Read and echo the waiting keystroke.

		key       = ReadKey ();
		scan_code = ( BYTE ) ( key >> 8 );
		ascii     = ( BYTE ) ( key & 0x00FF );

		display_character = ( ( ascii >= 32 ) && ( ascii <= 126 ) ) ? ( char ) ascii : '.';

		sprintf       ( message, " ReadKey () = 0x%04X   scan = 0x%02X   ascii = 0x%02X   '%c' ",
		                key, scan_code, ascii, display_character );
		PutTestBanner ( frame, ECHO_LIST_ROW + line_index, message, MakeAttribute ( COLOR_WHITE, COLOR_BLACK ) );

		// Blank the next slot in the rolling list to mark the write
		// position.

		line_index = ( line_index + 1 ) % ECHO_LIST_ROW_COUNT;
		PutTestBanner ( frame, ECHO_LIST_ROW + line_index, "", MakeAttribute ( COLOR_LIGHT_GRAY, COLOR_BLACK ) );

		FlipScreenBuffer ( frame );

		if ( scan_code == KEY_SCAN_ESC ) break;
	}

	RemoveKeyboardHandler ();
}

//----------------------------------------------------------------------------
// Function: main
//
// Description:
//
//   Test driver entry point: sets 25-row text mode, hides the hardware
//   cursor, creates the frame buffer, runs the nine test stages in order,
//   then releases the buffer and restores 25-row text mode (which shows
//   the cursor again for DOS).
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

int main ( void )
{
	TEXTBUFFER frame;

	SetTextMode ( TEXT_MODE_25_ROWS );
	HideCursor  ();

	frame = CreateTestBuffer ( SCREEN_COLUMN_COUNT, TEXT_MODE_25_ROWS );

	RunPipelineStage    ( &frame );
	RunColorStage       ( &frame );
	RunPlottingStage    ( &frame );
	RunBoxArtStage      ( &frame );
	RunCopyStage        ( &frame );
	RunBlockShadowStage ( &frame );
	RunCursorStage      ( &frame );
	RunTextModeStage    ();
	RunKeyboardStage    ( &frame );

	DestroyTextBuffer ( &frame );

	SetTextMode ( TEXT_MODE_25_ROWS );

	printf ( "MicroText test driver complete.\n" );

	return 0;
}

//----------------------------------------------------------------------------
