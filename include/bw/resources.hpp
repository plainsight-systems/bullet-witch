// Compatibility include for the separated SRC-01 types; no implementation.
// Owners, errors and costs are defined in the included contracts. Algorithm:
// include pure format/catalog interfaces. File opening now uses content/access.
// Optimization: a single decoder implementation remains authoritative.
// Design: cpp-guidelines I.1 (#ri-explicit), C.122 (#rh-separation):
// explicit ownership and injected file capability. cpp-performance COPY.2
// (guidelines/copy-move/COPY.2-mark-move-constructors-noexcept.md): container
// owners retain implicit moves. Implementation: SL.con.3 (#rsl-bounds) checks
// slices before access; COPY.4 (guidelines/copy-move/
// COPY.4-rule-of-five-rule-of-zero.md) keeps owning values rule-of-zero.

// Naming design/implementation consultation: cpp-guidelines P.3 (#rp-what)
// selects role-based names. cpp-performance COPY.4 (guidelines/copy-move/
// COPY.4-rule-of-five-rule-of-zero.md) preserves owning values and implicit
// moves; renaming adds no wrapper, allocation or payload copy.

#pragma once
#include "bw/formats/resource_archive.hpp"
#include "bw/resources/catalog.hpp"
