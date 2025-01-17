/******************************************************************************\
 * Technische Universitaet Darmstadt, Institut fuer Nachrichtentechnik
 * Copyright (c) 2001
 *
 * Author(s):
 *	Volker Fischer
 *
 * Description:
 * Sound card interface for Windows operating systems
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

#include "Sound.h"
#include <time.h>


/* Implementation *************************************************************/
/******************************************************************************\
* Wave in                                                                      *
\******************************************************************************/


_BOOLEAN CSound::Read(CVector<short>& psData)
{
	int			i = 0; //inits DM
	_BOOLEAN	bError = FALSE;

	/* Check if device must be opened or reinitialized */
	if (bChangDevIn == TRUE)
	{
		OpenInDevice();

		/* Reinit sound interface */
		InitRecording(iBufferSizeIn, bBlockingRec);

		/* Reset flag */
		bChangDevIn = FALSE;
	}

	/* Wait until data is available */
	if (!(m_WaveInHeader[iWhichBufferIn].dwFlags & WHDR_DONE))
	{
		if (bBlockingRec == TRUE)
			WaitForSingleObject(m_WaveInEvent, INFINITE);
		else
		{
			for (i = 0; i < iBufferSizeIn; i++) psData[i] = 0;
			return FALSE;
		}
	}

	/* Check if buffers got lost */
	int iNumInBufDone = 0;
	for (i = 0; i < NUM_SOUND_BUFFERS_IN; i++)
	{
		if (m_WaveInHeader[i].dwFlags & WHDR_DONE)
			iNumInBufDone++;
	}

	/* If the number of done buffers equals the total number of buffers, it is
	   very likely that a buffer got lost -> set error flag */
	if (iNumInBufDone == NUM_SOUND_BUFFERS_IN)
		bError = TRUE;
	else
		bError = FALSE;

	/* Copy data from sound card in output buffer */
	for (i = 0; i < iBufferSizeIn; i++)
		psData[i] = psSoundcardBuffer[iWhichBufferIn][i];

	/* Add the buffer so that it can be filled with new samples */
	AddInBuffer();

	/* In case more than one buffer was ready, reset event */
	ResetEvent(m_WaveInEvent);

	return bError;
}

void CSound::AddInBuffer()
{
	/* Unprepare old wave-header */
	waveInUnprepareHeader(
		m_WaveIn, &m_WaveInHeader[iWhichBufferIn], sizeof(WAVEHDR));

	/* Prepare buffers for sending to sound interface */
	PrepareInBuffer(iWhichBufferIn);

	/* Send buffer to driver for filling with new data */
	waveInAddBuffer(m_WaveIn, &m_WaveInHeader[iWhichBufferIn], sizeof(WAVEHDR));

	/* Toggle buffers */
	iWhichBufferIn++;
	if (iWhichBufferIn == NUM_SOUND_BUFFERS_IN)
		iWhichBufferIn = 0;
}

void CSound::PrepareInBuffer(int iBufNum)
{
	/* Set struct entries */
	m_WaveInHeader[iBufNum].lpData = (LPSTR) &psSoundcardBuffer[iBufNum][0];
	m_WaveInHeader[iBufNum].dwBufferLength = iBufferSizeIn * BYTES_PER_SAMPLE;
	m_WaveInHeader[iBufNum].dwFlags = 0;

	/* Prepare wave-header */
	waveInPrepareHeader(m_WaveIn, &m_WaveInHeader[iBufNum], sizeof(WAVEHDR));
}

