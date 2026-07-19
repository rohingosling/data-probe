//****************************************************************************
// Program: Data Probe (FileBuffer)
// Version: 1.5
// Date:    1992-07-14
// Author:  Rohin Gosling
//
// Description:
//
//   FileBuffer implementation: the farcoreleft-sized working buffer, the
//   whole-file and chunked (work-file) paging paths, the dirty-chunk
//   flush with its tail splice for size-changing edits, and the file
//   open/save round trips. All disk access goes through the Borland
//   low-level handle functions (open/read/write/lseek/chsize) in binary
//   mode - Data Probe treats every file as raw bytes.
//
//   Chunked mode never touches the original file until Save: Open copies
//   the original into the work file (DPROBE.$$$ in the current
//   directory), every flush and page reads and writes the work file
//   only, and Save copies the finished work file back over the original.
//
//   Find and Replace build on the same paging: FindPattern scans a
//   whole-file buffer in RAM or pages a chunked file window by window
//   through the working buffer (restoring the loaded chunk afterward), and
//   ReplaceInChunk splices a replacement into the loaded window so the
//   normal flush carries any size change into the file.
//
//****************************************************************************

#include <stddef.h>
#include <string.h>
#include <alloc.h>
#include <io.h>
#include <fcntl.h>
#include <sys\stat.h>

#include "mtext.h"
#include "mapp.h"
#include "filebuf.h"

//----------------------------------------------------------------------------
// File-Scope State
//----------------------------------------------------------------------------

// The near transfer buffer for the work-file tail splice, where the main
// buffer is occupied by the chunk being flushed.

static BYTE transfer_buffer [ FILE_BUFFER_TRANSFER_SIZE ];

//----------------------------------------------------------------------------
// Function: FullRead / FullWrite
//
// Description:
//
//   Transfer exactly length bytes to / from a far buffer, returning true
//   only on a complete transfer. read and write return the transferred
//   count as an int; a length above 32767 wraps both the return value
//   and ( int ) length negative identically, so the equality holds for a
//   full transfer and fails for a short (error) one. Centralizing the
//   idiom keeps every call site a single readable predicate.
//
// Arguments:
//
//   - handle : The open file handle.
//   - buffer : The far data buffer.
//   - length : The byte count to transfer (at most one buffer, so 16-bit).
//
// Returns:
//
//   - true when exactly length bytes were transferred, false otherwise.
//
//----------------------------------------------------------------------------

static bool FullRead ( int handle, void far *buffer, unsigned length )
{
	// Report a full transfer as equality on the transferred count.

	return read ( handle, buffer, length ) == ( int ) length;
}

static bool FullWrite ( int handle, void far *buffer, unsigned length )
{
	// Report a full transfer as equality on the transferred count.

	return write ( handle, buffer, length ) == ( int ) length;
}

//----------------------------------------------------------------------------
// Function: MatchAt
//
// Description:
//
//   Tests whether the pattern occurs in the far data buffer at the given
//   position - the innermost comparison of the pattern search. The caller
//   guarantees position + pattern_length stays within the buffer.
//
// Arguments:
//
//   - data           : The far data buffer to search.
//   - position       : The byte offset to test at.
//   - pattern        : The far pattern bytes.
//   - pattern_length : The pattern's length in bytes.
//
// Returns:
//
//   - true when the pattern matches at position, false otherwise.
//
//----------------------------------------------------------------------------

static bool MatchAt ( const BYTE far *data, unsigned long position, const BYTE far *pattern, unsigned pattern_length )
{
	// Local variables.

	unsigned i;

	// Compare the pattern byte-for-byte at position.

	for ( i = 0; i < pattern_length; i++ )
	{
		if ( data [ position + ( unsigned long ) i ] != pattern [ i ] ) return false;
	}

	// Every byte matched.

	return true;
}

//----------------------------------------------------------------------------
// Function: FileBuffer::FileBuffer / ~FileBuffer
//
// Description:
//
//   Initializes an unallocated, closed buffer; the destructor closes any
//   open file state (deleting the work file) and frees the buffer.
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

FileBuffer::FileBuffer ( void )
{
	// Start as an unallocated, closed buffer with no file state.

	buffer            = NULL;
	buffer_size       = 0;
	chunk_size        = 0;
	file_length       = 0;
	chunk_offset      = 0;
	chunk_length      = 0;
	chunk_disk_length = 0;
	work_length       = 0;
	work_handle       = -1;
	chunked           = false;
	chunk_dirty       = false;
	file_dirty        = false;
	file_path [ 0 ]   = '\0';
}

