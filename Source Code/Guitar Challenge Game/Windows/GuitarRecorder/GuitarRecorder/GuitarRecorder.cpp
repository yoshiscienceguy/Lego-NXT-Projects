// GuitarRecorder -- Song Recorder for Windows
// NXT Guitar Challenge Game
// (c)Copyright 2009 by Dave Parker.  All Rights Reserved.
// www.nxtprograms.com

#include "stdafx.h"
#include "commdlg.h"
#include "GuitarRecorder.h"

// Misc constants
#define CB_SZ				256		// size for misc strings
#define ID_TIMER_REDRAW		1		// redraw timer

// Screen metrics in pixels
const int DX_WINDOW = 320;	// default window width
const int DY_WINDOW = 500;	// default window height
const int Y_RECORDER = 200;	// y coord of the recording point in the window
const int DY_TEXT = 12;		// approximate font height
const int DX_NOTE_HALF = 5;	// half-width of a note (on each side of a string)
const int X_INFO = 4;		// x position for info displayed next to the fret board
inline int XFret(int fret) { return 40 + fret * 32; }		// X position of a fret string

// Data for the recording state
Byte rgFretTick[TICK_MAX];	// fret pressed at each tick in time
unsigned long tickSysStart; // system tick count when clock was last started or 0 if stopped
int tickStartStop;			// song tick when timer was last started or stopped
int fretNoteCur = -1;		// fret of current note being held 1-5, 0 to erase, -1 no change
int tickEndNoteCur;			// the end of the current note recorded so far
int tickMax;				// the largest tick value seen so far

// View data
int dyTick = 2;				// zoom factor (pixels per tick)

// Data for the song file
const char *szFileFilter = "Guitar Songs (*.gcs)\0*.gcs\0All Files\0*.*\0";  // file dialog filter
int indexFile;				// the song file name index (1-99)
bool fSongEdited;			// true if the song has been edited

// Utility Macros
#define IToA(i, buf)	_itoa_s(i, buf, sizeof(buf), 10)

// Misc Global Variables for windowing
HINSTANCE hInst;				// current instance
TCHAR szTitle[CB_SZ];			// The title bar text
TCHAR szWindowClass[CB_SZ];		// the main window class name
const char *szAppName = "Guitar Recorder";	// app name for alerts

// Forward declarations of functions included in this file
INT_PTR CALLBACK AboutDlgProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK InfoDlgProc(HWND, UINT, WPARAM, LPARAM);

// Start, reset, and test if the song timer is running
inline void StartSongTimer()	{ tickSysStart = GetTickCount(); }
inline void ResetSongTimer()	{ tickSysStart = 0; tickStartStop = 0; }
inline bool FSongTimerStarted()	{ return tickSysStart != 0; }

// Return the current timing tick value or 0 if the timer is not running.
// fStart is true if the timer can be started if not started yet.
int TickCurrent(bool fStart)
{
    // If the timer is running, compute ticks since the start
	int tick;
    if (FSongTimerStarted())
       tick = (GetTickCount() - tickSysStart) / TICK_INTERVAL + tickStartStop;
	else
	{
		// Timer is not running, start now if allowed
		if (fStart)
		   StartSongTimer();

		// Use the timer stop/restart time
		tick = tickStartStop;
	}

	// Keep track of max tick seen so far
	if (tick > tickMax)
		tickMax = tick;
    return tick;
}

// Stop the timer at its current position
void StopSongTimer()
{
	if (FSongTimerStarted())
	{
		tickStartStop = TickCurrent(false);
		tickSysStart = 0;
	}
}

// Init the song data for a new song
void InitNewSong()
{
	// Init song data
	ZeroMemory(&songHeader, sizeof(songHeader));
	strcpy_s(songHeader.szFileSignature, sizeof(songHeader.szFileSignature), STR_FILENAME_EXT);
	songHeader.bFileVersion = VER_FILE;
	ZeroMemory(rgNote, sizeof(rgNote));
	indexFile = 0;
	fSongEdited = false;

	// Init the recording state
	ZeroMemory(rgFretTick, sizeof(rgFretTick));
	ResetSongTimer();
	fretNoteCur = -1;
	tickEndNoteCur = 0;
	tickMax = 0;
}

