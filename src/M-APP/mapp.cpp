//****************************************************************************
// Program: MicroApp (User Interface Application Framework)
// Version: 1.0
// Date:    1992-07-13
// Author:  Rohin Gosling
//
// Description:
//
//   Text-mode UI application framework for DOS programs, layered directly 
//   on the MicroText library.
//
//   - Supplies a Component hierarchy, an event/action dispatch model with 
//     function-pointer callbacks, focus management, the Application run 
//     loop and render pipeline, reusable widgets and modal dialogs, and 
//     the ApplicationPanel (title bar, menu bar, workspace, status bar).
//
//   - Rendering is virtual and top-down:
//
//     - Each Component::Draw draws itself into a single off-screen back 
//       buffer; containers draw themselves and then their children; one 
//       FlipScreenBuffer presents the finished frame, flicker-free. 
//
//     - Floating elements (menus, dialogs) follow the save/shadow/restore 
//       discipline built on GetTextBlock, PutShadow, and PutTextBlock.
//
//   - Component coordinates are absolute screen cells (column x, row y),
//     not parent-relative. A Panel with its border enabled paints an opaque
//     filled rectangle; a borderless Panel is a transparent container that
//     draws only its children.
//
// Compilation:
//
//   - Borland C++ 3.1, large memory model:
//
//       bcc -c -ml -I..\M-TEXT mapp.cpp
//
//   - Borland C++ 3.1 predates the built-in C++ bool type, so this header
//     supplies typedef int bool with true/false constants.
//
//   - mapp.cpp defines _stklen = 32 KB: component trees and dialogs live
//     on the consumer's stack, and Borland's default 4 KB stack silently
//     overflows under a full tree (stack checking is off by default).
//     Applications must not define their own _stklen.
//
//****************************************************************************

#include <string.h>
#include <stddef.h>
#include <alloc.h>
#include <dos.h>
#include <dir.h>

#include "mtext.h"
#include "mapp.h"

//----------------------------------------------------------------------------
// Stack Size
//----------------------------------------------------------------------------

// MicroApp applications build their component tree as objects on the stack 
// (the documented consumer pattern), and a full tree plus dialogs (a ListBox 
// alone carries LIST_ITEM_CAPACITY in-struct items) plus the draw-call frames 
// overruns Borland's default 4 KB stack, silently, since bcc's stack checking 
// is off by default. 
//
// The framework therefore mandates a 32 KB stack for every application linked 
// against it.

extern unsigned _stklen = 32768U;

//----------------------------------------------------------------------------
// File-Scope Helpers
//----------------------------------------------------------------------------

//----------------------------------------------------------------------------
// Function: CopyBoundedText
//
// Description:
//
//   Copies a string into a fixed-size buffer, truncating to the buffer
//   size and always NUL-terminating. A NULL source yields an empty
//   string.
//
// Arguments:
//
//   - destination      : The buffer to copy into.
//   - source           : The string to copy (may be NULL).
//   - destination_size : The buffer size in bytes, including the NUL.
//
// Returns:
//
//   - None
//
//----------------------------------------------------------------------------

static void CopyBoundedText ( char *destination, const char *source, int destination_size )
{
	if ( source == NULL )
	{
		destination [ 0 ] = '\0';
		return;
	}

	strncpy ( destination, source, destination_size - 1 );

	destination [ destination_size - 1 ] = '\0';
}

//****************************************************************************
// Class: Component
//****************************************************************************

//----------------------------------------------------------------------------
// Function: Component::Component
//
// Description:
//
//   Initializes a component to an empty, visible, enabled, unfocused
//   state in light gray on black at the given geometry (the default
//   constructor uses zero geometry).
//
// Arguments:
//
//   - new_name   : Component name (default constructor: empty).
//   - new_x      : Absolute column.
//   - new_y      : Absolute row.
//   - new_width  : Width in cells.
//   - new_height : Height in cells.
//
// Returns:
//
//   - None
//
//----------------------------------------------------------------------------

Component::Component ( void )
{
	name        [ 0 ] = '\0';
	text        [ 0 ] = '\0';
	description [ 0 ] = '\0';

	x      = 0;
	y      = 0;
	width  = 0;
	height = 0;

	visible   = true;
	enabled   = true;
	focusable = false;
	focused   = false;

	foreground = COLOR_LIGHT_GRAY;
	background = COLOR_BLACK;

	parent = NULL;

	on_activate           = NULL;
	on_activate_user_data = NULL;
	on_change             = NULL;
	on_change_user_data   = NULL;
	on_focus              = NULL;
	on_focus_user_data    = NULL;
	on_blur               = NULL;
	on_blur_user_data     = NULL;
	on_key_down           = NULL;
	on_key_down_user_data = NULL;
	on_key_up             = NULL;
	on_key_up_user_data   = NULL;
}

Component::Component ( const char *new_name, int new_x, int new_y, int new_width, int new_height )
{
	name        [ 0 ] = '\0';
	text        [ 0 ] = '\0';
	description [ 0 ] = '\0';

	CopyBoundedText ( name, new_name, COMPONENT_NAME_SIZE );

	x      = new_x;
	y      = new_y;
	width  = new_width;
	height = new_height;

	visible   = true;
	enabled   = true;
	focusable = false;
	focused   = false;

	foreground = COLOR_LIGHT_GRAY;
	background = COLOR_BLACK;

	parent = NULL;

	on_activate           = NULL;
	on_activate_user_data = NULL;
	on_change             = NULL;
	on_change_user_data   = NULL;
	on_focus              = NULL;
	on_focus_user_data    = NULL;
	on_blur               = NULL;
	on_blur_user_data     = NULL;
	on_key_down           = NULL;
	on_key_down_user_data = NULL;
	on_key_up             = NULL;
	on_key_up_user_data   = NULL;
}

//----------------------------------------------------------------------------
// Function: Component::~Component
//
// Description:
//
//   Virtual destructor anchor for the hierarchy.
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

Component::~Component ( void )
{
}

//----------------------------------------------------------------------------
// Function: Component::SetName / SetText / SetDescription
//
// Description:
//
//   Bounded-copy mutators for the component's identity strings.
//
// Arguments:
//
//   - new_name / new_text / new_description : The string to store.
//
// Returns:
//
//   - None
//
//----------------------------------------------------------------------------

void Component::SetName ( const char *new_name )
{
	CopyBoundedText ( name, new_name, COMPONENT_NAME_SIZE );
}

void Component::SetText ( const char *new_text )
{
	CopyBoundedText ( text, new_text, COMPONENT_TEXT_SIZE );
}

void Component::SetDescription ( const char *new_description )
{
	CopyBoundedText ( description, new_description, COMPONENT_TEXT_SIZE );
}

//----------------------------------------------------------------------------
// Function: Component::Draw
//
// Description:
//
//   Base drawing behavior: nothing. Subclasses override.
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

void Component::Draw ( TEXTBUFFER *text_buffer )
{
	( void ) text_buffer;
}

//----------------------------------------------------------------------------
// Function: Component::HandleKey
//
// Description:
//
//   Base key behavior: the key is not consumed, so it bubbles to the
//   parent container. Subclasses override.
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

bool Component::HandleKey ( KEY_EVENT &key_event )
{
	( void ) key_event;

	return false;
}

//----------------------------------------------------------------------------
// Function: Component::SetFocus
//
// Description:
//
//   Sets or clears the focused flag and fires the OnFocus / OnBlur
//   handler for the transition.
//
// Arguments:
//
//   - new_focused : true to focus the component, false to blur it.
//
// Returns:
//
//   - None
//
//----------------------------------------------------------------------------

void Component::SetFocus ( bool new_focused )
{
	if ( focused == new_focused ) return;

	focused = new_focused;

	if ( focused )
	{
		if ( on_focus != NULL ) on_focus ( this, on_focus_user_data );
	}
	else
	{
		if ( on_blur != NULL ) on_blur ( this, on_blur_user_data );
	}
}

//----------------------------------------------------------------------------
// Function: Component::FireActivate / FireChange / FireKeyDown / FireKeyUp
//
// Description:
//
//   Invokes the registered OnActivate / OnChange / OnKeyDown / OnKeyUp
//   handler, if any, passing this component and the stored user data.
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

void Component::FireActivate ( void )
{
	if ( on_activate != NULL ) on_activate ( this, on_activate_user_data );
}

void Component::FireChange ( void )
{
	if ( on_change != NULL ) on_change ( this, on_change_user_data );
}

void Component::FireKeyDown ( void )
{
	if ( on_key_down != NULL ) on_key_down ( this, on_key_down_user_data );
}

void Component::FireKeyUp ( void )
{
	if ( on_key_up != NULL ) on_key_up ( this, on_key_up_user_data );
}

//****************************************************************************
// Class: Panel
//****************************************************************************

//----------------------------------------------------------------------------
// Function: Panel::Panel
//
// Description:
//
//   Initializes an empty container: no children, border enabled with
//   single box lines, no shadow.
//
// Arguments:
//
//   - new_name   : Component name (default constructor: empty).
//   - new_x      : Absolute column.
//   - new_y      : Absolute row.
//   - new_width  : Width in cells.
//   - new_height : Height in cells.
//
// Returns:
//
//   - None
//
//----------------------------------------------------------------------------

Panel::Panel ( void ) : Component ()
{
	component_count = 0;
	focused_index   = -1;
	border_enabled  = true;
	shadow_enabled  = false;
	line_thickness  = '-';
}

Panel::Panel ( const char *new_name, int new_x, int new_y, int new_width, int new_height ) : Component ( new_name, new_x, new_y, new_width, new_height )
{
	component_count = 0;
	focused_index   = -1;
	border_enabled  = true;
	shadow_enabled  = false;
	line_thickness  = '-';
}

//----------------------------------------------------------------------------
// Function: Panel::AddComponent
//
// Description:
//
//   Appends a child component to the panel and sets its parent pointer.
//   A full panel (COMPONENT_CHILD_CAPACITY children) ignores the call.
//
// Arguments:
//
//   - component : The child to add.
//
// Returns:
//
//   - None
//
//----------------------------------------------------------------------------

void Panel::AddComponent ( Component *component )
{
	if ( component == NULL )                            return;
	if ( component_count >= COMPONENT_CHILD_CAPACITY )  return;

	components [ component_count ] = component;
	component_count++;

	component->SetParent ( this );
}

//----------------------------------------------------------------------------
// Function: Panel::Draw
//
// Description:
//
//   Draws the panel and then its children in add order. With the border
//   enabled, the panel is opaque: an optional drop shadow is cast first,
//   then a filled box-art rectangle in the panel's colors, then the
//   optional title (the component text) centered on the top edge.
//   Borderless, the panel is a transparent container and draws only its
//   children.
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

void Panel::Draw ( TEXTBUFFER *text_buffer )
{
	// Local variables.

	char title [ COMPONENT_TEXT_SIZE + 2 ];
	int  title_length;
	int  title_x;
	int  i;

	// Hidden panels draw nothing.

	if ( !visible ) return;

	// A bordered panel is opaque and paints itself; a borderless panel is transparent and draws only its children.

	if ( border_enabled )
	{
		// Cast the optional drop shadow first, so the panel body paints in front of it.

		if ( shadow_enabled )
		{
			PutShadow ( text_buffer, x + 2, y + 1, width, height, 1, 1 );
		}

		// Fill the panel rectangle with a box-art border in the panel's colours.

		PutAsciiRectangle ( text_buffer, x, y, width, height, foreground, background, 0, 0, line_thickness, line_thickness, 1 );

		// Centered title on the top edge, padded with one space each side.

		if ( text [ 0 ] != '\0' )
		{
			title [ 0 ] = ' ';
			strcpy ( title + 1, text );
			strcat ( title, " " );

			title_length = strlen ( title );
			title_x      = x + ( width - title_length ) / 2;

			PutText ( text_buffer, title_x, y, title, foreground, background, 0, 0 );
		}
	}

	// Draw the child components in add order, over the panel body.

	for ( i = 0; i < component_count; i++ )
	{
		components [ i ]->Draw ( text_buffer );
	}
}

//----------------------------------------------------------------------------
// Function: Panel::HandleKey
//
// Description:
//
//   Container key behavior. Focus traversal across children arrives with
//   the input widgets; the base container consumes nothing.
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

bool Panel::HandleKey ( KEY_EVENT &key_event )
{
	// The base container consumes no keys of its own.

	( void ) key_event;

	// Let the key bubble to the parent.

	return false;
}

//****************************************************************************
// Class: Label
//****************************************************************************

//----------------------------------------------------------------------------
// Function: Label::Label
//
// Description:
//
//   Initializes a static text label. The parameterized constructor sizes
//   the label to its text (one row high).
//
// Arguments:
//
//   - new_name : Component name.
//   - new_x    : Absolute column.
//   - new_y    : Absolute row.
//   - new_text : The label text.
//
// Returns:
//
//   - None
//
//----------------------------------------------------------------------------

Label::Label ( void ) : Component ()
{
}

Label::Label ( const char *new_name, int new_x, int new_y, const char *new_text ) : Component ( new_name, new_x, new_y, 0, 1 )
{
	// Store the label text.

	SetText ( new_text );

	// Size the label to fit its text (one row high).

	width = strlen ( text );
}

//----------------------------------------------------------------------------
// Function: Label::Draw
//
// Description:
//
//   Plots the label text at (x, y) in the label's colors.
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

void Label::Draw ( TEXTBUFFER *text_buffer )
{
	// Hidden labels draw nothing.

	if ( !visible ) return;

	// Plot the label text at its position in the label's colours.

	PutText ( text_buffer, x, y, text, foreground, background, 0, 0 );
}

//****************************************************************************
// Class: Button
//****************************************************************************

//----------------------------------------------------------------------------
// Function: Button::Button
//
// Description:
//
//   Initializes a focusable one-row push button with the default color
//   sets: white-on-dark-gray unselected, white-on-navy selected
//   (focused), and white-on-red depressed.
//
// Arguments:
//
//   - new_name  : Component name.
//   - new_x     : Absolute column.
//   - new_y     : Absolute row.
//   - new_width : Button width in cells.
//   - new_text  : The button caption.
//
// Returns:
//
//   - None
//
//----------------------------------------------------------------------------

Button::Button ( void ) : Component ()
{
	unselected_foreground = COLOR_WHITE;
	unselected_background = COLOR_DARK_GRAY;
	selected_foreground   = COLOR_WHITE;
	selected_background   = COLOR_BLUE;
	depressed_foreground  = COLOR_WHITE;
	depressed_background  = COLOR_RED;

	depressed = false;
	focusable = true;
}

Button::Button ( const char *new_name, int new_x, int new_y, int new_width, const char *new_text ) : Component ( new_name, new_x, new_y, new_width, 1 )
{
	unselected_foreground = COLOR_WHITE;
	unselected_background = COLOR_DARK_GRAY;
	selected_foreground   = COLOR_WHITE;
	selected_background   = COLOR_BLUE;
	depressed_foreground  = COLOR_WHITE;
	depressed_background  = COLOR_RED;

	depressed = false;
	focusable = true;

	SetText ( new_text );
}

//----------------------------------------------------------------------------
// Function: Button::SetUnselectedColors / SetSelectedColors / SetDepressedColors
//
// Description:
//
//   Mutators for the button's three color sets.
//
// Arguments:
//
//   - new_foreground : Foreground color of the set.
//   - new_background : Background color of the set.
//
// Returns:
//
//   - None
//
//----------------------------------------------------------------------------

void Button::SetUnselectedColors ( BYTE new_foreground, BYTE new_background )
{
	unselected_foreground = new_foreground;
	unselected_background = new_background;
}

void Button::SetSelectedColors ( BYTE new_foreground, BYTE new_background )
{
	selected_foreground = new_foreground;
	selected_background = new_background;
}

