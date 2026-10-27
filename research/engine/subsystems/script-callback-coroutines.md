# Shared-VM mission callback coroutines

## Original boundary and reference

Original callback registration enters `0x481360`: trap binding `0x47c820`
registers type three and timer binding `0x47c710` registers type zero. The
mission callback owner `0x481a10` scans 64 records of stride 0x6c. Mission
composition owns that registry capacity. The original dispatcher `0x43eea0`
consumes `0x481ef0` serially and pumps each selected function to completion
before taking the next. The engine Context API supports explicitly reserved
shared-VM continuations, but mission composition reserves only one child slot
and preserves the original serial policy. The 64 records are conditions, not
64 concurrent scripts. Neither layer creates operating-system threads.

The production reference is the exact Lua 5.0.2 dependency, archive SHA256
`a6c85d85f912e1c321723084389d63dee7660b81b8292452b190ea7190dd73bc`:
`src/lapi.c` (`lua_newthread`, `lua_xmove`), `src/lstate.c`
(`luaE_newthread`, `lua_close`), `src/ldo.c` (`lua_resume`, `lua_yield`), and
`src/lib/lauxlib.c` (`luaL_ref`, `luaL_unref`). New threads share the global
state and global table while keeping their own stack and continuation.
Registry references keep each thread reachable across GC. Resume owns a
protected C boundary; a yielded native call resumes only once its poll clears.
Closing the shared state destroys all retained threads. We adopt those APIs
and lifetimes, adding bounded host task slots and context-local identities.
No asynchronous scheduler or untraced gameplay behavior is introduced.

## Ownership and algorithm

Context prepares its callback slot vector before any start. Each slot owns
its registry anchor, native reply/poll, terminal diagnostic and execution
phase. A monotonic nonzero identity prevents an old handle from addressing a
reused slot. Completed and failed tasks remain inspectable until explicit
retirement. Pending polls are canceled before registry retirement; all polls
are canceled before VM shutdown. The primary start/pump API remains separate
but uses the same internal task state and bounded resume algorithm.

A pump guard rejects nested pumping or task mutation from native callbacks.
Only the currently executing task receives native replies, waits and errors;
there is no process-global active context. Top-level load execution uses the
primary task as its native-call receiver. Every Lua operation that can longjmp
stays within a protected C API boundary; no owning C++ local crosses it.
Task lookup is bounded linear work over fixed slots. Lua and existing native
reply allocations retain their documented contracts; this does not claim
allocation-free Lua execution.

Both design and implementation retrieve Core SL.C.1 and performance MEM.9.
The first preserves C++ destruction across Lua error/yield boundaries; the
second places stable host slot storage in initialization rather than callback
pumping. Independent waits, shared globals, stale IDs, cancellation, native
exception isolation and bounded slot reuse are required engine verification.
[Mission policy](mission-script-callbacks.md) separately preserves serial
dispatch and the original registry modes.

## Verification

Scoped fatal clang-tidy and formatter pass for Context and its new unit tests.
The in-process suite passes independent primary/child waits, shared globals,
Context moves, exhaustion/stale identities, local native/poll errors, reentrant
pump rejection, failed-start cleanup and destruction while suspended. A hundred
start/wait/retire cycles leave identical live Lua bytes after every retirement;
all Domain charges disappear after Context destruction. A deliberate child
start OOM remains protected, releases the slot and leaves the primary task
resumable. Existing Lua heap, primary wait, protected host OOM and independent
Context memory contracts also pass against the modified implementation.

Retirement explicitly invokes Lua collection after removing the registry
anchor. This is a reclamation policy, not constant-time work. It prevents
unreachable canceled stacks from occupying the bounded heap until a future
allocation threshold. Full mission callback ordering and application acceptance
remain responsibilities of the mission integration and repository gates.