// If a note is being held, extend it to the current tick
void ExtendNote()
{
	if (fretNoteCur >= 0)
	{
		int tickCur = TickCurrent(false);
		for (int tick = tickEndNoteCur + 1; tick <= tickCur; tick++)
			rgFretTick[tick] = fretNoteCur;
		tickEndNoteCur = tickCur;
		fSongEdited = true;
	}
}

// End the current note, if any
void EndNote()
{
	// Extend the current note if any, then end it
	ExtendNote();
	fretNoteCur = -1;
}

// Start a new note at fret, ending any current note first.
void StartNote(int fret)
{
	// End note in progress if any
	EndNote();

	// Get the current timing tick and start the timer if not started yet
	int tickCur = TickCurrent(true);

    // Start a note at the current time
	rgFretTick[tickCur] = fret;
	fretNoteCur = fret;
	tickEndNoteCur = tickCur;
	fSongEdited = true;
}

// Fill out rgFretTick using the note list in rgNote[songHeader.cNotesSong],
// and compute tickMax as the largest tick value seen in a note.
void FillFretTimeMap()
{
	ZeroMemory(rgFretTick, sizeof(rgFretTick));
	for (int iNote = 0; iNote < songHeader.cNotesSong; iNote++)
	{
		int tickStart = rgNote[iNote].tickStart;
		int tickEnd = tickStart + rgNote[iNote].ticksLength;
		for (int tick = tickStart; tick < tickEnd; tick++)
			rgFretTick[tick] = rgNote[iNote].fret;
		tickMax = tickEnd;
	}
}

// Fill in the note list in rgNote using the fret tick map in rgFretTick
// and set songHeader.cNotesSong.  Show an alert on hwnd if the max note 
// count is exceeded.
void GetNoteList(HWND hwnd)
{
	int cNotes = 0;
	ZeroMemory(rgNote, sizeof(rgNote));

	// Determine notes by scanning for contiguous fret sequences
	int tickStartFirstNote = -1;	// for finding the first note, none yet
	int tick = 0; 
	while (tick < TICK_MAX)
	{
		// Find the next fret sequence
		int tickStart = tick;
		int fret = rgFretTick[tick];
		while (tick < TICK_MAX && rgFretTick[tick] == fret)
			tick++;

		// Record as a note if it was a non-empty fret
		if (fret != 0)
		{
			// Find first note and/or adjust for the first note delay
			if (tickStartFirstNote < 0)
			{
				tickStartFirstNote = tickStart;

				// Compute the starting delay if not already set
				if (songHeader.bSecStartDelay == 0)
					songHeader.bSecStartDelay = tickStartFirstNote * TICK_INTERVAL / 1000;
			}
			int ticksLength = tick - tickStart;
			tickStart -= tickStartFirstNote;

			// Split up into several notes if note is too long
			while (ticksLength > TICKS_NOTE_MAX && cNotes < MAX_NOTES)
			{
				rgNote[cNotes].fret = fret;
				rgNote[cNotes].tickStart = tickStart;
				rgNote[cNotes].ticksLength = TICKS_NOTE_MAX;
				ticksLength -= TICKS_NOTE_MAX;
				cNotes++;
			}
			if (cNotes < MAX_NOTES)
			{
				if (ticksLength > TICKS_NOTE_MAX)
					ticksLength = TICKS_NOTE_MAX;
				rgNote[cNotes].fret = fret;
				rgNote[cNotes].tickStart = tickStart;
				rgNote[cNotes].ticksLength = ticksLength;
				cNotes++;
			}
		}
		
		// Test for the note count limit
		if (cNotes == MAX_NOTES)
		{
			MessageBox(hwnd, "Note count limit exceeded, song truncated.", 
					   szAppName, MB_OK | MB_ICONERROR);
			break;
		}
	}

	// Set note count and recompute the fret time map for any changes made (first note or truncation)
	songHeader.cNotesSong = cNotes;
	FillFretTimeMap();
}

