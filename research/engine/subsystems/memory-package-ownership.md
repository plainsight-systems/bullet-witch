# Named-file package ownership

The root Lua route receives a borrowed package payload from its retained resource
lease. Its old iterator construction created a full default-heap `Bytes` copy.
The new explicit-domain parser copies that span once into final domain storage,
then parses metadata and names using the same owning allocator. The legacy
`parse(Bytes)` overload remains for existing tools/tests; injected allocation
failure never selects it.

This is an allocation/lifetime change to the already validated DPK v1 parser,
not a new binary format algorithm. The bounded header/table/name/member checks
remain those documented in [startup runtime](../../../docs/startup-runtime.md).
The repository's allocator-aware `ResourceArchive::parse` and checked
`Reader::text_view` were inspected as the concrete neighboring implementation:
move final byte storage, initialize nested allocator-bearing owners, construct
names directly from checked spans, retain offsets rather than pointers into a
movable container. Established native backing and shared allocator source studies
remain [the Mac backing](memory-macos-backing.md) and
[the Unreal memory reference](memory-unreal-reference.md); no new allocation
algorithm or source-copying dependency is introduced.

Package payloads, member vector, each member name, index nodes and index key
strings retain the supplied domain. Copied packages remain independent values
and retain their original owner. Member spans borrow their package; extracting
a member is allocation-free, but retaining a naked span cannot extend package
lifetime. Parse failure unwinds all partial owners. Exception diagnostics remain
standard-library storage outside domain coverage. Costs remain O(bytes + names
+ N log N) to parse and O(log N) lookup; the full default-heap staging copy on
the native root path is removed, not the deliberate final owned payload copy.

Both C++ corpora were queried in design and implementation. Full R.1/E.6 and
MEM.6 rules previously retrieved in this memory migration apply directly:
allocator capabilities propagate through nested ownership, and standard RAII
cleans partial parsing without an exhaustion fallback. RootDriver's explicit
constructor retains the domain; the legacy constructor remains a compatibility
route. Its callsite is coordinated with the context/RootDriver owner.

Focused strict compilation and fatal LLVM 21.1.8 clang-tidy passed for the parser
and resource-test translation units. The existing resource test executable was
rebuilt and passed, including domain-backed package copies, long nested names,
final-owner release, budget failure after payload acquisition, malformed later
member cleanup, and native allocation rejection without compatibility fallback.
Logs remain under ignored `research/evidence/reconstruction/memory/package/`.
Full repository and original-root acceptance are parent integration checks.
