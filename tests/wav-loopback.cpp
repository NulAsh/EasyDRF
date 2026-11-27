// Win32 integration test. Link this translation unit in place of Sound.obj.
// The production WAV writer and modem run normally. WinMM capture pulls PCM
// from the test recording at each wait, including after a receiver mode change.
#include <cstdio>
#include <deque>
#include <fstream>
#include <thread>
#include <chrono>
#include "../common/DrmReceiver.h"
#include "../common/DrmTransmitter.h"
#include "../common/settings.h"
#include "../common/callsign2.h"

extern CDRMReceiver DRMReceiver;
extern CDRMTransmitter DRMTransmitter;
static FILE* recording;
static std::deque<WAVEHDR*> captureQueue;
static int receivedFiles;

static void collectFile()
{
	CMOTObject object;
	if (!DRMReceiver.GetDataDecoder()->GetSlideShowPicture(object)) return;
	const string base = "received-" + std::to_string(++receivedFiles);
	std::ofstream(base + ".name") << object.strName;
	std::ofstream bytes(base + ".bin", std::ios::binary);
	if (object.vecbRawData.Size())
		bytes.write(reinterpret_cast<const char*>(object.vecbRawData.data()), object.vecbRawData.Size());
}

static void captureBlock()
{
	collectFile();
	WAVEHDR* block = captureQueue.front();
	captureQueue.pop_front();
	if (fread(block->lpData, 1, block->dwBufferLength, recording) != block->dwBufferLength)
	{
		memset(block->lpData, 0, block->dwBufferLength);
		DRMReceiver.GetParameters()->bRunThread = false;
	}
	block->dwFlags |= WHDR_DONE;
}

// Supply one virtual recording device and no physical playback dependency.
#define waveInGetNumDevs() 1
#define waveOutGetNumDevs() 1
#define waveInOpen(handle, ...) (*(handle) = reinterpret_cast<HWAVEIN>(1), MMSYSERR_NOERROR)
#define waveInPrepareHeader(...) MMSYSERR_NOERROR
#define waveInUnprepareHeader(...) MMSYSERR_NOERROR
#define waveInAddBuffer(handle, block, size) (captureQueue.push_back(block), MMSYSERR_NOERROR)
#define waveInStart(...) MMSYSERR_NOERROR
#define waveInStop(...) MMSYSERR_NOERROR
#define waveInReset(...) (captureQueue.clear(), MMSYSERR_NOERROR)
#define waveInClose(...) MMSYSERR_NOERROR
#define WaitForSingleObject(event, timeout) captureBlock()
#include "../sound/Sound.cpp"

static int encode(int argc, char** argv)
{
	// tx robustness qam long-interleave lead-in-frames ECC narrow low-protection file [file ...]
	if (argc < 10) return 2;
	CParameter& p = *DRMTransmitter.GetParameters();
	ERobMode mode = static_cast<ERobMode>(atoi(argv[2]));
	p.bOnlyPicture = true;
	const ESpecOcc bandwidth = atoi(argv[7]) ? SO_0 : SO_1;
	p.InitCellMapTable(mode, bandwidth);
	p.MSCPrLe.iPartB = atoi(argv[8]);
	p.eMSCCodingScheme = static_cast<CParameter::ECodScheme>(atoi(argv[3]));
	p.eSymbolInterlMode = atoi(argv[4]) ? CParameter::SI_LONG : CParameter::SI_SHORT;
	p.Service[0].strLabel = "Call";
	ECCmode = atoi(argv[6]);
	TxLevel = 1;
	setmodeqam(mode, p.eMSCCodingScheme, p.eSymbolInterlMode, bandwidth);
	auto* sound = DRMTransmitter.GetSoundInterface();
	char output[] = ".";
	sound->SetWaveOutDir(output);
	sound->SetOutDev(sound->GetNumDevOut());
	DRMTransmitter.SetCarOffset(350);
	DRMTransmitter.Init();
	p.Service[0].DataParam.iPacketLen = calcpacklen(p.iNumDecodedBitsMSC);
	auto* source = DRMTransmitter.GetAudSrcEnc();
	source->SetTheStartDelay(atoi(argv[5]));
	std::thread worker([] { DRMTransmitter.Start(); });
	for (int pass = 0; pass < 2; ++pass)
	{
		source->ClearPicFileNames();
		for (int i = 9; i < argc; ++i)
			source->SetPicFileName(argv[i], argv[i], CVector<short>());
		DRMTransmitter.Init();
		DRMTransmitter.Send();
		const auto timeout = std::chrono::steady_clock::now() + std::chrono::seconds(60);
		while (DRMTransmitter.GetWaveTransfer() != CDRMTransmitter::WaveComplete)
		{
			if (std::chrono::steady_clock::now() >= timeout)
			{
				DRMTransmitter.Stop();
				worker.join();
				return 3;
			}
			Sleep(pass ? 73 : 5);
		}
		DRMTransmitter.NotSend();
	}
	DRMTransmitter.Stop();
	worker.join();
	return 0;
}

int main(int argc, char** argv)
{
	if (argc >= 2 && string(argv[1]) == "tx") return encode(argc, argv);
	if (argc != 3 || string(argv[1]) != "rx") return 2;
	recording = fopen(argv[2], "rb");
	if (!recording || fseek(recording, sizeof(WaveHeader), SEEK_SET) != 0) return 2;
	DRMReceiver.GetParameters()->bOnlyPicture = true;
	DRMReceiver.SetFastReset(false);
	DRMReceiver.Init();
	DRMReceiver.Start();
	collectFile();
	fclose(recording);
	return receivedFiles ? 0 : 1;
}
