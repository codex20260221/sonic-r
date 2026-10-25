# Two-instance Windows LAN workflow — 2026-10-24

Use the existing branch `codex/windows-playable-20261024`, MSYS2 UCRT64,
PowerShell 7 and your privately supplied PC game-data directory. Pass `-DataDirectory` to the launcher or set the `SONICR_DATA_DIR` environment variable in your PowerShell session before running it.

For example:

```powershell
$env:SONICR_DATA_DIR = 'C:\path\to\owned-game-data'
```
Loss recovery is receive-only; see [its verification record](network-loss-2026-10-24.md).

## Build and launch

Build a development binary so connection diagnostics are available. In UCRT64:

```sh
cd /path/to/sonic-r/source/sdl
make -j8 CC=gcc BUILDDIR=build/lan-verification TARGET=build/lan-verification/sonicr.exe
```

After changing compiler options, use a fresh build directory or force a rebuild.
In PowerShell 7 at the repository root:

```powershell
./tests/windows_lan.ps1
./tests/windows_lan.ps1 -Status | Format-List
```

The launcher copies the 638,606,097-byte private runtime into two independent
data directories under ignored `source/sdl/build/lan-windows`. It does not copy
root `ONLINE.DAT`, original executables or credentials, and does not write to
the source data. Each role gets its own SAVE, GHOST, options and log files.
Prepared data is reused; this is about 1.3 GB of disk space for both roles.
Executable copies are verified against the same SHA-256. Nothing under this
session directory belongs in a commit.

Equivalent direct commands after preparation are:

```powershell
& ./source/sdl/build/lan-windows/host/sonicr-host.exe --lan --username LAN_HOST ./source/sdl/build/lan-windows/host/data
& ./source/sdl/build/lan-windows/client/sonicr-client.exe --lan --username LAN_CLIENT --host 127.0.0.1 ./source/sdl/build/lan-windows/client/data
```

Run those in separate shells, or use the launcher, which starts both GUI
processes concurrently. `--host` selects the destination; it does not
automatically enter the menu or create a session. The supported test uses the
default UDP port 7847; the existing `--port` option is parsed but does not
override the hard-coded session calls, so this workflow does not depend on it.
The launcher checks that 7847 is free before starting and never changes firewall
or system network settings.

`--lan` is an explicit opt-in. It clears in-memory online identity/session
state, skips matchmaker initialization/token loading/refresh/sign-in, and keeps
the existing LAN lobby paths. No on-disk token is deleted. The default online
path is unchanged. Direct `--host` connections skip LAN discovery broadcasts.
The original F3 fallback is too late to prevent the initial public code request;
do not use ordinary sign-in as the entry path for this test.

## Manual host/client menu workflow

These steps are derived from the production menu state machine; the connected
session portion has not been executed in this environment.

1. Focus each game window and press Enter through the logos/title. If attract
   playback has begun, return to the title/menu before proceeding.
2. Use Up/Down and Enter to select the fourth main-menu entry, the Network
   icon/Multiplayer entry, immediately above Options. It maps to `SCREEN_NET`.
   From the first Grand Prix entry, this is three Down moves; allow cursor
   animation to finish between presses.
3. Both instances should enter the LAN lobby without a QR or authentication
   screen. Their `stderr.log` should contain
   `NetworkScreen: --lan, public matchmaking disabled`.
4. In the host window press and release F1 once to create the host. Then, in
   the client window, press F2 once to join the configured loopback address.
   Join polling can take about two seconds. Wait until both names appear and
   the host reports two players before attempting to start.
5. F6 cycles the local character; choosing different characters makes ownership
   visible. F8 cycles track selection; use the host's default/Resort Island for
   the first test and confirm the same track appears on the client.
6. On the host, press and release F1 again to start. The client waits for the
   host. Both must complete START_GAME acknowledgement and level-ready/GO.
7. Use keyboard input with controllers neutral. Hold A in the focused host
   window for two seconds, steer, then switch to the client and inspect that
   remote player's motion. Repeat with the client moving independently. Both
   processes may poll the same physical controller, so shared pad input can
   conceal ownership problems.
8. Record track, characters, build hash, start/ack/GO logs, duration, movement
   observations, stalls/teleports, and any disconnects. Compare both directions
   before claiming live synchronization. A UDP endpoint or joined lobby alone
   does not establish race synchronization or packet-loss recovery.

## Connection evidence and cleanup

The development build writes diagnostics to each role's `stderr.log`, not its
`stdout.log`. Useful paired evidence is:

| Stage | Host | Client |
| --- | --- | --- |
| Lobby | `Network session created (host, port 7847)` and `Host: client joined as slot 1` | `Joined session at 127.0.0.1:7847` and `Client: SLOT_ASSIGN slot=1` |
| Start | `Host: START_GAME ack mask=...` | Level-load/race entry after START_GAME |
| Level barrier | `Host: all peers ready, sending LEVEL_GO` | `Client: received LEVEL_GO, entering race` |
| State stream | `Net: player 1 speaks protocol ...` | `Net: player 0 speaks protocol ...` |

Inspect owned UDP endpoints and recent diagnostics without touching other apps:

```powershell
./tests/windows_lan.ps1 -Status | Format-List
Get-Content ./source/sdl/build/lan-windows/host/stderr.log -Tail 40
Get-Content ./source/sdl/build/lan-windows/client/stderr.log -Tail 40
./tests/windows_lan.ps1 -Stop
```

The manifest is `source/sdl/build/lan-windows/processes.json`. Stop verifies the
recorded PID, executable path and exact start time, requests window close, and
fails if the process does not exit within five seconds. It never kills a
different process by name or removes private data. Close a stuck owned window
manually. The launcher was corrected for PowerShell 7's automatic JSON date
conversion so its identity check compares UTC ticks rather than display strings.

## What was actually observed

Both real Windows instances were launched from the same development binary,
with separate data and identical executable hashes. Recorded first-run PIDs
were host 44568 and client 43672. Both exposed `Sonic R` windows and logged
the logos, title, `InitLevel OK` and `MAIN GAME LOOP` during independent
attract-mode playback. The launcher status and safe close workflow were run.
All 496 source runtime file hashes remained unchanged after the run.

**No connected host/client session was observed.** Neither instance entered
the Network menu; there were no owned UDP endpoints, JOIN/SLOT_ASSIGN/start/GO
evidence, or live synchronization observations. Independent attract playback
is startup/runtime evidence only. It is not a multiplayer test.

Automated menu input was blocked by Windows Computer Use: two window-selection
attempts returned `Computer Use native pipe is unavailable: failed to connect
native pipe: The system cannot find the file specified. (os error 2)`.
No alternate UI injection or public service was used. The runnable setup above
remains available for manual verification; actual LAN racing, live loss/jitter
behavior and Dreamcast cross-play remain unverified. The next required live
step is the paired manual menu/connection/race check above.