void Button::SetDepressedColors ( BYTE new_foreground, BYTE new_background )
{
	depressed_foreground = new_foreground;
	depressed_background = new_background;
}

//----------------------------------------------------------------------------
// Function: Button::Draw
//
// Description:
//
//   Fills the button row and centers the caption, in the color set for
//   the button's current state: depressed, selected (focused), or
//   unselected.
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

void Button::Draw ( TEXTBUFFER *text_buffer )
{
	// Local variables.

	BYTE state_foreground;
	BYTE state_background;
	int  text_length;
	int  text_x;
	int  i;

	// Hidden buttons draw nothing.

	if ( !visible ) return;

	// Choose the colour set for the button's current state.

	if ( depressed )
	{
		state_foreground = depressed_foreground;
		state_background = depressed_background;
	}
	else if ( focused )
	{
		state_foreground = selected_foreground;
		state_background = selected_background;
	}
	else
	{
		state_foreground = unselected_foreground;
		state_background = unselected_background;
	}

	// Fill the button row in the state colour.

	for ( i = 0; i < width; i++ )
	{
		PutCharacter ( text_buffer, x + i, y, ' ', state_foreground, state_background, 0, 0 );
	}

	// Centre the caption within the button width.

	text_length = strlen ( text );
	text_x      = x + ( width - text_length ) / 2;

	// Draw the caption over the filled row.

	PutText ( text_buffer, text_x, y, text, state_foreground, state_background, 0, 0 );
}

//----------------------------------------------------------------------------
// Function: Button::HandleKey
//
// Description:
//
//   Enter presses the button (Space is reserved for the toggle widgets:
//   check boxes and radio buttons). The press and release are real
//   key transitions, tracked through MicroText's INT 9 key-state layer:
//   on the press, OnKeyDown fires and the depressed colors show for as
//   long as the key is physically held; on the release, OnKeyUp fires,
//   then OnActivate. The typematic repeats the hold left in the BIOS
//   keyboard buffer are drained, so a long hold is one clean
//   press/release cycle.
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

bool Button::HandleKey ( KEY_EVENT &key_event )
{
	// Local variables.

	Application *application;

	// Enter presses the button (Space is reserved for the toggle widgets).

	if ( key_event.scan_code == KEY_ENTER )
	{
		// Resolve the running application, used to repaint while the key is held.

		application = Application::GetInstance ();

		// Key down: show the depressed state while the key is held.

		depressed = true;
		FireKeyDown ();

		// Repaint so the depressed state shows immediately.

		if ( application != NULL ) application->RenderFrame ();

		// Hold in the depressed state until the key is physically released.

		while ( KeyDown ( key_event.scan_code ) ) { }

		// Key up: release, notify, and activate.

		depressed = false;
		FireKeyUp ();
		FireActivate ();

		// Drain the typematic repeats the hold queued in the BIOS buffer.

		while ( ( BYTE ) ( PeekKey () >> 8 ) == key_event.scan_code ) ReadKey ();

		// The key was consumed.

		return true;
	}

	// Any other key bubbles to the parent.

	return false;
}

//****************************************************************************
// Class: TextBox
//****************************************************************************

//----------------------------------------------------------------------------
// Function: TextBoxLineCount / TextBoxLineStart / TextBoxLineLength /
//           TextBoxLocate / TextBoxEnsureVisible
//
// Description:
//
//   File-scope helpers over a TextBox edit buffer, which holds
//   NUL-terminated text with '\n' line separators: count the lines, find
//   a line's start offset and length, convert a buffer offset to a
//   (line, column) pair, and adjust the scroll origin so the cursor
//   stays inside the visible interior.
//
// Arguments:
//
//   - Per helper: the edit buffer, and offsets / line indices / scroll
//     state as named.
//
// Returns:
//
//   - Per helper: the count, offset, length, or None.
//
//----------------------------------------------------------------------------

static unsigned TextBoxLineCount ( const char *buffer )
{
	// Local variables.

	unsigned count;
	unsigned i;

	// Every buffer holds at least one line.

	count = 1;

	// Count a line for each newline separator.

	for ( i = 0; buffer [ i ] != '\0'; i++ )
	{
		if ( buffer [ i ] == '\n' ) count++;
	}

	// Return the line count.

	return count;
}

static unsigned TextBoxLineStart ( const char *buffer, unsigned line )
{
	// Local variables.

	unsigned current_line;
	unsigned i;

	// Start at the first line, first byte.

	current_line = 0;
	i            = 0;

	// Advance past newlines until the requested line is reached (or the buffer ends).

	while ( ( current_line < line ) && ( buffer [ i ] != '\0' ) )
	{
		if ( buffer [ i ] == '\n' ) current_line++;
		i++;
	}

	// Return the line's start offset.

	return i;
}

static unsigned TextBoxLineLength ( const char *buffer, unsigned line_start )
{
	// Local variables.

	unsigned length;

	// Start with zero length.

	length = 0;

	// Count bytes up to the next newline or the end of the buffer.

	while ( ( buffer [ line_start + length ] != '\0' ) && ( buffer [ line_start + length ] != '\n' ) ) length++;

	// Return the line length.

	return length;
}

static void TextBoxLocate ( const char *buffer, unsigned position, unsigned *line, unsigned *column )
{
	// Local variables.

	unsigned i;

	// Start at line 0, column 0.

	*line   = 0;
	*column = 0;

	// Walk to the position, advancing the line on each newline and the column otherwise.

	for ( i = 0; ( i < position ) && ( buffer [ i ] != '\0' ); i++ )
	{
		if ( buffer [ i ] == '\n' )
		{
			( *line )++;
			*column = 0;
		}
		else
		{
			( *column )++;
		}
	}
}

static void TextBoxEnsureVisible
(
	const char *buffer,
	unsigned    cursor_position,
	int        *scroll_x,
	int        *scroll_y,
	int         interior_width,
	int         interior_height
)
{
	// Local variables.

	unsigned line;
	unsigned column;

	// Locate the cursor's line and column.

	TextBoxLocate ( buffer, cursor_position, &line, &column );

	// Scroll the origin the minimum needed to bring the cursor back inside the interior.

	if ( ( int ) line < *scroll_y )                     *scroll_y = ( int ) line;
	if ( ( int ) line >= *scroll_y + interior_height )  *scroll_y = ( int ) line - interior_height + 1;
	if ( ( int ) column < *scroll_x )                   *scroll_x = ( int ) column;
	if ( ( int ) column >= *scroll_x + interior_width ) *scroll_x = ( int ) column - interior_width + 1;
}

//----------------------------------------------------------------------------
// Function: TextBox::TextBox / TextBox::~TextBox
//
// Description:
//
//   The parameterized constructor farmalloc-s the edit buffer (capacity
//   in bytes, including the NUL) and initializes an empty, focusable
//   text box in insert mode. The destructor releases the buffer.
//
// Arguments:
//
//   - new_name             : Component name.
//   - new_x                : Absolute column.
//   - new_y                : Absolute row.
//   - new_width            : Width in cells, including the border.
//   - new_height           : Height in cells, including the border.
//   - new_edit_buffer_size : Edit buffer capacity in bytes.
//   - new_multi_line       : true for a multi-line editor.
//
// Returns:
//
//   - None
//
//----------------------------------------------------------------------------

TextBox::TextBox ( void ) : Component ()
{
	edit_buffer      = NULL;
	edit_buffer_size = 0;
	text_length      = 0;
	cursor_position  = 0;
	scroll_x                    = 0;
	scroll_y                    = 0;
	multi_line                  = false;
	insert_enabled              = true;
	border_foreground           = COLOR_DARK_GRAY;
	selected_border_foreground  = COLOR_LIGHT_GRAY;
	focusable                   = true;
}

TextBox::TextBox
(
	const char *new_name,
	int         new_x,
	int         new_y,
	int         new_width,
	int         new_height,
	unsigned    new_edit_buffer_size,
	bool        new_multi_line
)
: Component ( new_name, new_x, new_y, new_width, new_height )
{
	// Allocate the far edit buffer; on failure the capacity is left at zero.

	edit_buffer      = ( char far * ) farmalloc ( new_edit_buffer_size );
	edit_buffer_size = ( edit_buffer != NULL ) ? new_edit_buffer_size : 0;

	// Start the buffer empty when the allocation succeeded.

	if ( edit_buffer != NULL ) edit_buffer [ 0 ] = '\0';

	// Initialise the edit state.

	text_length                 = 0;
	cursor_position             = 0;
	scroll_x                    = 0;
	scroll_y                    = 0;
	multi_line                  = new_multi_line;
	insert_enabled              = true;
	border_foreground           = COLOR_DARK_GRAY;
	selected_border_foreground  = COLOR_LIGHT_GRAY;
	focusable                   = true;
}

TextBox::~TextBox ( void )
{
	if ( edit_buffer != NULL )
	{
		farfree ( ( void far * ) edit_buffer );
		edit_buffer = NULL;
	}
}

//----------------------------------------------------------------------------
// Function: TextBox::SetEditText
//
// Description:
//
//   Replaces the edit buffer contents (bounded to the buffer capacity)
//   and resets the cursor and scroll origin to the start.
//
// Arguments:
//
//   - new_text : The text to load (may be NULL for empty).
//
// Returns:
//
//   - None
//
//----------------------------------------------------------------------------

void TextBox::SetEditText ( const char far *new_text )
{
	// Local variables.

	unsigned i;

	// Nothing to do without an edit buffer.

	if ( edit_buffer == NULL ) return;

	// Start empty.

	text_length = 0;

	// Copy the new text, bounded to the buffer capacity.

	if ( new_text != NULL )
	{
		for ( i = 0; ( new_text [ i ] != '\0' ) && ( i < edit_buffer_size - 1 ); i++ )
		{
			edit_buffer [ i ] = new_text [ i ];
		}

		text_length = i;
	}

	// NUL-terminate the copied text.

	edit_buffer [ text_length ] = '\0';

	// Reset the cursor and scroll origin to the start.

	cursor_position = 0;
	scroll_x        = 0;
	scroll_y        = 0;
}

//----------------------------------------------------------------------------
// Function: TextBox::GetCursorLine / GetCursorColumn
//
// Description:
//
//   Reports the cursor position as a zero-based (line, column) pair.
//
// Arguments:
//
//   - None
//
// Returns:
//
//   - The cursor line / column.
//
//----------------------------------------------------------------------------

unsigned TextBox::GetCursorLine ( void )
{
	// Local variables.

	unsigned line;
	unsigned column;

	// No buffer means line 0.

	if ( edit_buffer == NULL ) return 0;

	// Locate the cursor's line and column.

	TextBoxLocate ( ( const char * ) edit_buffer, cursor_position, &line, &column );

	// Return the cursor line.

	return line;
}

unsigned TextBox::GetCursorColumn ( void )
{
	// Local variables.

	unsigned line;
	unsigned column;

	// No buffer means column 0.

	if ( edit_buffer == NULL ) return 0;

	// Locate the cursor's line and column.

	TextBoxLocate ( ( const char * ) edit_buffer, cursor_position, &line, &column );

	// Return the cursor column.

	return column;
}

//----------------------------------------------------------------------------
// Function: TextBox::Draw
//
// Description:
//
//   Draws the bordered text area (the border shows border_foreground,
//   switching to the text foreground while focused), the optional title
//   (the component text) centered on the top edge, the visible window of
//   the edit text from the scroll origin, and - while focused - a
//   software cursor as an inverted cell.
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

void TextBox::Draw ( TEXTBUFFER *text_buffer )
{
	char     title [ COMPONENT_TEXT_SIZE + 2 ];
	BYTE     frame_foreground;
	char     cursor_character;
	unsigned line_count;
	unsigned line;
	unsigned line_start;
	unsigned line_length;
	unsigned cursor_line;
	unsigned cursor_column;
	int      interior_width;
	int      interior_height;
	int      title_length;
	int      row;
	int      i;

	if ( !visible )            return;
	if ( edit_buffer == NULL ) return;

	interior_width  = width - 2;
	interior_height = height - 2;

	// Border and background: selected_border_foreground while focused,
	// border_foreground otherwise.

	frame_foreground = focused ? selected_border_foreground : border_foreground;

	// Draw the bordered text area in the frame colour.

	PutAsciiRectangle ( text_buffer, x, y, width, height, frame_foreground, background, 0, 0, '-', '-', 1 );

	// Optional title, centred on the top edge and padded with one space each side.

	if ( text [ 0 ] != '\0' )
	{
		title [ 0 ] = ' ';
		strcpy ( title + 1, text );
		strcat ( title, " " );

		title_length = strlen ( title );

		PutText ( text_buffer, x + ( width - title_length ) / 2, y, title, frame_foreground, background, 0, 0 );
	}

	// The visible window of the edit text.

	line_count = TextBoxLineCount ( ( const char * ) edit_buffer );

	// Draw each visible row of the edit text from the scroll origin, clipped to the interior.

	for ( row = 0; row < interior_height; row++ )
	{
		line = ( unsigned ) ( scroll_y + row );

		if ( line >= line_count ) break;

		line_start  = TextBoxLineStart  ( ( const char * ) edit_buffer, line );
		line_length = TextBoxLineLength ( ( const char * ) edit_buffer, line_start );

		for ( i = 0; i < interior_width; i++ )
		{
			if ( ( unsigned ) ( scroll_x + i ) >= line_length ) break;

			PutCharacter ( text_buffer, x + 1 + i, y + 1 + row, edit_buffer [ line_start + scroll_x + i ], foreground, background, 0, 0 );
		}
	}

	// Software cursor: the cell at the cursor, inverted, while focused.

	if ( focused )
	{
		// Locate the cursor within the edit text.

		TextBoxLocate ( ( const char * ) edit_buffer, cursor_position, &cursor_line, &cursor_column );

		// Only draw the cursor when it lies inside the visible interior.

		if
		(
			( ( int ) cursor_line   >= scroll_y                   ) &&
			( ( int ) cursor_line   <  scroll_y + interior_height ) &&
		    ( ( int ) cursor_column >= scroll_x                   ) &&
			( ( int ) cursor_column <  scroll_x + interior_width  )
		)
		{
			// Default the cursor cell to a space.

			cursor_character = ' ';

			// Show the character under the cursor, unless it is at the end or on a line break.

			if ( ( cursor_position < text_length ) && ( edit_buffer [ cursor_position ] != '\n' ) )
			{
				cursor_character = edit_buffer [ cursor_position ];
			}

			// Draw the cursor as an inverted cell (colours swapped).

			PutCharacter
			(
				text_buffer, 
				x + 1 + ( int ) cursor_column - scroll_x, 
				y + 1 + ( int ) cursor_line   - scroll_y, 
				cursor_character, 
				background, 
				foreground, 
				0, 
				0
			);
		}
	}
}

//----------------------------------------------------------------------------
// Function: TextBox::HandleKey
//
// Description:
//
//   The editing engine: printable characters insert or overwrite at the
//   cursor (overwrite never replaces a line break - it inserts instead);
//   Backspace and Delete remove characters; Enter inserts a line break
//   in multi-line boxes; the arrows, PgUp, PgDn, Home (start of line),
//   and End (end of line) move the cursor
//   (single-line boxes bubble the vertical keys); Ins toggles
//   insert/overwrite. Tab is never consumed, so focus can leave the box.
//   Every consumed key fires OnChange (the content, cursor position, or
//   mode may have changed), and every cursor move re-scrolls the view to
//   keep the cursor visible.
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