FileBuffer::~FileBuffer ( void )
{
	// Discard the file state (deleting the work file), then free the buffer.

	Close   ();
	Release ();
}

//----------------------------------------------------------------------------
// Function: FileBuffer::Allocate / Release
//
// Description:
//
//   Allocate probes free far memory with farcoreleft and sizes the
//   working buffer at min( configured_max, available - reserve ), with
//   the configured maximum clamped to the 1024-byte minimum and the
//   chunk ceiling (the chunk plus its growth headroom stays
//   within one 16-bit offset window). The allocation is the chunk size
//   plus the insert-growth headroom. Release frees the buffer.
//
// Arguments:
//
//   - configured_max : The configured maximum chunk size in bytes.
//
// Returns:
//
//   - Allocate: true when the buffer was allocated, false when far
//     memory cannot fit even the minimum chunk.
//
//----------------------------------------------------------------------------

bool FileBuffer::Allocate ( unsigned long configured_max )
{
	// Local variables.

	unsigned long available;

	// Already allocated: nothing to do.

	if ( buffer != NULL ) return true;

	// Adopt the configured chunk size, clamped to the allowed range.

	chunk_size = configured_max;

	if ( chunk_size < FILE_BUFFER_MINIMUM_CHUNK ) chunk_size = FILE_BUFFER_MINIMUM_CHUNK;
	if ( chunk_size > FILE_BUFFER_CHUNK_CEILING ) chunk_size = FILE_BUFFER_CHUNK_CEILING;

	// Probe free far memory.

	available = farcoreleft ();

	// Refuse when even the minimum chunk (plus the reserve and headroom) cannot fit.

	if ( available < FILE_BUFFER_MEMORY_RESERVE + FILE_BUFFER_MINIMUM_CHUNK + FILE_BUFFER_GROWTH_HEADROOM ) return false;

	// Shrink the chunk so the allocation leaves the memory reserve free.

	if ( chunk_size + FILE_BUFFER_GROWTH_HEADROOM + FILE_BUFFER_MEMORY_RESERVE > available )
	{
		chunk_size = available - FILE_BUFFER_MEMORY_RESERVE - FILE_BUFFER_GROWTH_HEADROOM;
	}

	// Allocate the chunk plus its insert-growth headroom.

	buffer_size = chunk_size + FILE_BUFFER_GROWTH_HEADROOM;
	buffer      = ( BYTE far * ) farmalloc ( buffer_size );

	// Report whether the allocation succeeded.

	return buffer != NULL;
}

void FileBuffer::Release ( void )
{
	// Free the working buffer, if any.

	if ( buffer != NULL )
	{
		farfree ( buffer );

		buffer      = NULL;
		buffer_size = 0;
	}
}

//----------------------------------------------------------------------------
// Function: FileBuffer::NewFile
//
// Description:
//
//   Starts a blank whole-file buffer - a genuinely empty file, no bytes
//   at all - discarding any previous file state. The HexView renders the
//   empty file as its minimum appearance: one ghost row carrying the
//   cursor, so there is always somewhere to type. The path is normally
//   empty: a new file stays unnamed and unwritten until Save (which
//   falls through to Save As, since Save on an unnamed buffer reports
//   FILE_IO_ERROR_NO_NAME).
//
// Arguments:
//
//   - path : The new file's path, or "" for an unnamed buffer.
//
// Returns:
//
//   - None
//
//----------------------------------------------------------------------------

void FileBuffer::NewFile ( const char *path )
{
	// Discard any previous file state.

	Close ();

	// Adopt the new path (bounded copy), empty for an unnamed buffer.

	strncpy ( file_path, ( path != NULL ) ? path : "", FILE_BUFFER_PATH_SIZE - 1 );

	file_path [ FILE_BUFFER_PATH_SIZE - 1 ] = '\0';
}

//----------------------------------------------------------------------------
// Function: FileBuffer::Open
//
// Description:
//
//   Opens a file for editing, discarding any previous file state. A file
//   no larger than the chunk size loads whole into the buffer. A larger
//   file is copied to the work file (so the original stays untouched
//   until Save), the work file is held open for paging, and chunk 0 is
//   loaded.
//
// Arguments:
//
//   - path : The file to open.
//
// Returns:
//
//   - FILE_IO_OK, or the FILE_IO_ERROR_* code of the failed step.
//
//----------------------------------------------------------------------------

