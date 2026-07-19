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

#ifndef _MICRO_APP
#define _MICRO_APP

#include "mtext.h"

//----------------------------------------------------------------------------
// Language Compatibility
//----------------------------------------------------------------------------

// Borland C++ 3.1 has no built-in bool type.

typedef int bool;

#define true   1
#define false  0

//----------------------------------------------------------------------------
// Capacity Constants
//----------------------------------------------------------------------------

#define COMPONENT_NAME_SIZE       32    // Component name buffer, including the NUL.
#define COMPONENT_TEXT_SIZE       81    // Component text/description buffer (one 80-column row + NUL).
#define COMPONENT_CHILD_CAPACITY  16    // Maximum children per container.
#define LIST_ITEM_CAPACITY        96    // Maximum items per ListBox / DropDownTextBox / Menu.
#define STATUS_BAR_ROW_COUNT      2     // StatusBar grid rows.
#define STATUS_BAR_COLUMN_COUNT   6     // StatusBar grid columns.
#define STATUS_BAR_FIELD_SIZE     16    // StatusBar label/value buffer, including the NUL.

//----------------------------------------------------------------------------
// Keyboard Scan Codes
//----------------------------------------------------------------------------

// Scan-code bytes of the raw INT 16h word (scan_code << 8) | ascii
// delivered by ReadKey and carried by KEY_EVENT::key.

#define KEY_ESC        0x01
#define KEY_BACKSPACE  0x0E
#define KEY_TAB        0x0F
#define KEY_R          0x13
#define KEY_ENTER      0x1C
#define KEY_F          0x21
#define KEY_G          0x22
#define KEY_SPACE      0x39
#define KEY_F10        0x44
#define KEY_HOME       0x47
#define KEY_UP         0x48
#define KEY_PGUP       0x49
#define KEY_LEFT       0x4B
#define KEY_RIGHT      0x4D
#define KEY_END        0x4F
#define KEY_DOWN       0x50
#define KEY_PGDN       0x51
#define KEY_INS        0x52
#define KEY_DELETE     0x53

//----------------------------------------------------------------------------
// Event and Action Model
//----------------------------------------------------------------------------

enum EVENT_TYPE { EVENT_NONE, EVENT_KEY, EVENT_KEY_DOWN, EVENT_KEY_UP, EVENT_ACTIVATE, EVENT_CHANGE, EVENT_FOCUS, EVENT_BLUR };

struct KEY_EVENT
{
	WORD key;          // Raw INT 16h word: (scan_code << 8) | ascii.
	BYTE scan_code;    // High byte of key.
	BYTE ascii;        // Low byte of key.
};

class Component;

typedef void ( *EventHandler ) ( Component *sender, void *user_data );

//----------------------------------------------------------------------------
// Class: Component
//
// Description:
//
//   Abstract base of everything drawable. Owns identity (name, text,
//   description), geometry (absolute x, y, width, height), state flags
//   (visible, enabled, focusable, focused), colors, the parent pointer,
//   and the registered event handlers with their user data.
//
//----------------------------------------------------------------------------

class Component
{
	// Data Members

protected:

	char          name        [ COMPONENT_NAME_SIZE ];
	char          text        [ COMPONENT_TEXT_SIZE ];
	char          description [ COMPONENT_TEXT_SIZE ];

	int           x;
	int           y;
	int           width;
	int           height;

	bool          visible;
	bool          enabled;
	bool          focusable;
	bool          focused;

	BYTE          foreground;
	BYTE          background;

	Component    *parent;

	EventHandler  on_activate;
	void         *on_activate_user_data;
	EventHandler  on_change;
	void         *on_change_user_data;
	EventHandler  on_focus;
	void         *on_focus_user_data;
	EventHandler  on_blur;
	void         *on_blur_user_data;
	EventHandler  on_key_down;
	void         *on_key_down_user_data;
	EventHandler  on_key_up;
	void         *on_key_up_user_data;