bool TextBox::HandleKey ( KEY_EVENT &key_event )
{
	char     *buffer;
	unsigned  line;
	unsigned  column;
	unsigned  line_count;
	unsigned  target_line;
	unsigned  target_start;
	unsigned  target_length;
	int       page_size;
	bool      consumed;

	// Nothing to edit without a buffer.

	if ( edit_buffer == NULL ) return false;

	// Local aliases for the edit buffer and the consumed flag.

	buffer   = ( char * ) edit_buffer;
	consumed = false;

	// Dispatch on the key.

	switch ( key_event.scan_code )
	{
		case KEY_LEFT:

			if ( cursor_position > 0 ) cursor_position--;
			consumed = true;
			break;

		case KEY_RIGHT:

			if ( cursor_position < text_length ) cursor_position++;
			consumed = true;
			break;

		case KEY_UP:
		case KEY_DOWN:
		case KEY_PGUP:
		case KEY_PGDN:

			if ( !multi_line ) return false;

			TextBoxLocate ( buffer, cursor_position, &line, &column );

			line_count = TextBoxLineCount ( buffer );
			page_size  = height - 2;
			if ( page_size < 1 ) page_size = 1;

			target_line = line;

			if ( key_event.scan_code == KEY_UP )   { if ( line > 0 ) target_line = line - 1; }
			if ( key_event.scan_code == KEY_DOWN ) { if ( line + 1 < line_count ) target_line = line + 1; }
			if ( key_event.scan_code == KEY_PGUP ) { target_line = ( line >= ( unsigned ) page_size ) ? line - page_size : 0; }
			if ( key_event.scan_code == KEY_PGDN ) { target_line = ( line + page_size < line_count ) ? line + page_size : line_count - 1; }

			target_start  = TextBoxLineStart  ( buffer, target_line );
			target_length = TextBoxLineLength ( buffer, target_start );

			cursor_position = target_start + ( ( column < target_length ) ? column : target_length );

			consumed = true;
			break;

		case KEY_HOME:

			TextBoxLocate ( buffer, cursor_position, &line, &column );

			cursor_position = TextBoxLineStart ( buffer, line );

			consumed = true;
			break;

		case KEY_END:

			TextBoxLocate ( buffer, cursor_position, &line, &column );

			target_start    = TextBoxLineStart  ( buffer, line );
			cursor_position = target_start + TextBoxLineLength ( buffer, target_start );

			consumed = true;
			break;

		case KEY_INS:

			insert_enabled = !insert_enabled;
			consumed = true;
			break;

		case KEY_DELETE:

			if ( cursor_position < text_length )
			{
				memmove ( buffer + cursor_position, buffer + cursor_position + 1, text_length - cursor_position );
				text_length--;
			}

			consumed = true;
			break;

		case KEY_BACKSPACE:

			if ( cursor_position > 0 )
			{
				cursor_position--;
				memmove ( buffer + cursor_position, buffer + cursor_position + 1, text_length - cursor_position );
				text_length--;
			}

			consumed = true;
			break;

		case KEY_ENTER:

			if ( !multi_line ) return false;

			if ( text_length + 1 < edit_buffer_size )
			{
				memmove ( buffer + cursor_position + 1, buffer + cursor_position, text_length - cursor_position + 1 );
				buffer [ cursor_position ] = '\n';
				cursor_position++;
				text_length++;
			}

			consumed = true;
			break;

		case KEY_TAB:

			return false;

		default:

			// Printable characters (including the upper code page 437
			// range) insert or overwrite at the cursor.

			if ( ( key_event.ascii >= 32 ) && ( key_event.ascii != 127 ) )
			{
				if ( !insert_enabled && ( cursor_position < text_length ) && ( buffer [ cursor_position ] != '\n' ) )
				{
					buffer [ cursor_position ] = ( char ) key_event.ascii;
					cursor_position++;
				}
				else if ( text_length + 1 < edit_buffer_size )
				{
					memmove ( buffer + cursor_position + 1, buffer + cursor_position, text_length - cursor_position + 1 );
					buffer [ cursor_position ] = ( char ) key_event.ascii;
					cursor_position++;
					text_length++;
				}

				consumed = true;
			}

			break;
	}

	// On a consumed key, re-scroll to keep the cursor visible and notify listeners.

	if ( consumed )
	{
		TextBoxEnsureVisible ( buffer, cursor_position, &scroll_x, &scroll_y, width - 2, height - 2 );

		FireChange ();
	}

	// Report whether the key was consumed.

	return consumed;
}

//****************************************************************************
// Class: CheckBox
//****************************************************************************

//----------------------------------------------------------------------------
// Function: CheckBox::CheckBox
//
// Description:
//
//   Initializes a focusable two-state check box, rendered [X] / [ ]
//   followed by its text.
//
// Arguments:
//
//   - new_name : Component name.
//   - new_x    : Absolute column.
//   - new_y    : Absolute row.
//   - new_text : The check box caption.
//
// Returns:
//
//   - None
//
//----------------------------------------------------------------------------

CheckBox::CheckBox ( void ) : Component ()
{
	checked   = false;
	focusable = true;
}

CheckBox::CheckBox ( const char *new_name, int new_x, int new_y, const char *new_text ) : Component ( new_name, new_x, new_y, 0, 1 )
{
	checked   = false;
	focusable = true;

	SetText ( new_text );

	width = 4 + strlen ( text );
}

//----------------------------------------------------------------------------
// Function: CheckBox::Draw
//
// Description:
//
//   Renders the marker "[#]" / "[ ]" - the checked mark is the CP437
//   filled-square glyph (0xFE), not an 'X' - plus the caption; while
//   focused, the check box is highlighted white-on-navy (the shared
//   control-focus colour).
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

void CheckBox::Draw ( TEXTBUFFER *text_buffer )
{
	// Local variables.

	BYTE state_foreground;
	BYTE state_background;
	char marker [ 5 ];

	// Hidden check boxes draw nothing.

	if ( !visible ) return;

	// Highlight white-on-navy while focused; otherwise use the component colours.

	state_foreground = focused ? COLOR_WHITE : foreground;
	state_background = focused ? COLOR_BLUE  : background;

	// The checked mark is the CP437 filled-square glyph (0xFE), not an 'X'.
	// The trailing space keeps the focus highlight continuous across the gap
	// between the marker and the caption.

	marker [ 0 ] = '[';
	marker [ 1 ] = checked ? ( char ) 0xFE : ' ';
	marker [ 2 ] = ']';
	marker [ 3 ] = ' ';
	marker [ 4 ] = '\0';

	// Draw the marker and the caption.

	PutText ( text_buffer, x,     y, marker, state_foreground, state_background, 0, 0 );
	PutText ( text_buffer, x + 4, y, text,   state_foreground, state_background, 0, 0 );
}

//----------------------------------------------------------------------------
// Function: CheckBox::HandleKey
//
// Description:
//
//   Space or Enter toggles the checked state and fires OnChange.
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

bool CheckBox::HandleKey ( KEY_EVENT &key_event )
{
	// Space or Enter toggles the checked state and fires OnChange.

	if ( ( key_event.scan_code == KEY_SPACE ) || ( key_event.scan_code == KEY_ENTER ) )
	{
		checked = !checked;
		FireChange ();

		return true;
	}

	// Any other key bubbles to the parent.

	return false;
}

//****************************************************************************
// Class: RadioButton / RadioButtonGroup
//****************************************************************************

//----------------------------------------------------------------------------
// Function: RadioButton::RadioButton
//
// Description:
//
//   Initializes a focusable radio button, rendered (*) / ( ) followed by
//   its text. Selection is mutually exclusive within the owning
//   RadioButtonGroup.
//
// Arguments:
//
//   - new_name : Component name.
//   - new_x    : Absolute column.
//   - new_y    : Absolute row.
//   - new_text : The radio button caption.
//
// Returns:
//
//   - None
//
//----------------------------------------------------------------------------

RadioButton::RadioButton ( void ) : Component ()
{
	selected  = false;
	group     = ( RadioButtonGroup * ) 0;
	focusable = true;
}

RadioButton::RadioButton ( const char *new_name, int new_x, int new_y, const char *new_text ) : Component ( new_name, new_x, new_y, 0, 1 )
{
	selected  = false;
	group     = ( RadioButtonGroup * ) 0;
	focusable = true;

	SetText ( new_text );

	width = 4 + strlen ( text );
}

//----------------------------------------------------------------------------
// Function: RadioButton::Draw
//
// Description:
//
//   Renders the marker "(o)" / "( )" - the selected mark is the CP437
//   "ball" glyph (0x07), not an asterisk - plus the caption; while focused,
//   the radio button is highlighted white-on-navy (the shared control-focus
//   colour).
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

void RadioButton::Draw ( TEXTBUFFER *text_buffer )
{
	// Local variables.

	BYTE state_foreground;
	BYTE state_background;
	char marker [ 5 ];

	// Hidden radio buttons draw nothing.

	if ( !visible ) return;

	// Highlight white-on-navy while focused; otherwise use the component colours.

	state_foreground = focused ? COLOR_WHITE : foreground;
	state_background = focused ? COLOR_BLUE  : background;

	// The selected mark is the CP437 "ball" glyph (0x07), not an asterisk.
	// The trailing space keeps the focus highlight continuous across the gap
	// between the marker and the caption.

	marker [ 0 ] = '(';
	marker [ 1 ] = selected ? ( char ) 0x07 : ' ';
	marker [ 2 ] = ')';
	marker [ 3 ] = ' ';
	marker [ 4 ] = '\0';

	// Draw the marker and the caption.

	PutText ( text_buffer, x,     y, marker, state_foreground, state_background, 0, 0 );
	PutText ( text_buffer, x + 4, y, text,   state_foreground, state_background, 0, 0 );
}

//----------------------------------------------------------------------------
// Function: RadioButton::HandleKey
//
// Description:
//
//   Space or Enter selects this radio button through the owning group
//   (clearing its siblings) and fires OnChange.
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

bool RadioButton::HandleKey ( KEY_EVENT &key_event )
{
	// Space or Enter selects this radio button.

	if ( ( key_event.scan_code == KEY_SPACE ) || ( key_event.scan_code == KEY_ENTER ) )
	{
		// Select through the owning group (clearing its siblings), or stand alone when ungrouped.

		if ( group != ( RadioButtonGroup * ) 0 )
		{
			group->SelectRadioButton ( this );
		}
		else
		{
			selected = true;
		}

		// Notify listeners of the change.

		FireChange ();

		// The key was consumed.

		return true;
	}

	// Any other key bubbles to the parent.

	return false;
}

//----------------------------------------------------------------------------
// Function: RadioButtonGroup::RadioButtonGroup
//
// Description:
//
//   Initializes a borderless (transparent) container for a mutually
//   exclusive set of radio buttons. Only RadioButton children may be
//   added, via AddRadioButton.
//
// Arguments:
//
//   - new_name   : Component name.
//   - new_x      : Absolute column.
//   - new_y      : Absolute row.
//   - new_width  : Width in cells.
//   - new_height : Height in cells.
//
// Returns:
//
//   - None
//
//----------------------------------------------------------------------------

RadioButtonGroup::RadioButtonGroup ( void ) : Panel ()
{
	border_enabled = false;
}

RadioButtonGroup::RadioButtonGroup ( const char *new_name, int new_x, int new_y, int new_width, int new_height ) : Panel ( new_name, new_x, new_y, new_width, new_height )
{
	border_enabled = false;
}

//----------------------------------------------------------------------------
// Function: RadioButtonGroup::AddRadioButton / SelectRadioButton /
//           GetSelectedButton
//
// Description:
//
//   AddRadioButton adds a radio button as a child and binds it to this
//   group. SelectRadioButton selects the target and clears every sibling
//   (the mutual-exclusion rule). GetSelectedButton returns the selected
//   member, if any.
//
// Arguments:
//
//   - radio_button : The radio button to add / select.
//
// Returns:
//
//   - GetSelectedButton: the selected RadioButton, or NULL.
//
//----------------------------------------------------------------------------

void RadioButtonGroup::AddRadioButton ( RadioButton *radio_button )
{
	// Ignore a null button.

	if ( radio_button == ( RadioButton * ) 0 ) return;

	// Add it as a child of the group.

	AddComponent ( radio_button );

	// Bind it to this group for mutual exclusion.

	radio_button->SetGroup ( this );
}

void RadioButtonGroup::SelectRadioButton ( RadioButton *radio_button )
{
	// Local variables.

	RadioButton *member;
	int          i;

	// Select the target button and clear every sibling.

	for ( i = 0; i < component_count; i++ )
	{
		member = ( RadioButton * ) components [ i ];

		member->SetSelected ( member == radio_button );
	}
}

RadioButton *RadioButtonGroup::GetSelectedButton ( void )
{
	// Local variables.

	RadioButton *member;
	int          i;

	// Return the first selected member, if any.

	for ( i = 0; i < component_count; i++ )
	{
		member = ( RadioButton * ) components [ i ];

		if ( member->IsSelected () ) return member;
	}

	// None selected.

	return ( RadioButton * ) 0;
}

//****************************************************************************
// Class: Group
//****************************************************************************

//----------------------------------------------------------------------------
// Function: Group::Group
//
// Description:
//
//   Initializes a visual grouping frame: a bordered panel whose title
//   (the component text) is drawn on the top edge, with no behavior of
//   its own.
//
// Arguments:
//
//   - new_name   : Component name.
//   - new_x      : Absolute column.
//   - new_y      : Absolute row.
//   - new_width  : Width in cells.
//   - new_height : Height in cells.
//   - new_text   : The frame title.
//
// Returns:
//
//   - None
//
//----------------------------------------------------------------------------

Group::Group ( void ) : Panel ()
{
}

Group::Group ( const char *new_name, int new_x, int new_y, int new_width, int new_height, const char *new_text ) : Panel ( new_name, new_x, new_y, new_width, new_height )
{
	SetText ( new_text );
}

//****************************************************************************
// Class: ListItem / ListBox / DropDownTextBox
//****************************************************************************

//----------------------------------------------------------------------------
// Function: ListItem::ListItem / SetText
//
// Description:
//
//   Initializes an empty list entry; SetText stores the entry text
//   (bounded copy).
//
// Arguments:
//
//   - new_text : The entry text.
//
// Returns:
//
//   - None
//
//----------------------------------------------------------------------------

ListItem::ListItem ( void )
{
	text [ 0 ] = '\0';
	user_data  = NULL;
}

void ListItem::SetText ( const char *new_text )
{
	CopyBoundedText ( text, new_text, COMPONENT_TEXT_SIZE );
}

//----------------------------------------------------------------------------
// Function: ListBox::ListBox
//
// Description:
//
//   Initializes an empty, focusable, bordered scrolling list.
//
// Arguments:
//
//   - new_name   : Component name.
//   - new_x      : Absolute column.
//   - new_y      : Absolute row.
//   - new_width  : Width in cells, including the border.
//   - new_height : Height in cells, including the border.
//
// Returns:
//
//   - None
//
//----------------------------------------------------------------------------

ListBox::ListBox ( void ) : Component ()
{
	item_count                 = 0;
	top_index                  = 0;
	selected_index             = 0;
	border_foreground          = COLOR_DARK_GRAY;
	selected_border_foreground = COLOR_LIGHT_GRAY;
	focusable                  = true;

	foreground = COLOR_BLACK;
	background = COLOR_LIGHT_GRAY;
}

ListBox::ListBox ( const char *new_name, int new_x, int new_y, int new_width, int new_height ) : Component ( new_name, new_x, new_y, new_width, new_height )
{
	item_count                 = 0;
	top_index                  = 0;
	selected_index             = 0;
	border_foreground          = COLOR_DARK_GRAY;
	selected_border_foreground = COLOR_LIGHT_GRAY;
	focusable                  = true;

	foreground = COLOR_BLACK;
	background = COLOR_LIGHT_GRAY;
}

//----------------------------------------------------------------------------
// Function: ListBox::AddItem / ClearItems / SetSelectedIndex
//
// Description:
//
//   AddItem appends an entry (up to LIST_ITEM_CAPACITY); ClearItems
//   empties the list; SetSelectedIndex moves the selection (clamped) and
//   scrolls it into view.
//
// Arguments:
//
//   - item_text          : Entry text to append.
//   - user_data          : Opaque pointer stored with the entry.
//   - new_selected_index : The selection to move to.
//
// Returns:
//
//   - None
//
//----------------------------------------------------------------------------

