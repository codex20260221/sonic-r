# Two-instance Windows LAN workflow — 2026-10-24

Use the currently authorized branch, MSYS2 UCRT64,
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

These steps are derived from the production menu state machine. The connected
session was subsequently exercised in the 2026-10-25 verification below.

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

## Initial observations — 2026-10-24

Both real Windows instances were launched from the same development binary,
with separate data and identical executable hashes. Both exposed `Sonic R`
windows and logged the logos, title, `InitLevel OK` and `MAIN GAME LOOP` during
independent attract-mode playback. The launcher status and safe close workflow were run.
All 496 source runtime file hashes remained unchanged after the run.

**No connected host/client session was observed in the initial run.** Neither
instance entered the Network menu; there were no owned UDP endpoints,
JOIN/SLOT_ASSIGN/start/GO evidence, or live synchronization observations. Independent attract playback
is startup/runtime evidence only. It is not a multiplayer test.

Automated menu input was blocked by Windows Computer Use: two window-selection
attempts returned `Computer Use native pipe is unavailable: failed to connect
native pipe: The system cannot find the file specified. (os error 2)`.
No alternate UI injection or public service was used. The runnable setup above
remained available for manual verification. The subsequent paired manual
menu/connection/race check is recorded below.

## Connected Windows verification — 2026-10-25

The authoritative checkout was clean on `main` at `041dbcd`. A fresh UCRT64
GCC development build completed successfully with the command above. No game
or network implementation changes were needed. Both launcher-created executable
copies matched the build's SHA-256:

```text
F82EA86464CD7753B07F7E0C85CFA90BFA4CD4C64533486E98C4F33250241CC6
```

The user operated the two game windows because the native Computer Use pipe
was still unavailable. Both instances entered the offline lobby. `LAN_HOST`
created the session on UDP 7847; `LAN_CLIENT` joined directly through
`127.0.0.1`, was assigned slot 1, and appeared in the host's two-player session.
Both players used Sonic on Resort Island.

Paired development diagnostics confirmed:

| Stage | Observed evidence |
| --- | --- |
| Offline lobby | Both logged `NetworkScreen: --lan, public matchmaking disabled` |
| Join | Host logged slot 1 joining with `playerCount=2`; client logged `SLOT_ASSIGN slot=1` |
| Race start | Host logged `START_GAME ack mask=0x3 (expected=0x3, retries=1)` |
| Level barrier | Host sent `LEVEL_GO` after slot 1 became ready; client logged `received LEVEL_GO, entering race` |
| State stream | Host logged player 1 speaking protocol 1; client logged player 0 speaking protocol 1 |

Both processes reached `InitIsland`, `InitLevel OK` and `MAIN GAME LOOP`. The
user confirmed independent movement in both directions for at least one minute:
host movement appeared on the client and client movement appeared on the host,
with no visible issues. Neither role's log contained a disconnect, timeout or
error at the verification checkpoint; both owned processes and UDP endpoints
remained active. Gameplay observations came from the user; automated inspection
covered the paired logs and process/endpoint status.

The original private runtime's 496 file paths, sizes and SHA-256 hashes were
unchanged after the check. Neither prepared runtime contained root `ONLINE.DAT`.
Raw logs, source-data hash manifests and local diagnostic evidence stayed
outside Git; prepared runtime copies remain in the ignored session directory.

Available checks also passed on this checkout: 140 codec cases, 615 transactional
receive cases, 96 loss scenarios / 8,822 deliveries, and 5 interpolation tests.
The launcher's PowerShell syntax and private-runtime ignore rules were checked.
The loss suite remains synthetic production-logic coverage.

This verifies a connected two-instance Windows race over loopback with manual
bidirectional movement checks. The user subsequently confirmed completing a
full race in this session with no issues.
