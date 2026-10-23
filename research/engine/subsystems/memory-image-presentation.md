# Image, font and front-end presentation storage

The native title/menu surfaces derive their CPU capability from the injected
ResourceService domain. Their state, fixed command/texture tables, decoded image
pixels, glyph/lookup vectors, selected loading name and costume labels retain
that domain. Encoding leases continue to retain original archive owners.

The concrete source reference is libpng v1.6.59 `example.c` (simplified RGBA read)
and `png.h` (`png_image_begin_read_from_memory`, `png_image_finish_read`,
`png_image_free`), inspected in the build's checksum-pinned source. The archive
SHA-256 is `2540302a1844ad2b2b501977abecfa850f265f97b78f065a712ab4074a89f5b5`,
as recorded by `cmake/PngDecoder.cmake`. The example allocates the destination
after format selection; finish-read writes into caller-provided storage.
The migration gives that destination the injected allocator directly, avoiding
an additional default-heap full-image buffer or post-decode pixel copy.

Existing PNG size validation, FONT channel/bearing interpretation, sprite order
and renderer contracts remain unchanged. Image/font values carry their allocators
through copies and moves, so they may outlive decode inputs and composition
owners. Construction failure releases partially decoded vectors. A domain
failure does not fall back to the global heap. Legacy parser/test calls without
an allocator retain their documented compatibility route.

The renderer borrows pixels during upload and copies them into its own native
storage; CPU decode storage retires after upload returns. Surface end clears
queued commands and releases texture handles and leases, while state/control
storage survives until surface destruction. GPU completion and accounting remain
the native renderer's responsibility. No per-frame command allocation is added.

libpng/zlib transient internals, exception diagnostics and the supplied loading
selector's `std::function` control allocation remain external. The simplified PNG
API is preserved instead of introducing new setjmp/longjmp and allocator-hook
behavior as an incidental memory migration. No claim of total codec/native
allocation coverage is made.

Both C++ corpora were queried in design and implementation. Previously retrieved
full rules E.6/R.1 and MEM.6/MEM.9 apply: RAII cleans failed construction, nested
containers receive their owner's capability, and fixed draw arrays do not grow
during presentation. The implementation retains the established shared
`memory::Allocator` rather than introducing a second container policy.

The five changed implementation/test translation units passed focused strict
compilation and fatal LLVM 21.1.8 clang-tidy. Existing synthetic PNG/FONT tests
plus ownership/rejection cases passed normally and under UBSan: copied images
and fonts retain their domain after the producer exits, final owners release
storage, malformed input returns partially decoded storage, and allocation or
budget rejection never falls back to default storage. Logs remain under ignored
`research/evidence/reconstruction/memory/image-presentation/`. Full composition
and original-content corpus acceptance remain the parent integration's checks;
no UI or native graphics acceptance is claimed by these tests.