void ListBox::AddItem ( const char *item_text, void *user_data )
{
	if ( item_count >= LIST_ITEM_CAPACITY ) return;

	items [ item_count ].SetText     ( item_text );
	items [ item_count ].SetUserData ( user_data );

	item_count++;
}

void ListBox::ClearItems ( void )
{
	item_count     = 0;
	top_index      = 0;
	selected_index = 0;
}

void ListBox::SetSelectedIndex ( int new_selected_index )
{
	// Local variables.

	int page_size;

	// An empty list has no selection.

	if ( item_count == 0 ) { selected_index = 0; top_index = 0; return; }

	// Adopt the requested selection.

	selected_index = new_selected_index;

	// Clamp the selection to the valid range.

	if ( selected_index < 0 )            selected_index = 0;
	if ( selected_index >= item_count )  selected_index = item_count - 1;

	// A page is the interior height (at least one row).

	page_size = height - 2;
	if ( page_size < 1 ) page_size = 1;

	// Scroll the selection into view.

	if ( selected_index < top_index )              top_index = selected_index;
	if ( selected_index >= top_index + page_size ) top_index = selected_index - page_size + 1;
}

//----------------------------------------------------------------------------
// Function: ListBox::Draw
//
// Description:
//
//   Draws the bordered list (border in selected_border_foreground while
//   focused), the visible window of entries from top_index, and the
//   selected entry as a white-on-navy highlight bar. Entry text is
//   clipped to the interior width.
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

void ListBox::Draw ( TEXTBUFFER *text_buffer )
{
	char clipped_text [ COMPONENT_TEXT_SIZE ];
	BYTE frame_foreground;
	int  page_size;
	int  interior_width;
	int  row;
	int  index;
	int  j;

	// Hidden lists draw nothing.

	if ( !visible ) return;

	// Border in the selected colour while focused.

	frame_foreground = focused ? selected_border_foreground : border_foreground;

	// Draw the bordered list body.

	PutAsciiRectangle ( text_buffer, x, y, width, height, frame_foreground, background, 0, 0, '-', '-', 1 );

	// Interior geometry.

	page_size      = height - 2;
	interior_width = width - 4;

	// Clip the interior width to the item-text capacity.

	if ( interior_width > COMPONENT_TEXT_SIZE - 1 ) interior_width = COMPONENT_TEXT_SIZE - 1;

	// Draw each visible row from the top of the scroll window.

	for ( row = 0; row < page_size; row++ )
	{
		// The item index for this row.

		index = top_index + row;

		// Stop past the last item.

		if ( index >= item_count ) break;

		// Clip the item text to the interior width.

		strncpy ( clipped_text, items [ index ].GetText (), interior_width );
		clipped_text [ interior_width ] = '\0';

		// Draw the selected item as a highlight bar, others as plain text.

		if ( index == selected_index )
		{
			for ( j = 1; j < width - 1; j++ )
			{
				PutCharacter ( text_buffer, x + j, y + 1 + row, ' ', COLOR_WHITE, COLOR_BLUE, 0, 0 );
			}

			PutText ( text_buffer, x + 2, y + 1 + row, clipped_text, COLOR_WHITE, COLOR_BLUE, 0, 0 );
		}
		else
		{
			PutText ( text_buffer, x + 2, y + 1 + row, clipped_text, foreground, background, 0, 0 );
		}
	}
}

//----------------------------------------------------------------------------
// Function: ListBox::HandleKey
//
// Description:
//
//   Up/Down move the selection, PgUp/PgDn move by a page, Home/End jump
//   to the first/last entry - all scrolled into view, each move firing
//   OnChange. Enter fires OnActivate on the selected entry.
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

bool ListBox::HandleKey ( KEY_EVENT &key_event )
{
	// Local variables.

	int page_size;

	// An empty list consumes nothing.

	if ( item_count == 0 ) return false;

	// A page is the interior height (at least one row).

	page_size = height - 2;
	if ( page_size < 1 ) page_size = 1;

	// Up/Down move one, PgUp/PgDn a page, Home/End to the ends; Enter activates the selection.

	switch ( key_event.scan_code )
	{
		case KEY_UP:    SetSelectedIndex ( selected_index - 1 );         FireChange (); return true;
		case KEY_DOWN:  SetSelectedIndex ( selected_index + 1 );         FireChange (); return true;
		case KEY_PGUP:  SetSelectedIndex ( selected_index - page_size ); FireChange (); return true;
		case KEY_PGDN:  SetSelectedIndex ( selected_index + page_size ); FireChange (); return true;
		case KEY_HOME:  SetSelectedIndex ( 0 );                          FireChange (); return true;
		case KEY_END:   SetSelectedIndex ( item_count - 1 );             FireChange (); return true;

		case KEY_ENTER:

			FireActivate ();
			return true;
	}

	// Any other key bubbles to the parent.

	return false;
}

//----------------------------------------------------------------------------
// Function: DropDownTextBox::DropDownTextBox / SetEntryText
//
// Description:
//
//   Initializes a focusable one-row entry field with an attached
//   drop-down list (populate it via GetDropDownList()->AddItem). Enter
//   or Down opens the drop-down under the field.
//
// Arguments:
//
//   - new_name             : Component name.
//   - new_x                : Absolute column.
//   - new_y                : Absolute row.
//   - new_width            : Field width in cells.
//   - new_drop_down_height : Maximum visible drop-down entries.
//
// Returns:
//
//   - None
//
//----------------------------------------------------------------------------

DropDownTextBox::DropDownTextBox ( void )
: Component (), drop_down_list ( "drop-down-list", 0, 0, 0, 0 )
{
	entry_text [ 0 ] = '\0';
	drop_down_open   = false;
	focusable        = true;

	foreground = COLOR_BLACK;
	background = COLOR_LIGHT_GRAY;
}

DropDownTextBox::DropDownTextBox ( const char *new_name, int new_x, int new_y, int new_width, int new_drop_down_height )
: Component ( new_name, new_x, new_y, new_width, 1 ), drop_down_list ( "drop-down-list", new_x, new_y + 1, new_width, new_drop_down_height + 2 )
{
	entry_text [ 0 ] = '\0';
	drop_down_open   = false;
	focusable        = true;

	foreground = COLOR_BLACK;
	background = COLOR_LIGHT_GRAY;
}

void DropDownTextBox::SetEntryText ( const char *new_entry_text )
{
	CopyBoundedText ( entry_text, new_entry_text, COMPONENT_TEXT_SIZE );
}

//----------------------------------------------------------------------------
// Function: DropDownTextBox::Draw
//
// Description:
//
//   Draws the one-row entry field (white-on-navy while focused) with a
//   drop marker on its right end.
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

void DropDownTextBox::Draw ( TEXTBUFFER *text_buffer )
{
	// Local variables.

	BYTE field_foreground;
	BYTE field_background;
	int  i;

	// Hidden fields draw nothing.

	if ( !visible ) return;

	// Highlight white-on-navy while focused; otherwise use the component colours.

	field_foreground = focused ? COLOR_WHITE : foreground;
	field_background = focused ? COLOR_BLUE  : background;

	// Fill the field row.

	for ( i = 0; i < width; i++ )
	{
		PutCharacter ( text_buffer, x + i, y, ' ', field_foreground, field_background, 0, 0 );
	}

	// Draw the entry text.

	PutText ( text_buffer, x + 1, y, entry_text, field_foreground, field_background, 0, 0 );

	// Drop marker: a down arrow on the right end of the field.

	PutCharacter ( text_buffer, x + width - 2, y, ( char ) 0x19, field_foreground, field_background, 0, 0 );
}

//----------------------------------------------------------------------------
// Function: DropDownTextBox::HandleKey
//
// Description:
//
//   Enter or Down opens the drop-down list under the field, following
//   the floating-element discipline (save the covered back-buffer
//   region, draw, restore on close) in its own modal key loop: Up/Down
//   move the list selection, Enter takes the selection into the entry
//   text and fires OnChange, Esc closes with no change.
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

bool DropDownTextBox::HandleKey ( KEY_EVENT &key_event )
{
	// Local variables.

	Application *application;
	TEXTBUFFER  *back_buffer;
	TEXTBUFFER   saved_block;
	WORD         key;
	BYTE         scan_code;
	int          i;

	// Only Enter or Down opens the drop-down.

	if ( ( key_event.scan_code != KEY_ENTER ) && ( key_event.scan_code != KEY_DOWN ) ) return false;

	// Resolve the running application.

	application = Application::GetInstance ();

	// Need an application and at least one entry.

	if ( application == NULL )                     return true;
	if ( drop_down_list.GetItemCount () == 0 )     return true;

	// The back buffer to composite the drop-down into.

	back_buffer = application->GetBackBuffer ();

	// Open the drop-down under the field: select the entry matching the
	// current text, save the covered region, and run the modal loop.

	drop_down_list.SetPosition ( x, y + 1 );

	// Pre-select the entry matching the current text.

	for ( i = 0; i < drop_down_list.GetItemCount (); i++ )
	{
		if ( strcmp ( drop_down_list.GetItem ( i )->GetText (), entry_text ) == 0 )
		{
			drop_down_list.SetSelectedIndex ( i );
			break;
		}
	}

	// Allocate a snapshot buffer for the region the drop-down and its shadow will cover.

	saved_block = CreateTextBuffer ( drop_down_list.GetWidth () + 2, drop_down_list.GetHeight () + 1, foreground, background );

	// Snapshot the covered region, then cast the shadow.

	GetTextBlock ( &saved_block, back_buffer, drop_down_list.GetX (), drop_down_list.GetY (), drop_down_list.GetWidth () + 2, drop_down_list.GetHeight () + 1 );
	PutShadow    ( back_buffer, drop_down_list.GetX () + 2, drop_down_list.GetY () + 1, drop_down_list.GetWidth (), drop_down_list.GetHeight (), 1, 1 );

	// Open the drop-down and give it focus.

	drop_down_open = true;
	drop_down_list.SetFocus ( true );

	// Modal key loop.

	for ( ;; )
	{
		// Draw the drop-down and present the frame.

		drop_down_list.Draw ( back_buffer );
		FlipScreenBuffer    ( back_buffer );

		// Read the next key.

		key       = ReadKey ();
		scan_code = ( BYTE ) ( key >> 8 );

		// Up/Down move the list selection.

		if ( scan_code == KEY_UP )    drop_down_list.SetSelectedIndex ( drop_down_list.GetSelectedIndex () - 1 );
		if ( scan_code == KEY_DOWN )  drop_down_list.SetSelectedIndex ( drop_down_list.GetSelectedIndex () + 1 );

		// Enter takes the selection into the entry text and notifies listeners.

		if ( scan_code == KEY_ENTER )
		{
			SetEntryText ( drop_down_list.GetSelectedItem ()->GetText () );
			FireChange ();
			break;
		}

		// Esc closes with no change.

		if ( scan_code == KEY_ESC ) break;
	}

	// Close the drop-down and blur it.

	drop_down_list.SetFocus ( false );
	drop_down_open = false;

	// Restore the covered region exactly.

	PutTextBlock      ( back_buffer, &saved_block, drop_down_list.GetX (), drop_down_list.GetY (), drop_down_list.GetWidth () + 2, drop_down_list.GetHeight () + 1 );
	DestroyTextBuffer ( &saved_block );
	FlipScreenBuffer  ( back_buffer );

	return true;
}

//****************************************************************************
// Class: MenuItem / Menu / MenuBar
//****************************************************************************

//----------------------------------------------------------------------------
// Function: MenuItem::MenuItem / SetShortcut
//
// Description:
//
//   Initializes a menu item: caption text, no sub-menu, not a separator,
//   and no shortcut label. Geometry is owned by the containing Menu.
//
//   The shortcut label is the application's own display string for the key
//   combination that reaches this item ("Ctrl+G", "Shift+Del"), following
//   the Turbo Vision TMenuItem precedent: the menu renders it right-aligned
//   in a shortcut column and dispatches nothing for it, leaving the key
//   handling entirely to the application. It defaults to empty, so existing
//   two-argument call sites stay source-compatible.
//
// Arguments:
//
//   - new_name     : Component name.
//   - new_text     : The item caption.
//   - new_shortcut : The display-only shortcut label, or "" for none.
//
// Returns:
//
//   - None
//
//----------------------------------------------------------------------------

MenuItem::MenuItem ( void ) : Component ()
{
	separator      = false;
	sub_menu       = ( Menu * ) 0;
	shortcut [ 0 ] = '\0';
}

MenuItem::MenuItem ( const char *new_name, const char *new_text, const char *new_shortcut ) : Component ( new_name, 0, 0, 0, 1 )
{
	separator      = false;
	sub_menu       = ( Menu * ) 0;
	shortcut [ 0 ] = '\0';

	SetText     ( new_text );
	SetShortcut ( new_shortcut );
}

void MenuItem::SetShortcut ( const char *new_shortcut )
{
	// A null label clears the shortcut; anything longer than the buffer is truncated.

	if ( new_shortcut == ( const char * ) 0 )
	{
		shortcut [ 0 ] = '\0';

		return;
	}

	strncpy ( shortcut, new_shortcut, MENU_SHORTCUT_SIZE - 1 );

	shortcut [ MENU_SHORTCUT_SIZE - 1 ] = '\0';
}

//----------------------------------------------------------------------------
// Function: Menu::Menu
//
// Description:
//
//   Initializes a closed, empty pop-up menu. The component text is the
//   menu's title on the menu bar. Default colors follow the application
//   frame convention: cyan items on a blue body, white when selected.
//
// Arguments:
//
//   - new_name : Component name.
//   - new_text : The menu title shown on the menu bar.
//
// Returns:
//
//   - None
//
//----------------------------------------------------------------------------

Menu::Menu ( void ) : Panel ()
{
	item_count     = 0;
	selected_index = 0;
	open           = false;

	saved_block.cells  = ( BYTE far * ) 0;
	saved_block.width  = 0;
	saved_block.height = 0;

	foreground        = COLOR_BLACK;
	background        = COLOR_LIGHT_GRAY;
	border_foreground = COLOR_DARK_GRAY;
}

Menu::Menu ( const char *new_name, const char *new_text ) : Panel ( new_name, 0, 0, 0, 0 )
{
	item_count     = 0;
	selected_index = 0;
	open           = false;

	saved_block.cells  = ( BYTE far * ) 0;
	saved_block.width  = 0;
	saved_block.height = 0;

	foreground        = COLOR_BLACK;
	background        = COLOR_LIGHT_GRAY;
	border_foreground = COLOR_DARK_GRAY;

	SetText ( new_text );
}

//----------------------------------------------------------------------------
// Function: Menu::AddItem
//
// Description:
//
//   Appends an item to the menu (up to LIST_ITEM_CAPACITY items).
//
// Arguments:
//
//   - item : The item to append.
//
// Returns:
//
//   - None
//
//----------------------------------------------------------------------------

void Menu::AddItem ( MenuItem *item )
{
	if ( item == ( MenuItem * ) 0 )        return;
	if ( item_count >= LIST_ITEM_CAPACITY ) return;

	items [ item_count ] = item;
	item_count++;
}

//----------------------------------------------------------------------------
// Function: Menu::Draw
//
// Description:
//
//   Draws the open menu body: a filled single-line box with the border
//   and separators in border_foreground, one row per item, separators as
//   full-width lines whose end caps union into the menu border, and the
//   selected item as a white-on-navy highlight bar.
//
//   An item carrying a shortcut label also draws it right-aligned in the
//   menu's shortcut column, one pad cell in from the right border and in
//   the same normal / highlight colours as the rest of its row (RunOpen
//   has already widened the drop-down to fit the column).
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