void CSound::InitRecording(int iNewBufferSize, _BOOLEAN bNewBlocking)
{
	/* Check if device must be opened or reinitialized */
	if (bChangDevIn == TRUE)
	{
		OpenInDevice();

		/* Reset flag */
		bChangDevIn = FALSE;
	}

	/* Set internal parameter */
	iBufferSizeIn = iNewBufferSize;
	bBlockingRec = bNewBlocking;

	/* Reset interface so that all buffers are returned from the interface */
	waveInReset(m_WaveIn);
	waveInStop(m_WaveIn);
	
	/* Reset current buffer ID (it is important to do this BEFORE calling
	   "AddInBuffer()" */
	iWhichBufferIn = 0;

	/* Create memory for sound card buffer */
	for (int i = 0; i < NUM_SOUND_BUFFERS_IN; i++)
	{
		/* Unprepare old wave-header in case that we "re-initialized" this
		   module. Calling "waveInUnprepareHeader()" with an unprepared
		   buffer (when the module is initialized for the first time) has
		   simply no effect */
		waveInUnprepareHeader(m_WaveIn, &m_WaveInHeader[i], sizeof(WAVEHDR));

		if (psSoundcardBuffer[i] != nullptr)
			delete[] psSoundcardBuffer[i];

		psSoundcardBuffer[i] = new short[iBufferSizeIn];


		/* Send all buffers to driver for filling the queue ----------------- */
		/* Prepare buffers before sending them to the sound interface */
		PrepareInBuffer(i);

		AddInBuffer();
	}

	/* Notify that sound capturing can start now */
	waveInStart(m_WaveIn);

	/* This reset event is very important for initialization, otherwise we will
	   get errors! */
	ResetEvent(m_WaveInEvent);
}

void CSound::OpenInDevice()
{
	/* Open wave-input and set call-back mechanism to event handle */
	if (m_WaveIn != nullptr)
	{
		waveInReset(m_WaveIn);
		waveInClose(m_WaveIn);
	}

	MMRESULT result = waveInOpen(&m_WaveIn, iCurInDev, &sWaveFormatEx,
		(DWORD) m_WaveInEvent, NULL, CALLBACK_EVENT);
	if (result != MMSYSERR_NOERROR)
		throw CGenErr("Sound Interface Start, waveInOpen() failed."
			"This error usually occurs if another application blocks the sound in.");
}

void CSound::SetInDev(int iNewDev)
{
	/* Set device to wave mapper if iNewDev is greater that the number of
	   sound devices in the system */
	if (iNewDev >= (int)iNumDevsIn)
		iNewDev = WAVE_MAPPER;

	/* Change only in case new device id is not already active */
	if (iNewDev != iCurInDev)
	{
		iCurInDev = iNewDev;
		bChangDevIn = TRUE;
	}
}

/******************************************************************************\
* Wave out                                                                     *
\******************************************************************************/

// Added NulAsh
WaveHeader CSound::MakeWaveHeader(int const sampleRate,
	short int const numChannels,
	short int const bitsPerSample)
{
	WaveHeader myHeader;

	// RIFF WAVE Header
	myHeader.chunkId[0] = 'R';
	myHeader.chunkId[1] = 'I';
	myHeader.chunkId[2] = 'F';
	myHeader.chunkId[3] = 'F';
	myHeader.format[0] = 'W';
	myHeader.format[1] = 'A';
	myHeader.format[2] = 'V';
	myHeader.format[3] = 'E';

	// Format subchunk
	myHeader.subChunk1Id[0] = 'f';
	myHeader.subChunk1Id[1] = 'm';
	myHeader.subChunk1Id[2] = 't';
	myHeader.subChunk1Id[3] = ' ';
	myHeader.audioFormat = 1; // FOR PCM
	myHeader.numChannels = numChannels; // 1 for MONO, 2 for stereo
	myHeader.sampleRate = sampleRate; // ie 44100 hertz, cd quality audio
	myHeader.bitsPerSample = bitsPerSample; //
	myHeader.byteRate = myHeader.sampleRate * myHeader.numChannels * myHeader.bitsPerSample / 8;
	myHeader.blockAlign = myHeader.numChannels * myHeader.bitsPerSample / 8;

	// Data subchunk
	myHeader.subChunk2Id[0] = 'd';
	myHeader.subChunk2Id[1] = 'a';
	myHeader.subChunk2Id[2] = 't';
	myHeader.subChunk2Id[3] = 'a';

	// All sizes for later:
	// chuckSize = 4 + (8 + subChunk1Size) + (8 + subChubk2Size)
	// subChunk1Size is constanst, i'm using 16 and staying with PCM
	// subChunk2Size = nSamples * nChannels * bitsPerSample/8
	// Whenever a sample is added:
	//    chunkSize += (nChannels * bitsPerSample/8)
	//    subChunk2Size += (nChannels * bitsPerSample/8)
	myHeader.chunkSize = 4 + 8 + 16 + 8 + 0;
	myHeader.subChunk1Size = 16;
	myHeader.subChunk2Size = 0;

	return myHeader;
}

