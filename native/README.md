# SimpleRemoteDesk Native

Native Windows rewrite of SimpleRemoteDesk.

## Goals

- Windows x64 first
- Low CPU and memory usage
- Smooth UI
- Event-driven architecture
- Separate UI, capture, network, audio and input layers
- No busy-wait loops
- Reusable buffers instead of per-frame allocations
- Modern fast path for newer Windows versions
- Compatibility fallback path for older supported Windows versions

## Planned modules

- app
- ui
- capture
- network
- codec
- input
- audio
- security
- transfer
- common

## Build

```bat
cmake -S native -B build/native -A x64
cmake --build build/native --config Release
```

## Important

The legacy C# code is used only as a behavioral reference.
The native version is intentionally not a line-by-line port.
