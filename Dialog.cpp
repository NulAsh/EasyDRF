﻿/******************************************************************************\
 * Copyright (c) 2004
 *
 * Author(s):
 *	Francesco Lanza
 *	Daz Man 2021 (DM)
 * 
 * Description:
 *	
 *
 ******************************************************************************
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation; either version 2 of the License, or (at your option) any later 
 * version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT 
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU General Public License for more 
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 
 * 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
\******************************************************************************/
#define WIN32_LEAN_AND_MEAN
#include <process.h>
#include <windows.h>
#include <commctrl.h>           // prototypes and defs for common controls 
#include <direct.h>
#include "dialog.h"
#include "main.h"
#include "resource.h"
#include "common/DrmReceiver.h"
#include "common/DrmTransmitter.h"
#include "common/libs/ptt.h"
#include "common/settings.h"
#include "common/list.h"
#include "common/bsr.h"
//#include "common/libs/callsign.h" //removed - updated DM
#include "common/libs/mixer.h"
#include "common/libs/graphwin.h"
#include "getfilenam.h"
#include "common/AudioFir.h"
#include <shellapi.h>
#include "common/callsign2.h"
#include "RS-defs.h" //added DM

extern void WFtext(int select);

CDRMReceiver	DRMReceiver;
CDRMTransmitter	DRMTransmitter;

CDataDecoder* DataDecoder;
CParameter*	TransmParam;
CAudioSourceEncoder* AudioSourceEncoder;

// Main dialog handler

#define starttx_time 14 //12 edited DM - This allows the decoder more time to lock
#define starttx_time_long 24 //This allows the decoder even longer to lock
#define stoptx_time 20 //this is a multiple of the timer interval //10 edited DM - This stops the end of the transmission being cut short

#define MAX_PATHLEN 255 //in case the routines can't handle a 255 char path like Windows can, we can reduce this (it's limited to 80 chars elsewhere...) DM

//Status LED colours DM
DWORD ioLEDcol = RED; //Red is default
DWORD freqLEDcol = RED; //Red is default
DWORD timeLEDcol = RED; //Red is default
DWORD frameLEDcol = RED; //Red is default
DWORD facLEDcol = RED; //Red is default
DWORD mscLEDcol = RED; //Red is default

//Vars for new combined file header for new RS coding method DM
int ECCmode = 1; //changed to int DM - 1-3 = Instances OLD, 4,5,6,7 = RS1,RS2,RS3,RS4 NEW
string EZHeaderID = "EasyDRFHeader/|"; //header ID string

unsigned int EncFileSize = 0; //Encoder current file size DM
//unsigned int PreviousTransportID = 0; //Encoder previous Transmport ID

unsigned int RxRSlevel = 0;	//added by DM to detect RS encoding on incoming file segments, even if file header fails - Used in DABMOT.cpp
unsigned int RxRSlevelold = 0;	//previous value of RxRSlevel

unsigned int DecFileSize = 0; //Decoder current file size DM
unsigned int HdrFileSize = 0; //Decoder current file size DM
unsigned int SerialFileSize = 0;
int unsigned RSfilesize = 0; //The size of the RS encoded data (this is the number of 255 byte blocks)

unsigned int totsize = 0; //total segment count global DM
unsigned int actsize = 0; //Decoder active segment count global DM
unsigned int actpos = 0; //Decoder active position global DM

unsigned int DecSegSize = 0; //Decoder Segment Size copy
unsigned int DecTotalSegs = 0; //Decoder total segments - the actual int being used...
//unsigned int CompTotalSegs = 0; //Computed total segments - no longer needed

unsigned int RScount = 0; //save RS attempts count
unsigned int RSpsegs = 0; //save RS segs on last attempt
unsigned int RSpercent = 0; //test for RS data level
bool RSsw = 0; //switch RS and erasure arrays alternately on each file

char erasures[2][8192 / 8]{}; //array for segment erasure data (saves the first/last good CRC segment numbers)
int erasuressegsize[2] = { 0,0 }; //save the segment size that was used for each array

unsigned char filestate = 0; //file save status - 0=blank,1=WAIT, 2=try..., 3=SAVED, 4=FAILED
unsigned char filestate2 = 0; //file save status - 0=blank,1=WAIT, 2=try..., 3=SAVED, 4=FAILED - Slightly different, for detecting when to clear the cache for each file
char showgood = 0; //stretch colour timing for SAVED (green) and FAILED (red)

#if RS_SIZE_METHOD == 1
unsigned int DecCheckReg = 0x00FFFF; //reset 16 bits for new version
#endif

#if RS_SIZE_METHOD == 0
unsigned int DecCheckReg = 0b00001111111111111111111111111111; //Decoder check register for serial segment total transmission
#endif

unsigned int RSlastTransportID = 0; //Last RS decoder transport ID
unsigned int DecTransportID = 0; //Current decoder transport ID
unsigned int BarTransportID = 0;

int BGbusy = 0; //Bargraph thread flag
int RSbusy = 0; //RS decoder thread flag
int RSError = 0; //To display RS decoding errors
int dcomperr = 0; //decompressor error
int lasterror = 0; //save RS error count
int lastRSbcERR = 0; //save last RS error block number

bool CRCOK = 0;

#define BARL 0 //bargraph left
#define BARY 240 //bargraph Y
#define BART 237 //bargraph top
#define BARB 243 //bargraph bottom
#define BARR 500 //bargraph right

int BarLastID = 0; //check if bargraph needs updating
int BarLastSeg = 0; //check if bargraph needs updating
int BarLastTot = 0; //check if bargraph needs resetting
int BarRedraw = 0;
int Bartotsizeold = 0;

//string DMfilename = {}; //added DM 130 is now 260 - (Windows max path length is 255 characters)
char DMfilename[260]{}; //added DM 130 is now 260 - (Windows max path length is 255 characters)
char DMfilename2[260]{}; //added DM 130 is now 260 - (Windows max path length is 255 characters)
//char DMdecodestat[15]{}; //File decode status

bool DMnewfile = TRUE;

int FrameSize = 0; //for debugging text message mode DM
int BlockSize = 0; //for debugging text message mode DM
int TextBytes = 0; //for debugging text message mode DM
int TextBytesi = 0; //for debugging text message mode DM

bool PTT = FALSE; //PTT via COM ports

int lasterror2 = 0; //a place for functions to return errors to...

// Initialize File Path
char rxfilepath[260] = { "Rx Files\\" }; //Added DM
char rxcorruptpath[260] = { "Corrupt\\" }; //Added DM
char bsrpath[260] = { "" }; //Added DM
char waveoutpath[260] = { "WaveOut\\" }; //Added NulAsh

int DMRSindex = 0; //write index for above array
int DMRSpsize = 0; //previous segment size

int DMmodehash = 0; //a hash of the transmit parameters, to make mode changes generate unique objects by adding it to the transport ID - DM
float DMSNRaverage = 0;
float DMSNRmax = 0;
int DMobjectnum = 0;
int DMrxokarray[50]{ 0 };
float DMSNRavarray[50]{ 0 };
float DMSNRmaxarray[50]{ 0 };
int unsigned DMgoodsegsarray[50]{ 0 };
int unsigned DMtotalsegsarray[50]{ 0 };
int unsigned DMpossegssarray[50]{ 0 };
//int DMspeechmodecount = 0;  //not used yet...

//moved from further down DM
long file_size[64]; //edited DM was 32
BOOL longleadin = TRUE;
BOOL autoaddfiles = FALSE;

int rxdisplaysave = -1;
BOOL IsRX = TRUE; //this one only controls the graphics display
BOOL IsRX2 = TRUE; //main control
BOOL RX_Running = FALSE;
BOOL TX_Running = FALSE;
BOOL UseTextMessage = FALSE;
BOOL RXTextMessageON = FALSE;
BOOL RXTextMessageWasClosed = FALSE;
BOOL SaveRXFile = TRUE;
BOOL ShowRXFile = FALSE;
BOOL ShowOnlyFirst = TRUE;
BOOL rxaudfilt = FALSE;
BOOL fastreset = FALSE;
BOOL rtsonfac = FALSE;
BOOL dtronfac = FALSE;
BOOL dolog = FALSE;
BOOL TxLevel = FALSE; //for -12dB switching DM
BOOL AllowRXTextMessage = TRUE; //default to on, but allow to be turned off DM
//int sensivity = 60; //no longer used DM

RECT WindowPosition; //save and load app window position
int	WindowX;
int	WindowY;
int width = 440;
int height = 310;

int TXpicpospt = 0;
FILE * logfile;
string rxcall     = "nocall";
string lastrxcall = "nocall";
string consrxcall = "nocall"; 

int numdevIn = 0; //edited DM  //init DM
int numdevOut = 0; //edited DM //init DM

int Display = 0; //Main selection
int disptype = 0; //Graphics display control 0=spectr, 1=psd, 2=level
int newdata = 0;
int numbsrsegments = 0;
int acthash = 0;
int bsrposind = 0; //init DM
int txbsrposind = 0; //init DM
BOOL bCloseMe = FALSE;

HWND bsrhand;

int moderestore = -1;
int qamrestore = -1;
int paintmode = 0;

BOOL DVcomp = TRUE; //use compressor by default
int debug = 0; //for tests

//string namebsrfile;
char namebsrfile[300]; //edit DM - increased from 130 to 300 char
string filetosend;

float specbufarr[300];

char filetitle[32][260]; // = { " " }; //32 was 8, 320 was 80 DM - 320 changed to 260 now (Windows max path length is 255 characters)
char pictfile[32][260]; // = { " " }; //32 was 8, 1200 was 300 DM - 1200 changed to 260 now (Windows max path length is 255 characters)

char lastfilename[260] = { "none" }; //was 130 DM  (Windows max path length is 255 characters)
char lastrxfilename[260] = { 0 }; //was 130 DM (Windows max path length is 255 characters)
int  lastrxfilesize = 0;
ERobMode lastrxrobmode = RM_ROBUSTNESS_MODE_B;
ESpecOcc  lastrxspecocc = SO_1;
CParameter::ECodScheme lastrxcodscheme = CParameter::CS_1_SM;
int lastrxprotlev = 0;

char robmode = ' ';
int specocc = 0;

/* Implementation of global functions *****************************************/
int messtate[20] = { -1,-1,-1,-1,-1,-1,-1,-1,-1,-1 }; //was 10 DM
//HWND messhwnd; //replaced by mainwindow below
HWND RXMessagehwnd;
HWND mainwindow; //added DM

void ClearBar(HWND hwnd) {
	HDC hdc = GetDC(hwnd);
	HPEN penx = nullptr;
	LOGBRUSH lbx;
	lbx.lbStyle = BS_SOLID;
	lbx.lbColor = GetSysColor(COLOR_3DFACE);
	lbx.lbHatch = 0;
	penx = ExtCreatePen(PS_GEOMETRIC | PS_SOLID | PS_ENDCAP_FLAT | PS_JOIN_MITER, 6, &lbx, 0, nullptr);
	SelectObject(hdc, penx); //penx is the window background colour, and 6 pixels square
	MoveToEx(hdc, BARL, BARY, nullptr);
	LineTo(hdc, BARR, BARY); //erase the rest of the window
	DeleteObject(penx);
	ReleaseDC(hwnd, hdc);
}

//Stats window display - DM
void sendinfo(HWND hwnd) {
	char tempstr[300];
//	lasterror2 = sprintf_s(tempstr, "RS%d Err:%d Ft:%d St:%d Ss:%d [%s] d%d tid%d-%d", RxRSlevel, lasterror, totsize, DecTotalSegs, DecSegSize, DMfilename, DecFileSize, DecTransportID, erasureswitch);//RSfilesize //added RS level info - in testing - DM //DecFileSize
//	lasterror2 = sprintf_s(tempstr, "RS%d E:%d Ft:%d St:%d Ss:%d [%s] RSf:%d tid:%d-%d hdr:%d", RxRSlevel, lasterror, totsize, DecTotalSegs, DecSegSize, DMfilename, RSfilesize, DecTransportID, erasureswitch, HdrFileSize);//RSfilesize //added RS level info - in testing - DM //DecFileSize
//	wsprintf(tempstr, "RS%d RSerr:%d fS:%d sS:%d File:%s", RxRSlevel, lasterror, totsize, DecTotalSegs, DMfilename); //added RS level info - in testing - DM
//	lasterror2 = sprintf_s(tempstr, "Fsegt:%d Ssegt:%d Ss:%d [%s] RSf:%d tid:%d-%d Hdr:%d", totsize, DecTotalSegs, DecSegSize, DMfilename, RSfilesize, DecTransportID, erasureswitch, HdrFileSize);//RSfilesize //added RS level info - in testing - DM //DecFileSize
//	lasterror2 = sprintf_s(tempstr, "[%s] Hdr:%d Bytes:%d tID:%d-%d Segs:%d Err:%d %s", DMfilename, HdrFileSize, RSfilesize, DecTransportID, erasureswitch, DecTotalSegs , lasterror, DMdecodestat);//RSfilesize //added RS level info - in testing - DM //DecFileSize
//	lasterror2 = sprintf_s(tempstr, "[%s] Hdr:%d Bytes:%d E:%d CE:%d %s", DMfilename, HdrFileSize, RSfilesize, lasterror, dcomperr, DMdecodestat);//RSfilesize //added RS level info - in testing - DM //DecFileSize
//	lasterror2 = sprintf_s(tempstr, "[%-s] Hdr:%05d Bytes:%05d ID:%05d-%01d %s", DMfilename, HdrFileSize, RSfilesize, DecTransportID, erasureswitch, DMdecodestat);//RSfilesize //added RS level info - in testing - DM //DecFileSize
//	lasterror2 = sprintf_s(tempstr, "[%-s] Bytes:%05d ID:%05d-%01d %s %d", DMfilename, RSfilesize, DecTransportID, erasureswitch, DMdecodestat, runonce);//RSfilesize //added RS level info - in testing - DM //DecFileSize
//	lasterror2 = sprintf_s(tempstr, "[%-s] Bytes:%05d ID:%05d-%01d %s", DMfilename, RSfilesize, DecTransportID, erasureswitch, DMdecodestat);//RSfilesize //added RS level info - in testing - DM //DecFileSize
//	lasterror2 = sprintf_s(tempstr, "[%-s] Bytes:%05d ID:%05d-%01d %s %2.1f %2.1f", DMfilename, RSfilesize, DecTransportID, erasureswitch, DMdecodestat, DMSNRaverage, DMSNRmax);//RSfilesize //added RS level info - in testing - DM //DecFileSize
//	lasterror2 = sprintf_s(tempstr, "[%-s] Bytes:%05d ID:%05d-%01d %s %d", DMfilename, RSfilesize, DecTransportID, erasureswitch, DMdecodestat, DMobjectnum);//RSfilesize //added RS level info - in testing - DM //DecFileSize
//	lasterror2 = sprintf_s(tempstr, "[%-s] Bytes:%05d ID:%05d-%01d %s", DMfilename, RSfilesize, DecTransportID, erasureswitch, DMdecodestat);//RSfilesize //added RS level info - in testing - DM //DecFileSize
//	lasterror2 = sprintf_s(tempstr, "[%-s] E:%d Bytes:%05d ID:%05d Bfr:%01d", DMfilename, lastRSbcERR, RSfilesize, DecTransportID, RSsw);//RSfilesize //added RS level info - in testing - DM //DecFileSize
//	lasterror2 = sprintf_s(tempstr, "[%-s] Bytes:%05d ID:%05d-%01d %d%%", DMfilename, RSfilesize, DecTransportID, erasureswitch, RSpercent);//RSfilesize //added RS level info - in testing - DM //DecFileSize
//	lasterror2 = sprintf_s(tempstr, "[%-s] E:%u Bytes:%05u D1:%u D2:%u D3:%u", DMfilename, lastRSbcERR, RSfilesize, debug1, debug2, debug3);//RSfilesize //added RS level info - in testing - DM //DecFileSize
//	lasterror2 = sprintf_s(tempstr, "[%-s] E:%u Bytes:%05u ID:%05u Bfr:%01u dbg:%u", DMfilename, lastRSbcERR, RSfilesize, DecTransportID, RSsw, debug);//RSfilesize //added RS level info - in testing - DM //DecFileSize
//	lasterror2 = sprintf_s(tempstr, "[%-s] E:%u Bytes:%05u ID:%05u Bfr:%01u B:%u", DMfilename, lastRSbcERR, RSfilesize, DecTransportID, RSsw, debug);//RSfilesize //added RS level info - in testing - DM //DecFileSize
	lasterror2 = sprintf_s(tempstr, "[%-s] E:%u Bytes:%05u ID:%05u Bfr:%01u", DMfilename, lastRSbcERR, RSfilesize, DecTransportID, RSsw);//RSfilesize //added RS level info - in testing - DM //DecFileSize
	lasterror2 = SendMessage(GetDlgItem(hwnd, IDC_EDIT6), WM_SETTEXT, 0, (LPARAM)tempstr); //send to stats window DM

	//compute the file save status message based on the number in filestate
	if (filestate == FS_BLANK) {
		//BLANK
		lasterror2 = sprintf_s(tempstr, " ");
		lasterror2 = SendMessage(GetDlgItem(hwnd, IDC_EDIT7), WM_SETTEXT, 0, (LPARAM)tempstr); //send to filestats window DM
	}

	if (IsRX2) {
		if (filestate == FS_WAIT) {
			//WAIT
			lasterror2 = sprintf_s(tempstr, "WAIT %02d%%", RSpercent);
			lasterror2 = SendMessage(GetDlgItem(hwnd, IDC_EDIT7), WM_SETTEXT, 0, (LPARAM)tempstr); //send to filestats window DM
		}
		if (filestate == FS_TRY) {
			//wait or try xxx
			lasterror2 = sprintf_s(tempstr, "Try... %03d", RScount);
			lasterror2 = SendMessage(GetDlgItem(hwnd, IDC_EDIT7), WM_SETTEXT, 0, (LPARAM)tempstr); //send to filestats window DM
		}
		if (filestate == FS_SAVED) {
			//SAVED
			lasterror2 = sprintf_s(tempstr, "SAVED");
			lasterror2 = SendMessage(GetDlgItem(hwnd, IDC_EDIT7), WM_SETTEXT, 0, (LPARAM)tempstr); //send to filestats window DM
			lastRSbcERR = 0; //clear RS errors
		}
		if (filestate == FS_FAILED) {
			//FAILED
			lasterror2 = sprintf_s(tempstr, "FAILED");
			lasterror2 = SendMessage(GetDlgItem(hwnd, IDC_EDIT7), WM_SETTEXT, 0, (LPARAM)tempstr); //send to filestats window DM
		}
	}
}

//NEW Colour "LEDs" for state information DM Oct 20, 2021
void PostWinMessage(unsigned int MessID, int iMessageParam)
{
	//use MessID as the identifier
	//use the parameter to set the colour for the particular LED
	//then update the LEDs last

	if (MessID == MS_RESET_ALL)
	{
		int i = 1;
		for (i = 1; i < 9; i++) messtate[i] = -1; //clear messtate array DM
		//set all indicators to RED
		ioLEDcol = RED; //Red is default
		freqLEDcol = RED; //Red is default
		timeLEDcol = RED; //Red is default
		frameLEDcol = RED; //Red is default
		facLEDcol = RED; //Red is default
		mscLEDcol = RED; //Red is default

		//updateLEDs(); //read the values above, and draw them

		if (rtsonfac) endrts();
		return;
	}
	if (messtate[MessID] != iMessageParam)
	{
		//convert the colour first, then send it to the correct LED - DM
		int lc = 0;
		if (iMessageParam == 3) lc = GetSysColor(COLOR_3DFACE); //BLACK; // MAGENTA; // DARKGREEN; // RNEIPINK; // ORANGE; // DARKRED; // BLUE; // GREY; //Convert colour
		if (iMessageParam == 2) lc = RED; //Convert colour
		if (iMessageParam == 1) lc = YELLOW; //Convert colour
		if (iMessageParam == 0) lc = GREEN; //Convert colour

		if (MessID == MS_IOINTERFACE)
			ioLEDcol = lc;
		else if (MessID == MS_FREQ_FOUND)
			freqLEDcol = lc;
		else if (MessID == MS_FAC_CRC)
		{
			facLEDcol = lc;
			if (rtsonfac) if (iMessageParam == 0) dorts();	else endrts(); //serial interface stuff
			if (dtronfac) if (iMessageParam == 0) dodtr();	else enddtr(); //serial interface stuff
		}
		else if (MessID == MS_MSC_CRC)
			mscLEDcol = lc;
		else if (MessID == MS_FRAME_SYNC)
			frameLEDcol = lc;
		else if (MessID == MS_TIME_SYNC)
			timeLEDcol = lc;
		else return;
		//updateLEDs();
		messtate[MessID] = iMessageParam;
	}
}

void updateLEDs(void) {
	HWND hwnd = mainwindow; //main window
	HDC hdc = GetDC(hwnd);

	constexpr int x = 15; //x position of LEDs
	int y = 0;
	constexpr int top = 27;
	constexpr int hgt = 18;
	int i = 0;

	SetBkColor(hdc, ioLEDcol);
	y = top + (i * hgt);
	TextOut(hdc, x, y, "   ", 2);
	i++;
	SetBkColor(hdc, freqLEDcol);
	y = top + (i * hgt);
	TextOut(hdc, x, y, "   ", 2);
	i++;
	SetBkColor(hdc, timeLEDcol);
	y = top + (i * hgt);
	TextOut(hdc, x, y, "   ", 2);
	i++;
	SetBkColor(hdc, frameLEDcol);
	y = top + (i * hgt);
	TextOut(hdc, x, y, "   ", 2);
	i++;
	SetBkColor(hdc, facLEDcol);
	y = top + (i * hgt);
	TextOut(hdc, x, y, "   ", 2);
	i++;
	SetBkColor(hdc, mscLEDcol);
	y = top + (i * hgt);
	TextOut(hdc, x, y, "   ", 2);

	ReleaseDC(hwnd, hdc);
}
/*--------------------------------------------------------------------
        PAINT WAVE DATA
        Repaint the dialog box's GraphClass display control
  --------------------------------------------------------------------*/

static void PaintWaveData ( HWND hDlg , BOOL erase )
{
    HWND hwndShowWave = GetDlgItem( hDlg, IDS_WAVE_PANE );

	DrawBar(mainwindow); //This works much better here! DM
	updateLEDs();
    InvalidateRect( hwndShowWave, nullptr, erase );
    UpdateWindow( hwndShowWave );
    return;
}

/*--------------------------------------------------------------------
        Threads
  --------------------------------------------------------------------*/

void RxFunction(  void *dummy  )
{
	SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
	try
	{
		DRMReceiver.Start();
	}
	catch (CGenErr GenErr)
	{
		RX_Running = FALSE;
		MessageBox(mainwindow, "RX Audio Setup Wrong\nWinDRM needs 2 soundcards or WinXP\nTry -r, -t or -p startup options", "RX Exception", 0);
		PTToff(); //added for PTT control DM
	}
}

void TxFunction(  void *dummy  )
{
	SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
	try
	{
		DRMTransmitter.Start();
	}
	catch (CGenErr GenErr)
	{
		TX_Running = FALSE;
		MessageBox(mainwindow, "TX Audio Setup Wrong\nWinDRM needs 2 soundcards or WinXP\n Try -r, -t or -p startup options", "TX Exception", 0);
		PTToff(); //added for PTT control DM
	}
}