// Return the file index ("Song nn") used by the given file name or path name, 
// or 0 if a valid index was not found..
int IndexOfFileName(const char *szPathName)
{
	// Scan the string to find the index at the end of the file name
	const char *pch = szPathName + strlen(szPathName) - 1;	// point at last char of file name
	while (pch > szPathName && *pch != '.')
		pch--;	  // find the file extension dot
	pch--;	 // now pointing at end of base file name
	while (pch > szPathName && isdigit(*pch))
		pch--;	  // find beginning of the file name index
	if (pch > szPathName)
	{
		pch++;	// now pointing at first digit
		char szIndex[CB_SZ];
		char *pchDst = szIndex;
		while (isdigit(*pch))
			*pchDst++ = *pch++;  // copy index part of name into szIndex
		*pchDst = 0;
		int index = atoi(szIndex);   // get the numeric file index
		if (index >= 1 && index <= MAX_FILES)
			return index;	// return index if in range
	}
	return 0;	// valid index not found
}


// Open an existing song at szPathName.  Return fSuccess.
bool FOpenSongPathName(HWND hwnd, const char *szPathName)
{
	// Open the given file
	HANDLE hf = CreateFile(szPathName, GENERIC_READ, FILE_SHARE_READ, NULL, 
		                   OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (hf == INVALID_HANDLE_VALUE)
	{
		MessageBox(hwnd, "Could not open file", szAppName, MB_OK | MB_ICONERROR);
		return false;
	}

	// Init the song data
	InitNewSong();

	// Determine the file name index 
	indexFile = IndexOfFileName(szPathName);

	// Read the file header
	DWORD cbRead;
	DWORD cb = sizeof(songHeader);
	bool fSuccess = false;
	if (ReadFile(hf, &songHeader, cb, &cbRead, NULL) && cbRead == cb)
	{
		// Check the file signature
		if (!strcmp((const char *) songHeader.szFileSignature, STR_FILENAME_EXT))
		{
			// Read the notes
			cb = songHeader.cNotesSong * sizeof(Note);
			if (ReadFile(hf, &rgNote, cb, &cbRead, NULL) && cbRead == cb)
			{
				// Fill in the fret time map for editing in memory
				FillFretTimeMap();
				fSuccess = true;
			}
		}
	}

	// Close file and return results
	CloseHandle(hf);
	if (!fSuccess)
		MessageBox(hwnd, "Invalid Song File", szAppName, MB_OK | MB_ICONERROR);
	return fSuccess;
}

// Prompt for a file location and open an existing song.  Return fSuccess.
bool FOpenSong(HWND hwnd)
{
	// Show Open File dialog
	OPENFILENAMEA ofn;
	char szFile[MAX_PATH];      // buffer for file name
	ZeroMemory(&ofn, sizeof(ofn));
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = hwnd;
	ofn.lpstrFile = szFile;
	ofn.lpstrFile[0] = '\0';
	ofn.nMaxFile = sizeof(szFile);
	ofn.lpstrFilter = szFileFilter;
	ofn.nFilterIndex = 1;
	ofn.lpstrFileTitle = NULL;
	ofn.nMaxFileTitle = 0;
	ofn.lpstrInitialDir = NULL;
	ofn.lpstrDefExt = STR_FILENAME_EXT;
	ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;
	if (!GetOpenFileName(&ofn)) 
		return false;

	// Open the file at the selected path
	return FOpenSongPathName(hwnd, ofn.lpstrFile);
}

// Make a song file name with the given index
void MakeSongFileName(char *szFileName, int index)
{
	char szIndex[10];
	IToA(index, szIndex);
	strcpy_s(szFileName, MAX_PATH, STR_FILENAME_BASE);
	strcat_s(szFileName, MAX_PATH, szIndex);
	strcat_s(szFileName, MAX_PATH, ".");
	strcat_s(szFileName, MAX_PATH, STR_FILENAME_EXT);
}

// Prompt for a file location and save the song.  Return fSuccess.
bool FSaveSong(HWND hwnd)
{
	// Make the suggested file name
	char szFile[MAX_PATH];
	if (indexFile == 0)
		indexFile = 1;	 // index is unspecified so far, start with default
	MakeSongFileName(szFile, indexFile);
	char szDir[MAX_PATH];
	GetCurrentDirectory(sizeof(szDir), szDir);

	// Show Save dialog
	OPENFILENAMEA ofn;
	ZeroMemory(&ofn, sizeof(ofn));
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = hwnd;
	ofn.lpstrFile = szFile;
	ofn.nMaxFile = sizeof(szFile);
	ofn.lpstrFilter = szFileFilter;
	ofn.nFilterIndex = 1;
	ofn.lpstrFileTitle = NULL;
	ofn.nMaxFileTitle = 0;
	ofn.lpstrInitialDir = szDir;
	ofn.lpstrDefExt = STR_FILENAME_EXT;
	ofn.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT;
	if (!GetSaveFileName(&ofn)) 
		return false;

	// Get the file index specified by the user if they changed it
	indexFile = IndexOfFileName(szFile);

	// Create the file
	HANDLE hf = CreateFile(ofn.lpstrFile, GENERIC_WRITE, FILE_SHARE_WRITE, NULL, 
		                   CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (hf == INVALID_HANDLE_VALUE)
	{
		MessageBox(hwnd, "Could not save file", szAppName, MB_OK | MB_ICONERROR);
		return false;
	}

	// Write the file
	bool fSuccess = false;
	DWORD cb;
	if (WriteFile(hf, &songHeader, sizeof(songHeader), &cb, NULL)
			&& WriteFile(hf, &rgNote, songHeader.cNotesSong * sizeof(Note), &cb, NULL))
		fSuccess = true;
	else
		MessageBox(hwnd, "Could not save file", szAppName, MB_OK | MB_ICONERROR);
	CloseHandle(hf);
	fSongEdited = false;
	return fSuccess;
}

// Set a dialog edit to a number value
BOOL FSetDlgItemTextNum(HWND hDlg, int idCtl, int n)
{
	char szNum[24];
	IToA(n, szNum);
	return SetDlgItemText(hDlg, idCtl, szNum);
}

// Get a dialog edit value as a number
int IGetDlgItemTextNum(HWND hDlg, int idCtl)
{
	char szNum[24];
	GetDlgItemText(hDlg, idCtl, szNum, sizeof(szNum));
	return atoi(szNum);
}

// Return the length of a dialog text item
int CchDlgItem(HWND hDlg, int idCtl)
{
	char sz[CB_SZ];
	GetDlgItemText(hDlg, idCtl, sz, CB_SZ - 1);
	return (int) strlen(sz);
}

// Check for existing song data and return true if it is OK to overwrite
bool FCanOverwriteSong(HWND hwnd)
{
	if (!fSongEdited)
		return true;
	return (MessageBox(hwnd, "Replace existing song?", szAppName, 
				   MB_OKCANCEL | MB_ICONWARNING) == IDOK);
}

// Draw the number n at the given (x, y) position in hdc with optional 
// labels szBefore and szAfter (pass NULL if not needed).
void DrawNumber(HDC hdc, int x, int y, const char *szBefore, int n, const char *szAfter)
{
	char sz[CB_SZ];
	if (szBefore)
		strcpy_s(sz, sizeof(sz), szBefore);
	else
		sz[0] = 0;

	char szNum[10];
	IToA(n, szNum);
	strcat_s(sz, sizeof(sz), szNum);

	if (szAfter)
		strcat_s(sz, sizeof(sz), szAfter);

	TextOut(hdc, x, y, sz, int(strlen(sz))); 
}

// Draw the fret board into hdc
void DrawFretBoard(HDC hdc)
{
	SelectObject(hdc, GetStockObject(BLACK_PEN));
	SelectObject(hdc, GetStockObject(NULL_BRUSH));
	Rectangle(hdc, XFret(1), -TICK_MAX, XFret(5), TICK_MAX);	// strings 1 and 5
	Rectangle(hdc, XFret(2), -TICK_MAX, XFret(4), TICK_MAX);	// strings 2 and 4
	Rectangle(hdc, XFret(3), -TICK_MAX, XFret(4), TICK_MAX);	// string 3
	Rectangle(hdc, XFret(1), Y_RECORDER - 1, XFret(5), TICK_MAX);	// recording point
}

// Draw the notes that appear in the rect rc in hdc
void DrawNotes(HDC hdc, RECT &rc)
{
	// Update any currently held note
	ExtendNote();

	// Get the tick range of the visible window
	int tickCur = TickCurrent(false);
	int tickTop = tickCur + (Y_RECORDER / dyTick);
	int tickBottom = tickTop - (rc.bottom - rc.top) / dyTick;
	if (tickBottom < 0)
		tickBottom = 0;

	// Draw notes for the tick range that appears in the window
	SelectObject(hdc, GetStockObject(BLACK_BRUSH));
	SelectObject(hdc, GetStockObject(BLACK_PEN));
	for (int tick = tickBottom; tick <= tickTop; tick++)
	{
		int fret = rgFretTick[tick];
		if (fret != 0)
		{
			int x = XFret(fret);
			int yBottom = Y_RECORDER + (tickCur - tick) * dyTick;
			int yTop = yBottom - dyTick;
			Rectangle(hdc, x - DX_NOTE_HALF, yTop, x + DX_NOTE_HALF, yBottom);
		}
	}
}

// Handle mouse messages to the main window to edit notes.
// Return true if an edit was made.
bool FEditMouseMessage(WPARAM wParam, int x, int y)
{
	// If a mouse button is not down, nothing to do
	if (!(wParam & (MK_LBUTTON | MK_RBUTTON)))
		return false;

	// Determine which fret string is hit or return if none
	int fret;
	for (fret = 1; fret <= 5; fret++)
	{
		int xFret = XFret(fret);
		if (x >= xFret - DX_NOTE_HALF && x <= xFret + DX_NOTE_HALF)
			break;
	}
	if (fret > 5)
		return false;

	// Right mouse button or shift-left button erases
	bool fErase = false;
	if ((wParam & MK_RBUTTON) || (wParam & MK_SHIFT))
		fErase = true;

	// Determine which tick to edit and change it
	int tick = TickCurrent(false) - ((y - Y_RECORDER) / dyTick);
	if (tick >= 0 && tick < TICK_MAX)
	{
		// If erasing, only erase if a note exists at that fret
		if (fErase)
		{
			if (rgFretTick[tick] != fret)
				return false;
			fret = 0;
		}
		rgFretTick[tick] = fret;
		fSongEdited = true;
	}
	return true;
}

// Update the scroll range for the main window
void UpdateScrollRange(HWND hwnd)
{
	// Init a non-scrolling range to start with
	SCROLLINFO si;
	ZeroMemory(&si, sizeof(si));
	si.cbSize = sizeof(si);
	si.fMask = SIF_ALL | SIF_DISABLENOSCROLL;

	// Scrolling is not allowed when recorder is running
	if (!FSongTimerStarted())
	{
		// Set scroll range and position based on tickStartStop
		si.nMin = 0;
		si.nMax = tickMax;
		si.nPage = 0;	// disable the page size indicator
		si.nPos = tickStartStop;
	}
	SetScrollInfo(hwnd, SB_VERT, &si, true); 
}

// Handle WM_COMMAND messages for the main app window, return the LRESULT
// and set *pfRedraw to true if a redraw is needed afterwards.
LRESULT LrDoCommand(HWND hwnd, UINT idCmd, WPARAM wParam, LPARAM lParam, bool *pfRedraw)
{
	switch (idCmd)
	{
	case ID_FILE_RECORD:
		// Check for existing song
		if (!FCanOverwriteSong(hwnd))
			break;

		// Init a new song
		InitNewSong();
		*pfRedraw = true;
		break;

	case ID_FILE_OPEN:
		// Check for existing song
		if (!FCanOverwriteSong(hwnd))
			break;

		// Prompt to open a file
		FOpenSong(hwnd);
		*pfRedraw = true;
		break;

	case ID_FILE_SAVE:
		// Determine the notes and check for empty song
		GetNoteList(hwnd);
		if (songHeader.cNotesSong == 0)
		{
			MessageBox(hwnd, "Song is empty.", szAppName, MB_OK | MB_ICONERROR);
			break;
		}

		// Prompt for song info first if the song title is blank
		if (!songHeader.szSongTitle[0])
		{
			if (DialogBox(hInst, MAKEINTRESOURCE(IDD_DIALOG_INFO), hwnd, InfoDlgProc) == IDCANCEL)
				break;
		}

		// Prompt to Save the song
		FSaveSong(hwnd);
		*pfRedraw = true;
		break;

	case ID_FILE_SONGINFO:
		// Edit song info with dialog
		DialogBox(hInst, MAKEINTRESOURCE(IDD_DIALOG_INFO), hwnd, InfoDlgProc);
		*pfRedraw = true;
		break;

	case ID_HELP_KEYS:
		MessageBox(hwnd, 
			"Editing Keys:\n\n"
			"1-5:\tRecord Note\n"
			"0:\tErase Note\n" 
			"Space:\tStart/Stop timer\n"
			"Enter:\tReset timer\n"
			"\n\n"
			"When timer is stopped:\n\n"
			"Up/Dn:\tScroll one tick\n"
			"PgUp/Dn:\tScroll page\n"
			"Home:\tScroll to beginning\n"
			"End:\tScroll to end\n"
			"Left click+drag:\tDraw Note\n"
			"Right click+drag:\tErase Note",
			szAppName, MB_OK);
		break;

	case IDM_ABOUT:
		DialogBox(hInst, MAKEINTRESOURCE(IDD_ABOUTBOX), hwnd, AboutDlgProc);
		break;

	case IDM_EXIT:
		DestroyWindow(hwnd);
		break;

	default:
		return DefWindowProc(hwnd, WM_COMMAND, wParam, lParam);
	}

	return 0;
}

// Message handler for the main app window
LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	bool fRedraw = false;	// set to true to redraw the screen after processing the message
	LRESULT lrRet = 0;		// return value
	int fret;
	int vk;
	int ticksWindow;
	RECT rc;

	switch (message)
	{
	case WM_SHOWWINDOW:
		// Set our redraw timer so that we redraw at least each TICK_INTERVAL
		SetTimer(hwnd, ID_TIMER_REDRAW, TICK_INTERVAL, NULL);
		break;

	case WM_KEYDOWN:
		// Ignore auto-repeat keys except for VK_UP and VK_DOWN
		vk = int(wParam);
		if ((HIWORD(lParam) & KF_REPEAT) && vk != VK_UP && vk != VK_DOWN)
			break;

		// User records notes by pressing the fret keys '1' - '5', and '0' to erase
		fret = vk - '0';
		if (fret >= 0 && fret <= 5)
			StartNote(fret);
		else
		{
			// Process other keys
			switch (vk)
			{
			case VK_RETURN:
				// Enter resets the song timer
				ResetSongTimer();
				break;

			case VK_SPACE:
				// Space key toggles start/stop of the song timer, 
				if (FSongTimerStarted())
					StopSongTimer();
				else
					StartSongTimer();
				break;

			// Keyboard support for scrolling
			case VK_HOME:	return SendMessage(hwnd, WM_VSCROLL, SB_TOP, NULL);
			case VK_END:	return SendMessage(hwnd, WM_VSCROLL, SB_BOTTOM, NULL);
			case VK_UP:		return SendMessage(hwnd, WM_VSCROLL, SB_LINEUP, NULL);
			case VK_DOWN:	return SendMessage(hwnd, WM_VSCROLL, SB_LINEDOWN, NULL);
			case VK_PRIOR:	return SendMessage(hwnd, WM_VSCROLL, SB_PAGEUP, NULL);
			case VK_NEXT:	return SendMessage(hwnd, WM_VSCROLL, SB_PAGEDOWN, NULL);
			}
			UpdateScrollRange(hwnd);
		}
		fRedraw = true;
		break;

	case WM_KEYUP:
		// End a note
		vk = int(wParam);
		fret = vk - '0';
		if (fretNoteCur >= 0 && fret == fretNoteCur)	// ignore ups that don't match the last down
		{
			EndNote();
			fRedraw = true;
		}
		break;

	case WM_LBUTTONDOWN:
	case WM_RBUTTONDOWN:
	case WM_MOUSEMOVE:
		// Allow mouse editing when timer is stopped
		if (!FSongTimerStarted())
		{
			if (FEditMouseMessage(wParam, LOWORD(lParam), HIWORD(lParam)))
				fRedraw = true;
		}
		break;

	case WM_VSCROLL:
		// Scrolling is only allowed when the timer is stopped
		if (!FSongTimerStarted())
		{
			// Get height of window in ticks
			GetClientRect(hwnd, &rc);
			ticksWindow = (rc.bottom - rc.top) / dyTick;

			// Handle scrolling requests
			switch (LOWORD(wParam))
			{
			case SB_BOTTOM:		tickStartStop = tickMax;		break;
			case SB_TOP:		tickStartStop = 0;				break;
			case SB_LINEDOWN:	tickStartStop += 1;				break;
			case SB_LINEUP:		tickStartStop -= 1;				break;
			case SB_PAGEDOWN:	tickStartStop += ticksWindow;	break;
			case SB_PAGEUP:		tickStartStop -= ticksWindow;	break;
				break;
			case SB_THUMBTRACK:
			case SB_THUMBPOSITION:
				tickStartStop = HIWORD(wParam);
				break;
			}
			if (tickStartStop < 0)
				tickStartStop = 0;
			else if (tickStartStop > tickMax)
				tickStartStop = tickMax;
			UpdateScrollRange(hwnd);
			fRedraw = true;
		}
		break;

	case WM_SIZE:
		// Window resized, so update scroll range
		UpdateScrollRange(hwnd);
		fRedraw = true;
		break;

	case WM_COMMAND:
		// Handle menu commands
		lrRet = LrDoCommand(hwnd, LOWORD(wParam), wParam, lParam, &fRedraw);
		break;

	case WM_PAINT:
		{
		// Draw the contents of the main window
		PAINTSTRUCT ps;
		GetClientRect(hwnd, &rc);
		HDC hdc;
		hdc = BeginPaint(hwnd, &ps);

		// Draw the fret board then the notes
		DrawFretBoard(hdc);
		DrawNotes(hdc, rc);
		
		// Draw the fret number currently pressed, if any
		if (fretNoteCur > 0)
		{
			SelectObject(hdc, GetStockObject(SYSTEM_FONT));
			DrawNumber(hdc, XFret(fretNoteCur) - 3, Y_RECORDER - DY_TEXT * 2, 
				       NULL, fretNoteCur, NULL);
		}

		// Draw the current tick value
		SelectObject(hdc, GetStockObject(DEFAULT_GUI_FONT));
		int tick = TickCurrent(false);
		DrawNumber(hdc, X_INFO, Y_RECORDER - DY_TEXT, "Tick ", tick, NULL);

		// Draw the MM:SS time
		int sec = songHeader.bSecStartDelay + (tick * TICK_INTERVAL / 1000);
		int min = sec / 60;
		sec -= min * 60;
		char szMinSec[10];
		wsprintf(szMinSec, "(%d:%02d)", min, sec);
		TextOut(hdc, X_INFO, Y_RECORDER, szMinSec, int(strlen(szMinSec)));

		// End the update then update the scrollbar too
		EndPaint(hwnd, &ps);
		UpdateScrollRange(hwnd);
		break;
		}

	case WM_TIMER:
		// Redraw the window at our redraw timing interval
		if (FSongTimerStarted())
			fRedraw = true;
		break;

	case WM_DESTROY:
		// Main window was closed, so shut down the app
		KillTimer(hwnd, ID_TIMER_REDRAW);
		PostQuitMessage(0);
		break;

	default:
		return DefWindowProc(hwnd, message, wParam, lParam);
	}

	// Redraw the screen if needed after processing the message
	if (fRedraw)
		InvalidateRect(hwnd, NULL, TRUE);

	// Return result
	return lrRet;
}

// Message handler for the Edit Song Info dialog
INT_PTR CALLBACK InfoDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	UNREFERENCED_PARAMETER(lParam);

	switch (message)
	{
	case WM_INITDIALOG:
		// Determine the note list to get the first note delay and note count
		GetNoteList(hDlg);

		// Fill in the song info
		SetDlgItemText(hDlg, IDC_EDIT_TITLE, songHeader.szSongTitle);
		SetDlgItemText(hDlg, IDC_EDIT_ARTIST, songHeader.szSongArtist);
		SetDlgItemText(hDlg, IDC_EDIT_ALBUM, songHeader.szSongAlbum);
		FSetDlgItemTextNum(hDlg, IDC_EDIT_NOTES, songHeader.cNotesSong);
		FSetDlgItemTextNum(hDlg, IDC_EDIT_SCORE, songHeader.bSongHighScore);
		FSetDlgItemTextNum(hDlg, IDC_EDIT_DELAY, songHeader.bSecStartDelay);
		return (INT_PTR)TRUE;

	case WM_COMMAND:
		switch (LOWORD(wParam))
		{
		case IDC_CLEAR_SCORE:
			// Zero the score in the dialog
			FSetDlgItemTextNum(hDlg, IDC_EDIT_SCORE, 0);
			return (INT_PTR)TRUE;

		case IDOK:
			// Check string lengths
			if (CchDlgItem(hDlg, IDC_EDIT_TITLE) > 15
				|| CchDlgItem(hDlg, IDC_EDIT_ARTIST) > 15
				|| CchDlgItem(hDlg, IDC_EDIT_ALBUM) > 15)
			{
				MessageBox(hDlg, "Song Title, Artist, and Album are limited to 15 characters.", 
						   szAppName, MB_OK | MB_ICONERROR);
				return (INT_PTR)FALSE;
			}

			// Store the new song info
			GetDlgItemText(hDlg, IDC_EDIT_TITLE, songHeader.szSongTitle, sizeof(songHeader.szSongTitle));
			GetDlgItemText(hDlg, IDC_EDIT_ARTIST, songHeader.szSongArtist, sizeof(songHeader.szSongArtist));
			GetDlgItemText(hDlg, IDC_EDIT_ALBUM, songHeader.szSongAlbum, sizeof(songHeader.szSongAlbum));
			songHeader.bSongHighScore = (Byte) IGetDlgItemTextNum(hDlg, IDC_EDIT_SCORE);
			songHeader.bSecStartDelay = (Byte) IGetDlgItemTextNum(hDlg, IDC_EDIT_DELAY);
			EndDialog(hDlg, IDOK);
			fSongEdited = true;
			return (INT_PTR)TRUE;
		
		case IDCANCEL:
			EndDialog(hDlg, IDCANCEL);
			return (INT_PTR)TRUE;
		}
		break;
	}
	return (INT_PTR)FALSE;
}