int FileBuffer::Open ( const char *path )
{
	// Local variables.

	int           source_handle;
	long          source_length;
	unsigned long remaining;
	unsigned      block;
	int           result;

	// Discard any previous file state.

	Close ();

	// Open the source file.

	source_handle = open ( path, O_RDONLY | O_BINARY );

	if ( source_handle < 0 ) return FILE_IO_ERROR_OPEN;

	// Measure the source file; a failed query abandons the open.

	source_length = filelength ( source_handle );

	if ( source_length < 0 )
	{
		close ( source_handle );

		return FILE_IO_ERROR_READ;
	}

	// Adopt the file's path (bounded copy).

	strncpy ( file_path, path, FILE_BUFFER_PATH_SIZE - 1 );

	file_path [ FILE_BUFFER_PATH_SIZE - 1 ] = '\0';

	if ( ( unsigned long ) source_length <= chunk_size )
	{
		// Whole-file mode: the entire file into the buffer.

		block = ( unsigned ) source_length;

		if ( ( block > 0 ) && !FullRead ( source_handle, buffer, block ) )
		{
			close ( source_handle );
			Close ();

			return FILE_IO_ERROR_READ;
		}

		close ( source_handle );

		// Record the whole-file state: the loaded chunk is the entire file.

		file_length       = ( unsigned long ) source_length;
		chunk_offset      = 0;
		chunk_length      = file_length;
		chunk_disk_length = file_length;
		chunked           = false;

		// The whole file is loaded.

		return FILE_IO_OK;
	}

	// Chunked mode: copy the original into the work file (through the
	// main buffer, which is about to be reloaded anyway), keep the work
	// file open for paging, and load chunk 0.

	work_handle = open ( FILE_BUFFER_WORK_PATH, O_RDWR | O_BINARY | O_CREAT | O_TRUNC, S_IREAD | S_IWRITE );

	if ( work_handle < 0 )
	{
		close ( source_handle );
		Close ();

		return FILE_IO_ERROR_OPEN;
	}

	// Copy in buffer-size blocks through the main buffer (about to be
	// reloaded by LoadChunk anyway).

	remaining = ( unsigned long ) source_length;

	while ( remaining > 0 )
	{
		block = ( remaining > buffer_size ) ? ( unsigned ) buffer_size : ( unsigned ) remaining;

		if ( !FullRead ( source_handle, buffer, block ) )
		{
			close ( source_handle );
			Close ();

			return FILE_IO_ERROR_READ;
		}

		if ( !FullWrite ( work_handle, buffer, block ) )
		{
			close ( source_handle );
			Close ();

			return FILE_IO_ERROR_WRITE;
		}

		remaining -= block;
	}

	// The source is fully copied; the work file carries the paging from here.

	close ( source_handle );

	work_length = ( unsigned long ) source_length;
	file_length = ( unsigned long ) source_length;
	chunked     = true;

	// Load the first window.

	result = LoadChunk ( 0 );

	if ( result != FILE_IO_OK ) Close ();

	// Report the outcome of the open.

	return result;
}

//----------------------------------------------------------------------------
// Function: FileBuffer::Save / SaveAs
//
// Description:
//
//   Save writes the file back to its path: whole-file mode writes the
//   buffer straight out; chunked mode flushes the dirty chunk to the
//   work file, copies the work file over the original (through the main
//   buffer), and reloads the current chunk. SaveAs renames the file
//   first - the caller should re-attach its view after a chunked Save,
//   since the buffer is reloaded on the chunk grid.
//
// Arguments:
//
//   - path : SaveAs: the new file path.
//
// Returns:
//
//   - FILE_IO_OK, or the FILE_IO_ERROR_* code of the failed step
//     (FILE_IO_ERROR_NO_NAME when the buffer is unnamed).
//
//----------------------------------------------------------------------------

