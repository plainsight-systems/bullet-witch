# Media CPU ownership migration

This slice routes engine-owned media storage through the existing reclaiming
CPU domain, preserving the implemented independent demux/video/audio workers and
consumed-audio playback clock. It introduces no frame-reset allocator or media
timing algorithm.

## Source basis and decision

Inspected pinned FFmpeg n7.1.1 `ffplay.c`, `packet_queue_flush`,
`packet_queue_destroy`, `decoder_abort`, and `stream_close`: cancellation wakes
blocked owners, joins codec threads, then releases queued references and queue
storage. Inspected Unreal revision `f45729c0b6f097038e72b52d366a62baeca3275b`,
`MediaUtils/Public/MediaSampleQueue.h`, `Dequeue`, `Enqueue`, `RequestFlush`:
queue removal and flush do not invalidate separately retained sample ownership.
Godot 4.5-stable `AudioRBResampler::write` uses preallocated storage and separate
read/write indices. The local pinned sources and hashes are documented in
[the decoder design](movie-demux-and-decode-design.md) and
[the media reference study](media-reference-design.md).

Apply those ownership contracts to this engine: allocator-bearing converted
frames retain their domain independently of decoder and Player lifetime. Fixed
PCM/segment storage is allocated before workers or native callbacks begin.
Reading/writing the PCM ring does not copy allocator capabilities, allocate,
release storage, or call domain accounting. State teardown first retires output
callbacks, cancels and joins workers, then releases owned storage.

## Coverage and boundaries

The selected native path supplies a domain to Player and the FFmpeg factory.
Domain storage covers Player state/control, decoder state/control, codec-owner
state/control, packet queue descriptor arrays, video queue descriptors, converted
BGRA payloads, converted PCM scratch, and the fixed PCM/sample-segment ring.
The native MediaAudio service also takes the selected domain and allocates its
State/control and fixed device/media mapping table through it. Successful native
setup publishes that state; failed setup joins/releases the local state before
throwing. Callback logic and AudioQueue timing are unchanged. AudioQueue-owned
buffer allocations remain native external storage.
Converted samples and copied sample containers retain the allocator capability.
Domain exhaustion is an explicit allocation failure; no system-heap fallback
occurs for an injected owner.

The existing `unique_ptr<Decoder>` factory interface is retained with a small
external adapter shell around the domain-owned state. Factory callable storage,
filesystem strings, exception objects, C++ thread runtime/stack storage and
FFmpeg's own packets, compressed buffers, codec frames, conversion contexts and
internal allocations remain external overhead. Compressed queue byte limits
still bound admitted payload; they do not become domain-accounted allocation.
No unmaintained FFmpeg allocator patch or global hook is introduced. Native
AudioQueue/Metal resources and device completion belong to their native owners.

Legacy decoder tool factories, synthetic Player construction and default sample
construction retain an explicit compatibility allocator. Native composition
uses the injected factory. Per-open captured capabilities survive worker startup
failure, stop and caller-retained samples; there is no mutable global domain.

Design MCP consulted both corpora and read full R.1, MEM.6 and MEM.9.
Implementation consulted both again and read E.6; MEM.6/MEM.9 remain applicable:
own storage through RAII, inject at subsystem boundaries, allocate callback
storage before callback execution. Tests must distinguish callback allocation
freedom from decoder-worker allocations, and engine-accounted bytes from external
library or native overhead.

## Verification

Focused strict builds and fatal LLVM 21.1.8 clang-tidy passed for the seven changed
implementation/test translation units, including native audio state and its
memory test. In-process PCM/Player tests passed in a
standalone strict build and with UBSan, covering retained sample ownership,
partial ring construction rollback, allocation-disabled callback operations and
Player failed-open cleanup.
The native audio memory test also passed with UBSan: an empty service retains its
domain, and a zero-budget preparation rejects State allocation before any worker
or AudioQueue is created, leaving no live charge and releasing ownership at
destruction. This test deliberately does not open an audio device.

A silent read-only native decode probe covered all four installed startup movies:
marvelousLogo (56 video frames / 112,046 audio frames), xeedLogo (60 / 95,966),
witchLOGO (600 / 958,399), advertise (3,687 / 5,899,200). Every video-byte and
PCM-byte SHA-256 matched the prior independent asynchronous decoder acceptance
logs exactly. Holding one decoded frame after decoder destruction left only its
3,686,400 payload bytes live; releasing it returned the domain to zero live
allocations. Each asset also passed cancellation after priming and failed-open
rollback checks. Peaks include the deliberately retained extra frame and are
observations of this probe, not proposed application budgets.

Logs, generated probe, source hashes and machine-readable comparisons remain
under ignored `research/evidence/reconstruction/memory/media/`. No window or
audio device was opened. This verifies decoded output and CPU ownership, not
native presentation, device timing, external FFmpeg memory bounds or full
repository acceptance; the parent integration owns the latter checks.