	// Accessors

public:

	const char *GetName        ( void ) { return name; }
	const char *GetText        ( void ) { return text; }
	const char *GetDescription ( void ) { return description; }
	int         GetX           ( void ) { return x; }
	int         GetY           ( void ) { return y; }
	int         GetWidth       ( void ) { return width; }
	int         GetHeight      ( void ) { return height; }
	bool        IsVisible      ( void ) { return visible; }
	bool        IsEnabled      ( void ) { return enabled; }
	bool        IsFocusable    ( void ) { return focusable; }
	bool        IsFocused      ( void ) { return focused; }
	BYTE        GetForeground  ( void ) { return foreground; }
	BYTE        GetBackground  ( void ) { return background; }
	Component  *GetParent      ( void ) { return parent; }

	// Mutators

	void SetName        ( const char *new_name );
	void SetText        ( const char *new_text );
	void SetDescription ( const char *new_description );
	void SetPosition    ( int new_x, int new_y )           { x = new_x; y = new_y; }
	void SetSize        ( int new_width, int new_height )  { width = new_width; height = new_height; }
	void SetVisible     ( bool new_visible )               { visible = new_visible; }
	void SetEnabled     ( bool new_enabled )               { enabled = new_enabled; }
	void SetFocusable   ( bool new_focusable )             { focusable = new_focusable; }
	void SetColors      ( BYTE new_foreground, BYTE new_background ) { foreground = new_foreground; background = new_background; }
	void SetParent      ( Component *new_parent )          { parent = new_parent; }

	void SetOnActivate  ( EventHandler handler, void *user_data ) { on_activate = handler; on_activate_user_data = user_data; }
	void SetOnChange    ( EventHandler handler, void *user_data ) { on_change   = handler; on_change_user_data   = user_data; }
	void SetOnFocus     ( EventHandler handler, void *user_data ) { on_focus    = handler; on_focus_user_data    = user_data; }
	void SetOnBlur      ( EventHandler handler, void *user_data ) { on_blur     = handler; on_blur_user_data     = user_data; }
	void SetOnKeyDown   ( EventHandler handler, void *user_data ) { on_key_down = handler; on_key_down_user_data = user_data; }
	void SetOnKeyUp     ( EventHandler handler, void *user_data ) { on_key_up   = handler; on_key_up_user_data   = user_data; }

	// Constructors / Destructor

	Component ( void );
	Component ( const char *name, int x, int y, int width, int height );

	virtual ~Component ( void );

	// Methods

	virtual void Draw      ( TEXTBUFFER *text_buffer );
	virtual bool HandleKey ( KEY_EVENT &key_event );
	virtual void SetFocus  ( bool new_focused );

	// Child introspection for tree walks (focus traversal); containers
	// override.

	virtual int        GetChildCount ( void )      { return 0; }
	virtual Component *GetChild      ( int index ) { ( void ) index; return ( Component * ) 0; }

	void FireActivate ( void );
	void FireChange   ( void );
	void FireKeyDown  ( void );
	void FireKeyUp    ( void );
};

//----------------------------------------------------------------------------
// Class: Panel
//
// Description:
//
//   General container and floating-window base. Owns the child list and
//   focus traversal across its children. With its border enabled, draws
//   an opaque filled rectangle (single or double box lines) with an
//   optional drop shadow and an optional centered title (the component
//   text); borderless, it is a transparent container that draws only its
//   children.
//
//----------------------------------------------------------------------------

class Panel : public Component
{
	// Data Members

protected:

	Component *components [ COMPONENT_CHILD_CAPACITY ];
	int        component_count;
	int        focused_index;

	bool       border_enabled;
	bool       shadow_enabled;
	char       line_thickness;    // '-' single or '=' double box lines.

	// Accessors

public:

	int        GetComponentCount ( void )        { return component_count; }
	Component *GetComponent      ( int index )   { return ( ( index >= 0 ) && ( index < component_count ) ) ? components [ index ] : ( Component * ) 0; }