int FileBuffer::Save ( void )
{
	// Local variables.

	int           target_handle;
	unsigned long remaining;
	unsigned      block;
	int           result;

	// An unnamed buffer cannot Save: the caller runs Save As instead.

	if ( file_path [ 0 ] == '\0' ) return FILE_IO_ERROR_NO_NAME;

	// Whole-file mode: write the buffer straight back to the file.

	if ( !chunked )
	{
		target_handle = open ( file_path, O_WRONLY | O_BINARY | O_CREAT | O_TRUNC, S_IREAD | S_IWRITE );

		if ( target_handle < 0 ) return FILE_IO_ERROR_OPEN;

		block = ( unsigned ) chunk_length;

		if ( ( block > 0 ) && !FullWrite ( target_handle, buffer, block ) )
		{
			close ( target_handle );

			return FILE_IO_ERROR_WRITE;
		}

		close ( target_handle );

		// The file is clean again.

		file_dirty = false;

		return FILE_IO_OK;
	}

	// Chunked mode: flush, copy the work file over the original, then
	// reload the current chunk (the copy ran through the main buffer).

	result = FlushChunk ();

	if ( result != FILE_IO_OK ) return result;

	target_handle = open ( file_path, O_WRONLY | O_BINARY | O_CREAT | O_TRUNC, S_IREAD | S_IWRITE );

	if ( target_handle < 0 ) return FILE_IO_ERROR_OPEN;

	lseek ( work_handle, 0L, SEEK_SET );

	remaining = work_length;

	while ( remaining > 0 )
	{
		block = ( remaining > buffer_size ) ? ( unsigned ) buffer_size : ( unsigned ) remaining;

		if ( !FullRead ( work_handle, buffer, block ) )
		{
			close ( target_handle );
			LoadChunk ( chunk_offset );

			return FILE_IO_ERROR_READ;
		}

		if ( !FullWrite ( target_handle, buffer, block ) )
		{
			close ( target_handle );
			LoadChunk ( chunk_offset );

			return FILE_IO_ERROR_WRITE;
		}

		remaining -= block;
	}

	close ( target_handle );

	// Reload the chunk the copy displaced from the buffer.

	result = LoadChunk ( chunk_offset );

	if ( result == FILE_IO_OK ) file_dirty = false;

	// Report the outcome of the save.

	return result;
}

int FileBuffer::SaveAs ( const char *path )
{
	// Adopt the new path, then run the normal Save.

	strncpy ( file_path, path, FILE_BUFFER_PATH_SIZE - 1 );

	file_path [ FILE_BUFFER_PATH_SIZE - 1 ] = '\0';

	return Save ();
}

//----------------------------------------------------------------------------
// Function: FileBuffer::Close
//
// Description:
//
//   Discards the file state without saving: closes and deletes the work
//   file and resets to an empty unnamed buffer. The allocation itself is
//   kept (Release frees it).
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

void FileBuffer::Close ( void )
{
	// Close and delete the work file, if one is open.

	if ( work_handle >= 0 )
	{
		close  ( work_handle );
		unlink ( FILE_BUFFER_WORK_PATH );

		work_handle = -1;
	}

	// Reset to an empty, unnamed buffer (the allocation itself is kept).

	file_length       = 0;
	chunk_offset      = 0;
	chunk_length      = 0;
	chunk_disk_length = 0;
	work_length       = 0;
	chunked           = false;
	chunk_dirty       = false;
	file_dirty        = false;
	file_path [ 0 ]   = '\0';
}

//----------------------------------------------------------------------------
// Function: FileBuffer::PageTo / ReflowChunk
//
// Description:
//
//   PageTo slides the chunk window: if the requested file offset is
//   already inside the loaded window it is a no-op; otherwise the dirty
//   window is flushed and a new window loads with **overlap** - based so
//   the target keeps context_bytes of the previously visible data on
//   the approach side (behind it when moving down-file, ahead of it
//   when moving up-file). The seam - and its paging pause - therefore
//   lands a screenful away from the crossing point, so reversing
//   direction scrolls straight on without paging again.
//
//   ReflowChunk flushes and reloads unconditionally - the application
//   calls it when in-RAM growth nears the buffer's physical capacity,
//   so the growth is spliced into the work file and the window returns
//   to its nominal size with the insert headroom restored. The reload
//   keeps the current window base (re-basing onto the offset only if
//   the trim would push it outside).
//
// Arguments:
//
//   - file_offset   : The absolute file offset that must be in the window.
//   - context_bytes : Overlap kept loaded on the approach side (a
//                     screenful; clamped below the window size).
//
// Returns:
//
//   - FILE_IO_OK, or the FILE_IO_ERROR_* code of the failed step.
//
//----------------------------------------------------------------------------