void Menu::Draw ( TEXTBUFFER *text_buffer )
{
	// Local variables.

	const char *shortcut;
	BYTE        row_foreground;
	BYTE        row_background;
	int         i;
	int         j;

	// Fill the menu body with a box-art border in the border colour.

	PutAsciiRectangle ( text_buffer, x, y, width, height, border_foreground, background, 0, 0, '-', '-', 1 );

	// Draw one row per item.

	for ( i = 0; i < item_count; i++ )
	{
		// A separator draws as a full-width line and carries no shortcut.

		if ( items [ i ]->IsSeparator () )
		{
			PutHorizontalAsciiLine ( text_buffer, x, y + 1 + i, width, border_foreground, background, 0, 0, '-', 1 );

			continue;
		}

		// The selected item reads white on navy, every other row in the menu's own colours.

		if ( i == selected_index )
		{
			row_foreground = COLOR_WHITE;
			row_background = COLOR_BLUE;

			// Selection highlight: a bar across the row, drawn before its text.

			for ( j = 1; j < width - 1; j++ )
			{
				PutCharacter ( text_buffer, x + j, y + 1 + i, ' ', row_foreground, row_background, 0, 0 );
			}
		}
		else
		{
			row_foreground = foreground;
			row_background = background;
		}

		PutText ( text_buffer, x + 2, y + 1 + i, items [ i ]->GetText (), row_foreground, row_background, 0, 0 );

		// An item's shortcut label renders right-aligned in the row's own
		// colours, one pad cell in from the right border. Display only - the
		// application owns the key that the label names.

		shortcut = items [ i ]->GetShortcut ();

		if ( shortcut [ 0 ] != '\0' )
		{
			PutText ( text_buffer, x + width - 2 - ( int ) strlen ( shortcut ), y + 1 + i, shortcut, row_foreground, row_background, 0, 0 );
		}
	}
}

//----------------------------------------------------------------------------
// Function: Menu::HandleKey
//
// Description:
//
//   Menus are driven by their modal RunOpen loop, not by focus dispatch.
//
// Arguments:
//
//   - key_event : The key event to handle.
//
// Returns:
//
//   - false always.
//
//----------------------------------------------------------------------------

bool Menu::HandleKey ( KEY_EVENT &key_event )
{
	( void ) key_event;

	return false;
}

//----------------------------------------------------------------------------
// Function: Menu::RunOpen
//
// Description:
//
//   Opens the menu at (open_x, open_y) and runs its modal key loop,
//   following the floating-element discipline: the covered back-buffer
//   region (body plus shadow) is saved with GetTextBlock, a PutShadow is
//   cast and the body drawn over it, and the region is restored with
//   PutTextBlock on close - so the background comes back exactly.
//
//   Navigation: Up/Down move the selection, skipping separators and
//   wrapping; Enter activates the selected item (or opens its sub-menu);
//   Right opens a sub-menu, or at the top level reports a switch to the
//   adjacent menu; Left reports a switch; Esc closes one level. An
//   activated item's OnActivate fires after the region is restored.
//
// Arguments:
//
//   - open_x : Screen column of the menu's top-left corner.
//   - open_y : Screen row of the menu's top-left corner.
//
// Returns:
//
//   - MENU_RESULT_CLOSED, MENU_RESULT_ACTIVATED, MENU_RESULT_SWITCH_LEFT,
//     or MENU_RESULT_SWITCH_RIGHT.
//
//----------------------------------------------------------------------------

int Menu::RunOpen ( int open_x, int open_y )
{
	Application *application;
	TEXTBUFFER  *back_buffer;
	MenuItem    *item;
	WORD         key;
	BYTE         scan_code;
	int          result;
	int          sub_menu_result;
	int          menu_width;
	int          item_length;
	int          longest_label;
	int          longest_shortcut;
	int          i;

	// Resolve the running application.

	application = Application::GetInstance ();

	// Nothing to open without an application or items.

	if ( application == NULL ) return MENU_RESULT_CLOSED;
	if ( item_count == 0 )     return MENU_RESULT_CLOSED;

	back_buffer = application->GetBackBuffer ();

	// Size the menu to its items and clamp it to the screen.

	menu_width = 12;

	// Size the menu to its widest item (label plus padding), noting the
	// widest shortcut label alongside it.

	longest_shortcut = 0;
	longest_label    = 0;

	for ( i = 0; i < item_count; i++ )
	{
		item_length = strlen ( items [ i ]->GetText () );

		if ( item_length > longest_label ) longest_label = item_length;

		if ( item_length + 4 > menu_width ) menu_width = item_length + 4;

		item_length = strlen ( items [ i ]->GetShortcut () );

		if ( item_length > longest_shortcut ) longest_shortcut = item_length;
	}

	// With a shortcut column present, widen the drop-down so the longest
	// label and the longest shortcut sit at least two spaces apart: two
	// cells of left border and pad, the label, the two-space gap, the
	// shortcut, then one pad cell and the right border.

	if ( longest_shortcut > 0 )
	{
		item_length = longest_label + longest_shortcut + 6;

		if ( item_length > menu_width ) menu_width = item_length;
	}

	// Place the menu at the requested position.

	x      = open_x;
	y      = open_y;
	width  = menu_width;
	height = item_count + 2;

	// Clamp the menu so its body and shadow stay on-screen.

	if ( x + width + 2 > back_buffer->width )   x = back_buffer->width - width - 2;
	if ( y + height + 1 > back_buffer->height ) y = back_buffer->height - height - 1;
	if ( x < 0 ) x = 0;
	if ( y < 0 ) y = 0;

	// Floating-element discipline: save the covered region (body plus
	// shadow), cast the shadow, then draw the body over it.

	saved_block = CreateTextBuffer ( width + 2, height + 1, foreground, background );

	GetTextBlock ( &saved_block, back_buffer, x, y, width + 2, height + 1 );
	PutShadow    ( back_buffer, x + 2, y + 1, width, height, 1, 1 );

	// Select the first non-separator item.

	selected_index = 0;

	while ( ( selected_index < item_count - 1 ) && items [ selected_index ]->IsSeparator () ) selected_index++;

	// Open the menu; default to a closed result.

	open   = true;
	result = MENU_RESULT_CLOSED;

	// Modal key loop.

	for ( ;; )
	{
		// Draw the menu and present the frame.

		Draw ( back_buffer );
		FlipScreenBuffer ( back_buffer );

		// Read the next key.

		key       = ReadKey ();
		scan_code = ( BYTE ) ( key >> 8 );

		// Up/Down move the selection (skipping separators); Enter activates or opens a sub-menu; Left/Right switch; Esc closes.

		if ( scan_code == KEY_UP )
		{
			do { selected_index = ( selected_index + item_count - 1 ) % item_count; }
			while ( items [ selected_index ]->IsSeparator () );
		}
		else if ( scan_code == KEY_DOWN )
		{
			do { selected_index = ( selected_index + 1 ) % item_count; }
			while ( items [ selected_index ]->IsSeparator () );
		}
		else if ( ( scan_code == KEY_ENTER ) || ( scan_code == KEY_RIGHT ) )
		{
			item = items [ selected_index ];

			if ( item->GetSubMenu () != ( Menu * ) 0 )
			{
				// Nested sub-menu: open it beside the selected item.

				sub_menu_result = item->GetSubMenu ()->RunOpen ( x + width - 1, y + 1 + selected_index );

				if ( sub_menu_result == MENU_RESULT_ACTIVATED ) { result = MENU_RESULT_ACTIVATED; break; }
			}
			else if ( scan_code == KEY_ENTER )
			{
				result = MENU_RESULT_ACTIVATED;
				break;
			}
			else
			{
				result = MENU_RESULT_SWITCH_RIGHT;
				break;
			}
		}
		else if ( scan_code == KEY_LEFT )
		{
			result = MENU_RESULT_SWITCH_LEFT;
			break;
		}
		else if ( ( scan_code == KEY_ESC ) || ( scan_code == KEY_F10 ) )
		{
			result = MENU_RESULT_CLOSED;
			break;
		}
	}

	// Restore the covered region exactly, then fire the activation.

	PutTextBlock      ( back_buffer, &saved_block, x, y, width + 2, height + 1 );
	DestroyTextBuffer ( &saved_block );
	FlipScreenBuffer  ( back_buffer );

	// The menu is closed.

	open = false;

	// Fire the activated item's handler after the background is restored.

	if ( result == MENU_RESULT_ACTIVATED ) items [ selected_index ]->FireActivate ();

	// Return the loop result.

	return result;
}

//----------------------------------------------------------------------------
// Function: MenuBar::MenuBar
//
// Description:
//
//   Initializes an empty, inactive menu bar. Default colors follow the
//   application-frame convention: cyan titles on a blue bar, white when selected.
//
// Arguments:
//
//   - new_name  : Component name.
//   - new_x     : Absolute column.
//   - new_y     : Absolute row.
//   - new_width : Bar width in cells.
//
// Returns:
//
//   - None
//
//----------------------------------------------------------------------------

MenuBar::MenuBar ( void ) : Panel ()
{
	menu_count     = 0;
	selected_index = 0;
	active         = false;
	border_enabled = false;

	foreground = COLOR_BLACK;
	background = COLOR_LIGHT_GRAY;
}

MenuBar::MenuBar ( const char *new_name, int new_x, int new_y, int new_width ) : Panel ( new_name, new_x, new_y, new_width, 1 )
{
	menu_count     = 0;
	selected_index = 0;
	active         = false;
	border_enabled = false;

	foreground = COLOR_BLACK;
	background = COLOR_LIGHT_GRAY;
}

//----------------------------------------------------------------------------
// Function: MenuBar::AddMenu / SetActive / GetMenuTitleX
//
// Description:
//
//   AddMenu appends a menu's title to the bar. SetActive marks whether
//   the bar owns the keyboard (title highlighting). GetMenuTitleX
//   returns the screen column where a menu's title is drawn.
//
// Arguments:
//
//   - menu / new_active / menu_index : As named.
//
// Returns:
//
//   - GetMenuTitleX: the title's screen column.
//
//----------------------------------------------------------------------------

void MenuBar::AddMenu ( Menu *menu )
{
	if ( menu == ( Menu * ) 0 )                    return;
	if ( menu_count >= COMPONENT_CHILD_CAPACITY )  return;

	menus [ menu_count ] = menu;
	menu_count++;
}

void MenuBar::SetActive ( bool new_active )
{
	active = new_active;

	if ( active ) selected_index = 0;
}

int MenuBar::GetMenuTitleX ( int menu_index )
{
	int title_x;
	int i;

	title_x = x + 2;

	for ( i = 0; i < menu_index; i++ )
	{
		title_x += strlen ( menus [ i ]->GetText () ) + 4;
	}

	// Return the title's screen column.

	return title_x;
}

//----------------------------------------------------------------------------
// Function: MenuBar::Draw
//
// Description:
//
//   Fills the bar row and draws the menu titles; while the bar is
//   active, the selected title is highlighted as a white-on-navy bar
//   (with one highlighted padding cell either side).
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

void MenuBar::Draw ( TEXTBUFFER *text_buffer )
{
	// Local variables.

	int title_x;
	int title_length;
	int i;

	// Hidden menu bars draw nothing.

	if ( !visible ) return;

	// Fill the bar row in the bar colours.

	for ( i = 0; i < width; i++ )
	{
		PutCharacter ( text_buffer, x + i, y, ' ', foreground, background, 0, 0 );
	}

	// Draw each menu title.

	for ( i = 0; i < menu_count; i++ )
	{
		// The screen column where this title is drawn.

		title_x = GetMenuTitleX ( i );

		// Highlight the selected title white-on-navy while the bar is active; otherwise draw it plain.

		if ( active && ( i == selected_index ) )
		{
			title_length = strlen ( menus [ i ]->GetText () );

			PutCharacter ( text_buffer, title_x - 1,            y, ' ', COLOR_WHITE, COLOR_BLUE, 0, 0 );
			PutText      ( text_buffer, title_x,                y, menus [ i ]->GetText (), COLOR_WHITE, COLOR_BLUE, 0, 0 );
			PutCharacter ( text_buffer, title_x + title_length, y, ' ', COLOR_WHITE, COLOR_BLUE, 0, 0 );
		}
		else
		{
			PutText ( text_buffer, title_x, y, menus [ i ]->GetText (), foreground, background, 0, 0 );
		}
	}
}

//----------------------------------------------------------------------------
// Function: MenuBar::HandleKey
//
// Description:
//
//   The bar is driven by its modal Activate loop, not by focus dispatch.
//
// Arguments:
//
//   - key_event : The key event to handle.
//
// Returns:
//
//   - false always.
//
//----------------------------------------------------------------------------

bool MenuBar::HandleKey ( KEY_EVENT &key_event )
{
	// The bar is driven by its modal Activate loop, not by focus dispatch.

	( void ) key_event;

	// Never consumed here.

	return false;
}

//----------------------------------------------------------------------------
// Function: MenuBar::Activate
//
// Description:
//
//   The modal menu-bar loop, entered on F10: Left/Right move the title
//   selection, Enter or Down opens the selected menu (whose own Left /
//   Right switches re-open the adjacent menu), and Esc or F10 leaves the
//   bar, returning the keyboard to whatever held focus before.
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

void MenuBar::Activate ( void )
{
	// Local variables.

	Application *application;
	WORD         key;
	BYTE         scan_code;
	int          result;
	bool         open_selected;

	// Resolve the running application.

	application = Application::GetInstance ();

	// Nothing to activate without an application or menus.

	if ( application == NULL ) return;
	if ( menu_count == 0 )     return;

	// Enter the bar: mark it active and select the first menu.

	active         = true;
	selected_index = 0;
	open_selected  = false;

	// Modal bar loop.

	for ( ;; )
	{
		// Repaint the frame (showing the active-bar highlight).

		application->RenderFrame ();

		// Open the selected menu when a request is pending.

		if ( open_selected )
		{
			// Clear the pending open request.

			open_selected = false;

			// Open the menu beneath its title.

			result = menus [ selected_index ]->RunOpen ( GetMenuTitleX ( selected_index ) - 1, y + 1 );

			// An activated item leaves the bar.

			if ( result == MENU_RESULT_ACTIVATED ) break;

			// A left switch re-opens the previous menu.

			if ( result == MENU_RESULT_SWITCH_LEFT )
			{
				selected_index = ( selected_index + menu_count - 1 ) % menu_count;
				open_selected  = true;
			}

			// A right switch re-opens the next menu.

			if ( result == MENU_RESULT_SWITCH_RIGHT )
			{
				selected_index = ( selected_index + 1 ) % menu_count;
				open_selected  = true;
			}

			// Restart the loop to open the newly selected menu.

			continue;
		}

		// Read the next key.

		key       = ReadKey ();
		scan_code = ( BYTE ) ( key >> 8 );

		// Left/Right move the selection, Enter/Down open the menu, Esc/F10 leave the bar.

		if ( scan_code == KEY_LEFT )
		{
			selected_index = ( selected_index + menu_count - 1 ) % menu_count;
		}
		else if ( scan_code == KEY_RIGHT )
		{
			selected_index = ( selected_index + 1 ) % menu_count;
		}
		else if ( ( scan_code == KEY_ENTER ) || ( scan_code == KEY_DOWN ) )
		{
			open_selected = true;
		}
		else if ( ( scan_code == KEY_ESC ) || ( scan_code == KEY_F10 ) )
		{
			break;
		}
	}

	// Leave the bar.

	active = false;
}

//****************************************************************************
// Class: TitleBar / StatusBar
//****************************************************************************

