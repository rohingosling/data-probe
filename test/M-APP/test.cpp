//****************************************************************************
// Program: MicroEdit (MicroApp Test Editor)
// Version: 0.4
// Date:    1992-07-14
// Author:  Rohin Gosling
//
// Description:
//
//   MicroEdit is a minimal text editor whose sole purpose is to validate
//   the MicroApp framework. This version completes the editor with the
//   modal dialog family and real file input/output:
//
//     - File > New / Open / Save / Save As drive the NewFileDialog,
//       FileOpenDialog, and FileSaveAsDialog: a filename box over a
//       directory ListBox (DOS findfirst/findnext), action and Cancel
//       buttons, Enter accepts, Esc cancels - and the editor really
//       loads and saves small plain-text files on disk.
//
//     - File > Exit asks through a ConfirmationBox (Yes default, Esc =
//       No); Help > About shows a MessageBox; errors surface as
//       MessageBoxes.
//
//     - Options > Settings opens a consumer-built Settings dialog
//       exercising ListBox (workspace background color, scrolling) and
//       DropDownTextBox (insert/overwrite mode), with Ok / Cancel.
//
//   All dialogs are modal, draw with a drop shadow, and restore the
//   exact background they covered on close.
//
//   Build with build.bat in this directory (results in BUILD.LOG), then
//   run TEST.EXE inside DOSBox.
//
// Compilation:
//
//   - Borland C++ 3.1, large memory model (see build.bat).
//
//****************************************************************************

#include <stdio.h>
#include <string.h>

#include "mtext.h"
#include "mapp.h"

//----------------------------------------------------------------------------
// File-Scope State
//----------------------------------------------------------------------------

// Component references, set by main before Run; used by the handlers.

static TextBox          *editor_pointer;
static RadioButtonGroup *mode_group_pointer;
static RadioButton      *insert_radio_pointer;
static RadioButton      *overwrite_radio_pointer;
static StatusBar        *status_bar_pointer;
static ApplicationPanel *chrome_pointer;

// The current document file name (empty = untitled) and the file I/O
// transfer buffer.

static char current_file_name [ 64 ];
static char file_buffer       [ 2048 ];

// Workspace background choices for the Settings dialog (list user_data
// points at these values).

static BYTE background_color_values [ 7 ] =
{
	COLOR_DARK_GRAY, COLOR_BLUE, COLOR_BLACK, COLOR_GREEN, COLOR_CYAN, COLOR_MAGENTA, COLOR_BROWN
};

static const char *background_color_names [ 7 ] =
{
	"Dark Gray", "Blue", "Black", "Green", "Cyan", "Magenta", "Brown"
};

//----------------------------------------------------------------------------
// Function: UpdateFileTitles
//
// Description:
//
//   Reflects the current document name in the editor panel's title and
//   the application title bar ("MicroEdit - NAME").
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

static void UpdateFileTitles ( void )
{
	char title [ 80 ];

	if ( current_file_name [ 0 ] != '\0' )
	{
		editor_pointer->SetText ( current_file_name );

		sprintf ( title, "MicroEdit - %s", current_file_name );
	}
	else
	{
		editor_pointer->SetText ( "UNTITLED.TXT" );

		strcpy ( title, "MicroEdit" );
	}

	chrome_pointer->GetTitleBar ()->SetText ( title );
}

//----------------------------------------------------------------------------
// Function: ShowMessage
//
// Description:
//
//   Runs a modal MessageBox with the given title and message.
//
// Arguments:
//
//   - title   : The dialog title.
//   - message : The message line.
//
// Returns:
//
//   - None
//
//----------------------------------------------------------------------------

static void ShowMessage ( const char *title, const char *message )
{
	MessageBox message_box ( "message-box", title, message );

	message_box.RunModal ( Application::GetInstance () );
}

//----------------------------------------------------------------------------
// Function: LoadEditorFile / SaveEditorFile
//
// Description:
//
//   Real file input/output for the editor: LoadEditorFile reads a
//   plain-text file (up to the editor buffer size) into the editor;
//   SaveEditorFile writes the editor text back to disk. Both update the
//   current document name and titles, and report failures through a
//   MessageBox.
//
// Arguments:
//
//   - file_name : The DOS file name to load / save.
//
// Returns:
//
//   - None
//
//----------------------------------------------------------------------------