int FileBuffer::PageTo ( unsigned long file_offset, unsigned long context_bytes )
{
	// Local variables.

	unsigned long window_offset;
	int           result;

	// A whole-file buffer never pages.

	if ( !chunked ) return FILE_IO_OK;

	// The target is already inside the loaded window: nothing to do.

	if ( ( file_offset >= chunk_offset ) && ( file_offset < chunk_offset + chunk_length ) ) return FILE_IO_OK;

	// Flush the dirty window before it is replaced.

	result = FlushChunk ();

	if ( result != FILE_IO_OK ) return result;

	// Clamp the overlap below the window size.

	if ( context_bytes >= chunk_size ) context_bytes = chunk_size / 2;

	if ( file_offset < chunk_offset )
	{
		// Approaching up-file: the target sits near the window's end,
		// with context_bytes still loaded after it.

		window_offset = ( file_offset + context_bytes > chunk_size )
		              ? file_offset + context_bytes - chunk_size
		              : 0;
	}
	else
	{
		// Approaching down-file (or a jump): the target sits
		// context_bytes past the window's start.

		window_offset = ( file_offset > context_bytes ) ? file_offset - context_bytes : 0;
	}

	// Load the re-based window.

	return LoadChunk ( window_offset );
}

int FileBuffer::ReflowChunk ( unsigned long file_offset )
{
	// Local variables.

	unsigned long window_offset;
	int           result;

	// A whole-file buffer never reflows.

	if ( !chunked ) return FILE_IO_OK;

	// Flush unconditionally, splicing the growth into the work file.

	result = FlushChunk ();

	if ( result != FILE_IO_OK ) return result;

	// Keep the current window base, re-basing only if the trim would push the offset outside.

	window_offset = chunk_offset;

	if ( ( file_offset < window_offset ) || ( file_offset >= window_offset + chunk_size ) )
	{
		window_offset = ( file_offset > chunk_size / 2 ) ? file_offset - chunk_size / 2 : 0;
	}

	// Reload at the nominal window size, restoring the insert headroom.

	return LoadChunk ( window_offset );
}

//----------------------------------------------------------------------------
// Function: FileBuffer::NoteEdit
//
// Description:
//
//   Records a HexView edit against the loaded chunk: tracks the chunk's
//   new length (size-changing edits grow or shrink the logical file
//   length by the same amount) and marks the chunk and file dirty.
//
// Arguments:
//
//   - new_chunk_length : The chunk's length after the edit.
//
// Returns:
//
//   - None
//
//----------------------------------------------------------------------------

void FileBuffer::NoteEdit ( unsigned long new_chunk_length )
{
	// Track the file's new logical length and mark the chunk and file dirty.

	file_length  = file_length + new_chunk_length - chunk_length;
	chunk_length = new_chunk_length;
	chunk_dirty  = true;
	file_dirty   = true;
}

//----------------------------------------------------------------------------
// Function: FileBuffer::FindPattern
//
// Description:
//
//   Searches from start_offset to the end of the file for the byte
//   pattern (Find and the find step of Replace). A whole-file buffer is
//   scanned directly in RAM. A chunked file is scanned by paging the work
//   file through the working buffer one window at a time: the dirty chunk
//   is flushed first so the work file carries the latest edits, then
//   consecutive windows are loaded with at least (pattern_length - 1)
//   bytes of overlap so a match straddling a load boundary still appears
//   whole in one window, so the scan sweeps the whole file by paging
//   chunks in and out. The originally loaded chunk is restored before
//   returning, so a search never disturbs the caller's view; on a match
//   the caller pages its window to found_offset itself.
//
// Arguments:
//
//   - pattern        : The far pattern bytes to search for.
//   - pattern_length : The pattern's length in bytes.
//   - start_offset   : The absolute file offset to search from.
//   - found          : Out: true when the pattern was located.
//   - found_offset   : Out: the absolute offset of the first match.
//
// Returns:
//
//   - FILE_IO_OK when the search completed (found reports the outcome), or
//     the FILE_IO_ERROR_* code of a failed page during a chunked scan.
//
//----------------------------------------------------------------------------

