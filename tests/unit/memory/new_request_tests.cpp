// Unit test: the global operator new request policy in bw/memory/global_allocation.hpp.
#include "bw/memory/global_allocation.hpp"

#include <new>

using bw::memory::new_request;

static_assert(new_request(0).bytes == 1 && new_request(0).alignment == 8);
static_assert(new_request(1).alignment == 8);
static_assert(new_request(8).alignment == 8);
static_assert(new_request(9).alignment == __STDCPP_DEFAULT_NEW_ALIGNMENT__);
static_assert(new_request(17).bytes == 17);
static_assert(new_request(131073).alignment == __STDCPP_DEFAULT_NEW_ALIGNMENT__);
static_assert(new_request(0, std::align_val_t{256}).bytes == 1);
static_assert(new_request(0, std::align_val_t{256}).alignment == 256);
static_assert(new_request(4, std::align_val_t{64}).alignment == 64);
static_assert(new_request(4096, std::align_val_t{4096}).alignment == 4096);

int main() { return 0; }