	// Mutators

	void SetBorderEnabled ( bool new_border_enabled )  { border_enabled = new_border_enabled; }
	void SetShadowEnabled ( bool new_shadow_enabled )  { shadow_enabled = new_shadow_enabled; }
	void SetLineThickness ( char new_line_thickness )  { line_thickness = new_line_thickness; }

	// Constructors

	Panel ( void );
	Panel ( const char *name, int x, int y, int width, int height );

	// Methods

	void AddComponent ( Component *component );

	virtual void Draw      ( TEXTBUFFER *text_buffer );
	virtual bool HandleKey ( KEY_EVENT &key_event );

	virtual int        GetChildCount ( void )      { return component_count; }
	virtual Component *GetChild      ( int index ) { return GetComponent ( index ); }
};

//----------------------------------------------------------------------------
// Class: Label
//
// Description:
//
//   Static single-row text.
//
//----------------------------------------------------------------------------

class Label : public Component
{
public:

	// Constructors

	Label ( void );
	Label ( const char *name, int x, int y, const char *text );

	// Methods

	virtual void Draw ( TEXTBUFFER *text_buffer );
};

//----------------------------------------------------------------------------
// Class: Button
//
// Description:
//
//   Push button with unselected / selected / depressed color sets. Fires
//   OnActivate on Enter and shows the depressed colors while Enter is
//   held.
//
//----------------------------------------------------------------------------

class Button : public Component
{
	// Data Members

protected:

	BYTE unselected_foreground;
	BYTE unselected_background;
	BYTE selected_foreground;
	BYTE selected_background;
	BYTE depressed_foreground;
	BYTE depressed_background;

	bool depressed;

	// Mutators

public:

	void SetUnselectedColors ( BYTE new_foreground, BYTE new_background );
	void SetSelectedColors   ( BYTE new_foreground, BYTE new_background );
	void SetDepressedColors  ( BYTE new_foreground, BYTE new_background );

	// Constructors

	Button ( void );
	Button ( const char *name, int x, int y, int width, const char *text );

	// Methods

	virtual void Draw      ( TEXTBUFFER *text_buffer );
	virtual bool HandleKey ( KEY_EVENT &key_event );
};

//----------------------------------------------------------------------------
// Class: TextBox
//
// Description:
//
//   Single- or multi-line text entry over a caller-visible edit buffer,
//   with insert/overwrite editing, a text cursor, and scrolling.
//
//----------------------------------------------------------------------------

class TextBox : public Component
{
	// Data Members

protected:

	char far *edit_buffer;         // farmalloc-ed at construction.
	unsigned  edit_buffer_size;    // Capacity in bytes, including the NUL.
	unsigned  text_length;         // Current text length.
	unsigned  cursor_position;     // Cursor offset into the text.
	int       scroll_x;            // First visible column offset.
	int       scroll_y;            // First visible line offset (multi-line).
	bool      multi_line;
	bool      insert_enabled;      // true = insert, false = overwrite.
	BYTE      border_foreground;            // Border color while unfocused.
	BYTE      selected_border_foreground;   // Border color while focused.

	// Accessors

public:

	bool     IsMultiLine      ( void ) { return multi_line; }
	bool     IsInsertEnabled  ( void ) { return insert_enabled; }
	unsigned GetTextLength    ( void ) { return text_length; }
	unsigned GetCursorLine    ( void );
	unsigned GetCursorColumn  ( void );

	// Mutators

	void SetMultiLine                ( bool new_multi_line )                 { multi_line = new_multi_line; }
	void SetInsertEnabled            ( bool new_insert_enabled )             { insert_enabled = new_insert_enabled; }
	void SetBorderForeground         ( BYTE new_border_foreground )          { border_foreground = new_border_foreground; }
	void SetSelectedBorderForeground ( BYTE new_selected_border_foreground ) { selected_border_foreground = new_selected_border_foreground; }