//----------------------------------------------------------------------------
// Function: TitleBar::TitleBar / SetVersionText
//
// Description:
//
//   Initializes a one-row title bar: left-aligned title (the component
//   text, updatable at run time) and a right-aligned version string, in
//   white on light blue by default.
//
// Arguments:
//
//   - new_name         : Component name.
//   - new_x            : Absolute column.
//   - new_y            : Absolute row.
//   - new_width        : Bar width in cells.
//   - new_text         : The title text.
//   - new_version_text : The right-aligned version string.
//
// Returns:
//
//   - None
//
//----------------------------------------------------------------------------

TitleBar::TitleBar ( void ) : Component ()
{
	version_text [ 0 ] = '\0';

	foreground = COLOR_WHITE;
	background = COLOR_BLUE;
}

TitleBar::TitleBar ( const char *new_name, int new_x, int new_y, int new_width, const char *new_text, const char *new_version_text ) : Component ( new_name, new_x, new_y, new_width, 1 )
{
	version_text [ 0 ] = '\0';

	foreground = COLOR_WHITE;
	background = COLOR_BLUE;

	SetText ( new_text );

	CopyBoundedText ( version_text, new_version_text, COMPONENT_TEXT_SIZE );
}

void TitleBar::SetVersionText ( const char *new_version_text )
{
	CopyBoundedText ( version_text, new_version_text, COMPONENT_TEXT_SIZE );
}

//----------------------------------------------------------------------------
// Function: TitleBar::Draw
//
// Description:
//
//   Fills the bar row, draws the title left-aligned and the version
//   string right-aligned.
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

void TitleBar::Draw ( TEXTBUFFER *text_buffer )
{
	// Local variables.

	int i;

	// Hidden title bars draw nothing.

	if ( !visible ) return;

	// Fill the bar row in the title colours.

	for ( i = 0; i < width; i++ )
	{
		PutCharacter ( text_buffer, x + i, y, ' ', foreground, background, 0, 0 );
	}
	// Draw the title left-aligned and the version string right-aligned.

	PutText ( text_buffer, x + 1, y, text, foreground, background, 0, 0 );
	PutText ( text_buffer, x + width - ( int ) strlen ( version_text ) - 1, y, version_text, foreground, background, 0, 0 );
}

//----------------------------------------------------------------------------
// Function: StatusBar::StatusBar / SetField / SetValue / SetFieldHighlight
//
// Description:
//
//   Initializes an empty status grid (STATUS_BAR_ROW_COUNT rows by
//   STATUS_BAR_COLUMN_COUNT label/value columns), black on cyan by
//   default, with no highlight spans. SetField sets a field's label and
//   value; SetValue updates the value only. SetFieldHighlight marks a
//   span of a field's rendered text to draw in the bar's highlight
//   foreground (zero length disables).
//
// Arguments:
//
//   - new_name / new_x / new_y / new_width : Geometry, as named.
//   - row / column                         : Field cell in the grid.
//   - label_text / value_text              : Field content.
//   - start / length                       : Highlight span within the
//                                            field's rendered text.
//
// Returns:
//
//   - None
//
//----------------------------------------------------------------------------

StatusBar::StatusBar ( void ) : Component ()
{
	// Local variables.

	int r;
	int c;

	// Clear every field label, value, and highlight span.

	for ( r = 0; r < STATUS_BAR_ROW_COUNT; r++ )
	{
		for ( c = 0; c < STATUS_BAR_COLUMN_COUNT; c++ )
		{
			field_labels     [ r ][ c ][ 0 ] = '\0';
			field_values     [ r ][ c ][ 0 ] = '\0';
			highlight_start  [ r ][ c ]      = 0;
			highlight_length [ r ][ c ]      = 0;
		}
	}

	// Default field positions: uniform columns across the bar.

	for ( c = 0; c < STATUS_BAR_COLUMN_COUNT; c++ )
	{
		column_positions [ c ] = 1 + c*( ( width > 0 ? width : 80 ) / STATUS_BAR_COLUMN_COUNT );
	}

	// Default colours: black on light gray, with no distinct value colour.

	foreground                = COLOR_BLACK;
	background                = COLOR_LIGHT_GRAY;
	highlight_foreground      = COLOR_BLACK;
	value_foreground          = COLOR_BLACK;
	value_foreground_enabled  = false;
}

StatusBar::StatusBar ( const char *new_name, int new_x, int new_y, int new_width ) : Component ( new_name, new_x, new_y, new_width, STATUS_BAR_ROW_COUNT )
{
	// Local variables.

	int r;
	int c;

	// Clear every field label, value, and highlight span.

	for ( r = 0; r < STATUS_BAR_ROW_COUNT; r++ )
	{
		for ( c = 0; c < STATUS_BAR_COLUMN_COUNT; c++ )
		{
			field_labels     [ r ][ c ][ 0 ] = '\0';
			field_values     [ r ][ c ][ 0 ] = '\0';
			highlight_start  [ r ][ c ]      = 0;
			highlight_length [ r ][ c ]      = 0;
		}
	}

	// Default field positions: uniform columns across the bar.

	for ( c = 0; c < STATUS_BAR_COLUMN_COUNT; c++ )
	{
		column_positions [ c ] = 1 + c*( ( width > 0 ? width : 80 ) / STATUS_BAR_COLUMN_COUNT );
	}

	// Default colours: black on light gray, with no distinct value colour.

	foreground                = COLOR_BLACK;
	background                = COLOR_LIGHT_GRAY;
	highlight_foreground      = COLOR_BLACK;
	value_foreground          = COLOR_BLACK;
	value_foreground_enabled  = false;
}

void StatusBar::SetField ( int row, int column, const char *label_text, const char *value_text )
{
	// Ignore out-of-range cells.

	if ( ( row    < 0 ) || ( row    >= STATUS_BAR_ROW_COUNT    ) ) return;
	if ( ( column < 0 ) || ( column >= STATUS_BAR_COLUMN_COUNT ) ) return;

	// Store the field's label and value (bounded copies).

	CopyBoundedText ( field_labels [ row ][ column ], label_text, STATUS_BAR_FIELD_SIZE );
	CopyBoundedText ( field_values [ row ][ column ], value_text, STATUS_BAR_FIELD_SIZE );
}

void StatusBar::SetValue ( int row, int column, const char *value_text )
{
	// Ignore out-of-range cells.

	if ( ( row    < 0 ) || ( row    >= STATUS_BAR_ROW_COUNT    ) ) return;
	if ( ( column < 0 ) || ( column >= STATUS_BAR_COLUMN_COUNT ) ) return;

	// Update the field's value only.

	CopyBoundedText ( field_values [ row ][ column ], value_text, STATUS_BAR_FIELD_SIZE );
}

void StatusBar::SetColumnPosition ( int column, int x_offset )
{
	// Ignore an out-of-range column.

	if ( ( column < 0 ) || ( column >= STATUS_BAR_COLUMN_COUNT ) ) return;

	// Store the column's x offset.

	column_positions [ column ] = x_offset;
}

void StatusBar::SetFieldHighlight ( int row, int column, int start, int length )
{
	// Ignore out-of-range cells.

	if ( ( row    < 0 ) || ( row    >= STATUS_BAR_ROW_COUNT    ) ) return;
	if ( ( column < 0 ) || ( column >= STATUS_BAR_COLUMN_COUNT ) ) return;

	// Store the highlight span for the field.

	highlight_start  [ row ][ column ] = start;
	highlight_length [ row ][ column ] = length;
}

//----------------------------------------------------------------------------
// Function: StatusBar::Draw
//
// Description:
//
//   Fills the status rows and draws each non-empty field as label
//   followed by value, at each column's configured x offset (uniform
//   columns by default). When a distinct value foreground is set, the
//   value portion of each field is re-drawn in it so labels and values
//   differ in colour. A field with a highlight span re-draws that span
//   of its text in the highlight foreground (e.g. the INS of an [INS]
//   indicator), clipped to the text's length.
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

void StatusBar::Draw ( TEXTBUFFER *text_buffer )
{
	char field_text [ STATUS_BAR_FIELD_SIZE * 2 ];
	int  field_length;
	int  span_start;
	int  span_end;
	int  r;
	int  c;
	int  i;

	// Hidden status bars draw nothing.

	if ( !visible ) return;

	// Draw each status row.

	for ( r = 0; r < STATUS_BAR_ROW_COUNT; r++ )
	{
		// Fill the row in the bar colours.

		for ( i = 0; i < width; i++ )
		{
			PutCharacter ( text_buffer, x + i, y + r, ' ', foreground, background, 0, 0 );
		}

		// Draw each non-empty field.

		for ( c = 0; c < STATUS_BAR_COLUMN_COUNT; c++ )
		{
			// Skip a field with neither a label nor a value.

			if ( ( field_labels [ r ][ c ][ 0 ] == '\0' ) && ( field_values [ r ][ c ][ 0 ] == '\0' ) ) continue;

			// Compose the field text as label followed by value.

			strcpy ( field_text, field_labels [ r ][ c ] );
			strcat ( field_text, field_values [ r ][ c ] );

			// Draw the field at its column offset.

			PutText ( text_buffer, x + column_positions [ c ], y + r, field_text, foreground, background, 0, 0 );

			// The value portion, re-drawn in the value foreground when a
			// distinct value colour is set (labels stay in the field
			// foreground). The value begins one column past the label.

			if ( value_foreground_enabled )
			{
				span_start = strlen ( field_labels [ r ][ c ] );
				span_end   = span_start + strlen ( field_values [ r ][ c ] );

				for ( i = span_start; i < span_end; i++ )
				{
					PutCharacter ( text_buffer, x + column_positions [ c ] + i, y + r, field_text [ i ],
					               value_foreground, background, 0, 0 );
				}
			}

			// The highlight span, re-drawn over the field in the
			// highlight foreground.

			if ( highlight_length [ r ][ c ] > 0 )
			{
				// Compute the highlight span within the field text.

				field_length = strlen ( field_text );
				span_start   = highlight_start [ r ][ c ];
				span_end     = span_start + highlight_length [ r ][ c ];

				// Clamp the span to the field text.

				if ( span_start < 0 )            span_start = 0;
				if ( span_end   > field_length ) span_end   = field_length;

				// Re-draw the span in the highlight foreground.

				for ( i = span_start; i < span_end; i++ )
				{
					PutCharacter ( text_buffer, x + column_positions [ c ] + i, y + r, field_text [ i ],
					               highlight_foreground, background, 0, 0 );
				}
			}
		}
	}
}

//****************************************************************************
// Class: ApplicationPanel
//****************************************************************************

//----------------------------------------------------------------------------
// Function: ApplicationPanel::ApplicationPanel
//
// Description:
//
//   Assembles the standard application frame for an 80-column screen of the given
//   row count: TitleBar on row 0, MenuBar on row 1, and StatusBar on the
//   last two rows, with the workspace area between them. The panel
//   itself is a borderless (transparent) root container.
//
// Arguments:
//
//   - new_name : Component name.
//   - new_rows : Screen rows (25 / 43 / 50).
//
// Returns:
//
//   - None
//
//----------------------------------------------------------------------------

ApplicationPanel::ApplicationPanel ( void )
: Panel (),
  title_bar  (),
  menu_bar   (),
  status_bar ()
{
	workspace      = ( Component * ) 0;
	border_enabled = false;
}

ApplicationPanel::ApplicationPanel ( const char *new_name, int new_rows )
: Panel      ( new_name, 0, 0, 80, new_rows              ),
  title_bar  ( "title-bar",  0, 0,            80, "", "" ),
  menu_bar   ( "menu-bar",   0, 1,            80         ),
  status_bar ( "status-bar", 0, new_rows - 2, 80         )
{
	workspace      = ( Component * ) 0;
	border_enabled = false;

	AddComponent ( &title_bar );
	AddComponent ( &menu_bar );
	AddComponent ( &status_bar );
}

//----------------------------------------------------------------------------
// Function: ApplicationPanel::SetWorkspace
//
// Description:
//
//   Installs the application's main component into the workspace area
//   (it is added as a child and remembered as the workspace).
//
// Arguments:
//
//   - new_workspace : The component to install.
//
// Returns:
//
//   - None
//
//----------------------------------------------------------------------------

void ApplicationPanel::SetWorkspace ( Component *new_workspace )
{
	// Remember the workspace component.

	workspace = new_workspace;

	// Add it as a child so it draws with the panel.

	AddComponent ( new_workspace );
}

//----------------------------------------------------------------------------
// Function: ApplicationPanel::Relayout
//
// Description:
//
//   Re-places the application frame for a new screen row count (a live Settings row
//   switch): the title bar (row 0) and menu bar (row 1) are fixed, the
//   status bar drops to the last two rows, and the workspace fills the
//   rows between the top bars and the status bar (rows 3 .. rows-4, a
//   spacer above and below). The caller has already re-set the text mode
//   and rebuilt the back buffer (Application::SetRows).
//
// Arguments:
//
//   - new_rows : The new screen row count (25 / 43 / 50).
//
// Returns:
//
//   - None
//
//----------------------------------------------------------------------------

void ApplicationPanel::Relayout ( int new_rows )
{
	// Adopt the new screen height.

	height = new_rows;

	// Drop the status bar to the last two rows.

	status_bar.SetPosition ( 0, new_rows - 2 );

	// Fit the workspace between the top bars and the status bar (a spacer row above and below).

	if ( workspace != NULL )
	{
		workspace->SetPosition ( 0, 3 );
		workspace->SetSize     ( 80, new_rows - 6 );
	}
}

//----------------------------------------------------------------------------
// Function: ApplicationPanel::Draw
//
// Description:
//
//   Draws the application frame and workspace through the container base (the panel
//   is borderless, so only the children draw).
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

void ApplicationPanel::Draw ( TEXTBUFFER *text_buffer )
{
	Panel::Draw ( text_buffer );
}

//----------------------------------------------------------------------------
// Function: ApplicationPanel::HandleKey
//
// Description:
//
//   Owns the application frame's global key: F10 enters the modal menu-bar loop
//   (and the loop's own F10/Esc leaves it, returning the keyboard to
//   whatever held focus).
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

bool ApplicationPanel::HandleKey ( KEY_EVENT &key_event )
{
	// F10 enters the modal menu-bar loop.

	if ( key_event.scan_code == KEY_F10 )
	{
		menu_bar.Activate ();

		return true;
	}

	// Any other key bubbles to the focus chain.

	return false;
}

//****************************************************************************
// Class: Application
//****************************************************************************

Application *Application::instance = NULL;

#define FOCUS_LIST_CAPACITY  64

//----------------------------------------------------------------------------
// Function: CollectFocusableComponents
//
// Description:
//
//   Depth-first walk of the component tree (draw order), appending every
//   visible, enabled, focusable component to the list - the traversal
//   order used by Tab and the focus-moving arrow keys.
//
// Arguments:
//
//   - component : The subtree root to walk.
//   - list      : The output list (FOCUS_LIST_CAPACITY entries).
//   - count     : In/out entry count.
//
// Returns:
//
//   - None
//
//----------------------------------------------------------------------------

static void CollectFocusableComponents ( Component *component, Component **list, int *count )
{
	// Local variables.

	int i;

	// Skip a null, hidden, or disabled subtree.

	if ( component == NULL )        return;
	if ( !component->IsVisible () ) return;
	if ( !component->IsEnabled () ) return;

	// Append this component when it is focusable and the list has room.

	if ( component->IsFocusable () && ( *count < FOCUS_LIST_CAPACITY ) )
	{
		list [ *count ] = component;
		( *count )++;
	}

	// Recurse into the children in draw order.

	for ( i = 0; i < component->GetChildCount (); i++ )
	{
		CollectFocusableComponents ( component->GetChild ( i ), list, count );
	}
}

//****************************************************************************
// Class: Dialog and the Modal Dialogs
//****************************************************************************

//----------------------------------------------------------------------------
// Function: DialogCloseOkHandler / DialogCloseCancelHandler
//
// Description:
//
//   Shared button handlers for the standard dialogs: close the owning
//   dialog (passed as user_data) with Ok/Yes or Cancel/No.
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

