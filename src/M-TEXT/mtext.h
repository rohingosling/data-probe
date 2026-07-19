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

#ifndef _MICRO_TEXT
#define _MICRO_TEXT

//----------------------------------------------------------------------------
// Types
//----------------------------------------------------------------------------

typedef unsigned char BYTE;
typedef unsigned      WORD;

// TEXTBUFFER - a rectangular grid of hardware text cells.
//
// - The physical screen is addressed as a TEXTBUFFER whose cells point at
//   segment 0xB800 offset 0, with width = 80 and height = 25, 43, or 50.
//
// - Off-screen buffers are farmalloc-ed by CreateTextBuffer.
//
// - The foreground and background members hold the buffer's default
//   colors. They are substituted for a passed color whenever the
//   corresponding transparency flag is set in a plotting call.

typedef struct
{
	BYTE far *cells;         // width*height*2 bytes: (char, attribute) pairs, matching 0xB800 layout
	int       width;         // columns
	int       height;        // rows
	BYTE      foreground;    // default foreground (used when foreground transparency is enabled)
	BYTE      background;    // default background (used when background transparency is enabled)
} TEXTBUFFER;

//----------------------------------------------------------------------------
// Color Constants
//----------------------------------------------------------------------------

#define COLOR_BLACK           0
#define COLOR_BLUE            1
#define COLOR_GREEN           2
#define COLOR_CYAN            3
#define COLOR_RED             4
#define COLOR_MAGENTA         5
#define COLOR_BROWN           6
#define COLOR_LIGHT_GRAY      7
#define COLOR_DARK_GRAY       8
#define COLOR_LIGHT_BLUE      9
#define COLOR_LIGHT_GREEN    10
#define COLOR_LIGHT_CYAN     11
#define COLOR_LIGHT_RED      12
#define COLOR_LIGHT_MAGENTA  13
#define COLOR_YELLOW         14
#define COLOR_WHITE          15

//----------------------------------------------------------------------------
// Text-Mode Constants
//----------------------------------------------------------------------------

#define TEXT_MODE_25_ROWS    25
#define TEXT_MODE_43_ROWS    43
#define TEXT_MODE_50_ROWS    50

// Line thickness: the line_thickness arguments of the box-art primitives
// take '-' for single box lines or '=' for double box lines.

//----------------------------------------------------------------------------
// Function Prototypes
//----------------------------------------------------------------------------

#ifdef __cplusplus
extern "C" {
#endif

// Mode and Screen Functions

void       SetTextMode       ( BYTE rows );                 // INT 10h; 25/43/50 rows
BYTE       GetTextMode       ( void );                      // Rows of the current mode; 0 if not a text mode
void       FlipScreenBuffer  ( TEXTBUFFER *source );        // Blit source -> physical 0xB800 (REP MOVSW), clipped to 80 x rows

// Buffer Lifecycle Functions

TEXTBUFFER CreateTextBuffer  ( int width, int height, BYTE foreground, BYTE background );
void       DestroyTextBuffer ( TEXTBUFFER *text_buffer );
void       ClearTextBuffer   ( TEXTBUFFER *text_buffer, char character, BYTE foreground, BYTE background );
void       CopyTextBuffer    ( TEXTBUFFER *destination_text_buffer, TEXTBUFFER *source_text_buffer );

// Cell and String Plotting

void PutCharacter ( TEXTBUFFER *text_buffer, int x, int y, char character, BYTE foreground, BYTE background, BYTE foreground_transparency_enabled, BYTE background_transparency_enabled );
void PutText      ( TEXTBUFFER *text_buffer, int x, int y, const char *s, BYTE foreground, BYTE background, BYTE foreground_transparency_enabled, BYTE background_transparency_enabled );

// Box-Art Primitives

void PutHorizontalAsciiLine ( TEXTBUFFER *text_buffer, int x, int y, int length, BYTE foreground, BYTE background, BYTE foreground_transparency_enabled, BYTE background_transparency_enabled, char line_thickness, BYTE end_caps_enabled );
void PutVerticalAsciiLine   ( TEXTBUFFER *text_buffer, int x, int y, int length, BYTE foreground, BYTE background, BYTE foreground_transparency_enabled, BYTE background_transparency_enabled, char line_thickness, BYTE end_caps_enabled );
void PutAsciiRectangle      ( TEXTBUFFER *text_buffer, int x, int y, int width, int height, BYTE foreground, BYTE background, BYTE foreground_transparency_enabled, BYTE background_transparency_enabled, char horizontal_line_thickness, char vertical_line_thickness, BYTE filled );

// Block Save-Restore and Shadow

void GetTextBlock ( TEXTBUFFER *destination_text_buffer, TEXTBUFFER *source_text_buffer, int x, int y, int width, int height );
void PutTextBlock ( TEXTBUFFER *destination_text_buffer, TEXTBUFFER *source_text_buffer, int x, int y, int width, int height );
void PutShadow    ( TEXTBUFFER *text_buffer, int x, int y, int width, int height, BYTE foreground_shadow_enabled, BYTE background_shadow_enabled );

// Attribute, Cursor, and Keyboard Helpers

// Shift-flag masks for the byte returned by GetShiftState (INT 16h AH=02h).

#define SHIFT_STATE_RIGHT_SHIFT  0x01
#define SHIFT_STATE_LEFT_SHIFT   0x02
#define SHIFT_STATE_SHIFT        0x03    // Either shift key.
#define SHIFT_STATE_CTRL         0x04
#define SHIFT_STATE_ALT          0x08

// The high nibble carries toggle states rather than held keys: the BIOS
// consumes the lock keys itself, flipping these bits without placing
// anything in the keyboard buffer, so ReadKey never sees them and only a
// poll of GetShiftState reports a lock's current state.

#define SHIFT_STATE_SCROLL_LOCK  0x10
#define SHIFT_STATE_NUM_LOCK     0x20
#define SHIFT_STATE_CAPS_LOCK    0x40
#define SHIFT_STATE_INSERT       0x80

BYTE MakeAttribute     ( BYTE foreground, BYTE background );   // (background << 4) | foreground
void ShowCursor        ( void );                               // INT 10h cursor on
void HideCursor        ( void );                               // INT 10h cursor off
void SetCursorPosition ( int x, int y );                       // INT 10h AH = 02h
int  KeyPressed        ( void );                               // INT 16h AH = 01h -> nonzero if a key is waiting
WORD ReadKey           ( void );                               // INT 16h AH = 00h -> (scan << 8) | ascii, blocking
WORD PeekKey           ( void );                               // INT 16h AH = 01h -> waiting key word without removing it; 0 if none
WORD GetShiftState     ( void );                               // INT 16h AH = 02h -> shift-flag byte (SHIFT_STATE_* masks)

// Key-state tracking: a chained INT 9 handler records make/break codes in
// a key-state table, so a key's physical held state can be polled while
// the BIOS keyboard buffer (KeyPressed / ReadKey) keeps working normally.

void InstallKeyboardHandler ( void );                          // Hook INT 9 (chains to the previous handler); enables KeyDown
void RemoveKeyboardHandler  ( void );                          // Restore the previous INT 9 handler
int  KeyDown                ( BYTE scan_code );                // Nonzero while the key is physically held (handler installed)

#ifdef __cplusplus
}
#endif

//----------------------------------------------------------------------------

#endif

//----------------------------------------------------------------------------
