// E2: translate observed little-endian Lua 5.0 32-bit chunks to the pinned
// host Lua ABI. Input is borrowed for the call; output owns bytes. Throws Error
// on bounds/version/depth/trailing-data failure. No script executes here.
// Algorithm: check header; recursively copy prototype structure, widening only
// serialized size_t and Instruction fields. Opcode words/numbers remain exact.
// Costs O(chunk bytes), one output allocation stream; nesting capped at 100.
// Optimization: no decompilation/recompilation and no per-instruction objects.
// Design/implementation MCP: I.30 (#ri-encapsulate) confines C/runtime details;
// SL.con.3 (#rsl-bounds) checks byte/index access. cpp-performance COPY.5
// (guidelines/copy-move/COPY.5-resource-owning-types-are-move-only.md):
// unique runtime owners cannot be copied; moves preserve stable heap identity.

#pragma once
#include "bw/formats/common.hpp"
namespace bw::script {
std::string host_chunk(std::span<const std::uint8_t> bytes);
}
