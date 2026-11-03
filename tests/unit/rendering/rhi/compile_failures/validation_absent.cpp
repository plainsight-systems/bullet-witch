// Must not compile under BW_RHI_DEBUG=0. build.hpp:12-19: with BW_RHI_DEBUG 0
// the validation layer's declarations (backend.hpp, wrap_with_validation)
// do not exist. The diagnostic must name the factory.
#include "bw/rendering/rhi/backend.hpp"

inline constexpr auto probe_factory = &bw::rhi::wrap_with_validation;