static void LoadEditorFile ( const char *file_name )
{
	FILE     *file;
	unsigned  length;

	file = fopen ( file_name, "rt" );

	if ( file == NULL )
	{
		ShowMessage ( "Error", "Could not open the file." );
		return;
	}

	length = fread ( file_buffer, 1, sizeof ( file_buffer ) - 1, file );

	file_buffer [ length ] = '\0';

	fclose ( file );

	editor_pointer->SetEditText ( file_buffer );

	strcpy ( current_file_name, file_name );

	UpdateFileTitles ();

	editor_pointer->FireChange ();
}

static void SaveEditorFile ( const char *file_name )
{
	FILE *file;

	file = fopen ( file_name, "wt" );

	if ( file == NULL )
	{
		ShowMessage ( "Error", "Could not save the file." );
		return;
	}

	fputs ( ( const char * ) editor_pointer->GetEditText (), file );

	fclose ( file );

	strcpy ( current_file_name, file_name );

	UpdateFileTitles ();

	ShowMessage ( "Save", "File saved." );
}

//----------------------------------------------------------------------------
// Function: OnEditorChange
//
// Description:
//
//   OnChange handler for the editor, fired on every consumed key: keeps
//   the mode radio group in sync and refreshes the status bar's Line /
//   Col / Mode fields.
//
// Arguments:
//
//   - sender    : The editor TextBox.
//   - user_data : Unused.
//
// Returns:
//
//   - None
//
//----------------------------------------------------------------------------

static void OnEditorChange ( Component *sender, void *user_data )
{
	char value [ 16 ];

	( void ) sender;
	( void ) user_data;

	mode_group_pointer->SelectRadioButton ( editor_pointer->IsInsertEnabled () ? insert_radio_pointer : overwrite_radio_pointer );

	sprintf ( value, "%u", editor_pointer->GetCursorLine () + 1 );
	status_bar_pointer->SetValue ( 0, 0, value );

	sprintf ( value, "%u", editor_pointer->GetCursorColumn () + 1 );
	status_bar_pointer->SetValue ( 0, 1, value );

	status_bar_pointer->SetValue ( 0, 2, editor_pointer->IsInsertEnabled () ? "INS" : "OVR" );
}

//----------------------------------------------------------------------------
// Function: OnInsertModeSelected
//
// Description:
//
//   OnChange handler shared by both mode radio buttons: sets the
//   editor's insert/overwrite mode according to which radio button
//   fired, then refreshes the status display.
//
// Arguments:
//
//   - sender    : The radio button that was selected.
//   - user_data : The editor TextBox.
//
// Returns:
//
//   - None
//
//----------------------------------------------------------------------------

static void OnInsertModeSelected ( Component *sender, void *user_data )
{
	TextBox *editor;

	editor = ( TextBox * ) user_data;

	editor->SetInsertEnabled ( sender == ( Component * ) insert_radio_pointer );

	editor->FireChange ();
}

//----------------------------------------------------------------------------
// Function: OnInfoPanelToggle
//
// Description:
//
//   OnChange handler for the Info-panel check box: shows or hides the
//   nested info panel.
//
// Arguments:
//
//   - sender    : The check box.
//   - user_data : The info panel.
//
// Returns:
//
//   - None
//
//----------------------------------------------------------------------------

static void OnInfoPanelToggle ( Component *sender, void *user_data )
{
	CheckBox *check_box;
	Panel    *panel;

	check_box = ( CheckBox * ) sender;
	panel     = ( Panel * ) user_data;

	panel->SetVisible ( check_box->IsChecked () );
}

//----------------------------------------------------------------------------
// Function: OnClearButtonActivate
//
// Description:
//
//   OnActivate handler for the Clear Text button: empties the editor.
//
// Arguments:
//
//   - sender    : The button.
//   - user_data : The editor TextBox.
//
// Returns:
//
//   - None
//
//----------------------------------------------------------------------------

static void OnClearButtonActivate ( Component *sender, void *user_data )
{
	TextBox *editor;

	( void ) sender;

	editor = ( TextBox * ) user_data;

	editor->SetEditText ( "" );
	editor->FireChange  ();
}