int FileBuffer::FindPattern ( const BYTE far *pattern, unsigned pattern_length, unsigned long start_offset, bool *found, unsigned long *found_offset )
{
	// Local variables.

	unsigned long position;
	unsigned long scan_start;
	unsigned long original_offset;
	unsigned long request;
	unsigned long window_end;
	int           result;

	// Default to an unmatched search.

	*found        = false;
	*found_offset = 0;

	// An empty pattern, or one longer than the remaining span, cannot match.

	if ( pattern_length == 0 )                                          return FILE_IO_OK;
	if ( ( unsigned long ) pattern_length > file_length )              return FILE_IO_OK;
	if ( start_offset + ( unsigned long ) pattern_length > file_length ) return FILE_IO_OK;

	// Whole-file mode: the entire file is in the buffer with the latest
	// edits, so scan it directly.

	if ( !chunked )
	{
		for ( position = start_offset; position + ( unsigned long ) pattern_length <= file_length; position++ )
		{
			if ( MatchAt ( buffer, position, pattern, pattern_length ) )
			{
				*found        = true;
				*found_offset = position;

				break;
			}
		}

		return FILE_IO_OK;
	}

	// Chunked mode: flush any edits so the work file is complete, then page
	// the work file window by window through the working buffer, restoring
	// the originally loaded chunk when done.

	result = FlushChunk ();

	if ( result != FILE_IO_OK ) return result;

	// Remember the loaded chunk so the caller's view is undisturbed.

	original_offset = chunk_offset;
	request         = start_offset;

	// Page the work file window by window.

	for ( ; ; )
	{
		result = LoadChunk ( request );

		if ( result != FILE_IO_OK )
		{
			LoadChunk ( original_offset );

			return result;
		}

		// Begin at start_offset within the first window, at 0 in the rest.

		scan_start = ( start_offset > chunk_offset ) ? start_offset - chunk_offset : 0;

		// Scan this window for the pattern.

		for ( position = scan_start; position + ( unsigned long ) pattern_length <= chunk_length; position++ )
		{
			if ( MatchAt ( buffer, position, pattern, pattern_length ) )
			{
				*found        = true;
				*found_offset = chunk_offset + position;

				break;
			}
		}

		if ( *found ) break;

		window_end = chunk_offset + chunk_length;

		if ( window_end >= work_length ) break;      // Scanned to the end of the file.

		// Advance so the next window re-covers the last (pattern_length - 1)
		// bytes, catching a match that straddles this window's end. A window
		// is always a full chunk_size except the final one (which ended the
		// loop above), so this advance is far larger than the overlap; the
		// guard just makes termination unconditional should that ever fail.

		request = window_end - ( unsigned long ) ( pattern_length - 1 );

		if ( request <= chunk_offset ) break;
	}

	// Restore the originally loaded chunk.

	LoadChunk ( original_offset );

	// The search completed; found reports the outcome.

	return FILE_IO_OK;
}

//----------------------------------------------------------------------------
// Function: FileBuffer::ReplaceInChunk
//
// Description:
//
//   Splices new_length replacement bytes over old_length bytes at a
//   chunk-relative offset within the loaded window (the single-match edit
//   of Replace, applied after the caller has paged the match into view).
//   A length change shifts the chunk tail - backward to open a gap for a
//   longer replacement, forward to close it for a shorter one - and the
//   size change is recorded through NoteEdit, so the paging layer's flush
//   splices it into the file exactly as an interactive edit would. Growth
//   that would pass the physical buffer is refused.
//
// Arguments:
//
//   - chunk_relative_offset : The match's offset within the loaded chunk.
//   - old_length            : The matched byte count to replace.
//   - new_bytes             : The far replacement bytes.
//   - new_length            : The replacement's length in bytes.
//
// Returns:
//
//   - true when the replacement was applied; false when the match lies
//     outside the loaded chunk or the growth would overflow the buffer.
//
//----------------------------------------------------------------------------

bool FileBuffer::ReplaceInChunk ( unsigned long chunk_relative_offset, unsigned long old_length, const BYTE far *new_bytes, unsigned long new_length )
{
	// Local variables.

	unsigned long tail_start;
	unsigned long tail_length;
	unsigned long new_chunk_length;
	unsigned long shift;
	unsigned long i;

	// The match must lie wholly inside the loaded chunk.

	if ( chunk_relative_offset + old_length > chunk_length ) return false;

	// The chunk's length after the splice; growth past the physical buffer is refused.

	new_chunk_length = chunk_length - old_length + new_length;

	if ( new_chunk_length > buffer_size ) return false;

	// The tail: everything after the matched bytes.

	tail_start  = chunk_relative_offset + old_length;
	tail_length = chunk_length - tail_start;

	if ( new_length > old_length )
	{
		// A longer replacement: open the gap by copying the tail backward
		// (high to low), so unmoved bytes are never overwritten first.

		shift = new_length - old_length;

		for ( i = tail_length; i > 0; i-- )
		{
			buffer [ tail_start + shift + i - 1 ] = buffer [ tail_start + i - 1 ];
		}
	}
	else if ( new_length < old_length )
	{
		// A shorter replacement: close the gap by copying the tail forward.

		shift = old_length - new_length;

		for ( i = 0; i < tail_length; i++ )
		{
			buffer [ tail_start - shift + i ] = buffer [ tail_start + i ];
		}
	}

	// Write the replacement bytes over the re-sized gap.

	for ( i = 0; i < new_length; i++ )
	{
		buffer [ chunk_relative_offset + i ] = new_bytes [ i ];
	}

	// Record the size change so the flush splices it into the file.

	NoteEdit ( new_chunk_length );

	// The replacement is applied.

	return true;
}