	// Constructors / Destructor

	TextBox ( void );
	TextBox ( const char *name, int x, int y, int width, int height, unsigned edit_buffer_size, bool multi_line );

	virtual ~TextBox ( void );

	// Methods

	virtual void Draw      ( TEXTBUFFER *text_buffer );
	virtual bool HandleKey ( KEY_EVENT &key_event );

	void        SetEditText ( const char far *new_text );
	const char far *GetEditText ( void ) { return edit_buffer; }
};

//----------------------------------------------------------------------------
// Class: CheckBox
//
// Description:
//
//   Two-state check box, rendered [X] / [ ] followed by its text.
//
//----------------------------------------------------------------------------

class CheckBox : public Component
{
	// Data Members

protected:

	bool checked;

	// Accessors

public:

	bool IsChecked ( void ) { return checked; }

	// Mutators

	void SetChecked ( bool new_checked ) { checked = new_checked; }

	// Constructors

	CheckBox ( void );
	CheckBox ( const char *name, int x, int y, const char *text );

	// Methods

	virtual void Draw      ( TEXTBUFFER *text_buffer );
	virtual bool HandleKey ( KEY_EVENT &key_event );
};

//----------------------------------------------------------------------------
// Class: RadioButton / RadioButtonGroup
//
// Description:
//
//   Mutually exclusive selection: selecting one RadioButton clears its
//   siblings within the owning RadioButtonGroup.
//
//----------------------------------------------------------------------------

class RadioButtonGroup;

class RadioButton : public Component
{
	// Data Members

protected:

	bool              selected;
	RadioButtonGroup *group;

	// Accessors

public:

	bool IsSelected ( void ) { return selected; }

	// Mutators

	void SetSelected ( bool new_selected ) { selected = new_selected; }
	void SetGroup    ( RadioButtonGroup *new_group ) { group = new_group; }

	// Constructors

	RadioButton ( void );
	RadioButton ( const char *name, int x, int y, const char *text );

	// Methods

	virtual void Draw      ( TEXTBUFFER *text_buffer );
	virtual bool HandleKey ( KEY_EVENT &key_event );
};

class RadioButtonGroup : public Panel
{
public:

	// Constructors

	RadioButtonGroup ( void );
	RadioButtonGroup ( const char *name, int x, int y, int width, int height );

	// Methods

	void         AddRadioButton     ( RadioButton *radio_button );
	void         SelectRadioButton  ( RadioButton *radio_button );
	RadioButton *GetSelectedButton  ( void );
};

//----------------------------------------------------------------------------
// Class: Group
//
// Description:
//
//   Visual grouping frame with no behavior: a bordered panel whose title
//   is drawn on the top edge.
//
//----------------------------------------------------------------------------

class Group : public Panel
{
public:

	// Constructors

	Group ( void );
	Group ( const char *name, int x, int y, int width, int height, const char *text );
};

//----------------------------------------------------------------------------
// Class: ListItem / ListBox / DropDownTextBox
//
// Description:
//
//   ListItem holds one selectable entry (text plus an opaque user-data
//   pointer). ListBox renders a scrolling, selectable list of items.
//   DropDownTextBox is a text box with an attached drop-down ListBox.
//
//----------------------------------------------------------------------------

class ListItem
{
	// Data Members

protected:

	char  text [ COMPONENT_TEXT_SIZE ];
	void *user_data;

	// Accessors

public:

	const char *GetText     ( void ) { return text; }
	void       *GetUserData ( void ) { return user_data; }

	// Mutators

	void SetText     ( const char *new_text );
	void SetUserData ( void *new_user_data ) { user_data = new_user_data; }

	// Constructors

	ListItem ( void );
};

class ListBox : public Component
{
	// Data Members

protected:

	ListItem items [ LIST_ITEM_CAPACITY ];
	int      item_count;
	int      top_index;                     // First visible item.
	int      selected_index;
	BYTE     border_foreground;             // Border color while unfocused.
	BYTE     selected_border_foreground;    // Border color while focused.

