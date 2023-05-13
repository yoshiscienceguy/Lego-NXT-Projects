// GuitarRecorder -- Song Recorder for Windows
// NXT Guitar Challenge Game
// (c)Copyright 2009 by Dave Parker.  All Rights Reserved.
// www.nxtprograms.com

#pragma once

#include "resource.h"


// Simple types
typedef unsigned char Byte;
typedef short Int;


//****************************************************************************
//   Definitions from GuitarShared.ncx (shared with NXT programs)
//****************************************************************************

// Misc Constants
#define TICK_INTERVAL     25       // Our timing tick interval in ms
#define TICK_MAX          32000    // max song length in ticks
#define TICKS_NOTE_MAX    255      // max length of a note (fits in byte)
#define MAX_NOTES         1000     // max note count in a song

// Constants for the song data file
#define STR_FILENAME_BASE   "Song "    // base file name
#define STR_FILENAME_EXT    "gcs"     // file name extension
#define MAX_FILES           99        // max index to append to base name
#define VER_FILE            1         // data file version

// Data for the song data file header (written in this order)
#define CB_SIG             4    // length of file signature
#define CB_SZ_FILE         16   // string length including terminator
#define CB_RESERVED        20   // length of reserved data area
#define CB_FILE_HEADER     (CB_SIG + 2 + (CB_SZ_FILE*3) + 2 + CB_RESERVED + 2)
struct SongFileHeader
{
	char szFileSignature[CB_SIG];   // file signature (same as file ext)
	Byte bFileVersion;              // file version number
	Byte bFileReserved;             // reserved (0)
	char szSongTitle[CB_SZ_FILE];   // song title
	char szSongArtist[CB_SZ_FILE];  // song artist
	char szSongAlbum[CB_SZ_FILE];   // song album
	Byte bSongHighScore;            // high score so far on this song (0-100)
	Byte bSecStartDelay;            // approx delay in sec before first note
	Byte rgbFileRes[CB_RESERVED];   // reserved (0)
	Int cNotesSong;                 // total number of notes in the song
};
SongFileHeader songHeader;

// Data for the notes in the song
struct Note
{
   Int tickStart;      // start time of note
   Byte ticksLength;   // length of note
   Byte fret;          // note fret (1-5, or 0 at end of song)
};
Note rgNote[MAX_NOTES];      // the notes in the song [cNotesSong]