//----------------------------------------------------------------------------
// Function: FileBuffer::ReadAt
//
// Description:
//
//   Reads up to length bytes at an absolute file offset into a far
//   destination, clamped to the end of the file - the byte source for
//   Export and Print, which sweep the whole file (or a selection) into a
//   text dump. A whole-file buffer already holds the latest edits in RAM,
//   so the bytes are copied straight out. A chunked file is read from the
//   work file directly: any dirty chunk is flushed first so the work file
//   carries every edit, and because the read lands in the caller's buffer
//   (not the chunk buffer) and the next page reseeks the work handle, the
//   loaded window is left exactly as it was.
//
// Arguments:
//
//   - file_offset  : The absolute file offset to read from.
//   - destination  : The far buffer to read into.
//   - length       : The maximum byte count to read (one transfer, 16-bit).
//
// Returns:
//
//   - The number of bytes read (0 at or past the end of the file, or on a
//     flush / read failure).
//
//----------------------------------------------------------------------------

unsigned FileBuffer::ReadAt ( unsigned long file_offset, BYTE far *destination, unsigned length )
{
	// Local variables.

	unsigned long available;
	unsigned      i;
	int           got;

	// Nothing to read at or past the end of the file.

	if ( file_offset >= file_length ) return 0;

	// Clamp the read to the end of the file.

	available = file_length - file_offset;

	if ( ( unsigned long ) length > available ) length = ( unsigned ) available;

	if ( length == 0 ) return 0;

	// Whole-file mode: the latest bytes are already in RAM.

	if ( !chunked )
	{
		for ( i = 0; i < length; i++ ) destination [ i ] = buffer [ file_offset + ( unsigned long ) i ];

		// The full clamped length was copied.

		return length;
	}

	// Chunked: flush a dirty chunk so the work file is complete, then read the
	// work file straight into the caller's buffer (the loaded window is
	// untouched).

	if ( chunk_dirty && ( FlushChunk () != FILE_IO_OK ) ) return 0;

	lseek ( work_handle, ( long ) file_offset, SEEK_SET );

	got = read ( work_handle, destination, length );

	// Report the bytes actually read.

	return ( got > 0 ) ? ( unsigned ) got : 0;
}

//----------------------------------------------------------------------------
// Function: FileBuffer::FlushChunk
//
// Description:
//
//   Writes a dirty chunk back to the work file. When the chunk's length
//   changed since it was loaded, the work file's tail (everything after
//   the chunk's old extent) is first shifted by the difference - up-file
//   for growth, down-file (with a truncate) for shrinkage - so the chunk
//   splices in without overwriting or orphaning the data after it.
//
// Arguments:
//
//   - None
//
// Returns:
//
//   - FILE_IO_OK, or the FILE_IO_ERROR_* code of the failed step.
//
//----------------------------------------------------------------------------

int FileBuffer::FlushChunk ( void )
{
	// Local variables.

	long delta;
	int  result;

	// Nothing to flush: a whole-file buffer, or a clean chunk.

	if ( !chunked || !chunk_dirty )
	{
		chunk_dirty = false;

		return FILE_IO_OK;
	}

	// A size-changing edit shifts the work file's tail by the difference first.

	delta = ( long ) chunk_length - ( long ) chunk_disk_length;

	if ( delta != 0 )
	{
		result = ShiftTail ( chunk_offset + chunk_disk_length, delta );

		if ( result != FILE_IO_OK ) return result;

		work_length = work_length + ( unsigned long ) delta;

		if ( delta < 0 ) chsize ( work_handle, ( long ) work_length );
	}

	// Write the chunk over its window in the work file.

	lseek ( work_handle, ( long ) chunk_offset, SEEK_SET );

	if ( ( chunk_length > 0 ) && !FullWrite ( work_handle, buffer, ( unsigned ) chunk_length ) )
	{
		return FILE_IO_ERROR_WRITE;
	}

	// The chunk and the work file agree again.

	chunk_disk_length = chunk_length;
	chunk_dirty       = false;

	return FILE_IO_OK;
}