	// Accessors

public:

	int       GetItemCount     ( void ) { return item_count; }
	int       GetSelectedIndex ( void ) { return selected_index; }
	ListItem *GetItem          ( int index ) { return ( ( index >= 0 ) && ( index < item_count ) ) ? &items [ index ] : ( ListItem * ) 0; }
	ListItem *GetSelectedItem  ( void ) { return GetItem ( selected_index ); }

	// Mutators

	void SetSelectedIndex            ( int new_selected_index );
	void SetBorderForeground         ( BYTE new_border_foreground )          { border_foreground = new_border_foreground; }
	void SetSelectedBorderForeground ( BYTE new_selected_border_foreground ) { selected_border_foreground = new_selected_border_foreground; }

	// Constructors

	ListBox ( void );
	ListBox ( const char *name, int x, int y, int width, int height );

	// Methods

	virtual void Draw      ( TEXTBUFFER *text_buffer );
	virtual bool HandleKey ( KEY_EVENT &key_event );

	void AddItem    ( const char *item_text, void *user_data );
	void ClearItems ( void );
};

class DropDownTextBox : public Component
{
	// Data Members

protected:

	char    entry_text [ COMPONENT_TEXT_SIZE ];
	ListBox drop_down_list;
	bool    drop_down_open;

	// Accessors

public:

	const char *GetEntryText    ( void ) { return entry_text; }
	ListBox    *GetDropDownList ( void ) { return &drop_down_list; }

	// Mutators

	void SetEntryText ( const char *new_entry_text );

	// Constructors

	DropDownTextBox ( void );
	DropDownTextBox ( const char *name, int x, int y, int width, int drop_down_height );

	// Methods

	virtual void Draw      ( TEXTBUFFER *text_buffer );
	virtual bool HandleKey ( KEY_EVENT &key_event );
};

//----------------------------------------------------------------------------
// Class: MenuItem / Menu / MenuBar
//
// Description:
//
//   MenuBar is the horizontal top bar of menu titles; each Menu is a
//   floating pop-up of MenuItems (nestable via a sub-menu pointer), with
//   arrow navigation, Enter/Right to open, Esc up one level, and
//   separators skipped. Menus follow the floating-element discipline:
//   save the covered region, cast a shadow, draw, restore on close.
//
//----------------------------------------------------------------------------

// Menu modal-loop results.

#define MENU_RESULT_CLOSED        0    // Closed with Esc; the menu bar stays active.
#define MENU_RESULT_ACTIVATED     1    // An item was activated; the whole menu system closes.
#define MENU_RESULT_SWITCH_LEFT   2    // Left at the top level: open the adjacent menu.
#define MENU_RESULT_SWITCH_RIGHT  3    // Right at the top level: open the adjacent menu.

// Capacity of a menu item's optional shortcut label, including its
// terminator - enough for the conventional combinations ("Shift+Del",
// "Ctrl+Ins", "Ctrl+G") with room to spare.

#define MENU_SHORTCUT_SIZE  16

class Menu;

class MenuItem : public Component
{
	// Data Members

protected:

	bool  separator;                            // true = a separator row, skipped by navigation.
	Menu *sub_menu;                             // Optional nested sub-menu.
	char  shortcut [ MENU_SHORTCUT_SIZE ];      // Optional display-only shortcut label; empty = none.

	// Accessors

public:

	bool        IsSeparator ( void ) { return separator; }
	Menu       *GetSubMenu  ( void ) { return sub_menu; }
	const char *GetShortcut ( void ) { return shortcut; }

	// Mutators

	void SetSeparator ( bool new_separator )  { separator = new_separator; }
	void SetSubMenu   ( Menu *new_sub_menu )  { sub_menu = new_sub_menu; }

	void SetShortcut ( const char *new_shortcut );

	// Constructors

