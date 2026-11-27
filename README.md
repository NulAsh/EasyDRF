# EasyDRF

This is a new project to update the WinDRM application, using the latest code from the DLL version, merged with the only available (older) code from the application version. The result is being updated to fix bugs and add features suitable for Shortwave broadcasting.

This Windows application program is an encoder and decoder for the HamDRM standard that is used for sending and receiving files over radio using COFDM modulation. The WinDRM code was based on the Dream DRM software, and has been modified to reduce it's bandwidth to 2.5kHz.

DRM stands for "Digital Radio Mondiale", and as this was certain to be trademarked, I decided to remove the term from the application name.

The application has been renamed to "EasyDRF" (Easy Digital Radio Files).

Daz Man 2021

## Saving a file to cassette

In the **Soundcard** menu, select **Wave Out** under **TX Output**. Click **TX File**, add your files, select **1 Instance**, and click **TX**. The recording appears in the `WaveOut` directory. Each export gets its own timestamped filename. Exporting the same named files in the same order with the same modem settings and callsign produces byte-identical WAV contents.

Start the receiver before playing the recording from the beginning. The normal lead-in gives it 5.6 seconds of modem signal to synchronize before the first file. **Long Leadin** extends this to 9.6 seconds. Each recording also includes the frames needed to recover the end of the last file.