static void DialogCloseOkHandler ( Component *sender, void *user_data )
{
	( void ) sender;

	( ( Dialog * ) user_data )->Close ( DIALOG_RESULT_OK );
}

static void DialogCloseCancelHandler ( Component *sender, void *user_data )
{
	( void ) sender;

	( ( Dialog * ) user_data )->Close ( DIALOG_RESULT_CANCEL );
}

//----------------------------------------------------------------------------
// Function: MoveDialogFocus
//
// Description:
//
//   Moves the dialog-local keyboard focus to the next or previous
//   focusable component within the dialog subtree, wrapping at the ends
//   and firing the focus/blur transitions.
//
// Arguments:
//
//   - dialog        : The dialog whose subtree is traversed.
//   - focused_child : In/out: the currently focused child.
//   - direction     : Positive for next, negative for previous.
//
// Returns:
//
//   - None
//
//----------------------------------------------------------------------------

static void MoveDialogFocus ( Dialog *dialog, Component **focused_child, int direction )
{
	// Local variables.

	Component *list [ FOCUS_LIST_CAPACITY ];
	int        count;
	int        index;
	int        i;

	// Collect the dialog's focusable components in tree order.

	count = 0;
	CollectFocusableComponents ( dialog, list, &count );

	// Nothing to move to.

	if ( count == 0 ) return;

	// Assume the focused child is not in the list.

	index = -1;

	// Locate the currently focused child in the list.

	for ( i = 0; i < count; i++ )
	{
		if ( list [ i ] == *focused_child ) { index = i; break; }
	}

	// Step to the next or previous entry, wrapping at the ends.

	if ( direction > 0 ) index = ( index + 1 ) % count;
	else                 index = ( index <= 0 ) ? count - 1 : index - 1;

	// Blur the outgoing child.

	if ( *focused_child != NULL ) ( *focused_child )->SetFocus ( false );

	// Adopt the new focused child.

	*focused_child = list [ index ];

	// Focus the incoming child.

	( *focused_child )->SetFocus ( true );
}

//----------------------------------------------------------------------------
// Function: Dialog::Dialog / Close
//
// Description:
//
//   Initializes a modal dialog base: a bordered, filled panel (black on
//   light gray) whose title is the component text. Close records the
//   result and ends the RunModal loop.
//
// Arguments:
//
//   - new_name   : Component name.
//   - new_x      : Absolute column.
//   - new_y      : Absolute row.
//   - new_width  : Width in cells.
//   - new_height : Height in cells.
//   - new_text   : The dialog title.
//   - new_result : The result code RunModal returns.
//
// Returns:
//
//   - None
//
//----------------------------------------------------------------------------

Dialog::Dialog ( void ) : Panel ()
{
	result = DIALOG_RESULT_CANCEL;
	closed = false;

	saved_block.cells  = ( BYTE far * ) 0;
	saved_block.width  = 0;
	saved_block.height = 0;

	foreground = COLOR_BLACK;
	background = COLOR_LIGHT_GRAY;
}

Dialog::Dialog ( const char *new_name, int new_x, int new_y, int new_width, int new_height, const char *new_text ) : Panel ( new_name, new_x, new_y, new_width, new_height )
{
	result = DIALOG_RESULT_CANCEL;
	closed = false;

	saved_block.cells  = ( BYTE far * ) 0;
	saved_block.width  = 0;
	saved_block.height = 0;

	foreground = COLOR_BLACK;
	background = COLOR_LIGHT_GRAY;

	SetText ( new_text );
}

void Dialog::Close ( int new_result )
{
	result = new_result;
	closed = true;
}

//----------------------------------------------------------------------------
// Function: Dialog::HandleKey
//
// Description:
//
//   The base modal key behavior: Esc closes the dialog as cancel / No.
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

bool Dialog::HandleKey ( KEY_EVENT &key_event )
{
	// Esc closes the dialog as cancel / No.

	if ( key_event.scan_code == KEY_ESC )
	{
		Close ( DIALOG_RESULT_CANCEL );

		return true;
	}

	// Any other key bubbles to the dialog's child handling.

	return false;
}

//----------------------------------------------------------------------------
// Function: Dialog::RunModal
//
// Description:
//
//   Runs the dialog modally, following the floating-element discipline:
//   the covered back-buffer region (body plus shadow) is saved with
//   GetTextBlock, a PutShadow is cast and the body drawn over it, and
//   the region is restored with PutTextBlock on close. The dialog owns
//   its own key loop and focus: keys route to the focused dialog child
//   and bubble up to the dialog itself (whose base behavior maps Esc to
//   cancel / No); unconsumed Tab / Shift+Tab and arrow keys cycle the
//   focus among the dialog's children. The dialog registers itself as
//   the application's modal dialog so any frame rendered mid-loop (e.g.
//   by a Button press) re-composites it on top.
//
// Arguments:
//
//   - application : The running application.
//
// Returns:
//
//   - The result code passed to Close (DIALOG_RESULT_*).
//
//----------------------------------------------------------------------------

int Dialog::RunModal ( Application *application )
{
	// Local variables.

	TEXTBUFFER *back_buffer;
	Component  *list [ FOCUS_LIST_CAPACITY ];
	Component  *focused_child;
	Component  *component;
	KEY_EVENT   key_event;
	int         count;
	bool        consumed;

	// Nothing to run without an application.

	if ( application == NULL ) return DIALOG_RESULT_CANCEL;

	// The back buffer everything composites into.

	back_buffer = application->GetBackBuffer ();

	// Floating-element discipline: save the region the dialog and its
	// shadow will cover, then cast the shadow (the body draws per frame).

	saved_block = CreateTextBuffer ( width + 2, height + 1, foreground, background );

	// Snapshot the covered region.

	GetTextBlock ( &saved_block, back_buffer, x, y, width + 2, height + 1 );
	PutShadow    ( back_buffer, x + 2, y + 1, width, height, 1, 1 );

	// Register as the application's modal dialog so a frame drawn mid-loop re-composites it on top.

	application->SetModalDialog ( this );

	// Initial focus: the first focusable child.

	count = 0;
	CollectFocusableComponents ( this, list, &count );

	// Take the first focusable child as the initial focus.

	focused_child = ( count > 0 ) ? list [ 0 ] : ( Component * ) 0;

	// Focus it.

	if ( focused_child != NULL ) focused_child->SetFocus ( true );

	// Start open, with a default cancel result.

	closed = false;
	result = DIALOG_RESULT_CANCEL;

	// Modal key loop, until Close() is called.

	while ( !closed )
	{
		Draw ( back_buffer );
		FlipScreenBuffer ( back_buffer );

		key_event.key       = ReadKey ();
		key_event.scan_code = ( BYTE ) ( key_event.key >> 8 );
		key_event.ascii     = ( BYTE ) ( key_event.key & 0x00FF );

		// Route to the focused child, bubbling up to the dialog itself.

		consumed  = false;
		component = ( focused_child != NULL ) ? focused_child : ( Component * ) this;

		while ( component != NULL )
		{
			if ( component->HandleKey ( key_event ) ) { consumed = true; break; }
			if ( component == ( Component * ) this )  break;

			component = component->GetParent ();
		}

		// Unconsumed Tab / arrows cycle the dialog-local focus.

		if ( !consumed )
		{
			if ( key_event.scan_code == KEY_TAB )
			{
				MoveDialogFocus ( this, &focused_child, ( key_event.ascii == 0x09 ) ? 1 : -1 );
			}
			else if ( ( key_event.scan_code == KEY_DOWN ) || ( key_event.scan_code == KEY_RIGHT ) )
			{
				MoveDialogFocus ( this, &focused_child, 1 );
			}
			else if ( ( key_event.scan_code == KEY_UP ) || ( key_event.scan_code == KEY_LEFT ) )
			{
				MoveDialogFocus ( this, &focused_child, -1 );
			}
		}
	}

	// Blur the focused child on close.

	if ( focused_child != NULL ) focused_child->SetFocus ( false );

	// Clear the application's modal dialog.

	application->SetModalDialog ( ( Dialog * ) 0 );

	// Restore the covered region exactly.

	PutTextBlock      ( back_buffer, &saved_block, x, y, width + 2, height + 1 );
	DestroyTextBuffer ( &saved_block );
	FlipScreenBuffer  ( back_buffer );

	// Return the result recorded by Close.

	return result;
}

//----------------------------------------------------------------------------
// Function: MessageBox::MessageBox / Draw / HandleKey
//
// Description:
//
//   A modal message dialog: title, one message line (the component
//   description), and an Ok button. Sized to the message and centered on
//   the screen. Esc closes as cancel.
//
// Arguments:
//
//   - new_name    : Component name.
//   - new_title   : The dialog title.
//   - new_message : The message line.
//
// Returns:
//
//   - None
//
//----------------------------------------------------------------------------

MessageBox::MessageBox ( void )
: Dialog (),
  ok_button ( "ok-button", 0, 0, 10, "Ok" )
{
	ok_button.SetOnActivate ( DialogCloseOkHandler, this );

	AddComponent ( &ok_button );
}

MessageBox::MessageBox ( const char *new_name, const char *new_title, const char *new_message )
: Dialog ( new_name, 0, 0, 0, 0, new_title ), ok_button ( "ok-button", 0, 0, 10, "Ok" )
{
	// Local variables.

	Application *application;
	int          message_length;
	int          rows;

	// Store the message line as the component description.

	SetDescription ( new_message );

	// Measure the message.

	message_length = strlen ( description );

	// Size the box to the message, clamped to sensible bounds.

	width  = message_length + 8;
	if ( width < 24 ) width = 24;
	if ( width > 76 ) width = 76;

	// Fixed height: title, message line, and the Ok button.

	height = 7;

	// Resolve the actual screen row count for centring.

	application = Application::GetInstance ();
	rows        = ( application != NULL ) ? ( int ) application->GetRows () : 25;

	// Centre the box on the screen.

	x = ( 80 - width ) / 2;
	y = ( rows - height ) / 2;

	// Place and wire the Ok button.

	ok_button.SetPosition   ( x + ( width - 10 ) / 2, y + 4 );
	ok_button.SetOnActivate ( DialogCloseOkHandler, this );

	// Add the Ok button as a child.

	AddComponent ( &ok_button );
}

void MessageBox::Draw ( TEXTBUFFER *text_buffer )
{
	Panel::Draw ( text_buffer );

	PutText ( text_buffer, x + ( width - ( int ) strlen ( description ) ) / 2, y + 2, description, foreground, background, 0, 0 );
}

bool MessageBox::HandleKey ( KEY_EVENT &key_event )
{
	return Dialog::HandleKey ( key_event );
}

//----------------------------------------------------------------------------
// Function: ConfirmationBox::ConfirmationBox / Draw / HandleKey
//
// Description:
//
//   A modal confirmation dialog: title, one message line (the component
//   description), and Yes / No buttons, with Yes holding the initial
//   focus (the default). Esc closes as No.
//
// Arguments:
//
//   - new_name    : Component name.
//   - new_title   : The dialog title.
//   - new_message : The message line.
//
// Returns:
//
//   - None
//
//----------------------------------------------------------------------------

ConfirmationBox::ConfirmationBox ( void )
: Dialog (),
  yes_button ( "yes-button", 0, 0, 10, "Yes" ),
  no_button  ( "no-button",  0, 0, 10, "No" )
{
	yes_button.SetOnActivate ( DialogCloseOkHandler,     this );
	no_button.SetOnActivate  ( DialogCloseCancelHandler, this );

	AddComponent ( &yes_button );
	AddComponent ( &no_button );
}

ConfirmationBox::ConfirmationBox ( const char *new_name, const char *new_title, const char *new_message )
: Dialog ( new_name, 0, 0, 0, 0, new_title ),
  yes_button ( "yes-button", 0, 0, 10, "Yes" ),
  no_button  ( "no-button",  0, 0, 10, "No" )
{
	// Local variables.
	
	Application *application;
	int          message_length;
	int          rows;

	// Store the message line as the component description.

	SetDescription ( new_message );

	// Measure the message.

	message_length = strlen ( description );

	// Size the box to the message, clamped to sensible bounds.

	width  = message_length + 8;
	if ( width < 30 ) width = 30;
	if ( width > 76 ) width = 76;

	// Fixed height: title, message line, and the buttons.

	height = 7;

	// Resolve the actual screen row count for centring.

	application = Application::GetInstance ();
	rows        = ( application != NULL ) ? ( int ) application->GetRows () : 25;

	// Centre the box on the screen.

	x = ( 80 - width ) / 2;
	y = ( rows - height ) / 2;

	// Place the Yes and No buttons side by side.

	yes_button.SetPosition ( x + width / 2 - 11, y + 4 );
	no_button.SetPosition  ( x + width / 2 + 1,  y + 4 );

	// Wire the buttons to the Ok / Cancel close handlers.

	yes_button.SetOnActivate ( DialogCloseOkHandler,     this );
	no_button.SetOnActivate  ( DialogCloseCancelHandler, this );

	// Add the buttons as children (Yes takes the initial focus).

	AddComponent ( &yes_button );
	AddComponent ( &no_button  );
}

void ConfirmationBox::Draw ( TEXTBUFFER *text_buffer )
{
	Panel::Draw ( text_buffer );

	PutText ( text_buffer, x + ( width - ( int ) strlen ( description ) ) / 2, y + 2, description, foreground, background, 0, 0 );
}

bool ConfirmationBox::HandleKey ( KEY_EVENT &key_event )
{
	return Dialog::HandleKey ( key_event );
}

//----------------------------------------------------------------------------
// Function: FileDialogListChangeHandler / FileDialogListActivateHandler
//
// Description:
//
//   Internal wiring for the file dialogs: moving the file-list selection
//   copies the name into the filename box; Enter on the list also
//   accepts the dialog.
//
// Arguments:
//
//   - sender    : The file list.
//   - user_data : The owning FileDialog.
//
// Returns:
//
//   - None
//
//----------------------------------------------------------------------------

static void FileDialogListChangeHandler ( Component *sender, void *user_data )
{
	( void ) sender;

	( ( FileDialog * ) user_data )->TakeSelectedFileName ();
}

static void FileDialogListActivateHandler ( Component *sender, void *user_data )
{
	( void ) sender;

	( ( FileDialog * ) user_data )->TakeSelectedFileName ();
	( ( FileDialog * ) user_data )->Close ( DIALOG_RESULT_OK );
}

//----------------------------------------------------------------------------
// Function: FileDialog::FileDialog
//
// Description:
//
//   The shared file-dialog layout: a filename TextBox over a file
//   ListBox (filled from the current directory via findfirst/findnext;
//   all files are treated as binary), with an action button and a Cancel
//   button. Moving the list selection copies the name into the filename
//   box; Enter (anywhere unconsumed, or on the list) accepts; Esc
//   cancels.
//
// Arguments:
//
//   - new_name        : Component name.
//   - new_text        : The dialog title.
//   - new_action_text : The action button caption (Create / Open / Save).
//
// Returns:
//
//   - None
//
//----------------------------------------------------------------------------

FileDialog::FileDialog ( void )
: Dialog (),
  file_list     ( "file-list", 0, 0, 0, 0 ),
  file_name_box ( "file-name", 0, 0, 0, 0, 64, false ),
  action_button ( "action-button", 0, 0, 10, "Ok" ),
  cancel_button ( "cancel-button", 0, 0, 10, "Cancel" )
{
}