_BOOLEAN CSound::Write(CVector<short>& psData)
{
	int			i = 0,j = 0; //inits DM
	int			iCntPrepBuf = 0;
	int			iIndexDoneBuf = 0;

	/* Check if device must be opened or reinitialized */
	if (bChangDevOut == TRUE)
	{
		OpenOutDevice();

		/* Reinit sound interface */
		InitPlayback(iBufferSizeOut, bBlockingPlay);

		/* Reset flag */
		bChangDevOut = FALSE;
	}

	/* Get number of "done"-buffers and position of one of them */
	GetDoneBuffer(iCntPrepBuf, iIndexDoneBuf);

	/* Now check special cases (Buffer is full or empty) */
	if (iCntPrepBuf == 0)
	{
		if (bBlockingPlay == TRUE)
		{
			/* Blocking wave out routine. Needed for transmitter. Always
			   ensure that the buffer is completely filled to avoid buffer
			   underruns */
			while (iCntPrepBuf == 0)
			{
				WaitForSingleObject(m_WaveOutEvent, INFINITE);

				GetDoneBuffer(iCntPrepBuf, iIndexDoneBuf);
			}
		}
		else
		{
			/* All buffers are filled, dump new block ----------------------- */
			// It would be better to kill half of the buffer blocks to set the start
			// back to the middle: TODO
			return TRUE; /* An error occurred */
		}
	}
	else if ((iCntPrepBuf == NUM_SOUND_BUFFERS_OUT) && (iCurOutDev != iNumDevsOut)) // Don't send empty buffer if writing to WAV file -- NulAsh
	{
		/* ---------------------------------------------------------------------
		   Buffer is empty -> send as many cleared blocks to the sound-
		   interface until half of the buffer size is reached */
		   /* Send half of the buffer size blocks to the sound-interface */
		for (j = 0; j < NUM_SOUND_BUFFERS_OUT / 2; j++)
		{
			/* First, clear these buffers */
			for (i = 0; i < iBufferSizeOut; i++)
				psPlaybackBuffer[j][i] = 0;

			/* Then send them to the interface */
			AddOutBuffer(j);
		}

		/* Set index for done buffer */
		iIndexDoneBuf = NUM_SOUND_BUFFERS_OUT / 2;

//		bError = TRUE;
	}
//	else
//		bError = FALSE;

	
	/* Copy data from input in soundcard buffer */
	for (i = 0; i < iBufferSizeOut; i++)
		psPlaybackBuffer[iIndexDoneBuf][i] = psData[i];

	/* Now, send the current block */
	AddOutBuffer(iIndexDoneBuf);

	return FALSE;
}

void CSound::GetDoneBuffer(int& iCntPrepBuf, int& iIndexDoneBuf)
{
	/* Get number of "done"-buffers and position of one of them */
	iCntPrepBuf = 0;
	for (int i = 0; i < NUM_SOUND_BUFFERS_OUT; i++)
	{
		if (m_WaveOutHeader[i].dwFlags & WHDR_DONE)
		{
			iCntPrepBuf++;
			iIndexDoneBuf = i;
		}
	}
}

_BOOLEAN CSound::IsEmpty(void)
{
	for (int i = 0; i < NUM_SOUND_BUFFERS_OUT; i++)
		if (!(m_WaveOutHeader[i].dwFlags & WHDR_DONE))
			return FALSE;
	return TRUE;
}

void CSound::AddOutBuffer(int iBufNum)
{
	/* Unprepare old wave-header */
	if (m_WaveOut != nullptr)
	{
		waveOutUnprepareHeader(
			m_WaveOut, &m_WaveOutHeader[iBufNum], sizeof(WAVEHDR));
	}

	/* Prepare buffers for sending to sound interface */
	PrepareOutBuffer(iBufNum);

	/* Send buffer to driver for filling with new data */
	if (m_WaveOut != nullptr)
	{
		waveOutWrite(m_WaveOut, &m_WaveOutHeader[iBufNum], sizeof(WAVEHDR));
	}
	else if (iCurOutDev == iNumDevsOut)
	{
		fwrite(m_WaveOutHeader[iBufNum].lpData, 1, m_WaveOutHeader[iBufNum].dwBufferLength, m_WaveOutFile);
		m_WaveOutHeader[iBufNum].dwFlags |= WHDR_DONE;
	}
}