//----------------------------------------------------------------------------
// Function: OnMenuNewFile / OnMenuOpenFile / OnMenuSave / OnMenuSaveAs
//
// Description:
//
//   The File menu's document actions, driving the three file dialogs
//   with real file input/output: New names a fresh empty document, Open
//   loads the chosen file, Save writes to the current name (falling
//   through to Save As for an untitled document), and Save As writes to
//   a newly chosen name.
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

static void OnMenuSaveAs ( Component *sender, void *user_data )
{
	char name [ 64 ];

	( void ) sender;
	( void ) user_data;

	{
		FileSaveAsDialog dialog;

		if ( dialog.RunModal ( Application::GetInstance () ) != DIALOG_RESULT_OK ) return;

		strcpy ( name, ( const char * ) dialog.GetFileName () );
	}

	if ( name [ 0 ] != '\0' ) SaveEditorFile ( name );
}

static void OnMenuSave ( Component *sender, void *user_data )
{
	if ( current_file_name [ 0 ] != '\0' )
	{
		SaveEditorFile ( current_file_name );
	}
	else
	{
		OnMenuSaveAs ( sender, user_data );
	}
}

static void OnMenuNewFile ( Component *sender, void *user_data )
{
	char name [ 64 ];

	( void ) sender;
	( void ) user_data;

	{
		NewFileDialog dialog;

		if ( dialog.RunModal ( Application::GetInstance () ) != DIALOG_RESULT_OK ) return;

		strcpy ( name, ( const char * ) dialog.GetFileName () );
	}

	editor_pointer->SetEditText ( "" );

	strcpy ( current_file_name, name );

	UpdateFileTitles ();

	editor_pointer->FireChange ();
}

static void OnMenuOpenFile ( Component *sender, void *user_data )
{
	char name [ 64 ];

	( void ) sender;
	( void ) user_data;

	{
		FileOpenDialog dialog;

		if ( dialog.RunModal ( Application::GetInstance () ) != DIALOG_RESULT_OK ) return;

		strcpy ( name, ( const char * ) dialog.GetFileName () );
	}

	if ( name [ 0 ] != '\0' ) LoadEditorFile ( name );
}

//----------------------------------------------------------------------------
// Function: OnMenuExit / OnMenuAbout
//
// Description:
//
//   File > Exit asks for confirmation (Yes default, Esc = No) before
//   quitting; Help > About shows the about MessageBox.
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

	{
		ConfirmationBox confirmation ( "exit-confirm", "Exit", "Exit MicroEdit?" );

		if ( confirmation.RunModal ( Application::GetInstance () ) != DIALOG_RESULT_YES ) return;
	}

	Application::GetInstance ()->Quit ();
}

static void OnMenuAbout ( Component *sender, void *user_data )
{
	( void ) sender;
	( void ) user_data;

	ShowMessage ( "About", "MicroEdit 0.4 - the MicroApp test editor." );
}

//----------------------------------------------------------------------------
// Function: OnMenuInsertMode / OnMenuOverwriteMode
//
// Description:
//
//   OnActivate handlers for Options > Insert Mode / Overwrite Mode: set
//   the editor's mode and refresh the radios and status display.
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

static void OnMenuInsertMode ( Component *sender, void *user_data )
{
	( void ) sender;
	( void ) user_data;

	editor_pointer->SetInsertEnabled ( true );
	editor_pointer->FireChange       ();
}

static void OnMenuOverwriteMode ( Component *sender, void *user_data )
{
	( void ) sender;
	( void ) user_data;

	editor_pointer->SetInsertEnabled ( false );
	editor_pointer->FireChange       ();
}

//----------------------------------------------------------------------------
// Function: OnDialogOkButton / OnDialogCancelButton
//
// Description:
//
//   Ok / Cancel button handlers for consumer-built dialogs: close the
//   dialog (passed as user_data) with the matching result.
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

static void OnDialogOkButton ( Component *sender, void *user_data )
{
	( void ) sender;

	( ( Dialog * ) user_data )->Close ( DIALOG_RESULT_OK );
}