	MenuItem ( void );
	MenuItem ( const char *name, const char *text, const char *shortcut = "" );
};

class Menu : public Panel
{
	// Data Members

protected:

	MenuItem  *items [ LIST_ITEM_CAPACITY ];
	int        item_count;
	int        selected_index;
	bool       open;
	BYTE       border_foreground;    // Border and separator color.
	TEXTBUFFER saved_block;          // Region covered while open (floating-element discipline).

	// Accessors

public:

	int  GetItemCount     ( void ) { return item_count; }
	int  GetSelectedIndex ( void ) { return selected_index; }
	bool IsOpen           ( void ) { return open; }

	// Mutators

	void SetBorderForeground ( BYTE new_border_foreground )  { border_foreground = new_border_foreground; }

	// Constructors

	Menu ( void );
	Menu ( const char *name, const char *text );

	// Methods

	virtual void Draw      ( TEXTBUFFER *text_buffer );
	virtual bool HandleKey ( KEY_EVENT &key_event );

	void AddItem ( MenuItem *item );
	int  RunOpen ( int open_x, int open_y );    // Floating-element discipline: save block, shadow, body, modal key loop, restore.
};

class MenuBar : public Panel
{
	// Data Members

protected:

	Menu *menus [ COMPONENT_CHILD_CAPACITY ];
	int   menu_count;
	int   selected_index;
	bool  active;             // true while the menu bar owns the keyboard (F10).

	// Accessors

public:

	int  GetMenuCount ( void ) { return menu_count; }
	bool IsActive     ( void ) { return active; }

	// Mutators

	void SetActive ( bool new_active );

	// Constructors

	MenuBar ( void );
	MenuBar ( const char *name, int x, int y, int width );

	// Methods

	virtual void Draw      ( TEXTBUFFER *text_buffer );
	virtual bool HandleKey ( KEY_EVENT &key_event );

	void AddMenu       ( Menu *menu );
	void Activate      ( void );                // Modal menu-bar loop (entered on F10; F10/Esc leave it).
	int  GetMenuTitleX ( int menu_index );      // Screen column of a menu's title on the bar.
};

//----------------------------------------------------------------------------
// Class: TitleBar / StatusBar
//
// Description:
//
//   TitleBar renders a left-aligned title (the component text, updatable
//   at run time) and a right-aligned version string on one row.
//   StatusBar renders a grid of label/value field pairs across its rows
//   (DataProbe uses 2 rows x 6 columns).
//
//----------------------------------------------------------------------------

class TitleBar : public Component
{
	// Data Members

protected:

	char version_text [ COMPONENT_TEXT_SIZE ];

	// Mutators

public:

	void SetVersionText ( const char *new_version_text );

	// Constructors

	TitleBar ( void );
	TitleBar ( const char *name, int x, int y, int width, const char *text, const char *version_text );

	// Methods

	virtual void Draw ( TEXTBUFFER *text_buffer );
};

class StatusBar : public Component
{
	// Data Members

protected:

	char field_labels     [ STATUS_BAR_ROW_COUNT ][ STATUS_BAR_COLUMN_COUNT ][ STATUS_BAR_FIELD_SIZE ];
	char field_values     [ STATUS_BAR_ROW_COUNT ][ STATUS_BAR_COLUMN_COUNT ][ STATUS_BAR_FIELD_SIZE ];
	int  column_positions [ STATUS_BAR_COLUMN_COUNT ];    // Field x offsets from the bar's left edge.

	// Per-field highlight span: highlight_length characters of a field's
	// rendered text, starting at highlight_start, draw in
	// highlight_foreground (e.g. the INS of an [INS] indicator). A zero
	// length (the default) disables the highlight.

	int  highlight_start      [ STATUS_BAR_ROW_COUNT ][ STATUS_BAR_COLUMN_COUNT ];
	int  highlight_length     [ STATUS_BAR_ROW_COUNT ][ STATUS_BAR_COLUMN_COUNT ];
	BYTE highlight_foreground;