void CSound::PrepareOutBuffer(int iBufNum)
{
	/* Set Header data */
	m_WaveOutHeader[iBufNum].lpData = (LPSTR) &psPlaybackBuffer[iBufNum][0];
	m_WaveOutHeader[iBufNum].dwBufferLength = iBufferSizeOut * BYTES_PER_SAMPLE;
	m_WaveOutHeader[iBufNum].dwFlags = 0;

	/* Prepare wave-header */
	if (m_WaveOut != nullptr)
	{
		waveOutPrepareHeader(m_WaveOut, &m_WaveOutHeader[iBufNum], sizeof(WAVEHDR));
	}
}

void CSound::InitPlayback(int iNewBufferSize, _BOOLEAN bNewBlocking)
{
	int	i = 0, j = 0; //inits DM

	/* Check if device must be opened or reinitialized */
	if (bChangDevOut == TRUE)
	{
		OpenOutDevice();

		/* Reset flag */
		bChangDevOut = FALSE;
	}

	/* Set internal parameters */
	iBufferSizeOut = iNewBufferSize;
	bBlockingPlay = bNewBlocking;

	/* Reset interface */
	if (m_WaveOut != nullptr)
	{
		waveOutReset(m_WaveOut);
	}

	for (j = 0; j < NUM_SOUND_BUFFERS_OUT; j++)
	{
		/* Unprepare old wave-header (in case header was not prepared before,
		   simply nothing happens with this function call */
		if (m_WaveOut != nullptr)
		{
			waveOutUnprepareHeader(m_WaveOut, &m_WaveOutHeader[j], sizeof(WAVEHDR));
		}

		/* Create memory for playback buffer */
		if (psPlaybackBuffer[j] != nullptr)
			delete[] psPlaybackBuffer[j];

		psPlaybackBuffer[j] = new short[iBufferSizeOut];

		/* Clear new buffer */
		for (i = 0; i < iBufferSizeOut; i++)
			psPlaybackBuffer[j][i] = 0;

		/* Prepare buffer for sending to the sound interface */
		PrepareOutBuffer(j);

		/* Initially, send all buffers to the interface */
		m_WaveOutHeader[j].dwFlags = WHDR_DONE;
		//AddOutBuffer(j);
	}
}

void CSound::OpenOutDevice()
{
	if (m_WaveOut != nullptr) //edit DM
	{
		waveOutReset(m_WaveOut);
		waveOutClose(m_WaveOut);
	}
	CloseOutFile();
	if (iCurOutDev == iNumDevsOut)
	{
		char tms[15];
		time_t rawtime;
		struct tm tm1;
		WaveHeader wh;
		time(&rawtime);
		localtime_s(&tm1, &rawtime);
		m_strWaveOutFileName = wavdir;
		if (m_strWaveOutFileName[m_strWaveOutFileName.length() - 1] != '\\') {
			m_strWaveOutFileName += '\\';
		}
		strftime(tms, 15, "%Y%m%d%H%M%S", &tm1);
		m_strWaveOutFileName += tms;
		m_strWaveOutFileName += ".wav";
		wh = MakeWaveHeader(sWaveFormatEx.nSamplesPerSec, sWaveFormatEx.nChannels, sWaveFormatEx.wBitsPerSample);
		fopen_s(&m_WaveOutFile, m_strWaveOutFileName.c_str(), "wb");
		fwrite(&wh, sizeof wh, 1, m_WaveOutFile);
		m_WaveOut = nullptr;
	}
	else
	{
		MMRESULT result = waveOutOpen(&m_WaveOut, iCurOutDev, &sWaveFormatEx,
			(DWORD)m_WaveOutEvent, NULL, CALLBACK_EVENT);

		if (result != MMSYSERR_NOERROR)
			throw CGenErr("Sound Interface Start, waveOutOpen() failed.");
	}
}