static void OnDialogCancelButton ( Component *sender, void *user_data )
{
	( void ) sender;

	( ( Dialog * ) user_data )->Close ( DIALOG_RESULT_CANCEL );
}

//----------------------------------------------------------------------------
// Function: OnMenuSettings
//
// Description:
//
//   Options > Settings: a consumer-built modal Settings dialog
//   exercising ListBox (workspace background color, more entries than
//   fit so the list scrolls) and DropDownTextBox (insert/overwrite
//   mode), with Ok / Cancel buttons. On Ok, the chosen background and
//   mode are applied.
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

static void OnMenuSettings ( Component *sender, void *user_data )
{
	int i;

	( void ) sender;
	( void ) user_data;

	Dialog dialog ( "settings-dialog", 20, 4, 40, 16, "Settings" );

	// Workspace background color list (scrolls: 7 entries, 5 visible).

	Label background_label ( "bg-label", 22, 6, "Workspace background:" );

	background_label.SetColors ( COLOR_BLACK, COLOR_LIGHT_GRAY );

	ListBox background_list ( "bg-list", 22, 7, 21, 7 );

	for ( i = 0; i < 7; i++ )
	{
		background_list.AddItem ( background_color_names [ i ], &background_color_values [ i ] );
	}

	for ( i = 0; i < 7; i++ )
	{
		if ( background_color_values [ i ] == chrome_pointer->GetBackground () )
		{
			background_list.SetSelectedIndex ( i );
			break;
		}
	}

	// Insert/overwrite mode drop-down.

	Label mode_label ( "mode-label", 45, 6, "Mode:" );

	mode_label.SetColors ( COLOR_BLACK, COLOR_LIGHT_GRAY );

	DropDownTextBox mode_drop ( "mode-drop", 45, 7, 13, 4 );

	mode_drop.GetDropDownList ()->AddItem ( "Insert",    NULL );
	mode_drop.GetDropDownList ()->AddItem ( "Overwrite", NULL );

	mode_drop.SetEntryText ( editor_pointer->IsInsertEnabled () ? "Insert" : "Overwrite" );

	// Ok / Cancel.

	Button ok_button     ( "settings-ok",     24, 17, 10, "Ok" );
	Button cancel_button ( "settings-cancel", 40, 17, 10, "Cancel" );

	ok_button.SetOnActivate     ( OnDialogOkButton,     &dialog );
	cancel_button.SetOnActivate ( OnDialogCancelButton, &dialog );

	dialog.AddComponent ( &background_label );
	dialog.AddComponent ( &background_list );
	dialog.AddComponent ( &mode_label );
	dialog.AddComponent ( &mode_drop );
	dialog.AddComponent ( &ok_button );
	dialog.AddComponent ( &cancel_button );

	if ( dialog.RunModal ( Application::GetInstance () ) == DIALOG_RESULT_OK )
	{
		ListItem *item;

		item = background_list.GetSelectedItem ();

		if ( item != NULL )
		{
			chrome_pointer->SetColors ( COLOR_LIGHT_GRAY, *( BYTE * ) item->GetUserData () );
		}

		editor_pointer->SetInsertEnabled ( strcmp ( mode_drop.GetEntryText (), "Insert" ) == 0 );
		editor_pointer->FireChange       ();
	}
}