	// The value portion of every field draws in value_foreground when
	// value_foreground_enabled is set, so labels and values can be two
	// different colours (e.g. cyan labels, white values). Off by default:
	// values inherit the field foreground, unchanged for existing callers.

	BYTE value_foreground;
	bool value_foreground_enabled;

	// Mutators

public:

	void SetField               ( int row, int column, const char *label_text, const char *value_text );
	void SetValue               ( int row, int column, const char *value_text );
	void SetColumnPosition      ( int column, int x_offset );
	void SetFieldHighlight      ( int row, int column, int start, int length );
	void SetHighlightForeground ( BYTE new_highlight_foreground )  { highlight_foreground = new_highlight_foreground; }
	void SetValueForeground     ( BYTE new_value_foreground )      { value_foreground = new_value_foreground; value_foreground_enabled = true; }

	// Constructors

	StatusBar ( void );
	StatusBar ( const char *name, int x, int y, int width );

	// Methods

	virtual void Draw ( TEXTBUFFER *text_buffer );
};

//----------------------------------------------------------------------------
// Class: ApplicationPanel
//
// Description:
//
//   The standard application frame: TitleBar on row 0, MenuBar on row 1,
//   a spacer, the workspace (where the application injects its main
//   component), a spacer, and the StatusBar on the last rows.
//
//----------------------------------------------------------------------------

class ApplicationPanel : public Panel
{
	// Data Members

protected:

	TitleBar   title_bar;
	MenuBar    menu_bar;
	StatusBar  status_bar;
	Component *workspace;

	// Accessors

public:

	TitleBar  *GetTitleBar  ( void ) { return &title_bar; }
	MenuBar   *GetMenuBar   ( void ) { return &menu_bar; }
	StatusBar *GetStatusBar ( void ) { return &status_bar; }
	Component *GetWorkspace ( void ) { return workspace; }

	// Mutators

	void SetWorkspace ( Component *new_workspace );
	void Relayout     ( int new_rows );          // Re-place the status bar and resize the workspace for a new row count.

	// Constructors

	ApplicationPanel ( void );
	ApplicationPanel ( const char *name, int rows );

	// Methods

	virtual void Draw      ( TEXTBUFFER *text_buffer );
	virtual bool HandleKey ( KEY_EVENT &key_event );
};

//----------------------------------------------------------------------------
// Class: Dialog and the Modal Dialogs
//
// Description:
//
//   Dialog is the modal base: a bordered, shadowed Panel that saves the
//   region it covers on open, runs its own key loop until closed, and
//   restores the region on close (Esc = cancel / No). MessageBox shows a
//   message with Ok; ConfirmationBox shows Yes / No (Esc = No). The three
//   file dialogs combine a file/directory ListBox (via DOS findfirst /
//   findnext), a filename TextBox, and action / Cancel buttons; all files
//   are treated as binary.
//
//----------------------------------------------------------------------------

class Application;

class Dialog : public Panel
{
	// Data Members

protected:

	TEXTBUFFER saved_block;     // Region covered while open.
	int        result;          // Dialog-specific result code.
	bool       closed;

	// Accessors

public:

	int GetResult ( void ) { return result; }

	// Constructors

	Dialog ( void );
	Dialog ( const char *name, int x, int y, int width, int height, const char *text );

	// Methods

	virtual bool HandleKey ( KEY_EVENT &key_event );

	int  RunModal ( Application *application );
	void Close    ( int new_result );
};

// Dialog result codes.

#define DIALOG_RESULT_CANCEL  0
#define DIALOG_RESULT_OK      1
#define DIALOG_RESULT_YES     1
#define DIALOG_RESULT_NO      0

class MessageBox : public Dialog
{
	// Data Members

protected:

	Button ok_button;

public:

	// Constructors

	MessageBox ( void );
	MessageBox ( const char *name, const char *title, const char *message );