void paintmodes(void) {

	//Send tuning tones or waterfall text

	if (paintmode == 1) {
		//send waterfall text

		moderestore = getmode(); //this gets the mode from the transmitter
		qamrestore = getqam(); //this gets the QAM level from the transmitter

		//set Mode W, 64QAM for tones
		DRMTransmitter.GetParameters()->InitCellMapTable(RM_ROBUSTNESS_MODE_B, DRMTransmitter.GetParameters()->GetSpectrumOccup());
		DRMTransmitter.GetParameters()->eMSCCodingScheme = CParameter::CS_3_SM;

		//Waterfall text is now generated in WFText.cpp
	}
	if (paintmode == 2) {
		//always generate tuning tones in Mode B so the IFFT bins are correct
		//QAM64 is used to make sure speech mode starts because it needs to be >= 2.5kbps to enable
		//Speech mode is used because it needs no file data to start

		//get the mode first, then restore it after
		moderestore = getmode();
		qamrestore = getqam();

		//set Mode B, 64QAM for tones so transmit mode doesn't fail due to a low bitrate
		DRMTransmitter.GetParameters()->InitCellMapTable(RM_ROBUSTNESS_MODE_B, DRMTransmitter.GetParameters()->GetSpectrumOccup());
		DRMTransmitter.GetParameters()->eMSCCodingScheme = CParameter::CS_3_SM;

		//Tones are now written directly in the OFDM routine
	}
}

int Playtype = 0;
BOOL isplaying = FALSE;

/*
void PlaySound(void *dummy)
{
	isplaying = TRUE;
	try
	{
		if (Playtype == 1)
		{
			PTTon();
			//PlaySound("tune.wav",nullptr,SND_FILENAME | SND_SYNC  | SND_NOSTOP | SND_NODEFAULT);
			PTToff();
		}
		else if (Playtype == 2)
		{
			PTTon();
			//paintmodes();
			//PlaySound("id.wav",nullptr,SND_FILENAME | SND_SYNC | SND_NOSTOP | SND_NODEFAULT);
			PTToff();
		}
		else if (Playtype == 3)
		{
			PTTon();
			//paintmode = 1;
			//PlaySound("g.wav",nullptr,SND_FILENAME | SND_SYNC | SND_NOSTOP | SND_NODEFAULT);
			PTToff();
		}
		else if (Playtype == 4)
		{
			PTTon();
			//paintmode = 0;
			//PlaySound("b.wav",nullptr,SND_FILENAME | SND_SYNC | SND_NOSTOP | SND_NODEFAULT);
			PTToff();
		}
	}
	catch (CGenErr GenErr)
	{
		MessageBox( messhwnd,"Works only with WinXP or later","Message",0);	 //edited DM
	}
	isplaying = FALSE;
}
*/

int actdevinr = 0; //inits DM
int actdevint = 0;
int actdevoutr = 0;
int actdevoutt = 0;