//----------------------------------------------------------------------------
// Function: main
//
// Description:
//
//   MicroEdit entry point, following the MicroApp consumer pattern: the
//   Application constructor sets the text mode, the chrome and component
//   tree are built, wired, and installed as the root, Run() drives the
//   render / dispatch loop until File > Exit (confirmed) or Esc, and the
//   Application destructor restores the text mode.
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
	{
		Application application ( TEXT_MODE_25_ROWS );

		// The chrome: title bar, menu bar, workspace area, status bar.

		ApplicationPanel chrome ( "chrome", 25 );

		chrome.SetColors ( COLOR_LIGHT_GRAY, COLOR_DARK_GRAY );

		chrome.GetTitleBar ()->SetText        ( "MicroEdit" );
		chrome.GetTitleBar ()->SetVersionText ( "Version 0.4" );

		// The menu tree.

		Menu file_menu    ( "file-menu",    "File" );
		Menu edit_menu    ( "edit-menu",    "Edit" );
		Menu options_menu ( "options-menu", "Options" );
		Menu help_menu    ( "help-menu",    "Help" );

		MenuItem file_new       ( "file-new",       "New..." );
		MenuItem file_open      ( "file-open",      "Open..." );
		MenuItem file_save      ( "file-save",      "Save" );
		MenuItem file_save_as   ( "file-save-as",   "Save As..." );
		MenuItem file_separator ( "file-separator", "" );
		MenuItem file_exit      ( "file-exit",      "Exit" );

		// Two of the three Edit items carry a display-only shortcut label,
		// exercising the drop-down's shortcut column: the width budget, the
		// right-aligned render in both the normal and highlight colours, and
		// a labelled item sitting beside an unlabelled one. The File, Options
		// and Help menus stay bare, so a menu with no shortcut column at all
		// is checked alongside. MicroEdit dispatches none of these keys -
		// that is the point: the labels are display only.

		MenuItem edit_cut   ( "edit-cut",   "Cut",   "Shift+Del" );
		MenuItem edit_copy  ( "edit-copy",  "Copy",  "Ctrl+Ins"  );
		MenuItem edit_paste ( "edit-paste", "Paste" );

		MenuItem options_insert    ( "options-insert",    "Insert Mode" );
		MenuItem options_overwrite ( "options-overwrite", "Overwrite Mode" );
		MenuItem options_separator ( "options-separator", "" );
		MenuItem options_settings  ( "options-settings",  "Settings..." );

		MenuItem help_about ( "help-about", "About" );

		file_separator.SetSeparator    ( true );
		options_separator.SetSeparator ( true );

		file_menu.AddItem ( &file_new );
		file_menu.AddItem ( &file_open );
		file_menu.AddItem ( &file_save );
		file_menu.AddItem ( &file_save_as );
		file_menu.AddItem ( &file_separator );
		file_menu.AddItem ( &file_exit );

		edit_menu.AddItem ( &edit_cut );
		edit_menu.AddItem ( &edit_copy );
		edit_menu.AddItem ( &edit_paste );

		options_menu.AddItem ( &options_insert );
		options_menu.AddItem ( &options_overwrite );
		options_menu.AddItem ( &options_separator );
		options_menu.AddItem ( &options_settings );

		help_menu.AddItem ( &help_about );

		chrome.GetMenuBar ()->AddMenu ( &file_menu );
		chrome.GetMenuBar ()->AddMenu ( &edit_menu );
		chrome.GetMenuBar ()->AddMenu ( &options_menu );
		chrome.GetMenuBar ()->AddMenu ( &help_menu );

		// The status bar: Line / Col / Mode plus key hints.

		chrome.GetStatusBar ()->SetField ( 0, 0, "Line:", "1" );
		chrome.GetStatusBar ()->SetField ( 0, 1, "Col:",  "1" );
		chrome.GetStatusBar ()->SetField ( 0, 2, "Mode:", "INS" );
		chrome.GetStatusBar ()->SetField ( 1, 0, "F10 Menu",  "" );
		chrome.GetStatusBar ()->SetField ( 1, 1, "Tab Focus", "" );
		chrome.GetStatusBar ()->SetField ( 1, 2, "Esc Quit",  "" );

		// The workspace: the live multi-line TextBox editing surface.

		TextBox editor ( "editor", 2, 3, 56, 19, 2048, true );

		editor.SetColors ( COLOR_LIGHT_GREEN, COLOR_BLACK );
		editor.SetText   ( "UNTITLED.TXT" );

		editor.SetEditText ( "MicroEdit 0.4 - dialogs and real file I/O.\n"
		                     "\n"
		                     "File > Save As... writes this text to disk through\n"
		                     "the Save As dialog; File > Open... loads a file back\n"
		                     "through the Open dialog (filename box, directory\n"
		                     "list, action and Cancel buttons; Esc cancels).\n"
		                     "\n"
		                     "File > Exit asks for confirmation. Help > About\n"
		                     "shows a message box. Options > Settings... changes\n"
		                     "the workspace background (scrolling list) and the\n"
		                     "editing mode (drop-down)." );

		// The Options group: radio group, check box, and button.

		Group options_group ( "options", 60, 3, 19, 10, "Options" );

		options_group.SetColors ( COLOR_BLACK, COLOR_LIGHT_GRAY );

		RadioButtonGroup mode_group ( "mode-group", 62, 5, 15, 2 );

		RadioButton insert_radio    ( "insert-radio",    62, 5, "Insert" );
		RadioButton overwrite_radio ( "overwrite-radio", 62, 6, "Overwrite" );

		insert_radio.SetColors    ( COLOR_BLACK, COLOR_LIGHT_GRAY );
		overwrite_radio.SetColors ( COLOR_BLACK, COLOR_LIGHT_GRAY );

		CheckBox info_check_box ( "info-check", 62, 8, "Info panel" );

		info_check_box.SetColors  ( COLOR_BLACK, COLOR_LIGHT_GRAY );
		info_check_box.SetChecked ( true );

		Button clear_button ( "clear-button", 62, 10, 15, "Clear Text" );

		// The nested info panel, toggled by the check box.

		Panel info_panel ( "info", 60, 15, 19, 6 );

		info_panel.SetColors        ( COLOR_BLACK, COLOR_LIGHT_GRAY );
		info_panel.SetLineThickness ( '=' );
		info_panel.SetShadowEnabled ( true );
		info_panel.SetText          ( "Info" );

		Label info_line_1 ( "info1", 62, 16, "This panel is" );
		Label info_line_2 ( "info2", 62, 17, "toggled by the" );
		Label info_line_3 ( "info3", 62, 18, "check box." );

		info_line_1.SetColors ( COLOR_BLACK, COLOR_LIGHT_GRAY );
		info_line_2.SetColors ( COLOR_BLACK, COLOR_LIGHT_GRAY );
		info_line_3.SetColors ( COLOR_BLACK, COLOR_LIGHT_GRAY );

		// Wire the handlers.

		editor_pointer          = &editor;
		mode_group_pointer      = &mode_group;
		insert_radio_pointer    = &insert_radio;
		overwrite_radio_pointer = &overwrite_radio;
		status_bar_pointer      = chrome.GetStatusBar ();
		chrome_pointer          = &chrome;

		current_file_name [ 0 ] = '\0';

		insert_radio.SetSelected ( true );

		insert_radio.SetOnChange    ( OnInsertModeSelected,  &editor );
		overwrite_radio.SetOnChange ( OnInsertModeSelected,  &editor );
		editor.SetOnChange          ( OnEditorChange,        NULL );
		info_check_box.SetOnChange  ( OnInfoPanelToggle,     &info_panel );
		clear_button.SetOnActivate  ( OnClearButtonActivate, &editor );

		file_new.SetOnActivate          ( OnMenuNewFile,       NULL );
		file_open.SetOnActivate         ( OnMenuOpenFile,      NULL );
		file_save.SetOnActivate         ( OnMenuSave,          NULL );
		file_save_as.SetOnActivate      ( OnMenuSaveAs,        NULL );
		file_exit.SetOnActivate         ( OnMenuExit,          NULL );
		options_insert.SetOnActivate    ( OnMenuInsertMode,    NULL );
		options_overwrite.SetOnActivate ( OnMenuOverwriteMode, NULL );
		options_settings.SetOnActivate  ( OnMenuSettings,      NULL );
		help_about.SetOnActivate        ( OnMenuAbout,         NULL );

		// Assemble the tree.

		mode_group.AddRadioButton ( &insert_radio );
		mode_group.AddRadioButton ( &overwrite_radio );

		options_group.AddComponent ( &mode_group );
		options_group.AddComponent ( &info_check_box );
		options_group.AddComponent ( &clear_button );

		info_panel.AddComponent ( &info_line_1 );
		info_panel.AddComponent ( &info_line_2 );
		info_panel.AddComponent ( &info_line_3 );

		chrome.SetWorkspace  ( &editor );
		chrome.AddComponent  ( &options_group );
		chrome.AddComponent  ( &info_panel );

		// Run until File > Exit or Esc; the destructor restores the mode.

		application.SetRoot ( &chrome );
		application.Run     ();
	}

	printf ( "MicroEdit exited.\n" );

	return 0;
}

//----------------------------------------------------------------------------