// Message handler for About box.
INT_PTR CALLBACK AboutDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	UNREFERENCED_PARAMETER(lParam);
	switch (message)
	{
	case WM_INITDIALOG:
		return (INT_PTR)TRUE;

	case WM_COMMAND:
		if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)
		{
			EndDialog(hDlg, LOWORD(wParam));
			return (INT_PTR)TRUE;
		}
		break;
	}
	return (INT_PTR)FALSE;
}

// Register the main app window class
ATOM MyRegisterClass(HINSTANCE hInstance)
{
	WNDCLASSEX wcex;
	wcex.cbSize			= sizeof(WNDCLASSEX);
	wcex.style			= CS_HREDRAW | CS_VREDRAW;
	wcex.lpfnWndProc	= WndProc;
	wcex.cbClsExtra		= 0;
	wcex.cbWndExtra		= 0;
	wcex.hInstance		= hInstance;
	wcex.hIcon			= LoadIcon(hInstance, MAKEINTRESOURCE(IDI_GUITARRECORDER));
	wcex.hCursor		= LoadCursor(NULL, IDC_ARROW);
	wcex.hbrBackground	= (HBRUSH)(COLOR_WINDOW+1);
	wcex.lpszMenuName	= MAKEINTRESOURCE(IDC_GUITARRECORDER);
	wcex.lpszClassName	= szWindowClass;
	wcex.hIconSm		= LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_SMALL));
	return RegisterClassEx(&wcex);
}

