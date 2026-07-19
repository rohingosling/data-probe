//****************************************************************************
// Program: Data Probe (FileBuffer)
// Version: 1.5
// Date:    1992-07-14
// Author:  Rohin Gosling
//
// Description:
//
//   FileBuffer is Data Probe's file buffer and chunked paging engine. It
//   owns the farmalloc-ed working buffer the HexView edits, sized on
//   launch from free far memory (farcoreleft) against the configured
//   maximum, and moves file data between disk and that buffer in one of
//   two modes:
//
//     - Whole-file mode: the file fits the configured chunk size; the
//       entire file lives in the buffer, and Save writes it back.
//
//     - Chunked mode: the file is larger than the buffer; a sliding
//       chunk window is paged in and out over a private working copy of
//       the file (the work file), so the original on disk is never
//       modified until Save. The window slides with overlap: paging
//       re-bases it so the crossing point keeps a screenful of already
//       loaded context on the approach side, so reversing direction
//       scrolls straight on without paging again. A dirty chunk is
//       flushed to the work file before the next window loads;
//       size-changing edits splice the chunk's growth or shrinkage into
//       the work file by shifting the file tail.
//
//   The buffer is allocated with growth headroom beyond the chunk size
//   so insert-mode edits can grow the chunk in RAM between flushes.
//
//****************************************************************************

#ifndef _FILE_BUFFER
#define _FILE_BUFFER

#include "mtext.h"
#include "mapp.h"

//----------------------------------------------------------------------------
// Buffer Sizing Constants
//----------------------------------------------------------------------------

#define FILE_BUFFER_DEFAULT_MAX      32768UL   // Default configured chunk size.
#define FILE_BUFFER_MINIMUM_CHUNK     1024UL   // Smallest allowed chunk size.
#define FILE_BUFFER_CHUNK_CEILING    64000UL   // Chunk ceiling: chunk + headroom stays under one 16-bit offset window.
#define FILE_BUFFER_GROWTH_HEADROOM   1024UL   // Buffer slack beyond the chunk for insert-mode growth between flushes.
#define FILE_BUFFER_MEMORY_RESERVE   32768UL   // Far memory left free for MicroApp's own allocations.
#define FILE_BUFFER_REFLOW_MARGIN       16UL   // Growth left before the buffer fills that forces a flush and reload.

#define FILE_BUFFER_TRANSFER_SIZE      512     // Near transfer buffer for the work-file tail splice.
#define FILE_BUFFER_PATH_SIZE           80     // Path capacity (DOS path plus 8.3 name).
#define FILE_BUFFER_ROW_ALIGN           16UL   // Window bases align to the display's 16-byte rows.

#define FILE_BUFFER_WORK_PATH  "DPROBE.$$$"    // The chunked-mode working copy (current directory).

//----------------------------------------------------------------------------
// Result Codes
//----------------------------------------------------------------------------

#define FILE_IO_OK             0
#define FILE_IO_ERROR_OPEN     1               // The file (or work file) could not be opened or created.
#define FILE_IO_ERROR_READ     2               // A read fell short.
#define FILE_IO_ERROR_WRITE    3               // A write fell short (disk full).
#define FILE_IO_ERROR_NO_NAME  4               // Save on an unnamed buffer: the caller should run Save As.

//----------------------------------------------------------------------------
// Class: FileBuffer
//
// Description:
//
//   The file buffer and paging model (a plain model class, not a
//   Component). The HexView edits the chunk in the working buffer
//   directly; the application reports size-changing edits back through
//   NoteEdit and asks PageTo to slide the window.
//
//----------------------------------------------------------------------------

class FileBuffer
{
	// Data Members

protected:

	BYTE far      *buffer;             // The farmalloc-ed working buffer.
	unsigned long  buffer_size;        // Allocated size: chunk_size + growth headroom.
	unsigned long  chunk_size;         // The chunk grid (the configured buffer size, after farcoreleft clamping).
	unsigned long  file_length;        // Logical file length in bytes, tracking unsaved growth.
	unsigned long  chunk_offset;       // File offset of the loaded chunk (row-aligned; overlap paging keeps it off the chunk grid).
	unsigned long  chunk_length;       // The loaded chunk's current length in bytes (may exceed chunk_size within the headroom).
	unsigned long  chunk_disk_length;  // The length the chunk occupies in the work file (what was loaded or last flushed).
	unsigned long  work_length;        // The work file's current length on disk (chunked mode).
	int            work_handle;        // The work file's open handle, -1 when closed (chunked mode).
	bool           chunked;            // true = the file pages through the work file; false = whole file in RAM.
	bool           chunk_dirty;        // The in-RAM chunk differs from the work file.
	bool           file_dirty;         // Any unsaved edit anywhere in the file.
	char           file_path [ FILE_BUFFER_PATH_SIZE ];   // The open file's path; empty = unnamed new buffer.

	// Accessors

public:

	BYTE far      *GetBuffer      ( void )  { return buffer; }
	unsigned long  GetBufferSize  ( void )  { return buffer_size; }
	unsigned long  GetChunkSize   ( void )  { return chunk_size; }
	unsigned long  GetFileLength  ( void )  { return file_length; }
	unsigned long  GetChunkOffset ( void )  { return chunk_offset; }
	unsigned long  GetChunkLength ( void )  { return chunk_length; }
	bool           GetChunked     ( void )  { return chunked; }
	bool           GetFileDirty   ( void )  { return file_dirty; }
	bool           GetAllocated   ( void )  { return buffer != NULL; }
	const char    *GetFilePath    ( void )  { return file_path; }

	// Constructors / Destructor

	FileBuffer  ( void );
	~FileBuffer ( void );

	// Methods

	bool Allocate ( unsigned long configured_max );      // farcoreleft-sized farmalloc; call once at startup.
	void Release  ( void );

	void NewFile  ( const char *path );                  // Empty buffer; path may be "" for an unnamed buffer.
	int  Open     ( const char *path );                  // Load whole file, or set up the work copy and page chunk 0.
	int  Save     ( void );                              // Write the file back to file_path.
	int  SaveAs   ( const char *path );                  // Rename, then Save.
	void Close    ( void );                              // Discard the file state and delete the work file.

	int  PageTo      ( unsigned long file_offset, unsigned long context_bytes );   // Slide the window to file_offset, keeping context_bytes of overlap on the approach side.
	int  ReflowChunk ( unsigned long file_offset );      // Flush unconditionally and reload at the window size (restores headroom).
	void NoteEdit    ( unsigned long new_chunk_length ); // The HexView changed chunk data: track the size and dirty state.

	// Find / Replace. FindPattern scans from start_offset to the end
	// of the file - the whole in-RAM buffer for a small file, or every
	// chunk paged sequentially through the working buffer for a large one
	// (the loaded chunk is restored before returning). ReplaceInChunk
	// splices new_bytes over old_length bytes at a chunk-relative offset
	// within the loaded window, shifting the chunk tail for a length change.

	int  FindPattern    ( const BYTE far *pattern, unsigned pattern_length, unsigned long start_offset, bool *found, unsigned long *found_offset );
	bool ReplaceInChunk ( unsigned long chunk_relative_offset, unsigned long old_length, const BYTE far *new_bytes, unsigned long new_length );

	// Reads up to length bytes at an absolute file offset into destination
	// (Export / Print). A whole-file buffer is copied straight from RAM; a
	// chunked file is read from the work file (flushed first so it carries
	// the latest edits) without disturbing the loaded window. Returns the
	// number of bytes read (0 at or past the end of the file).

	unsigned ReadAt ( unsigned long file_offset, BYTE far *destination, unsigned length );

	// Paging Helpers

protected:

	int  FlushChunk ( void );                            // Write a dirty chunk to the work file, splicing size changes.
	int  LoadChunk  ( unsigned long window_offset );     // Load a window-size chunk based at window_offset (row-aligned, clamped full).
	int  ShiftTail  ( unsigned long tail_start, long delta );   // Shift the work file's tail for a size-changing flush.
};

//----------------------------------------------------------------------------

#endif

//----------------------------------------------------------------------------