void CSound::SetOutDev(int iNewDev)
{
	/* Set device to wave mapper if iNewDev is greater that the number of
	   sound devices in the system */
	if (iNewDev > (int)iNumDevsOut)
		iNewDev = WAVE_MAPPER;

	/* Change only in case new device id is not already active */
	if (iNewDev != iCurOutDev)
	{
		iCurOutDev = iNewDev;
		bChangDevOut = TRUE;
	}
}

// added NulAsh
void CSound::CloseOutFile()
{
	if (m_WaveOutFile != nullptr)
	{
		long pos = ftell(m_WaveOutFile) - 8;
		if (pos == 36)
		{
			fclose(m_WaveOutFile);
			remove(m_strWaveOutFileName.c_str());
		}
		else
		{
			fseek(m_WaveOutFile, 4, SEEK_SET);
			fwrite(&pos, 4, 1, m_WaveOutFile);
			fseek(m_WaveOutFile, 40, SEEK_SET);
			pos -= 4 + 8 + 16 + 8;
			fwrite(&pos, 4, 1, m_WaveOutFile);
			fclose(m_WaveOutFile);
		}
		m_WaveOutFile = nullptr;
	}
}


/******************************************************************************\
* Common                                                                       *
\******************************************************************************/
void CSound::Close()
{
	int			i = 0; //inits DM
	MMRESULT	result = 0;

	/* Reset audio driver */
	if (m_WaveOut != nullptr) //edit DM
	{
		result = waveOutReset(m_WaveOut);
		if (result != MMSYSERR_NOERROR)
			m_WaveOut = nullptr; //edit DM
			//throw CGenErr("Sound Interface, waveOutReset() failed.");
	}

	if (m_WaveIn != nullptr) //edit DM
	{
		result = waveInReset(m_WaveIn);
		if (result != MMSYSERR_NOERROR)
			m_WaveIn = nullptr; //edit DM
			//throw CGenErr("Sound Interface, waveInReset() failed.");
	}

	/* Set event to ensure that thread leaves the waiting function */
	if (m_WaveInEvent != nullptr) //edit DM
		SetEvent(m_WaveInEvent);

	/* Wait for the thread to terminate */
	Sleep(500);

	/* Unprepare wave-headers */
	if (m_WaveIn != nullptr) //edit DM
	{
		for (i = 0; i < NUM_SOUND_BUFFERS_IN; i++)
		{
			result = waveInUnprepareHeader(
				m_WaveIn, &m_WaveInHeader[i], sizeof(WAVEHDR));

			if (result != MMSYSERR_NOERROR)
				throw CGenErr("Sound Interface, waveInUnprepareHeader()"
					" failed.");
		}

		/* Close the sound in device */
		result = waveInClose(m_WaveIn);
		if (result != MMSYSERR_NOERROR)
			throw CGenErr("Sound Interface, waveInClose() failed.");
	}

	if (m_WaveOut != nullptr) //edit DM
	{
		for (i = 0; i < NUM_SOUND_BUFFERS_OUT; i++)
		{
			result = waveOutUnprepareHeader(
				m_WaveOut, &m_WaveOutHeader[i], sizeof(WAVEHDR));

			if (result != MMSYSERR_NOERROR)
				throw CGenErr("Sound Interface, waveOutUnprepareHeader() failed.");
		}

		/* Close the sound out device */
		result = waveOutClose(m_WaveOut);
		if (result != MMSYSERR_NOERROR)
			throw CGenErr("Sound Interface, waveOutClose() failed.");
	}
	else
		CloseOutFile(); // added NulAsh


	/* Set flag to open devices the next time it is initialized */
	bChangDevIn = TRUE;
	bChangDevOut = TRUE;
}

