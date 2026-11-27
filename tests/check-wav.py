"""Run with a Release Win32 build from an x86 Visual Studio developer prompt.

    msbuild EasyDRF.vcxproj /p:Configuration=Release /p:Platform=Win32
    py -3 tests/check-wav.py
"""
from hashlib import sha256
from itertools import product
from pathlib import Path
import struct
import subprocess
import tempfile
import wave

ROOT = Path(__file__).resolve().parents[1]


def build_test(work):
    objects = sorted(p for p in (ROOT / "Release").glob("*.obj") if p.stem.lower() != "sound")
    if not objects:
        raise RuntimeError("Build the Release Win32 project first")
    libs = ("gdi32 winmm user32 libfftw libspeex ptt mixer comdlg32 Shell32 graphwin "
            "LzmaLib comctl32 advapi32").split()
    exe = work / "wav-loopback.exe"
    subprocess.run(["cl", "/nologo", "/EHsc", "/O2", "/MT", "/std:c++17", "/DNDEBUG",
                    "/DWIN32", "/D_WINDOWS", str(ROOT / "tests" / "wav-loopback.cpp"),
                    "/link", "/LTCG", "/SAFESEH:NO", "/SUBSYSTEM:CONSOLE", "/NODEFAULTLIB:libc.lib",
                    "/LIBPATH:" + str(ROOT / "common" / "libs"), "/OUT:" + str(exe),
                    *map(str, objects), *(name + ".lib" for name in libs)], cwd=work, check=True)
    return exe


def check(exe, folder, mode, qam, depth, lead, ecc, files, narrow=0, low_protection=0):
    folder.mkdir()
    for name, body in files.items():
        (folder / name).write_bytes(body)
    subprocess.run([str(exe), "tx", *map(str, (mode, qam, depth, lead, ecc, narrow, low_protection)), *files],
                   cwd=folder, check=True)
    a, b = sorted(folder.glob("*.wav"))
    raw = a.read_bytes()
    assert sha256(raw).digest() == sha256(b.read_bytes()).digest(), (mode, qam, depth, ecc)
    assert struct.unpack_from("<I", raw, 4)[0] == len(raw) - 8
    assert struct.unpack_from("<I", raw, 40)[0] == len(raw) - 44
    with wave.open(str(a)) as f:
        assert (f.getframerate(), f.getnchannels(), f.getsampwidth()) == (48000, 2, 2)
        assert f.getnframes() % 19200 == 0
    # Start a cold receiver and play the recording exactly once.
    subprocess.run([str(exe), "rx", a.name], cwd=folder, check=True, timeout=90)
    assert len(list(folder.glob("received-*.name"))) == len(files)
    for index, (name, body) in enumerate(files.items(), start=1):
        assert (folder / f"received-{index}.name").read_text() == name
        received = (folder / f"received-{index}.bin").read_bytes()
        if ecc >= 4:
            # Recover the systematic bytes of each interleaved RS codeword.
            data_bytes = (224, 192, 160, 128)[ecc - 4]
            assert len(received) % 255 == 0
            blocks = len(received) // 255
            data = b"".join(received[i::blocks][:data_bytes] for i in range(blocks))
            assert data[:16] == b"EasyDRFHeader/|\x02"
            start = int.from_bytes(data[16:18], "little")
            size = int.from_bytes(data[18:21], "little")
            assert data[21:start] == name.encode() and size == len(body)
            assert not any(data[start + size:])
            received = data[start:start + size]
        assert received == body
    print(f"mode={mode} qam={qam} long={depth} narrow={narrow} low={low_protection} ecc={ecc} frames={(len(raw)-44)//76800}", flush=True)


if __name__ == "__main__":
    with tempfile.TemporaryDirectory() as temp:
        work = Path(temp)
        exe = build_test(work)
        game = bytes((i * 31 + i // 11) % 256 for i in range(4099))
        for mode, qam, depth in product(range(3), range(3), range(2)):
            check(exe, work / f"{mode}-{qam}-{depth}", mode, qam, depth, 14, 1,
                  {"GAME.BIN": game, "A": b"\x91", "EMPTY.BIN": b""})
            for narrow, low in ((1, 0), (0, 1), (1, 1)):
                check(exe, work / f"{mode}-{qam}-{depth}-{narrow}-{low}",
                      mode, qam, depth, 14, 1, {"A": b"\x91"}, narrow, low)
        for ecc in range(4, 8):
            check(exe, work / f"rs{ecc}", 1, 1, 1, 24, ecc, {"GAME.BIN": game})