FileDialog::FileDialog ( const char *new_name, const char *new_text, const char *new_action_text )
: Dialog ( new_name, 19, 4, 42, 17, new_text ),
  file_list     ( "file-list", 21, 9, 38, 9 ),
  file_name_box ( "file-name", 21, 6, 38, 3, 64, false ),
  action_button ( "action-button", 27, 19, 10, new_action_text ),
  cancel_button ( "cancel-button", 43, 19, 10, "Cancel" )
{
	// Local variables.

	Application *application;
	int          rows;

	// Center vertically for the actual row count.

	application = Application::GetInstance ();
	rows        = ( application != NULL ) ? ( int ) application->GetRows () : 25;

	// Centre the dialog vertically within the screen.

	y = ( rows - height ) / 2;

	// Position the child components relative to the dialog.

	file_name_box.SetPosition ( x + 2, y + 2 );
	file_list.SetPosition     ( x + 2, y + 5 );
	action_button.SetPosition ( x + 8, y + 15 );
	cancel_button.SetPosition ( x + 24, y + 15 );

	// Black text on the light-gray dialog, with a white frame / label while
	// focused and dark-gray while not - the shared dialog focus convention.

	file_name_box.SetText                    ( "Name" );
	file_name_box.SetColors                  ( COLOR_BLACK, COLOR_LIGHT_GRAY );
	file_name_box.SetSelectedBorderForeground ( COLOR_WHITE );

	// Give the file list a white frame while focused (the shared focus convention).

	file_list.SetSelectedBorderForeground ( COLOR_WHITE );

	// Wire the file list: a selection change copies the name into the box; Enter accepts.

	file_list.SetOnChange   ( FileDialogListChangeHandler,   this );
	file_list.SetOnActivate ( FileDialogListActivateHandler, this );

	// Wire the action and cancel buttons to the close handlers.

	action_button.SetOnActivate ( DialogCloseOkHandler,     this );
	cancel_button.SetOnActivate ( DialogCloseCancelHandler, this );

	// Add the child components.

	AddComponent ( &file_name_box );
	AddComponent ( &file_list );
	AddComponent ( &action_button );
	AddComponent ( &cancel_button );

	// Populate the file list from the current directory.

	ReadDirectory ( "*.*" );
}

//----------------------------------------------------------------------------
// Function: FileDialog::ReadDirectory / TakeSelectedFileName / GetFileName /
//           SetFileName
//
// Description:
//
//   ReadDirectory fills the file list with the normal files matching the
//   path mask, via DOS findfirst/findnext. TakeSelectedFileName copies
//   the list selection into the filename box. GetFileName returns the
//   filename box contents; SetFileName seeds the filename box (used to
//   preset the current file name in Save As).
//
// Arguments:
//
//   - path_mask : The DOS wildcard mask (e.g. "*.*").
//   - file_name : SetFileName: the name to seed into the filename box.
//
// Returns:
//
//   - GetFileName: the entered / selected file name.
//
//----------------------------------------------------------------------------

void FileDialog::ReadDirectory ( const char *path_mask )
{
	// Local variables.

	struct ffblk file_block;
	int          done;

	// Start with an empty list.

	file_list.ClearItems ();

	// Find the first matching directory entry.

	done = findfirst ( path_mask, &file_block, 0 );

	// Add every matching file to the list.

	while ( !done )
	{
		file_list.AddItem ( file_block.ff_name, NULL );

		done = findnext ( &file_block );
	}

	// Select the first entry.

	file_list.SetSelectedIndex ( 0 );

	// Copy the selection into the filename box.

	TakeSelectedFileName ();
}

void FileDialog::TakeSelectedFileName ( void )
{
	ListItem *item;

	item = file_list.GetSelectedItem ();

	if ( item != NULL ) file_name_box.SetEditText ( item->GetText () );
}

const char far *FileDialog::GetFileName ( void )
{
	return file_name_box.GetEditText ();
}

void FileDialog::SetFileName ( const char far *file_name )
{
	file_name_box.SetEditText ( file_name );
}

//----------------------------------------------------------------------------
// Function: FileDialog::HandleKey
//
// Description:
//
//   Enter (unconsumed by any child) accepts the dialog; everything else
//   falls to the Dialog base (Esc cancels).
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

bool FileDialog::HandleKey ( KEY_EVENT &key_event )
{
	// Enter accepts the dialog.

	if ( key_event.scan_code == KEY_ENTER )
	{
		Close ( DIALOG_RESULT_OK );

		return true;
	}

	// Otherwise defer to the base (Esc cancels).

	return Dialog::HandleKey ( key_event );
}

//----------------------------------------------------------------------------
// Function: NewFileDialog / FileOpenDialog / FileSaveAsDialog constructors
//
// Description:
//
//   The three concrete file dialogs: the shared FileDialog layout with
//   their own titles and action captions.
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

NewFileDialog::NewFileDialog ( void ) : FileDialog ( "new-file-dialog", "New File", "Create" )
{
}

FileOpenDialog::FileOpenDialog ( void ) : FileDialog ( "file-open-dialog", "Open File", "Open" )
{
}

FileSaveAsDialog::FileSaveAsDialog ( void ) : FileDialog ( "file-save-as-dialog", "Save As", "Save" )
{
}

//----------------------------------------------------------------------------
// Function: Application::Application
//
// Description:
//
//   Sets the requested text mode, hides the hardware cursor, and creates
//   the off-screen back buffer at 80 columns by the mode's actual row
//   count (as confirmed by GetTextMode).
//
// Arguments:
//
//   - new_rows : Requested text rows (TEXT_MODE_25_ROWS / 43 / 50).
//
// Returns:
//
//   - None
//
//----------------------------------------------------------------------------

Application::Application ( BYTE new_rows )
{
	// Set the text mode, hide the cursor, and install the key-state handler.

	SetTextMode            ( new_rows );
	HideCursor             ();
	InstallKeyboardHandler ();    // Key-state tracking for Button press/release (KeyDown).

	// Confirm the actual row count, falling back to 25 rows.

	rows = GetTextMode ();
	if ( rows == 0 ) rows = TEXT_MODE_25_ROWS;

	// Create the off-screen back buffer at the confirmed size.

	back_buffer = CreateTextBuffer ( 80, ( int ) rows, COLOR_LIGHT_GRAY, COLOR_BLACK );

	// Initialise the application state and publish the singleton instance.

	root              = NULL;
	focused_component = NULL;
	modal_dialog      = NULL;
	running           = false;
	instance          = this;
}

//----------------------------------------------------------------------------
// Function: Application::SetRows
//
// Description:
//
//   Switches the screen row count at run time (a live Settings row change):
//   the requested text mode is set, the actual mode is confirmed with
//   GetTextMode (falling back to 25 rows if the adapter did not honour a
//   device-dependent 43/50-row mode), and the off-screen back buffer is
//   rebuilt at the new size only when the row count actually changed. The
//   caller re-lays-out the application frame (ApplicationPanel::Relayout) and renders
//   a fresh frame.
//
// Arguments:
//
//   - new_rows : The requested text rows (TEXT_MODE_25_ROWS / 43 / 50).
//
// Returns:
//
//   - None
//
//----------------------------------------------------------------------------

void Application::SetRows ( BYTE new_rows )
{
	// Local variables.

	BYTE actual_rows;

	// Set the requested text mode and hide the cursor.

	SetTextMode ( new_rows );
	HideCursor  ();

	// Confirm the actual row count, falling back to 25 rows.

	actual_rows = GetTextMode ();
	if ( actual_rows == 0 ) actual_rows = TEXT_MODE_25_ROWS;

	// Rebuild the back buffer only when the row count actually changed.

	if ( actual_rows != rows )
	{
		DestroyTextBuffer ( &back_buffer );

		back_buffer = CreateTextBuffer ( 80, ( int ) actual_rows, COLOR_LIGHT_GRAY, COLOR_BLACK );

		rows = actual_rows;
	}
}

//----------------------------------------------------------------------------
// Function: Application::~Application
//
// Description:
//
//   Releases the back buffer and restores 25-row text mode (which also
//   restores the hardware cursor for DOS).
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

Application::~Application ( void )
{
	// Remove the keyboard handler.

	RemoveKeyboardHandler ();

	// Release the back buffer.

	DestroyTextBuffer ( &back_buffer );

	// Restore 25-row text mode (which also restores the DOS hardware cursor).

	SetTextMode ( TEXT_MODE_25_ROWS );

	// Clear the singleton instance if it points here.

	if ( instance == this ) instance = NULL;
}

//----------------------------------------------------------------------------
// Function: Application::SetRoot / SetFocusedComponent
//
// Description:
//
//   SetRoot installs the root of the component tree. SetFocusedComponent
//   moves the keyboard focus, firing the blur / focus transitions on the
//   components involved.
//
// Arguments:
//
//   - new_root / new_focused_component : The component to install.
//
// Returns:
//
//   - None
//
//----------------------------------------------------------------------------

void Application::SetRoot ( Component *new_root )
{
	root = new_root;
}

void Application::SetFocusedComponent ( Component *new_focused_component )
{
	// Nothing to do if unchanged; otherwise blur the outgoing component.

	if ( focused_component == new_focused_component ) return;
	if ( focused_component != NULL                  ) focused_component->SetFocus ( false );

	// Adopt the new focused component.

	focused_component = new_focused_component;

	// Focus the incoming component.

	if ( focused_component != NULL ) focused_component->SetFocus ( true );
}

//----------------------------------------------------------------------------
// Function: Application::RenderFrame
//
// Description:
//
//   The render pipeline: clear the back buffer to the root's colors,
//   draw the component tree top-down into it, re-composite the open
//   modal dialog (shadow then body) if one is running, and present the
//   finished frame with a single FlipScreenBuffer.
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

void Application::RenderFrame ( void )
{
	// Clear the back buffer to the root's colours and draw the tree; fall back to default colours with no root.

	if ( root != NULL )
	{
		ClearTextBuffer ( &back_buffer, ' ', root->GetForeground (), root->GetBackground () );

		root->Draw ( &back_buffer );
	}
	else
	{
		ClearTextBuffer ( &back_buffer, ' ', COLOR_LIGHT_GRAY, COLOR_BLACK );
	}

	// Re-composite the open modal dialog so a frame rendered mid-dialog
	// (e.g. by a Button press inside it) keeps the dialog on top.

	if ( modal_dialog != NULL )
	{
		PutShadow ( &back_buffer, modal_dialog->GetX () + 2, modal_dialog->GetY () + 1,
		            modal_dialog->GetWidth (), modal_dialog->GetHeight (), 1, 1 );

		modal_dialog->Draw ( &back_buffer );
	}

	// Present the finished frame with a single flip.

	FlipScreenBuffer ( &back_buffer );
}

//----------------------------------------------------------------------------
// Function: Application::DispatchKey
//
// Description:
//
//   Routes a key event: first to the focused component (or the root when
//   nothing holds focus), bubbling each unconsumed key up the parent
//   chain, and finally to the application's global keys (Esc quits).
//
// Arguments:
//
//   - key_event : The key event to route.
//
// Returns:
//
//   - true if the key was consumed, false if it fell through unhandled.
//
//----------------------------------------------------------------------------

bool Application::DispatchKey ( KEY_EVENT &key_event )
{
	// Local variables.

	Component *component;

	// Route to the focus chain: focused component, then its parents.

	component = ( focused_component != NULL ) ? focused_component : root;

	while ( component != NULL )
	{
		if ( component->HandleKey ( key_event ) ) return true;

		component = component->GetParent ();
	}

	// Application global keys: focus traversal, then Esc.

	if ( key_event.scan_code == KEY_TAB )
	{
		// A plain Tab carries ascii 0x09; Shift+Tab arrives with ascii 0.

		if ( key_event.ascii == 0x09 ) FocusNext ();
		else                           FocusPrevious ();

		return true;
	}

	// Down / Right move focus to the next component.

	if ( ( key_event.scan_code == KEY_DOWN ) || ( key_event.scan_code == KEY_RIGHT ) )
	{
		FocusNext ();
		return true;
	}

	// Up / Left move focus to the previous component.

	if ( ( key_event.scan_code == KEY_UP ) || ( key_event.scan_code == KEY_LEFT ) )
	{
		FocusPrevious ();
		return true;
	}

	// Esc quits the application.

	if ( key_event.scan_code == KEY_ESC )
	{
		Quit ();
		return true;
	}

	// The key fell through unhandled.

	return false;
}

//----------------------------------------------------------------------------
// Function: Application::FocusNext / FocusPrevious
//
// Description:
//
//   Moves the keyboard focus to the next / previous visible, enabled,
//   focusable component in depth-first tree order, wrapping at the ends.
//   With nothing focused, FocusNext focuses the first candidate and
//   FocusPrevious the last.
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

void Application::FocusNext ( void )
{
	// Local variables.

	Component *list [ FOCUS_LIST_CAPACITY ];
	int        count;
	int        index;
	int        i;

	// Collect the focusable components in tree order.

	count = 0;
	CollectFocusableComponents ( root, list, &count );

	// Nothing to focus.

	if ( count == 0 ) return;

	// Find the currently focused component in the list.

	index = -1;
	for ( i = 0; i < count; i++ )
	{
		if ( list [ i ] == focused_component ) { index = i; break; }
	}

	// Step to the next entry, wrapping at the end.

	index = ( index + 1 ) % count;

	// Move focus there.

	SetFocusedComponent ( list [ index ] );
}

void Application::FocusPrevious ( void )
{
	// Local variables.

	Component *list [ FOCUS_LIST_CAPACITY ];
	int        count;
	int        index;
	int        i;

	// Collect the focusable components in tree order.

	count = 0;
	CollectFocusableComponents ( root, list, &count );

	// Nothing to focus.

	if ( count == 0 ) return;

	// Find the currently focused component in the list.

	index = -1;
	for ( i = 0; i < count; i++ )
	{
		if ( list [ i ] == focused_component ) { index = i; break; }
	}

	// Step to the previous entry, wrapping at the start.

	index = ( index <= 0 ) ? count - 1 : index - 1;

	// Move focus there.

	SetFocusedComponent ( list [ index ] );
}

//----------------------------------------------------------------------------
// Function: Application::Run
//
// Description:
//
//   The run loop: render a frame, wait for a key, split the raw key
//   word into a KEY_EVENT, and dispatch it - until Quit() clears the
//   running flag.
//
//   The wait polls KeyPressed rather than blocking in ReadKey, so that
//   the shift-state byte can be watched while idle. The lock keys
//   (NumLock, CapsLock, ScrollLock) never reach the keyboard buffer -
//   the BIOS consumes them and only flips a bit in its own flag byte -
//   so a component rendering a lock indicator would otherwise sit stale
//   until the next real keystroke. Re-rendering whenever the byte
//   changes keeps such indicators tracking the physical keyboard.
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

void Application::Run ( void )
{
	// Local variables.

	KEY_EVENT key_event;
	WORD      shift_state;
	WORD      last_shift_state;

	// Enter the run loop.

	running = true;

	// Give the first focusable component the initial focus.

	if ( focused_component == NULL ) FocusNext ();

	// Seed the shift-state watch with the keyboard's state at launch.

	last_shift_state = GetShiftState ();

	// Main loop: render a frame, wait for a key, and dispatch it, until Quit().

	while ( running )
	{
		RenderFrame ();

		// Idle until a key arrives, re-rendering on any shift-state change
		// so the lock indicators stay live (see the note above).

		while ( !KeyPressed () )
		{
			shift_state = GetShiftState ();

			if ( shift_state != last_shift_state )
			{
				last_shift_state = shift_state;

				RenderFrame ();
			}
		}

		key_event.key       = ReadKey ();
		key_event.scan_code = ( BYTE ) ( key_event.key >> 8 );
		key_event.ascii     = ( BYTE ) ( key_event.key & 0x00FF );

		DispatchKey ( key_event );
	}
}

//----------------------------------------------------------------------------
// Function: Application::Quit
//
// Description:
//
//   Stops the run loop after the current key is dispatched.
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

void Application::Quit ( void )
{
	// Stop the run loop after the current key is dispatched.

	running = false;
}

//----------------------------------------------------------------------------