CSound::CSound()
{
	int i = 0; //init DM

	/* Should be initialized because an error can occur during init */
	m_WaveInEvent = nullptr; //edit DM
	m_WaveOutEvent = nullptr; //edit DM
	m_WaveIn = nullptr; //edit DM
	m_WaveOut = nullptr; //edit DM
	wavdir = nullptr; //edit NulAsh
	m_WaveOutFile = nullptr; // edit NulAsh

	/* Init buffer pointer to zero */
	for (i = 0; i < NUM_SOUND_BUFFERS_IN; i++)
	{
		memset(&m_WaveInHeader[i], 0, sizeof(WAVEHDR));
		psSoundcardBuffer[i] = nullptr; //edit DM
	}

	for (i = 0; i < NUM_SOUND_BUFFERS_OUT; i++)
	{
		memset(&m_WaveOutHeader[i], 0, sizeof(WAVEHDR));
		psPlaybackBuffer[i] = nullptr; //edit DM
	}

	/* Init wave-format structure */
	sWaveFormatEx.wFormatTag = WAVE_FORMAT_PCM;
	sWaveFormatEx.nChannels = NUM_IN_OUT_CHANNELS;
	sWaveFormatEx.wBitsPerSample = BITS_PER_SAMPLE;
	sWaveFormatEx.nSamplesPerSec = SOUNDCRD_SAMPLE_RATE;//*NUM_IN_OUT_CHANNELS; //============================================================== Check this DM================
//	sWaveFormatEx.nSamplesPerSec = SOUNDCRD_SAMPLE_RATE*NUM_IN_OUT_CHANNELS; //============================================================== Check this DM================
	sWaveFormatEx.nBlockAlign = sWaveFormatEx.nChannels * sWaveFormatEx.wBitsPerSample / 8;
	sWaveFormatEx.nAvgBytesPerSec = sWaveFormatEx.nBlockAlign * sWaveFormatEx.nSamplesPerSec;
	sWaveFormatEx.cbSize = 0;

	/* Get the number of digital audio devices in this computer, check range */
	iNumDevsIn  = waveInGetNumDevs();
	iNumDevsOut = waveOutGetNumDevs();

	if (iNumDevsIn > MAX_NUMBER_SOUND_CARDS)
		iNumDevsIn = MAX_NUMBER_SOUND_CARDS;
	if (iNumDevsOut > MAX_NUMBER_SOUND_CARDS)
		iNumDevsOut = MAX_NUMBER_SOUND_CARDS;

	/* At least one device must exist in the system */
	if ((iNumDevsIn == 0) || (iNumDevsOut == 0))
		throw CGenErr("No audio device found.");

	/* Get info about the devices and store the names */
	for (i = 0; i < (int)iNumDevsIn; i++)
		if (!waveInGetDevCaps(i, &m_WaveInDevCaps, sizeof(WAVEINCAPS)))
			pstrDevicesIn[i] = m_WaveInDevCaps.szPname;
	for (i = 0; i < (int)iNumDevsOut; i++)
		if (!waveOutGetDevCaps(i, &m_WaveOutDevCaps, sizeof(WAVEOUTCAPS)))
			pstrDevicesOut[i] = m_WaveOutDevCaps.szPname;
	pstrDevicesOut[i] = "Wave Out"; // Added -- NulAsh

	/* We use an event controlled wave-in (wave-out) structure */
	/* Create events */
	m_WaveInEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
	m_WaveOutEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);

	/* Set flag to open devices */
	bChangDevIn = TRUE;
	bChangDevOut = TRUE;

	/* Default device number (first device in system) */
	iCurInDev = 0;
	iCurOutDev = 0;

	/* Non-blocking wave out is default */
	bBlockingPlay = FALSE;

	/* Blocking wave in is default */
	bBlockingRec = TRUE;
}

CSound::~CSound()
{
	int i;

	/* Delete allocated memory */
	for (i = 0; i < NUM_SOUND_BUFFERS_IN; i++)
	{
		if (psSoundcardBuffer[i] != nullptr)
			delete[] psSoundcardBuffer[i];
	}

	for (i = 0; i < NUM_SOUND_BUFFERS_OUT; i++)
	{
		if (psPlaybackBuffer[i] != nullptr)
			delete[] psPlaybackBuffer[i];
	}

	/* Close the handle for the events */
	if (m_WaveInEvent != nullptr)
		CloseHandle(m_WaveInEvent);

	if (m_WaveOutEvent != nullptr)
		CloseHandle(m_WaveOutEvent);
}
