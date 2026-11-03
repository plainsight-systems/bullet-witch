#ifndef BW_RENDERING_RHI_BUILD_HPP
#define BW_RENDERING_RHI_BUILD_HPP

// Responsibility: the one configuration switch of the RHI
// (docs/architecture/rhi.md, invariant 10: debug names, markers and the
// validation layer exist on every backend and compile out of shipping
// builds).
//
// Contract: BW_RHI_DEBUG is defined by the RHI module's CMake on its header
// interface target, as 1 for the Debug and RelWithDebInfo configurations and
// 0 for Release and MinSizeRel; a translation unit that includes an RHI
// header without it does not compile. With BW_RHI_DEBUG 0: DebugName holds
// no bytes, CommandList::begin_marker and end_marker are inline empty
// functions that record nothing, the validation layer's declarations
// (backend.hpp, wrap_with_validation) do not exist, and the pipeline
// description cache treats a miss as a named error (device.hpp). The absence
// is proven by a configure-time compile probe
// (tests/unit/rendering/rhi/CMakeLists.txt) that
// references the validation factory under BW_RHI_DEBUG=0 and must fail to
// compile naming it.
//
// Unreal f45729c0b6f097038e72b52d366a62baeca3275b
// Engine/Source/Runtime/Core/Public/Misc/Build.h:474-476:
// ENABLE_RHI_VALIDATION defaults to (UE_BUILD_DEBUG || UE_BUILD_DEVELOPMENT);
// Engine/Source/Runtime/RHI/Public/RHIDefinitions.h:48-54:
// RHI_USE_RESOURCE_DEBUG_NAME is 0 in Shipping and Test, else 1;
// Engine/Source/Runtime/RHI/Public/RHIBreadcrumbs.h:25-33: WITH_RHI_BREADCRUMBS
// gates the markers. Followed as one switch instead of three: the game has
// two configurations, development and shipping, and the three gates never
// differ between them.
//
// Core guidelines read: I.1 (the configuration is an explicit definition the
// build passes, never a default a header invents). Performance guidelines
// read: MEM.10 (debugging surface gated behind a build flag so shipping pays
// nothing).
#if !defined(BW_RHI_DEBUG)
#error "BW_RHI_DEBUG must be defined to 0 or 1 by the RHI module's CMake"
#endif
#if BW_RHI_DEBUG != 0 && BW_RHI_DEBUG != 1
#error "BW_RHI_DEBUG must be 0 or 1"
#endif

#endif