//----------------------------------------------------------------------------
// Function: FileBuffer::LoadChunk
//
// Description:
//
//   Loads a window-size chunk based at the requested window offset. The
//   window is NOT on a fixed grid - the overlap paging scheme bases it
//   wherever the crossing left it - so the base is only snapped down to
//   the display's 16-byte row boundary (keeping row alignment, so the
//   window's first byte always starts a display row), and pulled back
//   if a full window would run past the file's end. Short files load
//   from offset 0. The chunk arrives clean.
//
// Arguments:
//
//   - window_offset : The requested absolute file offset for the
//                     window's first byte.
//
// Returns:
//
//   - FILE_IO_OK, or FILE_IO_ERROR_READ when the read fell short.
//
//----------------------------------------------------------------------------

int FileBuffer::LoadChunk ( unsigned long window_offset )
{
	// Local variables.

	unsigned long length;
	bool          reaches_end;

	// Pull the window back so it stays full against the file's end, then
	// snap the base down to a display-row boundary.

	reaches_end = false;

	if ( work_length <= chunk_size )
	{
		window_offset = 0;
		reaches_end   = true;
	}
	else if ( window_offset + chunk_size > work_length )
	{
		window_offset = work_length - chunk_size;
		reaches_end   = true;
	}

	window_offset -= window_offset % FILE_BUFFER_ROW_ALIGN;

	length = work_length - window_offset;

	// A mid-file window loads exactly chunk_size, leaving the growth headroom
	// free for insert-mode edits. An end-anchored window instead runs to the
	// file's end, so snapping the base down to a row boundary cannot leave the
	// final up-to-( FILE_BUFFER_ROW_ALIGN - 1 ) bytes unreachable - those extra
	// bytes fit inside the growth headroom, and reaching work_length is also
	// what lets the FindPattern end guard ( window_end >= work_length ) stop.

	if ( !reaches_end && length > chunk_size ) length = chunk_size;

	// Read the window from the work file.

	lseek ( work_handle, ( long ) window_offset, SEEK_SET );

	if ( ( length > 0 ) && !FullRead ( work_handle, buffer, ( unsigned ) length ) )
	{
		return FILE_IO_ERROR_READ;
	}

	// Adopt the window: the chunk arrives clean.

	chunk_offset      = window_offset;
	chunk_length      = length;
	chunk_disk_length = length;
	chunk_dirty       = false;

	return FILE_IO_OK;
}

//----------------------------------------------------------------------------
// Function: FileBuffer::ShiftTail
//
// Description:
//
//   Shifts everything in the work file from tail_start to its end by
//   delta bytes, through the near transfer buffer: growth copies blocks
//   from the tail's end backward (so blocks never overwrite unread
//   data), shrinkage copies forward. The caller adjusts work_length and
//   truncates afterward.
//
// Arguments:
//
//   - tail_start : The work-file offset the tail begins at.
//   - delta      : The signed distance to shift by (non-zero).
//
// Returns:
//
//   - FILE_IO_OK, or the FILE_IO_ERROR_* code of the failed step.
//
//----------------------------------------------------------------------------

int FileBuffer::ShiftTail ( unsigned long tail_start, long delta )
{
	// Local variables.

	unsigned long tail_length;
	unsigned long remaining;
	unsigned long position;
	unsigned      block;

	// No tail to shift.

	if ( tail_start >= work_length ) return FILE_IO_OK;

	// Move the tail block by block through the near transfer buffer.

	tail_length = work_length - tail_start;
	remaining   = tail_length;

	while ( remaining > 0 )
	{
		block = ( remaining > FILE_BUFFER_TRANSFER_SIZE ) ? FILE_BUFFER_TRANSFER_SIZE : ( unsigned ) remaining;

		// Growth walks the tail backward; shrinkage walks it forward.

		position = ( delta > 0 ) ? tail_start + remaining - block : tail_start + tail_length - remaining;

		lseek ( work_handle, ( long ) position, SEEK_SET );

		if ( !FullRead ( work_handle, transfer_buffer, block ) ) return FILE_IO_ERROR_READ;

		lseek ( work_handle, ( long ) position + delta, SEEK_SET );

		if ( !FullWrite ( work_handle, transfer_buffer, block ) ) return FILE_IO_ERROR_WRITE;

		remaining -= block;
	}

	// The tail is shifted.

	return FILE_IO_OK;
}

//----------------------------------------------------------------------------