BOOL CALLBACK DialogProc (HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{

	HMENU   hMenu = nullptr; //edited DM on compiler advice
	int i = 0;
	int junk = 0; //added DM
	char AppTitle[50]{ 0 }; //added DM
	BOOL aa = FALSE; //added DM

	switch (message)
	{
		//Change background colours for the filestats box DM
	case WM_CTLCOLORSTATIC:
	{
		if (GetDlgCtrlID((HWND)lParam) == IDC_EDIT7)
		{
			HDC hdc = (HDC)wParam;
			int bkcol = 0;
			//if filestate == FS_BLANK use Window background
			//if filestate == FS_WAIT use blue background
			//if filestate == FS_TRY  use yellow background
			//if filestate == FS_SAVED use green background
			//if filestate == FS_FAILED use red background
			if (filestate == FS_BLANK) {
				//bkcol = BLUE; //change to window background colour
				bkcol = GetSysColor(COLOR_3DFACE);
			}
			if ((filestate == FS_WAIT) && (showgood == 0)) {
				bkcol = BLUE;
			}
			if ((filestate == FS_TRY) && (showgood == 0)) {
				bkcol = YELLOW;
			}
			if ((filestate == FS_SAVED) || (showgood > 0)) {
				bkcol = GREEN;
			}
			if ((filestate == FS_FAILED) || (showgood < 0)) {
				bkcol = RED;
			}
			//decay timer towards zero
			if (showgood > 0) showgood -= 1;
			if (showgood < 0) showgood += 1;

			// Do not return a brush created by CreateSolidBrush(...) because you'll get a memory leak
			SetBkColor(hdc, bkcol); //main background
			SetDCBrushColor(hdc, bkcol); //perimeter background
			return (INT_PTR)GetStockObject(DC_BRUSH);
		}
	}
	break;
	case WM_INITDIALOG:
	{
		HICON hIcon = LoadIcon(TheInstance, MAKEINTRESOURCE(IDI_ICON1));
		SendMessage(hwnd, WM_SETICON, WPARAM(ICON_SMALL), LPARAM(hIcon));
	}
	mainwindow = hwnd;
	SendMessage(GetDlgItem(hwnd, IDB_START), WM_SETTEXT, 0, (LPARAM)"TX Voice");
	SendMessage(GetDlgItem(hwnd, IDB_STARTPIC), WM_SETTEXT, 0, (LPARAM)"TX File");

	junk = sprintf_s(AppTitle, "EasyDRF - Build Date: %s", BUILD);

	//Set version number in title bar DM
	aa = SetWindowTextA(hwnd, AppTitle);

	getvar(); //read settings.txt and set variables from it

	//Restore previous window position
	RECT rect;
	if (GetWindowRect(hwnd, &rect)) {
		width = (rect.right - rect.left);
		height = (rect.bottom - rect.top);
		MoveWindow(hwnd, WindowX, WindowY, width, height, TRUE);
	}

	comtx(gettxport());
	checkcomport(hwnd, gettxport());
	//InitBsr(); //DLL version code needs this removed DM

	EZHeaderID = "EasyDRFHeader/|000000"; //set basic header ID string DM

	//Set the state of the Rx Text message DM
	if (AllowRXTextMessage == TRUE) CheckMenuItem(GetMenu(hwnd), ID_SETTINGS_TEXTMESSAGE_OPENRXTEXTMESSAGE, MF_BYCOMMAND | MF_CHECKED);
	else CheckMenuItem(GetMenu(hwnd), ID_SETTINGS_TEXTMESSAGE_OPENRXTEXTMESSAGE, MF_BYCOMMAND | MF_UNCHECKED);

	//numdev = DRMReceiver.GetSoundInterface()->GetNumDevIn(); //edited DM - may need rewriting to split it up into In/Out - YEP!
	//Get a list of the input and output sound devices installed on the PC and list them in the menu:
	numdevIn = DRMReceiver.GetSoundInterface()->GetNumDevIn(); //edited DM
	numdevOut = DRMReceiver.GetSoundInterface()->GetNumDevOut(); //edited DM
	DRMReceiver.GetSoundInterface()->SetWaveOutDir(waveoutpath); // added NulAsh
	DRMTransmitter.GetSoundInterface()->SetWaveOutDir(waveoutpath); // added NulAsh
	//SetMixerValues(numdevIn); //??? Find out what this does... DM ** REMOVED Sep 2022 because it crashes under Linux for some reason....
	hMenu = GetSubMenu(GetMenu(hwnd), 1);
	if (numdevIn >= 30) numdevIn = 30; //edited DM - limit to max 30 devices
	if (numdevOut >= 30) numdevOut = 30; //edited DM - limit to max 30 devices
	for (i = 0; i < numdevIn; i++)
	{
		string drivnam = DRMReceiver.GetSoundInterface()->GetDeviceNameIn(i); //get the device name for this i value
		AppendMenu(hMenu, MF_ENABLED, IDM_O_RX_I_DRIVERS0 + i, drivnam.c_str()); //add a menu item for this device NO CHECK MARK
	}
	AppendMenu(hMenu, MF_SEPARATOR, -1, nullptr);
	AppendMenu(hMenu, MF_ENABLED, -1, "TX Output");
	for (i = 0; i <= numdevOut; i++) // edited NulAsh - to include WaveOut device in menu
	{
		string drivnam = DRMReceiver.GetSoundInterface()->GetDeviceNameOut(i);
		AppendMenu(hMenu, MF_ENABLED, IDM_O_TX_O_DRIVERS0 + i, drivnam.c_str());
	}
	if (runmode != 'P')
	{
		AppendMenu(hMenu, MF_SEPARATOR, -1, nullptr);
		AppendMenu(hMenu, MF_ENABLED, -1, "Voice Input");
		for (i = 0; i < numdevIn; i++)
		{
			string drivnam = DRMReceiver.GetSoundInterface()->GetDeviceNameIn(i); //edited DM
			AppendMenu(hMenu, MF_ENABLED, IDM_O_VO_I_DRIVERS0 + i, drivnam.c_str());
		}
		AppendMenu(hMenu, MF_SEPARATOR, -1, nullptr);
		AppendMenu(hMenu, MF_ENABLED, -1, "Voice Output");
		for (i = 0; i <= numdevOut; i++) // edited NulAsh - to include WaveOut device in menu
		{
			string drivnam = DRMReceiver.GetSoundInterface()->GetDeviceNameOut(i); //edited DM
			AppendMenu(hMenu, MF_ENABLED, IDM_O_VO_O_DRIVERS0 + i, drivnam.c_str());
		}
	}
	else	// Picture only mode, dont start up voice sound !
	{
		AllowRXTextMessage = FALSE;
		CheckMenuItem(GetMenu(hwnd), ID_CODEC_LPC10, MF_BYCOMMAND | MF_UNCHECKED);
		CheckMenuItem(GetMenu(hwnd), ID_CODEC_SPEEX, MF_BYCOMMAND | MF_UNCHECKED);
		EnableMenuItem(GetMenu(hwnd), ID_CODEC_SPEEX, MF_GRAYED);
		EnableMenuItem(GetMenu(hwnd), ID_CODEC_LPC10, MF_GRAYED);
		EnableMenuItem(GetMenu(hwnd), ID_SETTINGS_TEXTMESSAGE, MF_GRAYED);
		EnableMenuItem(GetMenu(hwnd), ID_SETTINGS_TEXTMESSAGE_OPENRXTEXTMESSAGE, MF_GRAYED);
		EnableMenuItem(GetMenu(hwnd), ID_SOUNDCARD_OPENMIXER_VOICEINPUT, MF_GRAYED);
		EnableMenuItem(GetMenu(hwnd), ID_SOUNDCARD_OPENMIXER_VOICEOUTPUT, MF_GRAYED);
		EnableWindow(GetDlgItem(hwnd, IDB_START), FALSE);
	}

	//check the menu items for the selected devices only and clear the rest
	DrawMenuBar(hwnd);
	actdevinr = getsoundin('r');
	//		if (actdevinr >= numdevIn) actdevinr = numdevIn - 1; //edited DM for IO - make sure the selected device doesn't overflow the list?
	unchecksoundrx(hwnd, actdevinr + IDM_O_RX_I_DRIVERS0); //edited DM - This now needs a window menu index and not a driver index! So add the menu base offsets. DONE
	actdevoutt = getsoundout('t');
	//		if (actdevoutt >= numdevOut) actdevoutt = numdevOut - 1; //edited DM for IO 
	unchecksoundtx(hwnd, actdevoutt + IDM_O_TX_O_DRIVERS0); //edited DM
	actdevint = getsoundin('t');
	//		if (actdevint >= numdevIn) actdevint = numdevIn - 1; //edited DM for IO 
	unchecksoundvoI(hwnd, actdevint + IDM_O_VO_I_DRIVERS0); //edited DM
	actdevoutr = getsoundout('r');
	//		if (actdevoutr >= numdevOut) actdevoutr = numdevOut - 1; //edited DM for IO 
	unchecksoundvoO(hwnd, actdevoutr + IDM_O_VO_O_DRIVERS0); //edited DM

	if (runmode != 'T')
	{
		try
		{
			DRMReceiver.GetParameters()->bOnlyPicture = (runmode == 'P');
			DRMReceiver.Init();
			DRMReceiver.GetSoundInterface()->SetInDev(actdevinr);
			DRMReceiver.GetSoundInterface()->SetOutDev(actdevoutr);
			DataDecoder = DRMReceiver.GetDataDecoder();
			//				DRMReceiver.SetFreqAcqSensivity(2.0 - (_REAL)sensivity/100.0); //edited DM -------------- needs disabling for update -----------------

			RX_Running = TRUE;
			_beginthread(RxFunction, 0, nullptr); //edited DM on compiler advice
		}
		catch (CGenErr)
		{
			RX_Running = FALSE;
			MessageBox(hwnd, "Receiver will NOT work\nWinDRM needs 2 soundcards or WinXP\nTry -r, -t or -p startup options", "RX Init Exception", 0);
		}
	}


	if (runmode == 'R')
	{
		EnableMenuItem(GetMenu(hwnd), ID_CODEC_SPEEX, MF_GRAYED);
		EnableMenuItem(GetMenu(hwnd), ID_CODEC_LPC10, MF_GRAYED);
	}

	if (runmode != 'R')
	{
		try
		{
			DRMTransmitter.GetParameters()->bOnlyPicture = (runmode == 'P');
			DRMTransmitter.Init();
			DRMTransmitter.GetSoundInterface()->SetInDev(actdevint);
			DRMTransmitter.GetSoundInterface()->SetOutDev(actdevoutt);
			TransmParam = DRMTransmitter.GetParameters();
			TransmParam->Service[0].strLabel = getcall();
			TransmParam->Service[0].AudioParam.eAudioCoding = CParameter::AC_LPC;
			AudioSourceEncoder = DRMTransmitter.GetAudSrcEnc();
			AudioSourceEncoder->ClearTextMessage();
			AudioSourceEncoder->ClearPicFileNames();
			//				DRMTransmitter.SetCarOffset(350.0); //remmed DM
			//				DRMTransmitter.GetAudSrcEnc()->SetStartDelay(starttx_time_long); //Edited DM
			DRMTransmitter.GetAudSrcEnc()->SetTheStartDelay(starttx_time_long); //Edited DM

			TX_Running = TRUE; //this may mean "enabled" and not "transmitting"... DM
			_beginthread(TxFunction, 0, nullptr);
		}
		catch (CGenErr)
		{
			TX_Running = FALSE;
			MessageBox(hwnd, "Transmitter will NOT work\nWinDRM needs 2 soundcards or WinXP\nTry -r, -t or -p startup options", "TX Init Exception", 0);
			PTToff(); //this might not be required DM
		}
	}

	if (runmode == 'P')
	{
		TransmParam->Service[0].AudioParam.eAudioCoding = CParameter::AC_SSTV;
	}

	UseTextMessage = gettext();
	if ((UseTextMessage) && (TX_Running))
	{
		FILE* txtset = nullptr;
		char textbuf[200]{ 0 };
		txtset = fopen("textmessage.txt", "rt");
		if (txtset != nullptr)
		{
			junk = fscanf(txtset, "%[^\0]", &textbuf); //edited DM
			fclose(txtset);
			DRMTransmitter.GetAudSrcEnc()->ClearTextMessage();
			DRMTransmitter.GetAudSrcEnc()->SetTextMessage(textbuf);
			DRMTransmitter.GetParameters()->Service[0].AudioParam.bTextflag = TRUE;
		}
	}

	uncheckdisp(hwnd);
	if (Display == 0) {
		CheckMenuItem(GetMenu(hwnd), ID_DISPLAY_SPECTRUM, MF_BYCOMMAND | MF_CHECKED);
	}
	else if (Display == 1) {
		CheckMenuItem(GetMenu(hwnd), ID_DISPLAY_PSD, MF_BYCOMMAND | MF_CHECKED);
	}
	else if (Display == 2) {
		CheckMenuItem(GetMenu(hwnd), ID_DISPLAY_LEVEL, MF_BYCOMMAND | MF_CHECKED);
	}
	else if (Display == 3) {
		CheckMenuItem(GetMenu(hwnd), ID_SETTINGS_DISPLAY_WATERFALL, MF_BYCOMMAND | MF_CHECKED);
	}
	else if (Display == 4) {
		CheckMenuItem(GetMenu(hwnd), ID_SETTINGS_DISPLAY_TRANSFERFUNCTION, MF_BYCOMMAND | MF_CHECKED);
	}
	else if (Display == 5) {
		CheckMenuItem(GetMenu(hwnd), ID_SETTINGS_DISPLAY_IMPULSERESPONSE, MF_BYCOMMAND | MF_CHECKED);
	}
	else if (Display == 6) {
		CheckMenuItem(GetMenu(hwnd), ID_SETTINGS_DISPLAY_FACPHASE, MF_BYCOMMAND | MF_CHECKED);
	}
	else if (Display == 7) {
		CheckMenuItem(GetMenu(hwnd), ID_SETTINGS_DISPLAY_MSCPHASE, MF_BYCOMMAND | MF_CHECKED);
	}
	else if (Display == 8) {
		CheckMenuItem(GetMenu(hwnd), ID_SETTINGS_DISPLAY_TEST, MF_BYCOMMAND | MF_CHECKED);
	}
	else if (Display == 9) {
		CheckMenuItem(GetMenu(hwnd), ID_SETTINGS_DISPLAY_OSCILLOSCOPE, MF_BYCOMMAND | MF_CHECKED);
	}


		for (i=0;i<250;i++)
			specbufarr[i] = 1.0;

		if (_chdir(rxfilepath))
		{
			if( _mkdir(rxfilepath) != 0 )
				MessageBox( hwnd,"Failed to create Rx Files Directory","ERROR",0);	
			else
				MessageBox( hwnd,"Rx Files Directory created","INFO",0);	
		}
		else
			_chdir("..");
//		if (_chdir("Corrupt"))
		if (_chdir(rxcorruptpath))
		{
			if( _mkdir(rxcorruptpath) != 0 )
				MessageBox( hwnd,"Failed to create Corrupt Directory","ERROR",0);	
		}
		else
			_chdir("..");
		if (_chdir(waveoutpath))
		{
			if (_mkdir(waveoutpath) != 0)
				MessageBox(hwnd, "Failed to create WaveOut Directory", "ERROR", 0);
		}
		else
			_chdir("..");

		if (rtsonfac)
			CheckMenuItem(GetMenu(hwnd), ID_SETTINGS_PTTPORT_RTSHIGHONFAC, MF_BYCOMMAND | MF_CHECKED);
		else
			CheckMenuItem(GetMenu(hwnd), ID_SETTINGS_PTTPORT_RTSHIGHONFAC, MF_BYCOMMAND | MF_UNCHECKED);
		if (dtronfac)
			CheckMenuItem(GetMenu(hwnd), ID_SETTINGS_PTTPORT_DTRHIGHONFAC, MF_BYCOMMAND | MF_CHECKED);
		else
			CheckMenuItem(GetMenu(hwnd), ID_SETTINGS_PTTPORT_DTRHIGHONFAC, MF_BYCOMMAND | MF_UNCHECKED);

		DRMTransmitter.GetParameters()->eSymbolInterlMode = getinterleave();
		DRMTransmitter.GetParameters()->eMSCCodingScheme = getqam();
		DRMTransmitter.GetParameters()->SetSpectrumOccup(getspec());
		DRMTransmitter.GetParameters()->InitCellMapTable(getmode(),getspec());

		SetTimer(hwnd,1,100,TimerProc); //Original is 100mS //TEST 50mS - This affects timing of Tx/Rx switching, as well as speeding up the graphics display

		//Display Voice compressor checkmark
		if (DVcomp == true) CheckMenuItem(GetMenu(hwnd), ID_SETTINGS_VOICECOMPRESSOR, MF_BYCOMMAND | MF_CHECKED);
		else CheckMenuItem(GetMenu(hwnd), ID_SETTINGS_VOICECOMPRESSOR, MF_BYCOMMAND | MF_UNCHECKED);
		
		return TRUE;   
		
    case WM_COMMAND:
		OnCommand(hwnd, LOWORD (wParam), HIWORD (wParam));
        return TRUE;

    case WM_CLOSE:
		PTToff(); //added DM
		comtx('0');
		KillTimer(hwnd,1);
		GetWindowRect(hwnd, &WindowPosition); //get window position so next time it starts in the same place on the desktop
		savevar(); //save settings on exit
		DestroyWindow(hwnd);
 		DRMReceiver.Stop(); 
		DRMTransmitter.Stop();
		Sleep(1000);
		PostQuitMessage(0);
		return TRUE;
    case WM_PAINT:
		PaintWaveData(hwnd,TRUE);
        return FALSE;
    }
    return FALSE;
}

int snddev;
char cmdstr[20];

void OnCommand ( HWND hwnd, int ctrlid, int code)
{
	switch (ctrlid)
	{
	case IDB_STARTPIC: //Send a file
		if (IsRX2)
		{
			//if (callisok())  //disabled for SW Broadcasting use - DM
			DialogBox(TheInstance, MAKEINTRESOURCE(DLG_PICTURE_TX), hwnd, TXPictureDlgProc);
			//else
				//DialogBox(TheInstance, MAKEINTRESOURCE (DLG_CALLSIGN), hwnd, CallSignDlgProc); //disabled for SW Broadcasting use - DM
		}
		else
		{
			if (TX_Running) DRMTransmitter.NotSend();
			IsRX2 = TRUE; //switch to receive mode
			IsRX = TRUE; //Switch from transmit to receive
			PTToff(); //added DM
			if (rxdisplaysave != -1) {
				Display = rxdisplaysave; //Restore whatever mode was set previously DM
				disptype = Display;
				if (Display == 9) disptype = OSCDISP;
				rxdisplaysave = -1;
			}
			EnableMenuItem(GetMenu(hwnd), ID_SETTINGS_FILETRANSFER_SENDFILE, MF_ENABLED);
			EnableMenuItem(GetMenu(hwnd), ID_SETTINGS_DRMSETTINGS, MF_ENABLED);
			if (RX_Running) DRMReceiver.Rec();
			SendMessage(GetDlgItem(hwnd, IDB_START), WM_SETTEXT, 0, (LPARAM)"TX Voice");
			SendMessage(GetDlgItem(hwnd, IDB_STARTPIC), WM_SETTEXT, 0, (LPARAM)"TX File"); //edited from Pic to File DM
			SetDlgItemText(hwnd, IDC_DCFREQ, " ");
			SetDlgItemText(hwnd, IDC_EDIT, " ");
			SetDlgItemText(hwnd, IDC_EDIT2, " ");
			SetDlgItemText(hwnd, IDC_EDIT3, " ");
			SetDlgItemText(hwnd, IDC_EDIT4, " ");
			SetDlgItemText(hwnd, IDC_EDIT6, " "); //added DM

			if (strlen(rxdevice) >= 1) SelectSrc(rxdevice);
			if (paintmode > -1) {
				WFtext(99); //stop WF text if playing
			}
			paintmode = 0; //DM
		}
		break;
	case IDC_IDTONE:
		if ((TX_Running) && (!isplaying) && (messtate[MS_FAC_CRC] != 0))
		{
			if (paintmode == 0) {
				paintmode = 1; //WF text generation mode DM
				paintmodes();
				WFtext(2);
			}
			//			Playtype = 2;
			//			if (IsRX2) _beginthread(PlaySound, 0, NULL);
		}
	case IDC_GTONE:
		if ((TX_Running) && (!isplaying) && (messtate[MS_FAC_CRC] != 0))
		{
			if (paintmode == 0) {
				paintmode = 1; //WF text generation mode DM
				paintmodes();
				WFtext(0);
			}
			//			Playtype = 3;
			//			if (IsRX2) _beginthread(PlaySound, 0, NULL);
		}
	case IDC_BTONE:
		if ((TX_Running) && (!isplaying) && (messtate[MS_FAC_CRC] != 0))
		{
			if (paintmode == 0) {
				paintmode = 1; //WF text generation mode DM
				paintmodes();
				WFtext(1);
			}
			//			Playtype = 4;
			//			if (IsRX2) _beginthread(PlaySound, 0, NULL);
		}
	case IDC_TUNINGTONE: {
		if (paintmode == 0) {
			paintmode = 2; //tone generation mode DM
			paintmodes();
		}
	}
	case IDB_START: //Send voice mode
		if (IsRX2)
		{
			char dcbuf[20]{ 0 };
			if (runmode != 'P')
			{
				//if (callisok()) 
				//{
				SetTXmode(FALSE);
				DRMReceiver.NotRec();
				IsRX2 = FALSE; //Switch off receiver mode
				PTTon(); //added for com port control DM
				EnableMenuItem(GetMenu(hwnd), ID_SETTINGS_FILETRANSFER_SENDFILE, MF_GRAYED);
				EnableMenuItem(GetMenu(hwnd), ID_SETTINGS_DRMSETTINGS, MF_GRAYED);
				if (TX_Running)
				{
					DRMTransmitter.Init();
					//if (DRMTransmitter.GetParameters()->iNumDecodedBitsMSC <= 980)
					//if (DRMTransmitter.GetParameters()->iNumDecodedBitsMSC < 1040) { //was 1040
					//	MessageBox(hwnd, "Mode does not allow Voice", "Wrong Mode", 0);	//this should force return to receive mode... DM
					//}
					//else {
					DRMTransmitter.Send();
					sprintf(dcbuf, "%d", (int)DRMTransmitter.GetCarOffset());
					SetDlgItemText(hwnd, IDC_DCFREQ, dcbuf);
					//}
				}

				SendMessage(GetDlgItem(hwnd, IDB_START), WM_SETTEXT, 0, (LPARAM)"RX");
				SendMessage(GetDlgItem(hwnd, IDB_STARTPIC), WM_SETTEXT, 0, (LPARAM)"RX");
				if (strlen(txdevice) >= 1) SelectSrc(txdevice);

				//}
				//else
				//{
				//	DialogBox(TheInstance, MAKEINTRESOURCE (DLG_CALLSIGN), hwnd, CallSignDlgProc);
				//}
			}
		}
		else
		{
			if (TX_Running) DRMTransmitter.NotSend();
			IsRX2 = TRUE; //Switch from transmit to receive
			IsRX = TRUE; //Switch from transmit to receive
			PTToff(); //added for com port control DM
			if (rxdisplaysave != -1) {
				Display = rxdisplaysave; //Restore whatever mode was set previously DM
				disptype = Display;
				if (Display == 9) disptype = OSCDISP;
				rxdisplaysave = -1;
			}
			EnableMenuItem(GetMenu(hwnd), ID_SETTINGS_FILETRANSFER_SENDFILE, MF_ENABLED);
			EnableMenuItem(GetMenu(hwnd), ID_SETTINGS_DRMSETTINGS, MF_ENABLED);
			DRMReceiver.Rec();
			SendMessage(GetDlgItem(hwnd, IDB_START), WM_SETTEXT, 0, (LPARAM)"TX Voice");
			SendMessage(GetDlgItem(hwnd, IDB_STARTPIC), WM_SETTEXT, 0, (LPARAM)"TX File"); //edited from Pic to File DM
			SetDlgItemText(hwnd, IDC_DCFREQ, " ");
			SetDlgItemText(hwnd, IDC_EDIT, " ");
			SetDlgItemText(hwnd, IDC_EDIT2, " ");
			SetDlgItemText(hwnd, IDC_EDIT3, " ");
			SetDlgItemText(hwnd, IDC_EDIT4, " ");
			SetDlgItemText(hwnd, IDC_EDIT6, " "); //added DM
			if (strlen(rxdevice) >= 1) SelectSrc(rxdevice);
			if (paintmode > -1) {
				WFtext(99); //stop WF text if playing
			}
			paintmode = 0; //DM

			if (moderestore != -1) {
				if (moderestore == 0) {
					DRMTransmitter.GetParameters()->InitCellMapTable(RM_ROBUSTNESS_MODE_A, DRMTransmitter.GetParameters()->GetSpectrumOccup());
				}
				if (moderestore == 1) {
					DRMTransmitter.GetParameters()->InitCellMapTable(RM_ROBUSTNESS_MODE_B, DRMTransmitter.GetParameters()->GetSpectrumOccup());
				}
				if (moderestore == 2) {
					DRMTransmitter.GetParameters()->InitCellMapTable(RM_ROBUSTNESS_MODE_E, DRMTransmitter.GetParameters()->GetSpectrumOccup());
				}
				if (qamrestore == 0) {
					DRMTransmitter.GetParameters()->eMSCCodingScheme = CParameter::CS_1_SM;
				}
				if (qamrestore == 1) {
					DRMTransmitter.GetParameters()->eMSCCodingScheme = CParameter::CS_2_SM;
				}
				if (qamrestore == 2) {
					DRMTransmitter.GetParameters()->eMSCCodingScheme = CParameter::CS_3_SM;
				}
				moderestore = -1;
			}
		}
		break;

	case ID_PTTPORT_NONE:
		settxport('0');	comtx(gettxport());	checkcomport(hwnd, '0');	break;
	case ID_PTTPORT_COM1:
		settxport('1');	comtx(gettxport());	checkcomport(hwnd, '1');	break;
	case ID_PTTPORT_COM2:
		settxport('2');	comtx(gettxport());	checkcomport(hwnd, '2');	break;
	case ID_PTTPORT_COM3:
		settxport('3');	comtx(gettxport());	checkcomport(hwnd, '3');	break;
	case ID_PTTPORT_COM4:
		settxport('4');	comtx(gettxport());	checkcomport(hwnd, '4');	break;
	case ID_PTTPORT_COM5:
		settxport('5');	comtx(gettxport());	checkcomport(hwnd, '5');	break;
	case ID_PTTPORT_COM6:
		settxport('6');	comtx(gettxport());	checkcomport(hwnd, '6');	break;
	case ID_PTTPORT_COM7:
		settxport('7');	comtx(gettxport());	checkcomport(hwnd, '7');	break;
	case ID_PTTPORT_COM8:
		settxport('8');	comtx(gettxport());	checkcomport(hwnd, '8');	break;
	case ID_SETTINGS_PTTPORT_RTSHIGHONFAC:
		if (rtsonfac)
			CheckMenuItem(GetMenu(hwnd), ID_SETTINGS_PTTPORT_RTSHIGHONFAC, MF_BYCOMMAND | MF_UNCHECKED);
		else
			CheckMenuItem(GetMenu(hwnd), ID_SETTINGS_PTTPORT_RTSHIGHONFAC, MF_BYCOMMAND | MF_CHECKED);
		rtsonfac = !rtsonfac;
		break;
	case ID_SETTINGS_PTTPORT_DTRHIGHONFAC:
		if (dtronfac)
			CheckMenuItem(GetMenu(hwnd), ID_SETTINGS_PTTPORT_DTRHIGHONFAC, MF_BYCOMMAND | MF_UNCHECKED);
		else
			CheckMenuItem(GetMenu(hwnd), ID_SETTINGS_PTTPORT_DTRHIGHONFAC, MF_BYCOMMAND | MF_CHECKED);
		dtronfac = !dtronfac;
		break;

	case ID_SOUNDCARD_OPENMIXER_RXINPUT:
		snddev = getsoundin('r');
		wsprintf(cmdstr, "-R -D %d", snddev);
		ShellExecute(NULL, "open", "sndvol.exe", cmdstr, NULL, SW_SHOWNORMAL); //updated for Win7 mixer DM
		break;
	case ID_SOUNDCARD_OPENMIXER_TXOUTPUT:
		snddev = getsoundout('t');
		wsprintf(cmdstr, "-D %d", snddev);
		ShellExecute(NULL, "open", "sndvol.exe", cmdstr, NULL, SW_SHOWNORMAL); //updated for Win7 mixer DM
		break;
	case ID_SOUNDCARD_OPENMIXER_VOICEINPUT:
		snddev = getsoundin('t');
		wsprintf(cmdstr, "-R -D %d", snddev);
		ShellExecute(NULL, "open", "sndvol.exe", cmdstr, NULL, SW_SHOWNORMAL); //updated for Win7 mixer DM
		break;
	case ID_SOUNDCARD_OPENMIXER_VOICEOUTPUT:
		snddev = getsoundout('r');
		wsprintf(cmdstr, "-D %d", snddev);
		ShellExecute(NULL, "open", "sndvol.exe", cmdstr, NULL, SW_SHOWNORMAL); //updated for Win7 mixer DM
		break;
	case ID_SOUNDCARD_AUTOMIXERSWITCH:
		DialogBox(TheInstance, MAKEINTRESOURCE(DLG_MIXERSETTING), hwnd, MixerDlgProc);
		break;
	case ID_ABOUT_TEST:
		DialogBox(TheInstance, MAKEINTRESOURCE(DLG_ABOUT), hwnd, AboutDlgProc);
		break;
	case ID_ABOUT_HELP:
		DialogBox(TheInstance, MAKEINTRESOURCE(DLG_HELP), hwnd, AboutDlgProc);
		break;

	case ID_SETTINGS_CALLSIGN:
		DialogBox(TheInstance, MAKEINTRESOURCE(DLG_CALLSIGN), hwnd, CallSignDlgProc);
		break;

	case ID_SETTINGS_DRMSETTINGS:
		DialogBox(TheInstance, MAKEINTRESOURCE(DLG_DRMSETTINGS), hwnd, DRMSettingsDlgProc);
		break;

	case ID_SETTINGS_DRMSETTINGSRX:
		CreateDialog(TheInstance, MAKEINTRESOURCE(DLG_RXSETTINGS), hwnd, DRMRXSettingsDlgProc);
		break;

	case ID_SETTINGS_VOICECOMPRESSOR:
		//toggle Voice compressor on/off and display checkmark
		if (DVcomp == true) {
			DVcomp = false; //if it's on, turn it off
			CheckMenuItem(GetMenu(hwnd), ID_SETTINGS_VOICECOMPRESSOR, MF_BYCOMMAND | MF_UNCHECKED);
		}
		else {
			DVcomp = true; //if it's off, turn it on
			CheckMenuItem(GetMenu(hwnd), ID_SETTINGS_VOICECOMPRESSOR, MF_BYCOMMAND | MF_CHECKED);
		}
		break;

	case ID_SETTINGS_TEXTMESSAGE:
		CreateDialog(TheInstance, MAKEINTRESOURCE (DLG_TEXTMESSAGE), hwnd, TextMessageDlgProc);
		break;

	case ID_SETTINGS_TEXTMESSAGE_OPENRXTEXTMESSAGE:
		if (AllowRXTextMessage == TRUE) //if it was true, make it false
		{
			AllowRXTextMessage = FALSE;
			if (RXTextMessageON) SendMessage(RXMessagehwnd, WM_CLOSE, 0, 0);
			CheckMenuItem(GetMenu(hwnd), ID_SETTINGS_TEXTMESSAGE_OPENRXTEXTMESSAGE, MF_BYCOMMAND | MF_UNCHECKED);
		}
		else  //if it was false, make it true
		{
			AllowRXTextMessage = TRUE;
			RXTextMessageWasClosed = FALSE;
			CheckMenuItem(GetMenu(hwnd), ID_SETTINGS_TEXTMESSAGE_OPENRXTEXTMESSAGE, MF_BYCOMMAND | MF_CHECKED);
		}
		break;
	case ID_SETTINGS_FILETRANSFER_SAVERECEIVEDFILES:
		if (SaveRXFile)
			CheckMenuItem(GetMenu(hwnd), ID_SETTINGS_FILETRANSFER_SAVERECEIVEDFILES, MF_BYCOMMAND | MF_UNCHECKED);	
		else
			CheckMenuItem(GetMenu(hwnd), ID_SETTINGS_FILETRANSFER_SAVERECEIVEDFILES, MF_BYCOMMAND | MF_CHECKED);	
		SaveRXFile = !SaveRXFile;
		break; //??
	case ID_SETTINGS_FILETRANSFER_SHOWRECEIVEDFILES:
		if (ShowRXFile)
			CheckMenuItem(GetMenu(hwnd), ID_SETTINGS_FILETRANSFER_SHOWRECEIVEDFILES, MF_BYCOMMAND | MF_UNCHECKED);	
		else
			CheckMenuItem(GetMenu(hwnd), ID_SETTINGS_FILETRANSFER_SHOWRECEIVEDFILES, MF_BYCOMMAND | MF_CHECKED);	
		ShowRXFile = !ShowRXFile;
		break;
	case ID_SETTINGS_FILETRANSFER_SHOWONLYONE:
		if (ShowOnlyFirst)
		{
			wsprintf(lastfilename,"none");
			CheckMenuItem(GetMenu(hwnd), ID_SETTINGS_FILETRANSFER_SHOWONLYONE, MF_BYCOMMAND | MF_UNCHECKED);
		}
		else
			CheckMenuItem(GetMenu(hwnd), ID_SETTINGS_FILETRANSFER_SHOWONLYONE, MF_BYCOMMAND | MF_CHECKED);	
		ShowOnlyFirst = !ShowOnlyFirst;
		break;
	case ID_SETTINGS_FILETRANSFER_SENDFILE:
		DialogBox(TheInstance, MAKEINTRESOURCE (DLG_PICTURE_TX), hwnd, TXPictureDlgProc);
		break;

	case ID_DISPLAY_SPECTRUM:
		if (setdisplay(0)) {
			uncheckdisp(hwnd);
			CheckMenuItem(GetMenu(hwnd), ID_DISPLAY_SPECTRUM, MF_BYCOMMAND | MF_CHECKED);
		}
		break;
	case ID_DISPLAY_PSD:
		if (setdisplay(1)) {
			uncheckdisp(hwnd);
			CheckMenuItem(GetMenu(hwnd), ID_DISPLAY_PSD, MF_BYCOMMAND | MF_CHECKED);
		}
		break;
	case ID_DISPLAY_LEVEL:
		if (setdisplay(2)) {
		uncheckdisp(hwnd);
		CheckMenuItem(GetMenu(hwnd), ID_DISPLAY_LEVEL, MF_BYCOMMAND | MF_CHECKED);
		}
		break;
	case ID_SETTINGS_DISPLAY_WATERFALL:
		if (setdisplay(3)) {
			uncheckdisp(hwnd);
			CheckMenuItem(GetMenu(hwnd), ID_SETTINGS_DISPLAY_WATERFALL, MF_BYCOMMAND | MF_CHECKED);
		}
		break;
	case ID_SETTINGS_DISPLAY_TRANSFERFUNCTION:
		if (setdisplay(4)) {
			uncheckdisp(hwnd);
			CheckMenuItem(GetMenu(hwnd), ID_SETTINGS_DISPLAY_TRANSFERFUNCTION, MF_BYCOMMAND | MF_CHECKED);
		}
		break;
	case ID_SETTINGS_DISPLAY_IMPULSERESPONSE:
		if (setdisplay(5)) {
			uncheckdisp(hwnd);
			CheckMenuItem(GetMenu(hwnd), ID_SETTINGS_DISPLAY_IMPULSERESPONSE, MF_BYCOMMAND | MF_CHECKED);
		}
		break;
	case ID_SETTINGS_DISPLAY_FACPHASE:
		if (setdisplay(6)) {
			uncheckdisp(hwnd);
			CheckMenuItem(GetMenu(hwnd), ID_SETTINGS_DISPLAY_FACPHASE, MF_BYCOMMAND | MF_CHECKED);
		}
		break;
	case ID_SETTINGS_DISPLAY_MSCPHASE:
		if (setdisplay(7)) {
			uncheckdisp(hwnd);
			CheckMenuItem(GetMenu(hwnd), ID_SETTINGS_DISPLAY_MSCPHASE, MF_BYCOMMAND | MF_CHECKED);
		}
		break;
	case ID_SETTINGS_DISPLAY_TEST:
		if (setdisplay(8)) {
			uncheckdisp(hwnd);
			CheckMenuItem(GetMenu(hwnd), ID_SETTINGS_DISPLAY_TEST, MF_BYCOMMAND | MF_CHECKED);
		}
		break;
	case ID_SETTINGS_DISPLAY_OSCILLOSCOPE:
		if (setdisplay(9)) {
			uncheckdisp(hwnd);
			CheckMenuItem(GetMenu(hwnd), ID_SETTINGS_DISPLAY_OSCILLOSCOPE, MF_BYCOMMAND | MF_CHECKED);
		}
		break;

	case ID_CODEC_SPEEX:
		TransmParam->Service[0].AudioParam.eAudioCoding = CParameter::AC_SPEEX;
		CheckMenuItem(GetMenu(hwnd), ID_CODEC_SPEEX, MF_BYCOMMAND | MF_CHECKED) ;
		CheckMenuItem(GetMenu(hwnd), ID_CODEC_LPC10, MF_BYCOMMAND | MF_UNCHECKED) ;
		break;
 	case ID_CODEC_LPC10:
		TransmParam->Service[0].AudioParam.eAudioCoding = CParameter::AC_LPC;
		CheckMenuItem(GetMenu(hwnd), ID_CODEC_LPC10, MF_BYCOMMAND | MF_CHECKED) ;
		CheckMenuItem(GetMenu(hwnd), ID_CODEC_SPEEX, MF_BYCOMMAND | MF_UNCHECKED) ;
		break;

 	case IDC_RESETACQ:
		if (IsRX2) DRMReceiver.SetInStartMode();
		break;

	case IDC_RXFILES:
		ShellExecute(NULL, "open", rxfilepath, NULL, NULL, SW_SHOWDEFAULT); //just open the folder DM
		break;

	case IDC_GETPICANY:
		if (IsRX2 && RX_Running)
		{
			CMOTObject NewPic;
			int i = 0;
			if (DRMReceiver.GetDataDecoder()->GetSlideShowPartPicture(NewPic))
			{
				char filename[260]{ 0 }; //was 130 DM
				int picsize = 0;
				int filnamsize = 0;
				FILE * set = nullptr;
				picsize = NewPic.vecbRawData.Size();
				if (SaveRXFile)
				{
					wsprintf(filename, "%s%s", rxcorruptpath,NewPic.strName.c_str()); //edited DM
//					wsprintf(filename, "Corrupt\\%s",NewPic.strName.c_str());
					set = fopen(filename,"wb"); //saved partial file is opened here for writing DM
					if (set != nullptr)
					{
						for (i=0;i<picsize;i++) putc(NewPic.vecbRawData[i],set); //write partial file data DM
						fclose(set); //close file DM
					}
					else
						MessageBox( hwnd,"Could not open File",filename,0);	

					filnamsize = strlen(filename);

					if (filnamsize >= 11)
					{
						if (checkfilenam(filename, filnamsize))
							//ShellExecute(nullptr, "open", filename, nullptr, nullptr, SW_SHOWNORMAL); //Deactivated - Execution is a security risk DM
							ShellExecute(nullptr, "open", rxcorruptpath, nullptr, nullptr, SW_SHOWDEFAULT); //Open the folder instead DM
						else
							MessageBox( hwnd,"Executable File Saved",filename,0);	
					}
					else
						MessageBox( hwnd,"No Filename","Info",0);	
				}
			}
		}
		break;
	case IDC_SENDBSR:
		//BSR button links here DM
		if (IsRX2 && RX_Running)
		{

		//this appears to be for sending a BSR request DM
		//Before sending a BSR request, the bsr.bin file must be saved... DM
			if (GetBSR(&numbsrsegments, namebsrfile) == TRUE) //this saves the actual bsr.bin file to disk - it also handles hash etc. and returns TRUE if there were segments missing DM
			{
				if (namebsrfile == "bsr.bin") break; //is this needed?
				//now open a dialog window with the info and choices..
//				txbsrposind = numbsrsegments; //try this..
//				PostMessage(txbsrhwnd[txbsrposind], WM_NOTIFY, 0, 0);
				txbsrhwnd[txbsrposind] = CreateDialog(TheInstance, MAKEINTRESOURCE(DLG_SENDBSR), hwnd, SendBSRDlgProc); //This opens the dialog window to send the request DM
			}

			/*
			//OLD BSR CODE
			char myhashval; //edited DM
			//This grabs the BSR filename from the receiver... DM
			if (DRMReceiver.GetDataDecoder()->GetSlideShowBSR(&numbsrsegments,&namebsrfile,&myhashval)) //Equals new GetBSR
			{
				if (namebsrfile == "bsr.bin") break; //if it was a bsr.bin send from the other station, break here DM
				txbsrposind = -1;	// Search for old position
				for (int el=0;el<NO_BSR_WIND;el++)
					if (txbsr_onscreen_arr[el] == TRUE)
					{
						if (myhashval == txhasharr[el])
						{
							txbsrposind = el;
							el = NO_BSR_WIND;
						}
					}
				if (txbsrposind >= 0)	//Old pos Found
					PostMessage(txbsrhwnd[txbsrposind], WM_NOTIFY, 0, 0);
				else
				{
					txbsrposind = 0;	// Find new position
					for (int el=0;el<NO_BSR_WIND;el++)
						if (!txbsr_onscreen_arr[el])
						{
							txbsrposind = el;
							el = NO_BSR_WIND;
						}
					txbsr_onscreen_arr[txbsrposind] = TRUE;
					txhasharr[txbsrposind] = myhashval;
					txbsrhwnd[txbsrposind] = CreateDialog(TheInstance, MAKEINTRESOURCE (DLG_SENDBSR), hwnd, SendBSRDlgProc);
				}
			}
			*/
		}
		break;
	case ID_SETTINGS_LOADLASTRXFILE:
		if (strlen(lastrxfilename) >= 4)
		{
			int i = 0;
			int hashval = 0;
			int iFileNameSize = 0;
			unsigned char	xorfnam = 0;
			unsigned char	addfnam = 0;
			TXpicpospt = 1;
			wsprintf(filetitle[0],"%s",lastrxfilename);
			wsprintf(pictfile[0],"Rx Files\\%s",lastrxfilename);
			file_size[0] = lastrxfilesize;
			iFileNameSize = strlen(filetitle[0]);
			if (iFileNameSize > 255)	iFileNameSize = MAX_PATHLEN; //was 80 DM (Windows max path length is 255 characters)
			for (i=0;i<iFileNameSize;i++)
			{
				xorfnam ^= filetitle[0][i];
				addfnam += filetitle[0][i];
				addfnam ^= (unsigned char)i;
			}
			hashval = 256*(int)addfnam + (int)xorfnam;
			if (hashval <= 2) hashval += iFileNameSize;
			storesentfilename(pictfile[0],filetitle[0],hashval);
			DRMTransmitter.GetParameters()->SetMSCProtLev(lastrxprotlev);
			DRMTransmitter.GetParameters()->SetSpectrumOccup(lastrxspecocc);
			DRMTransmitter.GetParameters()->eMSCCodingScheme = lastrxcodscheme;
			DRMTransmitter.GetParameters()->InitCellMapTable(lastrxrobmode,lastrxspecocc);
			DRMTransmitter.Init();
			DRMTransmitter.GetParameters()->Service[0].DataParam.iPacketLen = calcpacklen(DRMTransmitter.GetParameters()->iNumDecodedBitsMSC); 
			if (IsRX2)
			{
				DialogBox(TheInstance, MAKEINTRESOURCE (DLG_PICTURE_TX), hwnd, TXPictureDlgProc);
			}
		}
		break;
   }
   //if menu was clicked in the range of any of the sound driver segments, set that device on and the others in that segment off DM
      if (ctrlid >= IDM_O_RX_I_DRIVERS0 && ctrlid < (IDM_O_END_DRIVERS0)) {
	   if (ctrlid >= IDM_O_RX_I_DRIVERS0 && ctrlid < (IDM_O_RX_I_DRIVERS0+ numdevIn)) {
		   int ctrl = ctrlid - IDM_O_RX_I_DRIVERS0;
		   setsoundin(ctrl, 'r'); //compute sound device number from click index
		   unchecksoundrx(hwnd, ctrlid); //clear all other check marks, and add one where clicked
	   }
	   if (ctrlid >= IDM_O_TX_O_DRIVERS0 && ctrlid <= (IDM_O_TX_O_DRIVERS0 + numdevOut)) { // edited NulAsh - to include WaveOut device in menu
		   int ctrl = ctrlid - IDM_O_TX_O_DRIVERS0;
		   setsoundout(ctrl, 't'); //compute sound device number from click index
		   unchecksoundtx(hwnd, ctrlid); //clear all other check marks, and add one where clicked
	   }
	   if (ctrlid >= IDM_O_VO_I_DRIVERS0 && ctrlid < (IDM_O_VO_I_DRIVERS0 + numdevIn)) {
		   int ctrl = ctrlid - IDM_O_VO_I_DRIVERS0;
		   setsoundin(ctrl, 't'); //compute sound device number from click index
		   unchecksoundvoI(hwnd, ctrlid); //clear all other check marks, and add one where clicked
	   }
	   if (ctrlid >= IDM_O_VO_O_DRIVERS0 && ctrlid <= (IDM_O_VO_O_DRIVERS0 + numdevOut)) { // edited NulAsh - to include WaveOut device in menu
		   int ctrl = ctrlid - IDM_O_VO_O_DRIVERS0;
		   setsoundout(ctrl, 'r'); //compute sound device number from click index
		   unchecksoundvoO(hwnd, ctrlid); //clear all other check marks, and add one where clicked
	   }
   }
}

int setdisplay(int type) {
	if ((type == 2) && (TransmParam->Service[0].eAudDataFlag != CParameter::SF_DATA)) {
		//mode 2 can always be set on speech mode
		Display = type;
		disptype = Display;
		return TRUE;
	}
	else if ((type == 9) && (TransmParam->Service[0].eAudDataFlag != CParameter::SF_DATA)) {
		Display = type;
		disptype = OSCDISP;
		return TRUE;
	}
	//only change these modes on receive
	else if (IsRX2 == TRUE) {
		Display = type;
		disptype = Display;
		return TRUE;
	}
	return FALSE;
}


CVector<_REAL>		scopeData1(19200, (_REAL)0.0); //initialize this here
//CVector<_REAL>		scopeData2(19200, (_REAL)0.0); //initialize this here
CVector<_REAL>		vecrData;
CVector<_REAL>		vecrScale;
BOOL firstnorx = TRUE;
int isspdisp = 0;
int stoptx = -1;	
float specagc = 1.0;
float levelsmooth = 1.0;

int oldseg = 0;

void CALLBACK TimerProc(HWND hwnd, UINT nMsg, UINT nIDEvent, DWORD dwTime)
{
	int i = 0;
	//	unsigned int i; //edited DM
	//	int specarrlen; //edited DM
	char tempstr[100]{}; //was 20 DM
	char interl = ' ';
	int qam = 0;
	int prot = 0;
	float snr = 0;
	try
	{

		isspdisp++;
		if (isspdisp >= 4) isspdisp = 0;

		if (stoptx == 0) OnCommand(mainwindow, IDB_START, 0);
		if (stoptx >= 0) stoptx--;

		disptype = Display;
		if (Display == 9) disptype = OSCDISP;

		if (IsRX2 && RX_Running)
		{
			newdata++;
			level = (int)(170.0 * DRMReceiver.GetReceiver()->GetLevelMeter());
			//NEW spectral AGC routine Daz Man 2021
			//apply a sensible threshold to the level input
#define THRESHOLD 20
#define LIMIT 3
			//smooth the level
			if (level > levelsmooth * LIMIT) { level = levelsmooth * LIMIT; } //slew limit attack
			level = max(level, THRESHOLD); //what is the scaling???
			levelsmooth = max(level, levelsmooth * 0.9); //integrator for decay

			//calculate the gain value
			specagc = (float)70.0 / levelsmooth;

			//original routine
			//		if (level * specagc >= 120)
			//			specagc -= (float)1; //was 0.1 DM  = attack time
			//		else if (level * specagc <= 80)
			//			specagc += (float)0.1; //was 0.1 DM = recovery time
			//		if (specagc >= 15.0) specagc = 15.0;

			if (Display == 0)	//spectrum
			{
				DCFreq = (int)DRMReceiver.GetParameters()->GetDCFrequency();
				DRMReceiver.GetReceiver()->GetInputSpec(vecrData);
				if (vecrData.Size() >= 500)
				{
					for (i = 0; i < 250; i++)
						specbufarr[i] = (3.0 * specbufarr[i] + vecrData[2 * i] + vecrData[2 * i + 1]) * 0.2;
					for (i = 0; i < 250; i++)
					{
						specarr[i] = (int)(specbufarr[i] * 40.0 * specagc); //
					}
				}
				if (isspdisp != 0) PaintWaveData(hwnd, TRUE);
			}
			if (Display == 3)	//waterfall
			{
				DRMReceiver.GetReceiver()->GetInputSpec(vecrData);
				if (vecrData.Size() >= 500)
				{
					for (i = 0; i < 250; i++)
						specarr[i] = 25.0 * vecrData[i] * specagc; //
				}
				if (isspdisp != 0) PaintWaveData(hwnd, FALSE);
			}
			if (Display == 8)	//Moving Waterfall
			{
				DCFreq = (int)DRMReceiver.GetParameters()->GetDCFrequency();
				DRMReceiver.GetReceiver()->GetInputSpec(vecrData);
				if (vecrData.Size() >= 500)
				{
					for (i = 0; i < 500; i++)
						specarr[i] = 40.0 * vecrData[i] * specagc; //
				}
				if (isspdisp != 0) PaintWaveData(hwnd, FALSE);
			}
		}
		else {
			if (IsRX2 == FALSE) {
				//Transmit mode
				//Either enable the level display or scope for speech mode
				//And for data mode, blank screen or display something relevant
				//if Display == 0 then the Level meter will be automatically displayed

				if (TransmParam->Service[0].eAudDataFlag == CParameter::SF_AUDIO) {
					//in Speech mode
					if ((IsRX2 == FALSE) && (IsRX == TRUE)) {
						if (rxdisplaysave == -1) {
							if (Display != 9) {
								if (Display != 2) {
									//In speech transmit mode, change the display to level or scope and then change back on receive
									rxdisplaysave = Display;//save the current display mode

								//change to scope
									Display = 9;
									disptype = OSCDISP;
								}
							}
						}
					}
				}
				else {
					//in Data transmit mode
					//Only run this once and only in data mode...
					if ((IsRX2 == FALSE) && (IsRX == TRUE)) {
						for (i = 0; i < 500; i++) specarr[i] = 0;
						IsRX = FALSE;
					}
				}

				if ((Display != 9) && (Display != 2)) {
					//blank display by changing it.. how?
					level = 0;
					//	IsRX = FALSE;
					//disptype = 2; //only in speech mode
				}
				newdata++;
				if (Display == 9)	//Oscilloscope for voice input DM NEW
				{
					DCFreq = 5000; //move vertical blue line off-screen DM
					if (paintmode == 0) {
						DRMTransmitter.GetAudSrcEnc()->GetTxSpeechBuffer(scopeData1); //this grabs a copy of the speech buffer output
					}
					specarrlen = 145;
					int frac = 19200 / specarrlen;
					int temp1 = 0;
					int temp2 = 0;
					int temp3 = 0;
					for (i = 0; i < 19200; i++)
					{
						temp1 = (int)(scopeData1[i] * 0.0027) + 85;
						//temp2 = (int)(scopeData2[i] * 0.0027) + 85;
						temp3++;
						if (temp3 >= frac) {
							temp3 = 0;
							specarr[i / frac] = temp1;
							//specarrim[i / frac] = temp2;
						}
					}
				}
			}

		}
		if (isspdisp != 0) return;

		if (IsRX2 && RX_Running)
		{
			if (Display == 1)	//shifted PSD
			{
				int tmp = 0;
				DRMReceiver.GetOFDMDemod()->GetPowDenSpec(vecrData);
				if (vecrData.Size() >= 256)  // size = 512
					for (i = 0; i < 250; i++)
					{
						if (robmode == 'E') tmp = (int)(-3.0 * vecrData[i + 42]) + 75; //was 45 DM 
						else tmp = (int)(-3.0 * vecrData[i + 85]) + 75; //was 45 DM
						specarr[i] = tmp;
					}
			}
			if (Display == 4)	//Transfer Funct.
			{
				int tmp = 0;
				DRMReceiver.GetChanEst()->GetTransferFunction(vecrData, vecrScale);
				specarrlen = min((int)vecrData.Size(), 530);
				for (i = 0; i < specarrlen; i++)
				{
					tmp = (int)(-0.5 * vecrData[i]) + 45;
					specarr[i] = tmp;
					tmp = (int)(-0.5 * vecrScale[i]) + 85;
					specarrim[i] = tmp;
				}
			}
			if (Display == 5)	//Impulse Response
			{
				int tmp = 0;
				_REAL				rLowerBound, rHigherBound;
				_REAL				rStartGuard, rEndGuard;
				_REAL				rPDSBegin, rPDSEnd;
				DRMReceiver.GetChanEst()->GetAvPoDeSp(vecrData, vecrScale, rLowerBound, rHigherBound, rStartGuard, rEndGuard, rPDSBegin, rPDSEnd);
				specarrlen = vecrData.Size(); //this is quite short
				for (i = 0; i < specarrlen; i++)
				{
					tmp = (int)(-1.5 * vecrData[i]) + 100;
					specarr[i] = tmp;
				}
			}
			if (Display == 6)	//FAC constellation
			{
				int tmp = 0;
				CVector<_COMPLEX>	veccData;
				DRMReceiver.GetFACMLC()->GetVectorSpace(veccData);
				specarrlen = veccData.Size();
				for (i = 0; i < specarrlen; i++)
				{
					tmp = (int)(50.0 * veccData[i].real());
					specarr[i] = tmp;
					tmp = (int)(50.0 * veccData[i].imag());
					specarrim[i] = tmp;
				}
			}
			if (Display == 7)	//MSC constellation
			{
				int tmp = 0;
				CVector<_COMPLEX>	veccData;
				DRMReceiver.GetMSCMLC()->GetVectorSpace(veccData);
				specarrlen = veccData.Size();
				if (specarrlen >= 530) specarrlen = 530;
				for (i = 0; i < specarrlen; i++)
				{
					tmp = (int)(50.0 * veccData[i].real());
					specarr[i] = tmp;
					tmp = (int)(50.0 * veccData[i].imag());
					specarrim[i] = tmp;
				}
			}
			if (Display == 9)	//Oscilloscope for voice output DM NEW
			{
				DCFreq = 5000; //move blue line off-screen DM
				if (paintmode == 0) {
					DRMReceiver.GetAudSrcDec()->GetSpeechBuffer(scopeData1); //this grabs a copy of the speech buffer output
				}
				specarrlen = 145;
				int frac = 19200 / specarrlen;
				int tmp = 0;
				int temp2 = 0;
				for (i = 0; i < 19200; i++)
				{
					tmp = (int)(scopeData1[i] * 0.0027) + 85;
					temp2++;
					if (temp2 >= frac) {
						temp2 = 0;
						specarr[i / frac] = tmp;
					}
				}
			}


#define BARGRAPH_IN_NEW_THREAD 2
#if BARGRAPH_IN_NEW_THREAD == 1
			if (BGbusy == 0) {
				std::thread BarDraw(DrawBar, hwnd); //launch the Bargraph in a new thread
				BarDraw.detach(); //detach and terminate after running
			}
#endif
#if BARGRAPH_IN_NEW_THREAD == 0
			//======================================================================================================================================================
			//SEGMENT BARGRAPH by Daz Man 2021
			//======================================================================================================================================================
			RECT rect{};
			int width = 218;
			if (GetWindowRect(hwnd, &rect))
			{
				width = (rect.right - rect.left) - 1;
				//int height = rect.bottom - rect.top;
			}
			//read erasures buffer and convert it to a line graph
			const int x = DRMReceiver.GetDataDecoder()->GetActPos(); //get current segment number
			int y = DRMReceiver.GetDataDecoder()->GetTotSize(); //Total segment count DM

			//if the GetTotSize function fails (it does sometimes - reason unknown), we can use our own computation if in RS mode
	//		if ((DecSegSize > 0) && (HdrFileSize > 0)){
	//			y = ceil((float)HdrFileSize / DecSegSize); //segment total = filesize/segsize
	//		}

			y = max(DecTotalSegs, y);
			y = max(y, x);

			if (y == 0) { y = 1; } //don't divide by zero!

			//The desired width is "width"
			//the actual width is "y"
			//the correction "n" is width/y
			//to use int scaling, n*1000 = BarLastSeg*1000/y

			//int n = width / max(totsize, DecTotalSegs);
	//		int n = float(width << 12) / (y << 12);
			int n = (width * 1000) / y;
			//this executes every 100mS, so make sure it doesn't run too often
			if ((BarLastSeg != x) && (CRCOK)) {
				int d = 0;

				if (n > 1000) { n = 1000; } //Don't stretch bargraph

				HDC hdc = GetDC(hwnd);
				MoveToEx(hdc, BARL, BARY, nullptr);
				/*
				XFORM xForm;
				xForm.eM11 = (FLOAT)2.0;
				xForm.eM12 = (FLOAT)0.0;
				xForm.eM21 = (FLOAT)0.0;
				xForm.eM22 = (FLOAT)2.0;
				xForm.eDx = 0;
				xForm.eDy = 0;
				SetWorldTransform(hdc, &xForm);
				*/
				//HPEN penz;
				HPEN penx = nullptr;
				HPEN penb = nullptr;
				HPEN penr = nullptr;
				HPEN peng = nullptr;
				LOGBRUSH lbb;
				lbb.lbStyle = BS_SOLID;
				lbb.lbColor = RGB(0, 0, 0);
				lbb.lbHatch = 0;
				LOGBRUSH lbr;
				lbr.lbStyle = BS_SOLID;
				lbr.lbColor = RED; // RGB(255, 0, 0);
				lbr.lbHatch = 0;
				LOGBRUSH lbg;
				lbg.lbStyle = BS_SOLID;
				lbg.lbColor = GREEN; // RGB(60, 255, 0);
				lbg.lbHatch = 0;
				LOGBRUSH lbx;
				lbx.lbStyle = BS_SOLID;
				lbx.lbColor = GetSysColor(COLOR_3DFACE); //RGB(127, 127, 127);
				lbx.lbHatch = 0;

				const int pwidth = 1;
				penb = ExtCreatePen(PS_GEOMETRIC | PS_SOLID | PS_ENDCAP_FLAT | PS_JOIN_MITER, pwidth, &lbb, 0, nullptr); //Black for current segment
				penr = ExtCreatePen(PS_GEOMETRIC | PS_SOLID | PS_ENDCAP_FLAT | PS_JOIN_MITER, 1, &lbr, 0, nullptr); //Red for no segment received
				peng = ExtCreatePen(PS_GEOMETRIC | PS_SOLID | PS_ENDCAP_FLAT | PS_JOIN_MITER, pwidth, &lbg, 0, nullptr); //Green for good segment
				penx = ExtCreatePen(PS_GEOMETRIC | PS_SOLID | PS_ENDCAP_FLAT | PS_JOIN_MITER, 6, &lbx, 0, nullptr); //Grey background erase
				//penz = ExtCreatePen(PS_GEOMETRIC | PS_SOLID | PS_ENDCAP_FLAT | PS_JOIN_MITER, 1, &lbx, 0, nullptr); //Grey background erase

				//Erase the last black line - for the 2nd/3rd pass etc.
				if (x < BarLastSeg) {
					SelectObject(hdc, peng); //use green - because if there was a black line drawn, there must have been a good segment
					MoveToEx(hdc, ((BarLastSeg * n) / 1000), BARB, nullptr);
					LineTo(hdc, ((BarLastSeg * n) / 1000), BART); //erase
				}

				//data is good and we have a valid new segment - so update the graph
				//copy from buffer and draw to the screen
				//only read as far into the buffer as we need to...
				for (i = 0; i < y; i++) {
					//for each bit read, set the display red on 0 and green on 1
					d = (erasures[erasureswitch][i >> 3] >> (i & 7)) & 1; //get the correct bit
					//if (d == 0) { d = 0xFF0000; } //make 0 = 0xFF0000 = 16711680 red
					//if (d == 1) { d = 0x00FF00; } //make 1 = 0x00FF00 = 65280    green

					if (d == 0) {
						SelectObject(hdc, penr); //select red pen
						MoveToEx(hdc, (i * n) / 1000, BARB, nullptr); //
						LineTo(hdc, (i * n) / 1000, BART); //draw a red line up
					}
					if (d == 1) {
						SelectObject(hdc, peng);//select green pen
						MoveToEx(hdc, (i * n) / 1000, BARB, nullptr); //
						LineTo(hdc, (i * n) / 1000, BART); //draw a green line up
					}
				}

				//			MoveToEx(hdc, floor((x+1)*n), BARY, nullptr);
				//			LineTo(hdc, floor((x + 3) * n), BARY); //draw a black tip on the line

				SelectObject(hdc, penb);
				MoveToEx(hdc, ((x * n) / 1000), BARB, nullptr);
				LineTo(hdc, ((x * n) / 1000), BART); //draw a black tip on the line

				//did the transport ID change? (new file) - check if the Total segment count has changed also, and redraw the window background where needed
				if ((BarTransportID != DecTransportID) || (y != BarLastTot) || (x > BarLastTot)) {

					SelectObject(hdc, penx); //penx is the window background colour, and 6 pixels square
					MoveToEx(hdc, ((x * n) / 1000) + 5, BARY, nullptr);
					LineTo(hdc, BARR, BARY); //erase the rest of the window
					BarTransportID = DecTransportID;
					BarLastTot = y; //update totsegs
				}

				DeleteObject(penr);
				DeleteObject(peng);
				DeleteObject(penb);
				DeleteObject(penx);
				//DeleteObject(penz);

				BarLastSeg = x;

				ReleaseDC(hwnd, hdc);

			}
#endif
			//======================================================================================================================================================

			if (DRMReceiver.GetParameters()->Service[0].IsActive())	//flags, etc.
			{
				CParameter::EAudCod ecodec;
				firstnorx = TRUE;
				rxcall = DRMReceiver.GetParameters()->Service[0].strLabel; //Get callsign from the incoming data DM
				if (rxcall == lastrxcall) consrxcall = rxcall;				//?
				else if (rxcall.length() >= 3) lastrxcall = rxcall;			//?
				SendMessage(GetDlgItem(hwnd, IDC_EDIT), WM_SETTEXT, 0, (LPARAM)rxcall.c_str());
				snr = (float)DRMReceiver.GetChanEst()->GetSNREstdB();	//Get SNR DM
				sprintf(tempstr, "%.1f", snr);							//Format for display DM
				SendMessage(GetDlgItem(hwnd, IDC_EDIT4), WM_SETTEXT, 0, (LPARAM)tempstr); //Display SNR DM
				snr = DRMReceiver.GetParameters()->GetDCFrequency();
				sprintf(tempstr, "%d", (int)snr);
				SendMessage(GetDlgItem(hwnd, IDC_DCFREQ), WM_SETTEXT, 0, (LPARAM)tempstr);

				if (DRMReceiver.GetParameters()->Service[0].eAudDataFlag == CParameter::SF_AUDIO)
				{
					sprintf(tempstr, "%d ", DRMReceiver.GetAudSrcDec()->getdecodperc()); //may need updating DM ======================== This is for speech mode DM
					SendMessage(GetDlgItem(hwnd, IDC_EDIT5), WM_SETTEXT, 0, (LPARAM)tempstr);

					wsprintf(tempstr, "MSCbits:%u  AudDecBits:%u Frame:%u", DRMReceiver.GetParameters()->iNumDecodedBitsMSC, DRMReceiver.GetParameters()->iNumAudioDecoderBits, FrameSize);
					SendMessage(GetDlgItem(hwnd, IDC_EDIT6), WM_SETTEXT, 0, (LPARAM)tempstr); //Added receive blocksize DM ================

					ecodec = DRMReceiver.GetParameters()->Service[0].AudioParam.eAudioCoding;
					if (ecodec == CParameter::AC_LPC)
						SendMessage(GetDlgItem(hwnd, IDC_EDIT3), WM_SETTEXT, 0, (LPARAM)"LPC-10");
					else if (ecodec == CParameter::AC_SPEEX)
						SendMessage(GetDlgItem(hwnd, IDC_EDIT3), WM_SETTEXT, 0, (LPARAM)"SPEEX");
					else if (ecodec == CParameter::AC_SSTV)
						SendMessage(GetDlgItem(hwnd, IDC_EDIT3), WM_SETTEXT, 0, (LPARAM)"SSTV");
					else
						SendMessage(GetDlgItem(hwnd, IDC_EDIT3), WM_SETTEXT, 0, (LPARAM)" ");
				}
				else
				{
					//lasterror = 0;

					//compute average and peak SNR for logging DM
					float error = (float)((float)DRMReceiver.GetChanEst()->GetSNREstdB() - DMSNRaverage);
					DMSNRaverage = (float)DMSNRaverage + error * 0.01 + (0.01 * (max(error, 0.0) * 0.5)); //slew towards average
					DMSNRmax = (float)max(DMSNRmax * 0.9999, (float)DRMReceiver.GetChanEst()->GetSNREstdB());

					//This is data mode (file receive) DM
					if (RxRSlevel == 0) {
						sprintf(tempstr, "Data"); //
					}
					else
						sprintf(tempstr, "RS%d Data", RxRSlevel); //
					SendMessage(GetDlgItem(hwnd, IDC_EDIT3), WM_SETTEXT, 0, (LPARAM)tempstr);

					totsize = DRMReceiver.GetDataDecoder()->GetTotSize(); //Total segment count DM
					//totsize = max(totsize, CompTotalSegs);//Computed from old header DM
					//totsize = max(totsize, DecTotalSegs); //From new serial backup DM
					// 
					//These are now directly saved out of the respective routine, instead of here DM
					//actsize = DRMReceiver.GetDataDecoder()->GetActSize(); //Current number of good segments DM - This is now updated directly inside the DataDecoder class
					//actpos = DRMReceiver.GetDataDecoder()->GetActPos();   //Current incoming segment DM

					sprintf(tempstr, "%u / %u / %u", totsize, actsize, actpos); //This is the decoder Info display DM
					SendMessage(GetDlgItem(hwnd, IDC_EDIT5), WM_SETTEXT, 0, (LPARAM)tempstr);

					/*
					RSfilesize = DecFileSize; //This is exactly what was sent, after RS coding into multiples of 255
					if (RSfilesize == 0) {
						RSfilesize = HdrFileSize; //grab it from the old header if available
					}
					if ((HdrFileSize > 0) && (RSfilesize != HdrFileSize)) {
						RSfilesize = HdrFileSize; //grab it from the old header if there's an error (version conflict)
					}
					*/

					//=========================================================================================================================================================
					//This is for non-RS received files that have ALL segments decoded OK - DM
					//Do not execute this on RS coded files DM
					//Maybe this should be moved to the DABMOT code or the MOTSlideShow code??? DABMOT is where the RS decoder is
					CMOTObject NewPic; //Generate "NewPic" from the CMOTObject class DM
					if (DRMReceiver.GetDataDecoder()->GetSlideShowPicture(NewPic))
					{
#if BUFFERDEBUG
						//Save some debug info:
						//====================================================================================
						//Buffer debugging....
						//Dump the entire Global buffer to a disk file to verify it's integrity DM
						FILE* set = nullptr;
						if ((set = fopen("Rx Files\\debug2.txt", "wb")) == nullptr) {
							// handle error here DM
							lasterror |= 2048;
						}
						else {
							i = 0; //start at zero
							//this is normally buffer1
							while (i < NewPic.vecbRawData.size()) { //edit DM
								putc(GlobalDMRxRSData[i], set);
								i++;
							}
							fclose(set); //file is closed here - but only if it was opened
						}
						//====================================================================================
#endif

						if (RxRSlevel == 0) {


							//RxRSlevel is sent using 3 of the previously unused bits in the segment header
							//This code section does not use the extra RS coding, so RxRSlevel must == 0
							// 
							//This executes only when a complete file is received DM
							char filename[260]{ 0 }; //was 130 DM - (Windows max path length is 255 characters) (HamDRM only uses 80 characters though...)
							int picsize = 0; //edited DM - reverted
							int filnamsize = 0;
							FILE* set = nullptr; //changed to nullptr on compiler advice
							picsize = NewPic.vecbRawData.Size();

							//check if the file is a BSR (request?) file from the other station DM
							//This appears to be for INCOMING BSR files DM
							if (strcmp(NewPic.strName.c_str(), "bsr.bin") == 0)
							{
								set = fopen("bsrreq.bin", "wt"); //save incoming BSR requests under this name DM =====================================
								if (set != nullptr)
								{
									for (i = 0; i < picsize; i++) putc(NewPic.vecbRawData[i], set); //write incoming BSR request to the file DM
									fclose(set);

									//Decompress incoming BSR request file here DM
									char filenam[300];
									char filenam2[300];
									wsprintf(filenam, "%s%s", bsrpath, "bsrreq.bin");
									wsprintf(filenam2, "%s%s", bsrpath, "cbsrreq.bin");
									CopyFile(filenam, filenam2, FALSE);
									decompressBSR(filenam2, filenam); //decompress renamed to decompressBSR to prevent conflict with zlib DM
									remove(filenam2); //delete temp file DM

									filetosend = readbsrfile(bsrpath);
								}
								else
									filetosend = 'x';

								if (filetosend.size() >= 3)
								{
									//acthash = gethash(); //removed DM
									//the hash used was the iTID (transmit ID) DM
									acthash = 0; // gethash(); //removed DM

									bsrposind = -1;	// Search for old position
									for (int el = 0; el < NO_BSR_WIND; el++)
									{
										if (bsr_onscreen_arr[el] == TRUE)
										{
											if (acthash == hasharr[el])  //This is now set to zero DM - This should be OK, as it appears to just be a unique number for the BSR window DM
											{
												if (bsrcall[el] == consrxcall)
												{
													bsrposind = el;
													el = NO_BSR_WIND;
												}
											}
										}
									}
									if (bsrposind >= 0)	//Old pos Found
									{
										PostMessage(bsrhwnd[bsrposind], WM_NOTIFY, 0, 0);
									}
									else				//Find new pos
									{
										bsrposind = 0;	// Find new position
										for (int el = 0; el < NO_BSR_WIND; el++)
										{
											if (bsr_onscreen_arr[el] == FALSE)
											{
												bsrposind = el;
												el = NO_BSR_WIND;
											}
										}
										bsr_onscreen_arr[bsrposind] = TRUE;
										hasharr[bsrposind] = acthash;
										bsrcall[bsrposind] = consrxcall;
										bsrhwnd[bsrposind] =
											CreateDialog(TheInstance, MAKEINTRESOURCE(DLG_ANSWERBSR), hwnd, AnswerBSRDlgProc);
									}
								}
							}
							else if (SaveRXFile)
							{
								int iFileNameSize = 0; //edited DM - reverted
								int tmphashval = 0;
								unsigned char	xorfnam = 0;
								unsigned char	addfnam = 0;
								wsprintf(filename, "%s", NewPic.strName.c_str());
								iFileNameSize = strlen(filename);
								if (iFileNameSize > 255)	iFileNameSize = MAX_PATHLEN;  //was 80 now 260 DM - (Windows max path length is 255 characters)
								for (i = 0; i < iFileNameSize; i++)
								{
									xorfnam ^= filename[i];
									addfnam += filename[i];
									addfnam ^= (unsigned char)i;
								}
								tmphashval = 256 * (int)addfnam + (int)xorfnam;
								if (tmphashval <= 2) tmphashval += iFileNameSize;

								for (int el = 0; el < NO_BSR_WIND; el++)
								{
									if (tmphashval == txhasharr[el])
									{
										if (txbsr_onscreen_arr[el] == TRUE)
										{
											bCloseMe = TRUE;
											SendMessage(txbsrhwnd[el], WM_NOTIFY, 0, 0);
										}
									}
								}

								wsprintf(lastrxfilename, "%s", NewPic.strName.c_str());
								lastrxfilesize = picsize;
								lastrxrobmode = DRMReceiver.GetParameters()->GetWaveMode();
								lastrxspecocc = DRMReceiver.GetParameters()->GetSpectrumOccup();
								lastrxcodscheme = DRMReceiver.GetParameters()->eMSCCodingScheme;
								lastrxprotlev = DRMReceiver.GetParameters()->MSCPrLe.iPartB;

								//wsprintf(filename, "Rx Files\\%s", NewPic.strName.c_str());
								strcpy(filename, NewPic.strName.c_str());

								//===========================================================================================
								//Daz Man
								//zlib-LZMA decompression
								//test the file by checking for the extra filename extension to denote it's been compressed
								//if it is present, write into buffer1 and expand the file using zlib and save into buffer2 for writing to file
								//else, write file directly to disk
								//finally close file and delete buffers from heap
								//===========================================================================================
								const uLongf BUFSIZE = 524288; //512k should be enough for anything practical - HEAP STORAGE
#if USEGZIP

							//If file is bigger than 512k, bypass decompression and save it directly DM
							//.gz is used for standard mode to be compatible
								if ((stricmp(&filename[strlen(filename) - 3], ".gz") == 0) && (picsize <= BUFSIZE)) {
									//data is compressed, so decompress it
									_BYTE* buffer1 = new _BYTE[BUFSIZE]; //File read buffer
									_BYTE* buffer2 = new _BYTE[BUFSIZE]; //zlib decompression buffer
									i = 0;
									while (i < picsize) {
										buffer1[i] = NewPic.vecbRawData[i]; //save data to buffer1
										i++;
									}
									uLongf filesize = BUFSIZE; //to tell zlib how big the buffer is
									int result; //not used at this time
									result = uncompress(buffer2, &filesize, buffer1, picsize); //decompress data using zlib

									//cut extension off filename
									filename[strlen(filename) - 3] = 0; //terminate the string early to cut off the extra .gz extension DM

									LogData(filename); //log the SNR stats DM

									char savename[260] = "";
									wsprintf(savename, "Rx Files\\%s", filename);

									//saved file is opened here for writing DM
									if ((set = fopen(savename, "wb")) == nullptr) {
										// handle error here DM
									}
									else {
										i = 0;
										while (unsigned(i) < filesize) { //edit DM
											putc(buffer2[i], set);
											i++;
										}
										fclose(set); //file is closed here - but only if it was opened
										filestate = 2; //File decode status
									}
									delete[] buffer1; //remove the buffer arrays from the heap
									delete[] buffer2;
								}
								else if ((stricmp(&filename[strlen(filename) - 3], ".lz") == 0) && (picsize <= BUFSIZE)) {
#endif //USEGZIP
#if !USEGZIP
									if ((stricmp(&filename[strlen(filename) - 3], ".lz") == 0) && (picsize <= BUFSIZE)) {

#endif //!USEGZIP
										//data is compressed, so decompress it
										_BYTE* buffer1 = new _BYTE[BUFSIZE]; //File read buffer
										_BYTE* buffer2 = new _BYTE[BUFSIZE]; //zlib decompression buffer
										i = 0;
										while (i < picsize) {
											buffer1[i] = NewPic.vecbRawData[i]; //save data to buffer1
											i++;
										}
										SizeT filesize = BUFSIZE; //to tell zlib how big the buffer is
										int result = 0; //not used at this time
#define propslength 5
										SizeT filesizein = picsize;
										SizeT outsize = BUFSIZE;
										//original filesize is saved in 3 bytes after props
										dcomperr = LzmaUncompress(buffer2, &outsize, buffer1 + propslength + 3, &filesizein, buffer1, propslength);

										//grab original filesize that was saved after props
										int i = 0;
										filesize = (BYTE)(buffer1 + propslength)[i]; //byte 1
										i++; //next
										filesize |= (BYTE)(buffer1 + propslength)[i] << 8; //byte 2
										i++; //next
										filesize |= (BYTE)(buffer1 + propslength)[i] << 16; //byte 3

										//outsize no longer used

										//cut extension off filename
										filename[strlen(filename) - 3] = 0; //terminate the string early to cut off the extra .lz extension DM

										LogData(filename, 1); //log the SNR stats DM

										char savename[260] = "";
										wsprintf(savename, "Rx Files\\%s", filename);

										//saved file is opened here for writing DM
										if ((set = fopen(savename, "wb")) == nullptr) {
											// handle error here DM
										}
										else {
											i = 0;
											while (unsigned(i) < filesize) { //edit DM
												putc(buffer2[i], set);
												i++;
											}
											fclose(set); //file is closed here - but only if it was opened
											filestate = FS_SAVED; //File decode status
											filestate2 = FS_SAVED; //File decode status
											showgood = SHOWCOL; //show green
										}
										delete[] buffer1; //remove the buffer arrays from the heap
										delete[] buffer2;
									}
									else {
										//data is not compressed, save normally - no need for extra buffers
										//Also - if incoming file is bigger than 512k (!) don't decompress it because it will overflow the buffers (can only happen if a *.lz file is sent (?), so save it normally)
										LogData(filename, 1); //log the SNR stats DM

										char savename[260] = "";
										wsprintf(savename, "Rx Files\\%s", filename);

										//saved file is opened here for writing DM
										if ((set = fopen(savename, "wb")) == nullptr) {
											//handle error here DM
										}
										else {
											//no error
											//file data is written here DM
											for (i = 0; i < picsize; i++) {
												putc(NewPic.vecbRawData[i], set);
											}
											fclose(set); //file is closed here
											filestate = FS_SAVED; //File decode status
											filestate2 = FS_SAVED; //File decode status
											showgood = SHOWCOL; //show green
										}
									}

									//for (i=0;i<picsize;i++) putc(NewPic.vecbRawData[i],set); //file data was written here DM - DISABLED NOW
			//						}
									//Consider deleting the auto-open function, as it could be a security risk DM - Deactivated
									if (ShowRXFile)
									{
										filnamsize = strlen(filename);
										if (filnamsize >= 11)
										{
											if (checkfilenam(filename, filnamsize))
											{
												//if (stricmp(lastfilename, filename) != 0)
													//ShellExecute(nullptr, "open", filename, nullptr, nullptr, SW_SHOWNORMAL); //Deactivated - Auto-opening files is a security risk DM
												ShellExecute(nullptr, "open", rxfilepath, nullptr, nullptr, SW_SHOWDEFAULT); //Open the folder instead DM
											}
											else
											{
												if (stricmp(lastfilename, filename) != 0)
													MessageBox(hwnd, filename, "Executable File Saved", 0);
											}
										}
										if (ShowOnlyFirst) wsprintf(lastfilename, "%s", filename);
									}
							}
						}
					}

						sendinfo(hwnd); //send stats to window
				}

					if (DRMReceiver.GetParameters()->GetWaveMode() == RM_ROBUSTNESS_MODE_A)
						robmode = 'A';
					if (DRMReceiver.GetParameters()->GetWaveMode() == RM_ROBUSTNESS_MODE_B)
						robmode = 'B';
					if (DRMReceiver.GetParameters()->GetWaveMode() == RM_ROBUSTNESS_MODE_E)
						robmode = 'E';
					if ((DRMReceiver.GetParameters()->GetSpectrumOccup()) == SO_0)
						specocc = 3;
					else if ((DRMReceiver.GetParameters()->GetSpectrumOccup()) == SO_1)
						specocc = 5;
					if (DRMReceiver.GetParameters()->eSymbolInterlMode == CParameter::SI_LONG)
						interl = 'L';
					else if (DRMReceiver.GetParameters()->eSymbolInterlMode == CParameter::SI_SHORT)
						interl = 'S';
					if (DRMReceiver.GetParameters()->eMSCCodingScheme == CParameter::CS_2_SM)
						qam = 16;
					else if (DRMReceiver.GetParameters()->eMSCCodingScheme == CParameter::CS_3_SM)
						qam = 64;
					else
						qam = 4;
					prot = DRMReceiver.GetParameters()->MSCPrLe.iPartB;
					wsprintf(tempstr, "%c/%c/%d/%d/2.%d", robmode, interl, qam, prot, specocc);
					SendMessage(GetDlgItem(hwnd, IDC_EDIT2), WM_SETTEXT, 0, (LPARAM)tempstr);
					if ((DRMReceiver.GetParameters()->Service[0].eAudDataFlag == CParameter::SF_AUDIO) && (DRMReceiver.GetParameters()->Service[0].AudioParam.bTextflag == TRUE))
					{
						if ((AllowRXTextMessage) && (runmode != 'P'))
						{
							if ((!RXTextMessageON) && (!RXTextMessageWasClosed))
								RXMessagehwnd = CreateDialog(TheInstance,
									MAKEINTRESOURCE(DLG_RXTEXTMESSAGE), hwnd, RXTextMessageDlgProc);
							else if (RXTextMessageON)
								SendMessage(RXMessagehwnd, WM_NOTIFY, 0, 0);
						}
					}
			}
				else if (firstnorx)
				{
					//if (RXTextMessageON) SendMessage(RXMessagehwnd,WM_CLOSE,0,0);
					wsprintf(lastfilename, "%s", "none");
					SendMessage(GetDlgItem(hwnd, IDC_EDIT), WM_SETTEXT, 0, (LPARAM)"No Service");
					SendMessage(GetDlgItem(hwnd, IDC_EDIT2), WM_SETTEXT, 0, (LPARAM)" ");
					SendMessage(GetDlgItem(hwnd, IDC_EDIT3), WM_SETTEXT, 0, (LPARAM)" ");
					SendMessage(GetDlgItem(hwnd, IDC_EDIT4), WM_SETTEXT, 0, (LPARAM)"0");
					SendMessage(GetDlgItem(hwnd, IDC_EDIT5), WM_SETTEXT, 0, (LPARAM)" ");
					SendMessage(GetDlgItem(hwnd, IDC_DCFREQ), WM_SETTEXT, 0, (LPARAM)" ");
					SendMessage(GetDlgItem(hwnd, IDC_EDIT6), WM_SETTEXT, 0, (LPARAM)" "); //added DM
					filestate = FS_BLANK;
					SendMessage(GetDlgItem(hwnd, IDC_EDIT7), WM_SETTEXT, 0, (LPARAM)" "); //added DM

					//Clear bargraph DM
					ClearBar(hwnd);

					firstnorx = FALSE;
				}
				else
				{
					RXTextMessageWasClosed = FALSE;
				}
		}
			else if (!IsRX2 && TX_Running)
			{
				//Grey out LEDs
				PostWinMessage(MS_IOINTERFACE, 3);
				PostWinMessage(MS_FREQ_FOUND, 3);
				PostWinMessage(MS_TIME_SYNC, 3);
				PostWinMessage(MS_FRAME_SYNC, 3);
				PostWinMessage(MS_FAC_CRC, 3);
				PostWinMessage(MS_MSC_CRC, 3);

				if (paintmode == 2) {
					SendMessage(GetDlgItem(hwnd, IDC_EDIT3), WM_SETTEXT, 0, (LPARAM)"TUNE");
					SendMessage(GetDlgItem(hwnd, IDC_EDIT6), WM_SETTEXT, 0, (LPARAM)"TRANSMITTING TUNING TONES");
				}
				else if (paintmode == 1) {
					SendMessage(GetDlgItem(hwnd, IDC_EDIT3), WM_SETTEXT, 0, (LPARAM)"WF TEXT");
					SendMessage(GetDlgItem(hwnd, IDC_EDIT6), WM_SETTEXT, 0, (LPARAM)"TRANSMITTING WATERFALL TEXT");
				}
				else {
					int numbits = 0, percent = 0;

					firstnorx = TRUE;
					if (TransmParam->Service[0].eAudDataFlag == CParameter::SF_AUDIO)
					{
						level = (int)(170.0 * DRMTransmitter.GetReadData()->GetLevelMeter()); //this is for reading audio input level to the speech codecs DM

						if (TransmParam->Service[0].AudioParam.eAudioCoding == CParameter::AC_LPC)
							SendMessage(GetDlgItem(hwnd, IDC_EDIT3), WM_SETTEXT, 0, (LPARAM)"LPC-10");
						if (TransmParam->Service[0].AudioParam.eAudioCoding == CParameter::AC_SPEEX)
							SendMessage(GetDlgItem(hwnd, IDC_EDIT3), WM_SETTEXT, 0, (LPARAM)"SPEEX");
						if (TransmParam->Service[0].AudioParam.eAudioCoding == CParameter::AC_SSTV)
							SendMessage(GetDlgItem(hwnd, IDC_EDIT3), WM_SETTEXT, 0, (LPARAM)"SSTV");

						SendMessage(GetDlgItem(hwnd, IDC_EDIT5), WM_SETTEXT, 0, (LPARAM)"Voice TX");
					}
					else
					{
						level = 0; //blank the level display DM
						if (ECCmode < 4) {
							sprintf(tempstr, "Data"); //
						}
						else
							sprintf(tempstr, "RS%d Data", ECCmode - 3); //
						SendMessage(GetDlgItem(hwnd, IDC_EDIT3), WM_SETTEXT, 0, (LPARAM)tempstr);

						numbits = DRMTransmitter.GetAudSrcEnc()->GetPicCnt();
						percent = DRMTransmitter.GetAudSrcEnc()->GetPicPerc();
						wsprintf(tempstr, "%d / %d%%", numbits, percent);
						SendMessage(GetDlgItem(hwnd, IDC_EDIT5), WM_SETTEXT, 0, (LPARAM)tempstr);

						wsprintf(tempstr, "Sending data of %u bytes", EncFileSize); //added info - in testing - DM
						SendMessage(GetDlgItem(hwnd, IDC_EDIT6), WM_SETTEXT, 0, (LPARAM)tempstr); //send to stats window DM

						percent = DRMTransmitter.GetAudSrcEnc()->GetNoOfPic();
						if ((numbits + 1 > percent) && (stoptx == -1)) stoptx = stoptx_time;
					}
					numbits = DRMTransmitter.GetParameters()->iNumDecodedBitsMSC;
					wsprintf(tempstr, "%d", numbits);
					SendMessage(GetDlgItem(hwnd, IDC_EDIT4), WM_SETTEXT, 0, (LPARAM)tempstr);
					SendMessage(GetDlgItem(hwnd, IDC_EDIT), WM_SETTEXT, 0, (LPARAM)getcall());
					if (DRMTransmitter.GetParameters()->GetWaveMode() == RM_ROBUSTNESS_MODE_A)
						robmode = 'A';
					if (DRMTransmitter.GetParameters()->GetWaveMode() == RM_ROBUSTNESS_MODE_B)
						robmode = 'B';
					if (DRMTransmitter.GetParameters()->GetWaveMode() == RM_ROBUSTNESS_MODE_E)
						robmode = 'E';
					if ((DRMTransmitter.GetParameters()->GetSpectrumOccup()) == SO_0)
						specocc = 3;
					else if ((DRMTransmitter.GetParameters()->GetSpectrumOccup()) == SO_1)
						specocc = 5;
					if (DRMTransmitter.GetParameters()->eSymbolInterlMode == CParameter::SI_LONG)
						interl = 'L';
					else if (DRMTransmitter.GetParameters()->eSymbolInterlMode == CParameter::SI_SHORT)
						interl = 'S';
					if (DRMTransmitter.GetParameters()->eMSCCodingScheme == CParameter::CS_2_SM)
						qam = 16;
					else if (DRMTransmitter.GetParameters()->eMSCCodingScheme == CParameter::CS_3_SM)
						qam = 64;
					else
						qam = 4;
					prot = DRMTransmitter.GetParameters()->MSCPrLe.iPartB;
					wsprintf(tempstr, "%c/%c/%d/%d/2.%d", robmode, interl, qam, prot, specocc);
					SendMessage(GetDlgItem(hwnd, IDC_EDIT2), WM_SETTEXT, 0, (LPARAM)tempstr);

					DMmodehash = robmode + specocc * 4 + qam * 16 + max((ECCmode - 3), 0) * 32; //compute a mode hash to add to the TID in RS modes - DM =============================================================================================

					//send debugging info to stats window DM
					if (TransmParam->Service[0].eAudDataFlag == CParameter::SF_AUDIO) {
						lasterror2 = sprintf_s(tempstr, "BlockSize:%d TxtStart:%d TextEnd:%d", BlockSize, TextBytesi, TextBytes);
						lasterror2 = SendMessage(GetDlgItem(hwnd, IDC_EDIT6), WM_SETTEXT, 0, (LPARAM)tempstr); //send to stats window DM
					}
				}
			}


			if (IsRX2)
			{
				if ((disptype == 3) || (disptype == 8))
					PaintWaveData(hwnd, FALSE);
				else
					PaintWaveData(hwnd, TRUE);
			}
			else
				PaintWaveData(hwnd, TRUE);

	}
		catch (CGenErr GenErr) //Add some descriptive messages here for the exception generated, and also delete heap buffers if they still exist DM
		{
			return;
		}

}


BOOL CALLBACK AboutDlgProc
   (HWND hwnd, UINT message, UINT wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_INITDIALOG:
        return TRUE;
    case WM_COMMAND:
        switch (LOWORD (wParam))
        {
        case IDOK:
        case IDCANCEL:
            EndDialog (hwnd, 0);
            return TRUE;
        }
        break;
    }
    return FALSE;
}


BOOL CALLBACK SendBSRDlgProc
   (HWND hwnd, UINT message, UINT wParam, LPARAM lParam)
{
	

	char tmpbsrname[20]{};
	int tmpno = 0;
	BOOL send3 = FALSE;
    switch (message)
    {
    case WM_INITDIALOG:
    case WM_NOTIFY:
		//This deals with the BSR request window
		if (bCloseMe)
		{
			bCloseMe = FALSE;
			tmpno = GetDlgItemInt( hwnd, IDC_BSRINST, NULL, FALSE );
			txbsr_onscreen_arr[tmpno] = FALSE;
			EndDialog (hwnd, 0);
		}
		else
		{
//			wsprintf(tmpbsrname,"bsr%d.bin",txbsrposind);  //removed DM == figure out what this was doing - probably multi-window BSR
			wsprintf(tmpbsrname, "%s%s", bsrpath, "bsr.bin"); //added DM
			SetDlgItemInt( hwnd, IDC_SENDBSR_NOSEG, numbsrsegments, FALSE );

//try to get BSR filename to display...
//			SetDlgItemText(hwnd, IDC_SENDBSR_FNAME, namebsrfile); //edited DM -- THIS DOES NOT WORK
//			SetDlgItemText(hwnd, IDC_SENDBSR_FNAME, namebsrfile.c_str()); //edited DM -- THIS DOES NOT WORK
			SetDlgItemText(hwnd, IDC_SENDBSR_FNAME, DMfilename); //display filename - This works, but it relies on other code, and may not be in sync...
			SetDlgItemInt( hwnd, IDC_BSRINST, txbsrposind, FALSE ); //display how many segments
			SetDlgItemText( hwnd, IDC_SENDBSR_FROMCALL, consrxcall.c_str()); //display callsign
//			CopyFile("bsr.bin",tmpbsrname,FALSE); //removed DM
		}
        return TRUE;
    case WM_COMMAND:
        switch (LOWORD (wParam))
        {
        case IDOK3:
			send3 = TRUE;
        case IDOK:
			if (!IsRX2) return TRUE; //Return if we are transmitting.. DM
			//This appears to be for sending a bsr.bin REQUEST file to another station DM

			/*
			//New code: (not working) DM
			TXpicpospt = 0;
			SendBSR(1, 1); //send the BSR request - added DM - doesn't work with old queueing system
			SetTXmode(TRUE);

			EnableMenuItem(GetMenu(messhwnd), ID_SETTINGS_FILETRANSFER_SENDFILE, MF_GRAYED);
			EnableMenuItem(GetMenu(messhwnd), ID_SETTINGS_DRMSETTINGS, MF_GRAYED);
			SendMessage(GetDlgItem(messhwnd, IDB_START), WM_SETTEXT, 0, (LPARAM)"RX");
			SendMessage(GetDlgItem(messhwnd, IDB_STARTPIC), WM_SETTEXT, 0, (LPARAM)"RX");
			*/

			char filenam[300];
			char filenam2[300];

			DRMTransmitter.GetAudSrcEnc()->ClearPicFileNames();
//			dummy.Init(0);
			wsprintf(filenam, "%s%s", bsrpath, "bsr.bin");
			wsprintf(filenam2, "%s%s", bsrpath, "cbsr.bin");
//			if (fast != 0) //Make this a dialog checkbox option DM
//			{
				// compress the bsr.bin file
				CopyFile(filenam, filenam2, FALSE);
				compressBSR(filenam2, filenam); //renamed to prevent conflict with zlib DM
				remove(filenam2); //delete temp file DM

//			}
//			tmpno = GetDlgItemInt( hwnd, IDC_BSRINST, NULL, FALSE );
//			wsprintf(tmpbsrname,"bsr%d.bin",tmpno);
//			CopyFile(tmpbsrname,"bsr.bin",FALSE);
 
			strcpy(pictfile[0],"bsr.bin");	strcpy(filetitle[0],"bsr.bin");
			strcpy(pictfile[1],"bsr.bin");	strcpy(filetitle[1],"bsr.bin");
			TXpicpospt = 2;
			if (send3)
			{
				strcpy(pictfile[2],"bsr.bin");	strcpy(filetitle[2],"bsr.bin");
				strcpy(pictfile[3],"bsr.bin");	strcpy(filetitle[3],"bsr.bin");
				TXpicpospt = 4;
			}
			SetTXmode(TRUE);
			if (RX_Running) DRMReceiver.NotRec();
			IsRX2 = FALSE;
			PTTon(); //added DM
			EnableMenuItem(GetMenu(mainwindow), ID_SETTINGS_FILETRANSFER_SENDFILE, MF_GRAYED);
			EnableMenuItem(GetMenu(mainwindow), ID_SETTINGS_DRMSETTINGS, MF_GRAYED);
			if (TX_Running)
			{
				char dcbuf[20];
				DRMTransmitter.Init();
				DRMTransmitter.Send();
				sprintf(dcbuf,"%d",(int)DRMTransmitter.GetCarOffset());
				SetDlgItemText(mainwindow, IDC_DCFREQ, dcbuf);
			}
			SendMessage (GetDlgItem (mainwindow, IDB_START), WM_SETTEXT, 0, (LPARAM)"RX");
			SendMessage (GetDlgItem (mainwindow, IDB_STARTPIC), WM_SETTEXT, 0, (LPARAM)"RX");
			TXpicpospt = 0;
			
			return TRUE;

		case IDCANCEL:
			tmpno = GetDlgItemInt( hwnd, IDC_BSRINST, NULL, FALSE );
//			wsprintf(tmpbsrname,"bsr%d.bin",tmpno); //removed DM
			wsprintf(tmpbsrname, "%s%s", bsrpath, "bsr.bin"); //added DM
			remove(tmpbsrname); //delete bsr.bin file when cancelled DM
			txbsr_onscreen_arr[tmpno] = FALSE;
            EndDialog (hwnd, 0);
            return TRUE;
        }
        break;
    case WM_CLOSE:
		tmpno = GetDlgItemInt( hwnd, IDC_BSRINST, NULL, FALSE );
//		wsprintf(tmpbsrname,"bsr%d.bin",tmpno); //removed DM
		wsprintf(tmpbsrname, "%s%s", bsrpath, "bsr.bin"); //added DM
		remove(tmpbsrname);
		txbsr_onscreen_arr[tmpno] = FALSE;
 		EndDialog (hwnd, 0);
		return TRUE;
     }
    return FALSE;
}


BOOL CALLBACK AnswerBSRDlgProc
   (HWND hwnd, UINT message, UINT wParam, LPARAM lParam)
{
	char tmpbsrname[20]{};
	int tmpno = 0;
	BOOL send3 = FALSE;
    switch (message)
    {
    case WM_NOTIFY:
    case WM_INITDIALOG:
		SetDlgItemInt( hwnd, ID_ABSRINST, bsrposind, FALSE );
		SetDlgItemInt( hwnd, IDC_SENDNOBSR, segnobsrfile(), FALSE );
		SetDlgItemText( hwnd, IDC_SENDNAMEBSR, filetosend.c_str());
		SetDlgItemText( hwnd, IDC_SENDCALLBSR, consrxcall.c_str());
//		wsprintf(tmpbsrname,"bsrreq%d.bin",bsrposind);
//		CopyFile("bsrreq.bin",tmpbsrname,FALSE); //is this necessary? DM
        return TRUE;
    case WM_COMMAND:
        switch (LOWORD (wParam))
        {
        case IDOK3:
			send3 = TRUE;
        case IDOK:
			if (!IsRX2) return TRUE;
			tmpno = GetDlgItemInt( hwnd, ID_ABSRINST, NULL, FALSE );
//			wsprintf(tmpbsrname,"bsrreq%d.bin",tmpno); //removed DM
//			CopyFile(tmpbsrname,"bsrreq.bin",FALSE); //removed DM
			readbsrfile(bsrpath); //edited DM - readbsrfile needs a path now (but not a filename)
			if (send3)
				writeselsegments(3);
			else
				writeselsegments(1);
			if (RX_Running) DRMReceiver.NotRec();
			IsRX2 = FALSE;
			PTTon(); //added DM
			EnableMenuItem(GetMenu(mainwindow), ID_SETTINGS_FILETRANSFER_SENDFILE, MF_GRAYED);
			EnableMenuItem(GetMenu(mainwindow), ID_SETTINGS_DRMSETTINGS, MF_GRAYED);
			if (TX_Running)
			{
				char dcbuf[20];
				DRMTransmitter.Init();
				DRMTransmitter.Send();
				sprintf(dcbuf,"%d",(int)DRMTransmitter.GetCarOffset());
				SetDlgItemText(mainwindow, IDC_DCFREQ, dcbuf);
			}
			SendMessage (GetDlgItem (mainwindow, IDB_START), WM_SETTEXT, 0, (LPARAM)"RX");
			SendMessage (GetDlgItem (mainwindow, IDB_STARTPIC), WM_SETTEXT, 0, (LPARAM)"RX");
            return TRUE;
		case IDCANCEL:
			tmpno = GetDlgItemInt( hwnd, ID_ABSRINST, NULL, FALSE );
//			wsprintf(tmpbsrname,"bsrreq%d.bin",tmpno); //removed DM = This is to handle multiple BSR windows and the DLL version doesn't seem to support it DM
			wsprintf(tmpbsrname, "%s%s", bsrpath, "bsr.bin"); //added DM
			remove(tmpbsrname);
			bsr_onscreen_arr[tmpno] = FALSE;
            EndDialog (hwnd, 0);
            return TRUE;
        }
        break;
    case WM_CLOSE:
		tmpno = GetDlgItemInt( hwnd, ID_ABSRINST, NULL, FALSE );
//		wsprintf(tmpbsrname,"bsrreq%d.bin",tmpno); //removed DM = This is to handle multiple BSR windows and the DLL version doesn't seem to support it DM
		wsprintf(tmpbsrname, "%s%s", bsrpath, "bsr.bin"); //added DM
		remove(tmpbsrname);
		bsr_onscreen_arr[tmpno] = FALSE;
 		EndDialog (hwnd, 0);
		return TRUE;
     }
    return FALSE;
}

BOOL CALLBACK CallSignDlgProc
   (HWND hwnd, UINT message, UINT wParam, LPARAM lParam)
{
	char callbuf[20]{ 0 };
    switch (message)
    {
    case WM_INITDIALOG:
		SetDlgItemText( hwnd, IDC_EDIT1, getcall() );
        return TRUE;
    case WM_COMMAND:
        switch (LOWORD (wParam))
        {
        case IDOK:
			GetDlgItemText( hwnd, IDC_EDIT1, callbuf, 20);
			setcall(callbuf);
			savevar();
			if (runmode != 'R') TransmParam->Service[0].strLabel = getcall();
            EndDialog (hwnd, 0);
            return TRUE;
        case IDCANCEL:
            EndDialog (hwnd, 0);
            return TRUE;
        }
        break;
    }
    return FALSE;
}


BOOL CALLBACK DRMSettingsDlgProc
   (HWND hwnd, UINT message, UINT wParam, LPARAM lParam)
{
	char dcbuf[40]{ 0 }; //was 20 DM
	float TxDCFreq = 0.0;
    switch (message)
    {
    case WM_INITDIALOG:
		if (DRMTransmitter.GetParameters()->GetSpectrumOccup() == SO_1)
			SendMessage (GetDlgItem (hwnd, ID_SETTINGS_BW_25), BM_SETCHECK, (WPARAM)1, 0);
		else
			SendMessage (GetDlgItem (hwnd, ID_SETTINGS_BW_23), BM_SETCHECK, (WPARAM)1, 0);
		if (DRMTransmitter.GetParameters()->eSymbolInterlMode == CParameter::SI_SHORT)
			SendMessage (GetDlgItem (hwnd, ID_INTERLEAVE_SHORT400MS), BM_SETCHECK, (WPARAM)1, 0);
		else
			SendMessage (GetDlgItem (hwnd, ID_INTERLEAVE_LONG2S), BM_SETCHECK, (WPARAM)1, 0);
		
		if (DRMTransmitter.GetParameters()->GetWaveMode() == RM_ROBUSTNESS_MODE_A) 
			SendMessage(GetDlgItem(hwnd, ID_MODE_ROBMODEA), BM_SETCHECK, (WPARAM)1, 0);
		else if (DRMTransmitter.GetParameters()->GetWaveMode() == RM_ROBUSTNESS_MODE_B)
			SendMessage (GetDlgItem (hwnd, ID_MODE_ROBMODEB), BM_SETCHECK, (WPARAM)1, 0);
		else //if (DRMTransmitter.GetParameters()->GetWaveMode() == RM_ROBUSTNESS_MODE_E) //else must be mode E! DM
			SendMessage(GetDlgItem(hwnd, ID_MODE_ROBMODEE), BM_SETCHECK, (WPARAM)1, 0);
		
		if (DRMTransmitter.GetParameters()->eMSCCodingScheme == CParameter::CS_1_SM)
			SendMessage (GetDlgItem (hwnd, ID_MSCCODING_4QAM ), BM_SETCHECK, (WPARAM)1, 0);
		else if (DRMTransmitter.GetParameters()->eMSCCodingScheme == CParameter::CS_2_SM)
			SendMessage (GetDlgItem (hwnd, ID_MSCCODING_16QAM), BM_SETCHECK, (WPARAM)1, 0);
		else
			SendMessage (GetDlgItem (hwnd, ID_MSCCODING_64QAM), BM_SETCHECK, (WPARAM)1, 0);
		if (DRMTransmitter.GetParameters()->MSCPrLe.iPartB == 0)
			SendMessage (GetDlgItem (hwnd, ID_SETTINGS_MSCPROTECTION_NORMAL), BM_SETCHECK, (WPARAM)1, 0);
		else
			SendMessage (GetDlgItem (hwnd, ID_SETTINGS_MSCPROTECTION_LOW), BM_SETCHECK, (WPARAM)1, 0);
		if (TxLevel == FALSE) SendMessage(GetDlgItem(hwnd, ID_TXLEVEL), BM_SETCHECK, (WPARAM)1, 0); //if TxLevel == FALSE, then use -12dB output.
		else SendMessage(GetDlgItem(hwnd, ID_TXLEVEL), BM_SETCHECK, (WPARAM)0, 0); //if TxLevel == TRUE, use -3dB. DM 2022

		TxDCFreq = DRMTransmitter.GetCarOffset();
		sprintf(dcbuf,"%.1f",TxDCFreq);
		SetDlgItemText( hwnd, IDC_DCOFFS, dcbuf);
        return TRUE;

    case WM_COMMAND:
        switch (LOWORD (wParam))
        {
        case IDCANCEL:
            EndDialog (hwnd, 0);
            return TRUE;

        case IDOK:
 			GetDlgItemText( hwnd, IDC_DCOFFS, dcbuf, 40); //was 20 DM == Changed for better alignment DM
			if (sscanf(dcbuf,"%f",&TxDCFreq) == 1)
				DRMTransmitter.SetCarOffset(TxDCFreq);
			DRMTransmitter.Init();
			DRMTransmitter.GetParameters()->Service[0].DataParam.iPacketLen = 
				calcpacklen(DRMTransmitter.GetParameters()->iNumDecodedBitsMSC); 
			setmodeqam(DRMTransmitter.GetParameters()->GetWaveMode(),
				       DRMTransmitter.GetParameters()->eMSCCodingScheme,
					   DRMTransmitter.GetParameters()->eSymbolInterlMode,
					   DRMTransmitter.GetParameters()->GetSpectrumOccup());
            EndDialog (hwnd, 0);
            return TRUE;

		case ID_INTERLEAVE_SHORT400MS:
			DRMTransmitter.GetParameters()->eSymbolInterlMode = CParameter::SI_SHORT;
			SendMessage (GetDlgItem (hwnd, ID_INTERLEAVE_SHORT400MS), BM_SETCHECK, (WPARAM)1, 0);
			SendMessage (GetDlgItem (hwnd, ID_INTERLEAVE_LONG2S), BM_SETCHECK, (WPARAM)0, 0);
			break;
		case ID_INTERLEAVE_LONG2S:
			DRMTransmitter.GetParameters()->eSymbolInterlMode = CParameter::SI_LONG;
			SendMessage (GetDlgItem (hwnd, ID_INTERLEAVE_SHORT400MS), BM_SETCHECK, (WPARAM)0, 0);
			SendMessage (GetDlgItem (hwnd, ID_INTERLEAVE_LONG2S), BM_SETCHECK, (WPARAM)1, 0);
			break;
		case ID_SETTINGS_MSCPROTECTION_NORMAL:
			DRMTransmitter.GetParameters()->SetMSCProtLev(0);
			SendMessage (GetDlgItem (hwnd, ID_SETTINGS_MSCPROTECTION_NORMAL), BM_SETCHECK, (WPARAM)1, 0);
			SendMessage (GetDlgItem (hwnd, ID_SETTINGS_MSCPROTECTION_LOW), BM_SETCHECK, (WPARAM)0, 0);
			break;
		case ID_SETTINGS_MSCPROTECTION_LOW:
			DRMTransmitter.GetParameters()->SetMSCProtLev(1);
			SendMessage (GetDlgItem (hwnd, ID_SETTINGS_MSCPROTECTION_NORMAL), BM_SETCHECK, (WPARAM)0, 0);
			SendMessage (GetDlgItem (hwnd, ID_SETTINGS_MSCPROTECTION_LOW), BM_SETCHECK, (WPARAM)1, 0);
			break;
		case ID_MSCCODING_4QAM:
			DRMTransmitter.GetParameters()->eMSCCodingScheme = CParameter::CS_1_SM;
			SendMessage (GetDlgItem (hwnd, ID_MSCCODING_4QAM ), BM_SETCHECK, (WPARAM)1, 0);
			SendMessage (GetDlgItem (hwnd, ID_MSCCODING_16QAM), BM_SETCHECK, (WPARAM)0, 0);
			SendMessage (GetDlgItem (hwnd, ID_MSCCODING_64QAM), BM_SETCHECK, (WPARAM)0, 0);
			break;
		case ID_MSCCODING_16QAM:
			DRMTransmitter.GetParameters()->eMSCCodingScheme = CParameter::CS_2_SM;
			SendMessage (GetDlgItem (hwnd, ID_MSCCODING_4QAM ), BM_SETCHECK, (WPARAM)0, 0);
			SendMessage (GetDlgItem (hwnd, ID_MSCCODING_16QAM), BM_SETCHECK, (WPARAM)1, 0);
			SendMessage (GetDlgItem (hwnd, ID_MSCCODING_64QAM), BM_SETCHECK, (WPARAM)0, 0);
			break;
		case ID_MSCCODING_64QAM:
			DRMTransmitter.GetParameters()->eMSCCodingScheme = CParameter::CS_3_SM;
			SendMessage (GetDlgItem (hwnd, ID_MSCCODING_4QAM ), BM_SETCHECK, (WPARAM)0, 0);
			SendMessage (GetDlgItem (hwnd, ID_MSCCODING_16QAM), BM_SETCHECK, (WPARAM)0, 0);
			SendMessage (GetDlgItem (hwnd, ID_MSCCODING_64QAM), BM_SETCHECK, (WPARAM)1, 0);
			break;
		case ID_MODE_ROBMODEA:
			DRMTransmitter.GetParameters()->InitCellMapTable(RM_ROBUSTNESS_MODE_A,
				DRMTransmitter.GetParameters()->GetSpectrumOccup());
			SendMessage (GetDlgItem (hwnd, ID_MODE_ROBMODEA), BM_SETCHECK, (WPARAM)1, 0);
			SendMessage (GetDlgItem (hwnd, ID_MODE_ROBMODEB), BM_SETCHECK, (WPARAM)0, 0);
			SendMessage (GetDlgItem (hwnd, ID_MODE_ROBMODEE), BM_SETCHECK, (WPARAM)0, 0);
			break;
		case ID_MODE_ROBMODEB:
			DRMTransmitter.GetParameters()->InitCellMapTable(RM_ROBUSTNESS_MODE_B,
				DRMTransmitter.GetParameters()->GetSpectrumOccup());
			SendMessage (GetDlgItem (hwnd, ID_MODE_ROBMODEA), BM_SETCHECK, (WPARAM)0, 0);
			SendMessage (GetDlgItem (hwnd, ID_MODE_ROBMODEB), BM_SETCHECK, (WPARAM)1, 0);
			SendMessage (GetDlgItem (hwnd, ID_MODE_ROBMODEE), BM_SETCHECK, (WPARAM)0, 0);
			break;
		case ID_MODE_ROBMODEE:
			DRMTransmitter.GetParameters()->InitCellMapTable(RM_ROBUSTNESS_MODE_E,
				DRMTransmitter.GetParameters()->GetSpectrumOccup());
			SendMessage(GetDlgItem(hwnd, ID_MODE_ROBMODEA), BM_SETCHECK, (WPARAM)0, 0);
			SendMessage(GetDlgItem(hwnd, ID_MODE_ROBMODEB), BM_SETCHECK, (WPARAM)0, 0);
			SendMessage(GetDlgItem(hwnd, ID_MODE_ROBMODEE), BM_SETCHECK, (WPARAM)1, 0);
			break;
		case ID_SETTINGS_BW_25:
			SendMessage (GetDlgItem (hwnd, ID_SETTINGS_BW_25), BM_SETCHECK, (WPARAM)1, 0);
			SendMessage (GetDlgItem (hwnd, ID_SETTINGS_BW_23), BM_SETCHECK, (WPARAM)0, 0);
			DRMTransmitter.GetParameters()->SetSpectrumOccup(SO_1);
			break;
		case ID_SETTINGS_BW_23:
			SendMessage (GetDlgItem (hwnd, ID_SETTINGS_BW_23), BM_SETCHECK, (WPARAM)1, 0);
			SendMessage (GetDlgItem (hwnd, ID_SETTINGS_BW_25), BM_SETCHECK, (WPARAM)0, 0);
			DRMTransmitter.GetParameters()->SetSpectrumOccup(SO_0);
			break;
		case ID_SET_A_MAXSPEED:
			DRMTransmitter.GetParameters()->eSymbolInterlMode = CParameter::SI_LONG;
			SendMessage (GetDlgItem (hwnd, ID_INTERLEAVE_SHORT400MS), BM_SETCHECK, (WPARAM)0, 0);
			SendMessage (GetDlgItem (hwnd, ID_INTERLEAVE_LONG2S), BM_SETCHECK, (WPARAM)1, 0);
			DRMTransmitter.GetParameters()->SetMSCProtLev(1);
			SendMessage (GetDlgItem (hwnd, ID_SETTINGS_MSCPROTECTION_NORMAL), BM_SETCHECK, (WPARAM)0, 0);
			SendMessage (GetDlgItem (hwnd, ID_SETTINGS_MSCPROTECTION_LOW), BM_SETCHECK, (WPARAM)1, 0);
			DRMTransmitter.GetParameters()->eMSCCodingScheme = CParameter::CS_2_SM;
			SendMessage (GetDlgItem (hwnd, ID_MSCCODING_64QAM), BM_SETCHECK, (WPARAM)0, 0);
			SendMessage (GetDlgItem (hwnd, ID_MSCCODING_16QAM), BM_SETCHECK, (WPARAM)1, 0);
			SendMessage (GetDlgItem (hwnd, ID_MSCCODING_4QAM ), BM_SETCHECK, (WPARAM)0, 0);
			DRMTransmitter.GetParameters()->InitCellMapTable(RM_ROBUSTNESS_MODE_A, DRMTransmitter.GetParameters()->GetSpectrumOccup());
			SendMessage (GetDlgItem (hwnd, ID_MODE_ROBMODEE), BM_SETCHECK, (WPARAM)0, 0); //added DM
			SendMessage (GetDlgItem (hwnd, ID_MODE_ROBMODEB), BM_SETCHECK, (WPARAM)0, 0);
			SendMessage (GetDlgItem (hwnd, ID_MODE_ROBMODEA), BM_SETCHECK, (WPARAM)1, 0);
			break;
		case ID_SET_B_DEFAULT:
			DRMTransmitter.GetParameters()->eSymbolInterlMode = CParameter::SI_SHORT;
			SendMessage(GetDlgItem(hwnd, ID_INTERLEAVE_SHORT400MS), BM_SETCHECK, (WPARAM)1, 0);
			SendMessage(GetDlgItem(hwnd, ID_INTERLEAVE_LONG2S), BM_SETCHECK, (WPARAM)0, 0);
			DRMTransmitter.GetParameters()->SetMSCProtLev(0);
			SendMessage(GetDlgItem(hwnd, ID_SETTINGS_MSCPROTECTION_NORMAL), BM_SETCHECK, (WPARAM)1, 0);
			SendMessage(GetDlgItem(hwnd, ID_SETTINGS_MSCPROTECTION_LOW), BM_SETCHECK, (WPARAM)0, 0);
			DRMTransmitter.GetParameters()->eMSCCodingScheme = CParameter::CS_2_SM;
			SendMessage(GetDlgItem(hwnd, ID_MSCCODING_64QAM), BM_SETCHECK, (WPARAM)0, 0);
			SendMessage(GetDlgItem(hwnd, ID_MSCCODING_16QAM), BM_SETCHECK, (WPARAM)1, 0);
			SendMessage(GetDlgItem(hwnd, ID_MSCCODING_4QAM), BM_SETCHECK, (WPARAM)0, 0);
			DRMTransmitter.GetParameters()->InitCellMapTable(RM_ROBUSTNESS_MODE_B,
				DRMTransmitter.GetParameters()->GetSpectrumOccup());
			SendMessage(GetDlgItem(hwnd, ID_MODE_ROBMODEA), BM_SETCHECK, (WPARAM)0, 0);
			SendMessage(GetDlgItem(hwnd, ID_MODE_ROBMODEB), BM_SETCHECK, (WPARAM)1, 0);
			SendMessage(GetDlgItem(hwnd, ID_MODE_ROBMODEE), BM_SETCHECK, (WPARAM)0, 0); //added DM
			break;
		case ID_SET_B_ROBUST:
			DRMTransmitter.GetParameters()->eSymbolInterlMode = CParameter::SI_SHORT;
			SendMessage(GetDlgItem(hwnd, ID_INTERLEAVE_SHORT400MS), BM_SETCHECK, (WPARAM)1, 0);
			SendMessage(GetDlgItem(hwnd, ID_INTERLEAVE_LONG2S), BM_SETCHECK, (WPARAM)0, 0);
			DRMTransmitter.GetParameters()->SetMSCProtLev(0);
			SendMessage(GetDlgItem(hwnd, ID_SETTINGS_MSCPROTECTION_NORMAL), BM_SETCHECK, (WPARAM)1, 0);
			SendMessage(GetDlgItem(hwnd, ID_SETTINGS_MSCPROTECTION_LOW), BM_SETCHECK, (WPARAM)0, 0);
			DRMTransmitter.GetParameters()->eMSCCodingScheme = CParameter::CS_1_SM;
			SendMessage(GetDlgItem(hwnd, ID_MSCCODING_64QAM), BM_SETCHECK, (WPARAM)0, 0);
			SendMessage(GetDlgItem(hwnd, ID_MSCCODING_16QAM), BM_SETCHECK, (WPARAM)0, 0);
			SendMessage(GetDlgItem(hwnd, ID_MSCCODING_4QAM), BM_SETCHECK, (WPARAM)1, 0);
			DRMTransmitter.GetParameters()->InitCellMapTable(RM_ROBUSTNESS_MODE_B, DRMTransmitter.GetParameters()->GetSpectrumOccup());
			SendMessage(GetDlgItem(hwnd, ID_MODE_ROBMODEB), BM_SETCHECK, (WPARAM)1, 0);
			SendMessage(GetDlgItem(hwnd, ID_MODE_ROBMODEA), BM_SETCHECK, (WPARAM)0, 0);
			SendMessage(GetDlgItem(hwnd, ID_MODE_ROBMODEE), BM_SETCHECK, (WPARAM)0, 0); //added DM
			break;
		case ID_TXLEVEL:
			if (TxLevel == TRUE) {
				TxLevel = FALSE; //toggle
				SendMessage(GetDlgItem(hwnd, ID_TXLEVEL), BM_SETCHECK, (WPARAM)1, 0); //-12dB setting is on when TxLevel is 0
			}
			else {
				TxLevel = TRUE; //toggle
				SendMessage(GetDlgItem(hwnd, ID_TXLEVEL), BM_SETCHECK, (WPARAM)0, 0); //-12dB setting is off when TxLevel is 1
			}
}
        break;
    }
    return FALSE;
}

int centerfreq = 350; //Default low frequency edge of signal (not centre frequency) DM
int windowsize = 5; //was 200, changed to 5 by DM Aug 2022 - This is the default decoder frequency search window size in Hz. Narrower works better in noise. =============================

BOOL CALLBACK DRMRXSettingsDlgProc
   (HWND hwnd, UINT message, UINT wParam, LPARAM lParam)
{
	BOOL sucess = FALSE;
    switch (message)
    {
    case WM_INITDIALOG:
//		SetDlgItemInt(hwnd, IDC_FREQACC, sensivity, FALSE); //this should be removed DM
		SetDlgItemInt(hwnd, IDC_SEARCHWINLOWER, centerfreq, FALSE);
		SetDlgItemInt(hwnd, IDC_SEARCHWINUPPER, windowsize, FALSE);
		if (rxaudfilt)
			SendMessage(GetDlgItem(hwnd, ID_SETTINGS_AUDIOFILTER_RXFILTER), BM_SETCHECK, (WPARAM)1, 0);
		else
			SendMessage(GetDlgItem(hwnd, ID_SETTINGS_AUDIOFILTER_RXFILTER), BM_SETCHECK, (WPARAM)0, 0);
		setRXfilt((rxaudfilt==TRUE));
		if (fastreset)
			SendMessage(GetDlgItem(hwnd, IDC_SETTINGS_FASTRESET), BM_SETCHECK, (WPARAM)1, 0);
		else
			SendMessage(GetDlgItem(hwnd, IDC_SETTINGS_FASTRESET), BM_SETCHECK, (WPARAM)0, 0);
		DRMReceiver.SetFastReset(fastreset==TRUE);
		break;
    case WM_COMMAND:
        switch (LOWORD (wParam))
        {
        case IDOK:
//			sensivity = GetDlgItemInt(hwnd, IDC_FREQACC, &sucess, FALSE); // -------------- needs disabling for DLL files update -----------------
//			if (sucess) DRMReceiver.SetFreqAcqSensivity(2.0 - (_REAL)sensivity/100.0); //edited DM  -------------- needs disabling for DLL files update -----------------
			centerfreq = GetDlgItemInt(hwnd, IDC_SEARCHWINLOWER, &sucess, FALSE);
			windowsize = GetDlgItemInt(hwnd, IDC_SEARCHWINUPPER, &sucess, FALSE);
			if (sucess)
			{
				windowsize = GetDlgItemInt(hwnd, IDC_SEARCHWINUPPER, &sucess, FALSE);
				if (sucess) DRMReceiver.SetFreqAcqWinSize((_REAL)centerfreq,(_REAL)windowsize);
			}
            EndDialog (hwnd, 0);
            return TRUE;
        case IDAPP:
//			sensivity = GetDlgItemInt(hwnd, IDC_FREQACC, &sucess, FALSE); // -------------- needs disabling for DLL files update -----------------
//			if (sucess) DRMReceiver.SetFreqAcqSensivity(2.0 - (_REAL)sensivity/100.0); //edited DM  -------------- needs disabling for DLL files update -----------------
			centerfreq = GetDlgItemInt(hwnd, IDC_SEARCHWINLOWER, &sucess, FALSE);
			if (sucess)
			{
				windowsize = GetDlgItemInt(hwnd, IDC_SEARCHWINUPPER, &sucess, FALSE);
				if (sucess) DRMReceiver.SetFreqAcqWinSize((_REAL)centerfreq,(_REAL)windowsize);
			}
            return TRUE;
		case ID_SETTINGS_AUDIOFILTER_RXFILTER:
			if (rxaudfilt)
			{
				rxaudfilt = FALSE;
				SendMessage(GetDlgItem(hwnd, ID_SETTINGS_AUDIOFILTER_RXFILTER), BM_SETCHECK, (WPARAM)0, 0);
				setRXfilt(false);
			}
			else
			{
				rxaudfilt = TRUE;
				SendMessage(GetDlgItem(hwnd, ID_SETTINGS_AUDIOFILTER_RXFILTER), BM_SETCHECK, (WPARAM)1, 0);
				setRXfilt(true);
			}
			break;
		case IDC_SETTINGS_FASTRESET:
			if (fastreset)
				SendMessage(GetDlgItem(hwnd, IDC_SETTINGS_FASTRESET), BM_SETCHECK, (WPARAM)0, 0);
			else
				SendMessage(GetDlgItem(hwnd, IDC_SETTINGS_FASTRESET), BM_SETCHECK, (WPARAM)1, 0);
			fastreset = !fastreset;
			DRMReceiver.SetFastReset(fastreset==TRUE);
			break;
		}
	break;
	}
    return FALSE;
}

BOOL CALLBACK TextMessageDlgProc (HWND hwnd, UINT message, UINT wParam, LPARAM lParam)
{
	FILE * txtset = nullptr;
	char textbuf[200]{0};
	int junk = 0; //added DM
    switch (message)
    {
    case WM_INITDIALOG:
		txtset = fopen("textmessage.txt","rt");
		if (txtset != NULL)
		{
			junk = fscanf(txtset,"%[^\0]",&textbuf); //edited DM
			fclose(txtset);
		}
		else 
		strcpy(textbuf," \x0d\x0a This is a Test ");
		SetDlgItemText( hwnd, IDC_TEXTMESSAGE, textbuf);		
		if (UseTextMessage == TRUE)
			SendMessage(GetDlgItem(hwnd, IDC_USETEXTMESSAGE), BM_SETCHECK, (WPARAM)1, 0);
		else
			SendMessage(GetDlgItem(hwnd, IDC_USETEXTMESSAGE), BM_SETCHECK, (WPARAM)0, 0);
        return TRUE;
    case WM_COMMAND:
        switch (LOWORD (wParam))
        {
        case IDOK:
			GetDlgItemText( hwnd, IDC_TEXTMESSAGE, textbuf, 200);
			txtset = fopen("textmessage.txt","wt");
			if (txtset != NULL)
			{
				fprintf(txtset,"%s",&textbuf);
				fclose(txtset);
			}
			
			DRMTransmitter.GetAudSrcEnc()->ClearTextMessage();
			if (UseTextMessage)
			{
				DRMTransmitter.GetAudSrcEnc()->SetTextMessage(textbuf);
				DRMTransmitter.GetParameters()->Service[0].AudioParam.bTextflag = TRUE;
			}
			else
				DRMTransmitter.GetParameters()->Service[0].AudioParam.bTextflag = FALSE;
            EndDialog (hwnd, 0);
            return TRUE;
        case IDCANCEL:
            EndDialog (hwnd, 0);
            return TRUE;
		case IDC_USETEXTMESSAGE:
			if (UseTextMessage == TRUE)
			{
				UseTextMessage = FALSE;
				settext(FALSE);
				SendMessage(GetDlgItem(hwnd, IDC_USETEXTMESSAGE), BM_SETCHECK, (WPARAM)0, 0);
				DRMTransmitter.GetAudSrcEnc()->ClearTextMessage();
			}
			else
			{
				UseTextMessage = TRUE;
				settext(TRUE);
				SendMessage(GetDlgItem(hwnd, IDC_USETEXTMESSAGE), BM_SETCHECK, (WPARAM)1, 0);
			}
			return TRUE;
        }
        break;
    case WM_CLOSE:
 		EndDialog (hwnd, 0);
		return TRUE;
    }
    return FALSE;
}

BOOL CALLBACK RXTextMessageDlgProc
   (HWND hwnd, UINT message, UINT wParam, LPARAM lParam)
{
	char textbuf[200]{0};
    switch (message)
    {
    case WM_INITDIALOG:
		RXTextMessageON = TRUE;
		strcpy(textbuf,"Waiting");
		SetDlgItemText( hwnd, IDC_RXTEXTMESSAGE, textbuf);
		return TRUE;
    case WM_NOTIFY:
		strcpy(textbuf,DRMReceiver.GetParameters()->Service[0].AudioParam.strTextMessage.c_str());
		if (strlen(textbuf) >= 1)
			SetDlgItemText( hwnd, IDC_RXTEXTMESSAGE, textbuf);
		return TRUE;
    case WM_COMMAND:
		RXTextMessageWasClosed = TRUE;
		RXTextMessageON = FALSE;
		EndDialog (hwnd, 0);
		return TRUE;
    case WM_CLOSE:
		RXTextMessageON = FALSE;
 		EndDialog (hwnd, 0);
		return TRUE;
    }
    return FALSE;
}

BOOL CALLBACK TXPictureDlgProc
   (HWND hwnd, UINT message, UINT wParam, LPARAM lParam)
{
	BOOL res;
    switch (message)
    {
    case WM_INITDIALOG:
		putfiles(hwnd);
		SendMessage(GetDlgItem(hwnd, IDC_SENDONCE), BM_SETCHECK, (WPARAM)(ECCmode == 1), 0);
		SendMessage(GetDlgItem(hwnd, IDC_SENDTWICE), BM_SETCHECK, (WPARAM)(ECCmode == 2), 0);
		SendMessage(GetDlgItem(hwnd, IDC_SENDTHREE), BM_SETCHECK, (WPARAM)(ECCmode == 3), 0);
		SendMessage(GetDlgItem(hwnd, IDC_RS1), BM_SETCHECK, (WPARAM)(ECCmode == 4), 0);
		SendMessage(GetDlgItem(hwnd, IDC_RS2), BM_SETCHECK, (WPARAM)(ECCmode == 5), 0);
		SendMessage(GetDlgItem(hwnd, IDC_RS3), BM_SETCHECK, (WPARAM)(ECCmode == 6), 0);
		SendMessage(GetDlgItem(hwnd, IDC_RS4), BM_SETCHECK, (WPARAM)(ECCmode == 7), 0);
		SendMessage(GetDlgItem(hwnd, IDC_LEADINLONG), BM_SETCHECK, (WPARAM)longleadin, 0);
		SendMessage(GetDlgItem(hwnd, IDC_ADDALLFILES), BM_SETCHECK, (WPARAM)autoaddfiles, 0);
		return TRUE;
		break;
    case WM_COMMAND:
        switch (LOWORD (wParam))
        {
        case IDOK:
			if (TXpicpospt == 0)
			{
				EndDialog (hwnd, 0);
				return TRUE;
			}
			SetTXmode(TRUE);
			if (RX_Running) DRMReceiver.NotRec();
			IsRX2 = FALSE;
			PTTon(); //added DM
			EnableMenuItem(GetMenu(mainwindow), ID_SETTINGS_FILETRANSFER_SENDFILE, MF_GRAYED);
			EnableMenuItem(GetMenu(mainwindow), ID_SETTINGS_DRMSETTINGS, MF_GRAYED);
			if (TX_Running)
			{
				char dcbuf[20];
				DRMTransmitter.Init();
				//if (callisok()) DRMTransmitter.Send(); //edited DM
				DRMTransmitter.Send();
				sprintf(dcbuf,"%d",(int)DRMTransmitter.GetCarOffset());
				SetDlgItemText(mainwindow, IDC_DCFREQ, dcbuf);
			}
			SendMessage(GetDlgItem(mainwindow, IDB_START), WM_SETTEXT, 0, (LPARAM)"RX");
			SendMessage(GetDlgItem(mainwindow, IDB_STARTPIC), WM_SETTEXT, 0, (LPARAM)"RX");
			EndDialog (hwnd, 0);
			return TRUE;
		case ID_CHOOSEFILE:
			if (TXpicpospt > 31) //edited DM
			{
				MessageBox(hwnd,"Too many Files in Buffer","Buffer Full",MB_OK);
				return TRUE; 
			}
			res = GetFileName(hwnd,pictfile[TXpicpospt],filetitle[TXpicpospt], sizeof(pictfile[TXpicpospt]),&file_size[TXpicpospt]);
			if (res == 1) TXpicpospt++; //increment file counter/pointer
			putfiles(hwnd); //add filename to window

			//if autoaddfiles = TRUE then scan for more matching filenames for SWRG - DM 
			if (autoaddfiles == 1) {
				BOOL hResult = FALSE;
				HANDLE hFind = 0;
				WIN32_FIND_DATA   FindFileData{ 0 };

				int maxmatch = 20; //match 20 files maximum
				//make a mask from first pictfile[TXpicpospt]
				char temp[260]{ 0 }; //make an array to hold the filename path string
				char temp2[260]{ 0 }; //make an array to copy it to
				strcpy(temp, pictfile[TXpicpospt - 1]); //copy the filename string into temp
				int templen = 0; //an int to hold the length
				int templen2 = 0; //an int to hold the length 
				templen = strlen(temp); //find the length of the string
				templen2 = templen; //copy for testing
				//Match a maximum of 20 files - if any fails, stop
				while (maxmatch > 0) {
					//only check back 8 places or so (this could be extended to allow for descriptive names to be added)
					while ((templen > (templen - (min(templen, 8)))) && (maxmatch > 0)) {
						//add code here to replace extension with a * so it matches any file type DM - Working 13/03/2021 1:16PM
						//check for "."
						if (temp[templen] == 46) {
						//as soon as a dot is found, replace ext with *0
							templen++; //go to next char
							//make sure we have room (must be at least 1 char after .)
							if ((templen2-templen) >= 1) {
								temp[templen] = 42; //add a *
								templen++; //go to next char
								if (templen <= templen2) {
									temp[templen] = 0; //add a 0 to terminate string if needed
								}
								templen--;
							}
							templen--;
						}

						//quit as soon as a hyphen is found
						if (temp[templen] == 45) {
							maxmatch = 0;
							break;
						}
						//Arithmetic is done in ASCII values
							//look for numbers 0-9
						if ((temp[templen] >= 48) && (temp[templen] <= 57)) {
							//found a number! increment it if it's 0-9
							if ((temp[templen] >= 48) && (temp[templen] <= 57)) {
								temp[templen] = temp[templen] + 1;
								//if it's become a colon it overflowed - set it to zero and inc the next digit instead
								if (temp[templen] == 58) {
									temp[templen] = 48; //set it to zero and carry it next
									templen--; //point to tens digit
									temp[templen] = temp[templen] + 1; //increment tens to carry
									templen++; //point to units digit again - DM fixed Oct 19, 2022
								}
								//a number was found - add the file to the list
								//next filename to find is in temp
								hFind = FindFirstFileA(temp, &FindFileData); //get the file handle
								if (hFind != INVALID_HANDLE_VALUE) {
									//a file was found
									file_size[TXpicpospt] = FindFileData.nFileSizeLow; //add file size info

									//take the short name and use it to overwrite the name on the full path
									strcpy(temp2, temp); //copy the full path to a new array
									fixname(temp2, FindFileData.cFileName); //overwrite the wildcard filename with the name that was actually found
									strcpy(filetitle[TXpicpospt], FindFileData.cFileName); //filetitle holds the short filename in a 2D array
									strcpy(pictfile[TXpicpospt], temp2); //pictfile holds the full path and filename in a 2D array
									hResult = FindClose(hFind); //close file handle
									TXpicpospt++; //increment array pointer
								}
								else {
									//if file not found, exit this routine
									maxmatch = 1; //DM fixed Oct 19, 2022
									break; //DM fixed Oct 19, 2022
								}
							}
						}
						//no number found - try the next character
						else templen--;
					}

					maxmatch--;
					//Then load the next matching filename, and continue until a hyphen is found
				}
				putfiles(hwnd); //add filenames to window
			}  //end Autoadd files

				return TRUE;
		case ID_DELALLFILES:
			TXpicpospt = 0;
			putfiles(hwnd);
			return TRUE;
		case ID_CANCELPICTX:
			EndDialog (hwnd, 0);
			return TRUE;
		case IDC_ADDALLFILES:	//added DM
			if (autoaddfiles == TRUE)
			{
				SendMessage(GetDlgItem(hwnd, IDC_ADDALLFILES), BM_SETCHECK, (WPARAM)0, 0);
				autoaddfiles = FALSE;
			}
			else
			{
				SendMessage(GetDlgItem(hwnd, IDC_ADDALLFILES), BM_SETCHECK, (WPARAM)1, 0);
				autoaddfiles = TRUE;
			}
			return TRUE;
		case IDC_SENDONCE:
			ECCmode = 1;
			SendMessage(GetDlgItem(hwnd, IDC_SENDONCE), BM_SETCHECK, (WPARAM)1, 0);
			SendMessage(GetDlgItem(hwnd, IDC_SENDTWICE), BM_SETCHECK, (WPARAM)0, 0);
			SendMessage(GetDlgItem(hwnd, IDC_SENDTHREE), BM_SETCHECK, (WPARAM)0, 0);
			SendMessage(GetDlgItem(hwnd, IDC_RS1), BM_SETCHECK, (WPARAM)0, 0);
			SendMessage(GetDlgItem(hwnd, IDC_RS2), BM_SETCHECK, (WPARAM)0, 0);
			SendMessage(GetDlgItem(hwnd, IDC_RS3), BM_SETCHECK, (WPARAM)0, 0);
			SendMessage(GetDlgItem(hwnd, IDC_RS4), BM_SETCHECK, (WPARAM)0, 0);
			return TRUE;
		case IDC_SENDTWICE:
			ECCmode = 2;
			SendMessage(GetDlgItem(hwnd, IDC_SENDONCE), BM_SETCHECK, (WPARAM)0, 0);
			SendMessage(GetDlgItem(hwnd, IDC_SENDTWICE), BM_SETCHECK, (WPARAM)1, 0);
			SendMessage(GetDlgItem(hwnd, IDC_SENDTHREE), BM_SETCHECK, (WPARAM)0, 0);
			SendMessage(GetDlgItem(hwnd, IDC_RS1), BM_SETCHECK, (WPARAM)0, 0);
			SendMessage(GetDlgItem(hwnd, IDC_RS2), BM_SETCHECK, (WPARAM)0, 0);
			SendMessage(GetDlgItem(hwnd, IDC_RS3), BM_SETCHECK, (WPARAM)0, 0);
			SendMessage(GetDlgItem(hwnd, IDC_RS4), BM_SETCHECK, (WPARAM)0, 0);
			return TRUE;
		case IDC_SENDTHREE:
			ECCmode = 3;
			SendMessage(GetDlgItem(hwnd, IDC_SENDONCE), BM_SETCHECK, (WPARAM)0, 0);
			SendMessage(GetDlgItem(hwnd, IDC_SENDTWICE), BM_SETCHECK, (WPARAM)0, 0);
			SendMessage(GetDlgItem(hwnd, IDC_SENDTHREE), BM_SETCHECK, (WPARAM)1, 0);
			SendMessage(GetDlgItem(hwnd, IDC_RS1), BM_SETCHECK, (WPARAM)0, 0);
			SendMessage(GetDlgItem(hwnd, IDC_RS2), BM_SETCHECK, (WPARAM)0, 0);
			SendMessage(GetDlgItem(hwnd, IDC_RS3), BM_SETCHECK, (WPARAM)0, 0);
			SendMessage(GetDlgItem(hwnd, IDC_RS4), BM_SETCHECK, (WPARAM)0, 0);
			return TRUE;
		case IDC_RS1:
			ECCmode = 4;
			SendMessage(GetDlgItem(hwnd, IDC_SENDONCE), BM_SETCHECK, (WPARAM)0, 0);
			SendMessage(GetDlgItem(hwnd, IDC_SENDTWICE), BM_SETCHECK, (WPARAM)0, 0);
			SendMessage(GetDlgItem(hwnd, IDC_SENDTHREE), BM_SETCHECK, (WPARAM)0, 0);
			SendMessage(GetDlgItem(hwnd, IDC_RS1), BM_SETCHECK, (WPARAM)1, 0);
			SendMessage(GetDlgItem(hwnd, IDC_RS2), BM_SETCHECK, (WPARAM)0, 0);
			SendMessage(GetDlgItem(hwnd, IDC_RS3), BM_SETCHECK, (WPARAM)0, 0);
			SendMessage(GetDlgItem(hwnd, IDC_RS4), BM_SETCHECK, (WPARAM)0, 0);
			return TRUE;
		case IDC_RS2:
			ECCmode = 5;
			SendMessage(GetDlgItem(hwnd, IDC_SENDONCE), BM_SETCHECK, (WPARAM)0, 0);
			SendMessage(GetDlgItem(hwnd, IDC_SENDTWICE), BM_SETCHECK, (WPARAM)0, 0);
			SendMessage(GetDlgItem(hwnd, IDC_SENDTHREE), BM_SETCHECK, (WPARAM)0, 0);
			SendMessage(GetDlgItem(hwnd, IDC_RS1), BM_SETCHECK, (WPARAM)0, 0);
			SendMessage(GetDlgItem(hwnd, IDC_RS2), BM_SETCHECK, (WPARAM)1, 0);
			SendMessage(GetDlgItem(hwnd, IDC_RS3), BM_SETCHECK, (WPARAM)0, 0);
			SendMessage(GetDlgItem(hwnd, IDC_RS4), BM_SETCHECK, (WPARAM)0, 0);
			return TRUE;
		case IDC_RS3:
			ECCmode = 6;
			SendMessage(GetDlgItem(hwnd, IDC_SENDONCE), BM_SETCHECK, (WPARAM)0, 0);
			SendMessage(GetDlgItem(hwnd, IDC_SENDTWICE), BM_SETCHECK, (WPARAM)0, 0);
			SendMessage(GetDlgItem(hwnd, IDC_SENDTHREE), BM_SETCHECK, (WPARAM)0, 0);
			SendMessage(GetDlgItem(hwnd, IDC_RS1), BM_SETCHECK, (WPARAM)0, 0);
			SendMessage(GetDlgItem(hwnd, IDC_RS2), BM_SETCHECK, (WPARAM)0, 0);
			SendMessage(GetDlgItem(hwnd, IDC_RS3), BM_SETCHECK, (WPARAM)1, 0);
			SendMessage(GetDlgItem(hwnd, IDC_RS4), BM_SETCHECK, (WPARAM)0, 0);
			return TRUE;
		case IDC_RS4:
			ECCmode = 7;
			SendMessage(GetDlgItem(hwnd, IDC_SENDONCE), BM_SETCHECK, (WPARAM)0, 0);
			SendMessage(GetDlgItem(hwnd, IDC_SENDTWICE), BM_SETCHECK, (WPARAM)0, 0);
			SendMessage(GetDlgItem(hwnd, IDC_SENDTHREE), BM_SETCHECK, (WPARAM)0, 0);
			SendMessage(GetDlgItem(hwnd, IDC_RS1), BM_SETCHECK, (WPARAM)0, 0);
			SendMessage(GetDlgItem(hwnd, IDC_RS2), BM_SETCHECK, (WPARAM)0, 0);
			SendMessage(GetDlgItem(hwnd, IDC_RS3), BM_SETCHECK, (WPARAM)0, 0);
			SendMessage(GetDlgItem(hwnd, IDC_RS4), BM_SETCHECK, (WPARAM)1, 0);
			return TRUE;
		case IDC_LEADINLONG:
			if (longleadin)
			{
				longleadin = FALSE;
				SendMessage (GetDlgItem (hwnd, IDC_LEADINLONG ), BM_SETCHECK, (WPARAM)0, 0);
//				DRMTransmitter.GetAudSrcEnc()->SetStartDelay(starttx_time); //edited DM
				DRMTransmitter.GetAudSrcEnc()->SetTheStartDelay(starttx_time); //added DM
			}
			else
			{
				longleadin = TRUE;
				SendMessage (GetDlgItem (hwnd, IDC_LEADINLONG ), BM_SETCHECK, (WPARAM)1, 0);
//				DRMTransmitter.GetAudSrcEnc()->SetStartDelay(starttx_time_long); //edited DM
				DRMTransmitter.GetAudSrcEnc()->SetTheStartDelay(starttx_time_long); //added DM
			}
			return TRUE;
        }
        break;
    case WM_CLOSE:
 		EndDialog (hwnd, 0);
		return TRUE;
   }
    return FALSE;
}

//Tick or untick the appropriate sound device in the menu
//This now needs a window menu INDEX number and not a driver index number! So add the menu base offsets. DONE
//edited DM - num = menu index (NOT driver index)
void unchecksoundrx(HWND hDlg, int num) 
{
	for (int i = 0; i <= numdevIn; i++) {
		int num2 = IDM_O_RX_I_DRIVERS0 + i;
		if (num2 == num) CheckMenuItem(GetMenu(hDlg), num, MF_BYCOMMAND | MF_CHECKED); 
		else CheckMenuItem(GetMenu(hDlg), num2, MF_BYCOMMAND | MF_UNCHECKED);
	}
	DRMReceiver.GetSoundInterface()->SetInDev(getsoundin('r')); //set data receiver sound in device DM
}
void unchecksoundtx(HWND hDlg, int num)
{
	for (int i = 0; i <= numdevOut; i++) {
		int num2 = IDM_O_TX_O_DRIVERS0 + i;
		if (num2 == num) CheckMenuItem(GetMenu(hDlg), num, MF_BYCOMMAND | MF_CHECKED);
		else CheckMenuItem(GetMenu(hDlg), num2, MF_BYCOMMAND | MF_UNCHECKED);
	}
	DRMTransmitter.GetSoundInterface()->SetOutDev(getsoundout('t')); //set data transmitter sound out device DM
}
void unchecksoundvoI(HWND hDlg, int num)
{
	for (int i = 0; i <= numdevIn; i++) {
		int num2 = IDM_O_VO_I_DRIVERS0 + i;
		if (num2 == num) CheckMenuItem(GetMenu(hDlg), num, MF_BYCOMMAND | MF_CHECKED);
		else CheckMenuItem(GetMenu(hDlg), num2, MF_BYCOMMAND | MF_UNCHECKED);
	}
	DRMTransmitter.GetSoundInterface()->SetInDev(getsoundin('t')); //set voice transmitter sound in device
}
void unchecksoundvoO(HWND hDlg, int num)
{
	for (int i = 0; i <= numdevOut; i++) {
		int num2 = IDM_O_VO_O_DRIVERS0 + i;
		if (num2 == num) CheckMenuItem(GetMenu(hDlg), num, MF_BYCOMMAND | MF_CHECKED);
		else CheckMenuItem(GetMenu(hDlg), IDM_O_VO_O_DRIVERS0 + i, MF_BYCOMMAND | MF_UNCHECKED);
	}
	DRMReceiver.GetSoundInterface()->SetOutDev(getsoundout('r')); //set voice receiver sound out device
}

//overwrite the wildcard filename on the full path with the name that was found DM 
void fixname(char* wildpath, char* filename) {
	int pos = strlen(wildpath); //get the length of the path (with wildcard)
	pos--; //go back 1 char to point to * (because strlen has it pointing at the null terminator)
	int pos2 = 0; //start at 0 for filename and go forward
	char test = 0;
	//find the last backslash in the path
	while ((test != 92) && (pos > 0)) {
		pos--;
		test = wildpath[pos]; //try combining this into the conditional (careful! C++ only evaluates the left hand side of && if it's false...)
	}
	if (test == 92) {
		//found it, pos = a pointer to the \...
		pos++; //next char is the start of the filename
		//now overwrite the wildcard name with the filename that was found
		while (test != 0) {
			wildpath[pos] = filename[pos2]; //copy first...
			test = wildpath[pos]; //then test for a zero - that way we will always copy the null termination
			pos++;
			pos2++;
			//make sure this writes the zero!
		}
	}
	else {
		//we failed to find a slash!
	}
}


//DM - The routines below are from the DLL version hamdrm.cpp file:   ======================================================================================================================

// Send BSR
BOOL __cdecl SendBSR(int inst, int fast)
{
	CVector<short> dummy;
	char filenam[300];
	char filenam2[300];

	DRMTransmitter.GetAudSrcEnc()->ClearPicFileNames();
	dummy.Init(0);
	wsprintf(filenam, "%s%s", bsrpath, "bsr.bin");
	wsprintf(filenam2, "%s%s", bsrpath, "cbsr.bin");
	if (fast != 0)
	{
		// compress the bsr.bin file
		CopyFile(filenam, filenam2, FALSE);
		compressBSR(filenam2, filenam); //renamed to prevent conflict with zlib DM
	}
	DRMTransmitter.GetAudSrcEnc()->SetPicFileName(filenam, "bsr.bin", dummy);
	if (inst >= 2)
		DRMTransmitter.GetAudSrcEnc()->SetPicFileName(filenam, "bsr.bin", dummy);
	if (inst >= 3)
		DRMTransmitter.GetAudSrcEnc()->SetPicFileName(filenam, "bsr.bin", dummy);
	if (inst >= 4)
		DRMTransmitter.GetAudSrcEnc()->SetPicFileName(filenam, "bsr.bin", dummy);
	return TRUE;
}

int iTID = 0;

BOOL __cdecl GetFileRX(char* FileName)
{
	CMOTObject NewPic;
	if (DRMReceiver.GetDataDecoder()->GetSlideShowPicture(NewPic))
	{
		char filenam[300]{ 0 };
		int picsize = 0, i = 0;
		FILE* set = nullptr;

		picsize = NewPic.vecbRawData.Size();
		iTID = NewPic.iTransportID;
		if (strcmp(NewPic.strName.c_str(), "bsr.bin") == 0)
		{
			char filenam2[300]{ 0 };
			wsprintf(filenam, "%s%s", bsrpath, "comp.bin");
			set = fopen(filenam, "wb");
			if (set != NULL)
			{
				for (i = 0; i < picsize; i++) putc(NewPic.vecbRawData[i], set);
				fclose(set);
			}

			wsprintf(filenam2, "%s%s", bsrpath, "bsrreq.bin");
			decompressBSR(filenam, filenam2); //renamed to prevent conflict with zlib DM

			strcpy(FileName, NewPic.strName.c_str());
		}
		else
		{
			wsprintf(filenam, "%s%s", rxfilepath, NewPic.strName.c_str());
			set = fopen(filenam, "wb");
			if (set != NULL)
			{
				for (i = 0; i < picsize; i++) putc(NewPic.vecbRawData[i], set);
				fclose(set);
			}
			strcpy(FileName, NewPic.strName.c_str());
		}
		return TRUE;
	}
	return FALSE;
}

int __cdecl GetLastTID()
{
	return iTID;
}

int locpiccnt, locpicnum;
BOOL stopsend = FALSE;
int stopct = 0;
int stopval = 10;

BOOL __cdecl GetPercentTX(int* piccnt, int* percent)
{
	if (TX_Running)
	{
		locpiccnt = DRMTransmitter.GetAudSrcEnc()->GetPicCnt();
		locpicnum = DRMTransmitter.GetAudSrcEnc()->GetNoOfPic();
		*piccnt = locpiccnt;
		*percent = DRMTransmitter.GetAudSrcEnc()->GetPicPerc();

		if (stopct == 0)
		{
			if (locpiccnt + 1 > locpicnum)
			{
				stopct = 1;
				if (DRMTransmitter.GetParameters()->GetInterleaverDepth() == CParameter::SI_LONG)
					stopval = 12;
				else
					stopval = 4;
			}
		}
		else
		{
			stopct++;
			if (stopct >= stopval)
			{
				stopct = 0;
				return TRUE;
			}
		}
		return FALSE;
	}
	else
	{
		*piccnt = 0;
		*percent = 0;
		stopct = 0;
		return FALSE;
	}
}

// BSR functions - Only GetBSR is used for now.. DM
int numseg;
string bsrname;

BOOL __cdecl GetBSR(int* numbsrsegments, char* namebsrfile)
{
	int numseg;
	string bsrname;

	if (DRMReceiver.GetDataDecoder()->GetSlideShowBSR(&numseg, &bsrname, bsrpath))
	{
		*numbsrsegments = numseg;
		strcpy(namebsrfile, bsrname.c_str());
		return TRUE;
	}
	*numbsrsegments = 0;
	strcpy(namebsrfile, "No segments missing");
	return FALSE;
}

/*

BOOL __cdecl readthebsrfile(char* fnam, int* segno)
{
	strcpy(fnam, readbsrfile(bsrpath).c_str());
	*segno = segnobsrfile();
	return (strlen(fnam) >= 2);
}

void   __cdecl writebsrselsegments(int inst)
{
	writeselsegments(inst);
}

void __cdecl GetData(int* totsize, int* actsize, int* actpos)
{
	*totsize = DRMReceiver.GetDataDecoder()->GetTotSize();
	*actsize = DRMReceiver.GetDataDecoder()->GetActSize();
	*actpos = DRMReceiver.GetDataDecoder()->GetActPos();
}

BOOL __cdecl GetCorruptFileRX(char* FileName)
{
	CMOTObject NewPic;
	if (DRMReceiver.GetDataDecoder()->GetSlideShowPartPicture(NewPic))
	{
		char filenam[300];
		int picsize, i;
		FILE* set = NULL;

		picsize = NewPic.vecbRawData.Size();
		iTID = NewPic.iTransportID;
		wsprintf(filenam, "%s%s", rxcorruptpath, NewPic.strName.c_str());
		set = fopen(filenam, "wb");
		if (set != NULL)
		{
			for (i = 0; i < picsize; i++) putc(NewPic.vecbRawData[i], set);
			fclose(set);
		}
		strcpy(FileName, NewPic.strName.c_str());
		return TRUE;
	}
	return FALSE;
}

CVector<_BINARY>	actsegs;

BOOL  __cdecl GetActSegm(char* data)
{
	if (DRMReceiver.GetDataDecoder()->GetSlideShowPartActSegs(actsegs))
	{
		int i;
		int size = actsegs.Size();
		if (size >= 650) size = 650;
		for (i = 0; i < size; i++)
		{
			if (actsegs[i] == TRUE) *(data + i) = 0;
			else *(data + i) = 1;
		}
		return TRUE;
	}
	else
		return FALSE;
}

void __cdecl ResetRX(void)
{
	DRMReceiver.SetInStartMode();
}

*/

void DrawBar(HWND hwnd) {
	BGbusy = 1; //flag that the thread is running

	//======================================================================================================================================================
	//SEGMENT BARGRAPH by Daz Man 2021
	//======================================================================================================================================================
	RECT rect{};
	unsigned int width = 218;
	unsigned int i = 0;
	if (GetWindowRect(hwnd, &rect))
	{
		width = (rect.right - rect.left) - 1;
		//int height = rect.bottom - rect.top;
	}
	//read erasures buffer and convert it to a line graph
	unsigned const int x = actpos; //get current segment number
	unsigned int y = totsize; //Total segment count DM

	y = max(DecTotalSegs, y);
	y = max(y, x);

	//Check for a sane size value so it doesn't stall the system...
	if (y < 32767) {

		if (y == 0) { y = 1; } //don't divide by zero!

		//The desired width is "width"
		//the actual width is "y"
		//the correction "n" is width/y
		//to use int scaling, n*1000 = BarLastSeg*1000/y
#define SCALE 968
		int n = (width * SCALE) / y;
		//this executes every 100mS, so make sure it doesn't run too often
		if ((BarLastSeg != x) && (CRCOK)) {
			int d = 0;

			if (n > SCALE) { n = SCALE; } //Don't stretch bargraph

			HDC hdc = GetDC(hwnd);
			MoveToEx(hdc, BARL, BARY, nullptr);
			/*
			XFORM xForm;
			xForm.eM11 = (FLOAT)2.0;
			xForm.eM12 = (FLOAT)0.0;
			xForm.eM21 = (FLOAT)0.0;
			xForm.eM22 = (FLOAT)2.0;
			xForm.eDx = 0;
			xForm.eDy = 0;
			SetWorldTransform(hdc, &xForm);
			*/
			//HPEN penz;
			HPEN penx = nullptr;
			HPEN penb = nullptr;
			HPEN penr = nullptr;
			HPEN peng = nullptr;
			LOGBRUSH lbb;
			lbb.lbStyle = BS_SOLID;
			lbb.lbColor = RGB(0, 0, 0);
			lbb.lbHatch = 0;
			LOGBRUSH lbr;
			lbr.lbStyle = BS_SOLID;
			lbr.lbColor = RED; // RGB(255, 0, 0);
			lbr.lbHatch = 0;
			LOGBRUSH lbg;
			lbg.lbStyle = BS_SOLID;
			lbg.lbColor = GREEN; // RGB(60, 255, 0);
			lbg.lbHatch = 0;
			LOGBRUSH lbx;
			lbx.lbStyle = BS_SOLID;
			lbx.lbColor = GetSysColor(COLOR_3DFACE); //RGB(127, 127, 127);
			lbx.lbHatch = 0;

			constexpr int pwidth = 1;
			penb = ExtCreatePen(PS_GEOMETRIC | PS_SOLID | PS_ENDCAP_FLAT | PS_JOIN_MITER, pwidth, &lbb, 0, nullptr); //Black for current segment
			penr = ExtCreatePen(PS_GEOMETRIC | PS_SOLID | PS_ENDCAP_FLAT | PS_JOIN_MITER, 1, &lbr, 0, nullptr); //Red for no segment received
			peng = ExtCreatePen(PS_GEOMETRIC | PS_SOLID | PS_ENDCAP_FLAT | PS_JOIN_MITER, pwidth, &lbg, 0, nullptr); //Green for good segment
			penx = ExtCreatePen(PS_GEOMETRIC | PS_SOLID | PS_ENDCAP_FLAT | PS_JOIN_MITER, 6, &lbx, 0, nullptr); //Grey background erase
			//penz = ExtCreatePen(PS_GEOMETRIC | PS_SOLID | PS_ENDCAP_FLAT | PS_JOIN_MITER, 1, &lbx, 0, nullptr); //Grey background erase

			//Erase the last black line - for the 2nd/3rd pass etc.
			if (x < BarLastSeg) {
				SelectObject(hdc, peng); //use green - because if there was a black line drawn, there must have been a good segment
				MoveToEx(hdc, ((BarLastSeg * n) / 1000), BARB, nullptr);
				LineTo(hdc, ((BarLastSeg * n) / 1000), BART); //erase
			}
#define GLOBALBARBUF 1 //if this is set, bargraph erasure data is read via the global erasure buffer, otherwise it is read via the class variables (not working)
#if GLOBALBARBUF == 1
			//data is good and we have a valid new segment - so update the graph
			//copy from buffer and draw to the screen
			//only read as far into the buffer as we need to...
			for (i = 0; i < y; i++) {
				//for each bit read, set the display red on 0 and green on 1
				d = (erasures[RSsw][i >> 3] >> (i & 7)) & 1; //get the correct bit
				if (d == 0) {
					SelectObject(hdc, penr); //select red pen
					MoveToEx(hdc, (i * n) / 1000, BARB, nullptr); //
					LineTo(hdc, (i * n) / 1000, BART); //draw a red line up
				}
				else {
					SelectObject(hdc, peng);//select green pen
					MoveToEx(hdc, (i * n) / 1000, BARB, nullptr); //
					LineTo(hdc, (i * n) / 1000, BART); //draw a green line up
				}
			}
#endif
#if GLOBALBARBUF == 0
			//this won't work, because vvbiSegment is buried in classes...
			for (i = 0; i < vvbiSegment.Size(); i++) {
				//for each bit read, set the display red on 0 and green on 1
				//d = (erasures[RSsw][i >> 3] >> (i & 7)) & 1; //get the correct bit
				if (vvbiSegment[i].Size() >= 1) {
					SelectObject(hdc, peng);//select green pen
					MoveToEx(hdc, (i * n) / 1000, BARB, nullptr); //
					LineTo(hdc, (i * n) / 1000, BART); //draw a green line up
				}
				else {
					SelectObject(hdc, penr); //select red pen
					MoveToEx(hdc, (i * n) / 1000, BARB, nullptr); //
					LineTo(hdc, (i * n) / 1000, BART); //draw a red line up
				}
			}
#endif
			SelectObject(hdc, penb);
			MoveToEx(hdc, ((x * n) / 1000), BARB, nullptr);
			LineTo(hdc, ((x * n) / 1000), BART); //draw a black tip on the line

			//did the transport ID change? (new file) - check if the Total segment count has changed also, and redraw the window background where needed
			if ((BarTransportID != DecTransportID) || (y != BarLastTot) || (x > BarLastTot)) {

				SelectObject(hdc, penx); //penx is the window background colour, and 6 pixels square
				MoveToEx(hdc, ((x * n) / 1000) + 5, BARY, nullptr);
				LineTo(hdc, BARR, BARY); //erase the rest of the window
				BarTransportID = DecTransportID;
				BarLastTot = y; //update totsegs
			}

			DeleteObject(penr);
			DeleteObject(peng);
			DeleteObject(penb);
			DeleteObject(penx);
			//DeleteObject(penz);

			BarLastSeg = x;

			ReleaseDC(hwnd, hdc);
		}
	}
	BGbusy = 0; //flag that the thread has terminated
}

void PTTon() {
	//check if it's off
	if (PTT != TRUE) {
		dotx(); //activate PTT
		PTT = TRUE;
	}
}

void PTToffD() {
	Sleep(1000); //delay switching to prevent cutting the audio

	//check if it's on 
	if (PTT == TRUE) {
		endtx(); //deactivate PTT
		PTT = FALSE;
	}
}

void PTToff() {
	//check if it's on 
	if (PTT == TRUE) {
		std::thread TXoff(PTToffD); //launch PTToffD in a new thread to avoid stalling the calling routine
		TXoff.detach(); //detach and terminate after running
	}
}

