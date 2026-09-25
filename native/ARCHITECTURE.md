# Native rewrite architecture

## Primary objective

Rebuild SimpleRemoteDesk as a native x64 Windows application with predictable latency,
low idle CPU usage and clear separation between UI, transport and media processing.

The legacy C# implementation is a behavioral reference only.

## Compatibility strategy

Two runtime paths are planned:

- Windows 8.1 / 10 / 11: modern accelerated capture path (DXGI Desktop Duplication where available).
- Windows 7: compatibility capture path (GDI/D3D9 fallback).

All optional APIs must be resolved at runtime or isolated behind capability checks so that
loading the executable does not fail on an older Windows version.

Windows 7 compatibility is a release requirement, but must be verified on a real/virtual
Windows 7 x64 machine. It must not be assumed from compilation alone.

## Process model

Two executables:

- SimpleRemoteHost.exe
- SimpleRemoteViewer.exe

Shared native static library:

- srd_core

## Modules

### common
RAII handles, logging, byte buffers, clocks, thread helpers and error handling.

### protocol
Packet definitions, serialization and framing. Protocol details must not depend on the UI.

### security
Authentication, session key derivation, authenticated encryption and replay protection.

### network
Winsock transport. Initial implementation may use overlapped sockets; IOCP is the target
for long-lived asynchronous channels.

### capture
IScreenCapture interface.

Implementations:
- DxgiScreenCapture (Windows 8+)
- GdiScreenCapture (Windows 7 fallback)

The capture layer reports frame metadata and dirty regions. It does not encode or send.

### codec
Frame encoder/decoder abstraction. The first compatibility implementation can keep JPEG
for protocol bring-up. A more efficient video path can be added after correctness is stable.

### input
Mouse/keyboard serialization on Viewer and SendInput injection on Host.
Mouse movement is coalesced: only the newest unsent pointer position is retained.

### audio
WASAPI-based capture/playback with bounded jitter buffers. No unbounded queues.

### transfer
Clipboard/file manifest and chunked transfer, isolated from the interactive control path.

### ui
Win32 windowing plus Direct2D/DirectWrite rendering. No periodic full-window redraw loop.
UI receives immutable state snapshots/events from the core.

## Threading model

No busy waiting.

Host:
- UI thread
- network I/O workers
- capture worker
- encoder worker
- optional audio worker

Viewer:
- UI/render thread
- network I/O workers
- decoder worker
- optional audio worker

Bounded queues are mandatory between media stages. When the Viewer is slower than the Host,
old video frames are dropped instead of building latency.

Control packets are prioritized over media.

## Legacy behavior to preserve

Packet families found in v2.11.4:

1  Screen
2  MouseMove
3  MouseButton
4  MouseWheel
5  Key
6  AudioFormat
7  Audio
8  KeyCombination
9  Preview
10 Cursor
20 FileClipboardGet
21 FileManifest
22 FileChunk
23 FileTransferEnd
24 FileClipboardPut
25 FileTransferAck

Legacy server currently uses base port +0 through +4 for screen, control, preview, audio
and file transfer. The native architecture should encapsulate channel mapping so this can
later be reduced or multiplexed without touching the rest of the application.

## Performance rules

1. Never allocate a full new frame buffer every render iteration unless unavoidable.
2. Reuse buffers through pools/ring buffers.
3. Never queue unlimited video/audio/control work.
4. Coalesce mouse-move traffic.
5. Drop stale video frames before increasing latency.
6. Do not invoke UI work for every network packet.
7. Encode only when a new frame is available.
8. Prefer dirty-region processing when the capture backend provides it.
9. Keep file transfer off the latency-sensitive control path.
10. Measure CPU, memory, frame latency and bandwidth before/after each optimization.

## Migration milestones

### M0 - Bootstrap
CMake builds native x64 executables and shared core library.

### M1 - Session core
Host and Viewer connect locally, authenticate, exchange framed encrypted test packets,
disconnect cleanly and survive reconnects.

### M2 - Input
Viewer sends mouse/keyboard input and Host injects it through SendInput.

### M3 - Screen
GDI reference capture works first; DXGI accelerated capture is then added behind the same
interface. Viewer renders frames without WinForms/GDI+ allocations.

### M4 - Discovery and profiles
LAN/VPN discovery, saved computers and connection selection.

### M5 - Audio
WASAPI capture/playback with bounded buffering.

### M6 - Clipboard and file transfer
Manifest/chunk transfer and clipboard integration.

### M7 - Native UI
Recreate Host and Viewer UX in Win32 + Direct2D/DirectWrite.

### M8 - Compatibility/performance
Windows 7/8.1/10/11 x64 matrix, long-session tests, reconnect tests, CPU/RAM/bandwidth profiling.