	// Methods

	virtual void Draw      ( TEXTBUFFER *text_buffer );
	virtual bool HandleKey ( KEY_EVENT &key_event );
};

class ConfirmationBox : public Dialog
{
	// Data Members

protected:

	Button yes_button;
	Button no_button;

public:

	// Constructors

	ConfirmationBox ( void );
	ConfirmationBox ( const char *name, const char *title, const char *message );

	// Methods

	virtual void Draw      ( TEXTBUFFER *text_buffer );
	virtual bool HandleKey ( KEY_EVENT &key_event );
};

class FileDialog : public Dialog
{
	// Data Members

protected:

	ListBox file_list;
	TextBox file_name_box;
	Button  action_button;
	Button  cancel_button;

	// Accessors

public:

	const char far *GetFileName ( void );
	void            SetFileName ( const char far *file_name );

	// Constructors

	FileDialog ( void );
	FileDialog ( const char *name, const char *text, const char *action_text );

	// Methods

	virtual bool HandleKey ( KEY_EVENT &key_event );

	void ReadDirectory        ( const char *path_mask );    // DOS findfirst/findnext listing.
	void TakeSelectedFileName ( void );                     // Copy the list selection into the filename box.
};

class NewFileDialog : public FileDialog
{
public:

	NewFileDialog ( void );
};

class FileOpenDialog : public FileDialog
{
public:

	FileOpenDialog ( void );
};

class FileSaveAsDialog : public FileDialog
{
public:

	FileSaveAsDialog ( void );
};

//----------------------------------------------------------------------------
// Class: Application
//
// Description:
//
//   The engine. Owns the root component, the off-screen back TEXTBUFFER,
//   the focus state, and the run loop. Run() renders the component tree
//   into the back buffer, flips it with FlipScreenBuffer, blocks on
//   ReadKey, and routes the resulting KEY_EVENT to the focused component;
//   an unconsumed key bubbles focused component -> parent containers ->
//   Application, which owns the global keys (F10 menu toggle, Esc).
//
//   The consumer pattern: construct the Application (sets the text mode),
//   build the component tree, SetRoot, Run(), and let the destructor
//   restore the text mode.
//
//----------------------------------------------------------------------------

class Application
{
	// Data Members

protected:

	TEXTBUFFER  back_buffer;
	Component  *root;
	Component  *focused_component;
	Dialog     *modal_dialog;        // The open modal dialog, if any (composited by RenderFrame).
	BYTE        rows;
	bool        running;

	static Application *instance;    // The running application (set by the constructor).

	// Accessors

public:

	Component  *GetRoot             ( void ) { return root; }
	Component  *GetFocusedComponent ( void ) { return focused_component; }
	TEXTBUFFER *GetBackBuffer       ( void ) { return &back_buffer; }
	BYTE        GetRows             ( void ) { return rows; }
	bool        IsRunning           ( void ) { return running; }

	static Application *GetInstance ( void ) { return instance; }

	// Mutators

	void SetRoot                ( Component *new_root );
	void SetFocusedComponent    ( Component *new_focused_component );
	void SetModalDialog         ( Dialog *new_modal_dialog )  { modal_dialog = new_modal_dialog; }
	void SetRows                ( BYTE new_rows );            // Re-set the text mode and rebuild the back buffer for a new row count (25/43/50).

	// Constructors / Destructor

	Application ( BYTE rows );

	~Application ( void );

	// Methods

	void Run           ( void );
	void Quit          ( void );
	void RenderFrame   ( void );                     // Draw tree -> back buffer -> flip.
	bool DispatchKey   ( KEY_EVENT &key_event );     // Route to focus, bubble to parents, then globals.
	void FocusNext     ( void );                     // Move focus to the next focusable component (tree order).
	void FocusPrevious ( void );                     // Move focus to the previous focusable component.
};

//----------------------------------------------------------------------------

#endif

//----------------------------------------------------------------------------
