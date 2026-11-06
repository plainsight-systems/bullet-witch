#include "bw/content/cooked_asset.hpp"

#include "blake3.h"

#include <cstddef>
#include <span>

namespace bw::content {

Hash hash_bytes(std::span<const std::byte> bytes) noexcept {
  blake3_hasher hasher;
  blake3_hasher_init(&hasher);
  blake3_hasher_update(&hasher, bytes.data(), bytes.size());
  Hash hash{};
  blake3_hasher_finalize(&hasher, hash.data(), hash.size());
  return hash;
}

CookedError verify_payload_hash(std::span<const std::byte> file,
                                const CookedHeader& header) noexcept {
  if (hash_bytes(cooked_payload(file, header)) != header.payload_hash) {
    return CookedError::PayloadHashMismatch;
  }
  return CookedError::None;
}

} // namespace bw::content