// Init the app instance by creating and showing the main window, return fSuccess.
BOOL FInitInstance(HINSTANCE hInstance, int nCmdShow)
{
   hInst = hInstance; // Store instance handle in our global variable
   HWND hwnd = CreateWindow(szWindowClass, szTitle, WS_OVERLAPPEDWINDOW | WS_VSCROLL,
							CW_USEDEFAULT, 0, DX_WINDOW, DY_WINDOW, NULL, NULL, hInstance, NULL);
   if (!hwnd)
      return FALSE;

   InitNewSong();
   ShowWindow(hwnd, nCmdShow);
   UpdateWindow(hwnd);
   return TRUE;
}

// Main Win32 entry point
int APIENTRY _tWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPTSTR lpCmdLine, int nCmdShow)
{
	UNREFERENCED_PARAMETER(hPrevInstance);
	UNREFERENCED_PARAMETER(lpCmdLine);
	
	// Perform instance and application initialization
	LoadString(hInstance, IDS_APP_TITLE, szTitle, CB_SZ);
	LoadString(hInstance, IDC_GUITARRECORDER, szWindowClass, CB_SZ);
	MyRegisterClass(hInstance);
	if (!FInitInstance (hInstance, nCmdShow))
		return FALSE;
	HACCEL hAccelTable = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_GUITARRECORDER));

	// Main message loop
	MSG msg;
	while (GetMessage(&msg, NULL, 0, 0))
	{
		if (!TranslateAccelerator(msg.hwnd, hAccelTable, &msg))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
	}
	return (int) msg.wParam;
}

