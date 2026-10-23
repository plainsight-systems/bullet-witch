// Mac target profile location selection. Resolve the user's Application Support
// directory through Foundation, append the application namespace, and return an
// owned path. Does not create or inspect files. Throws if OS gives no location.
// Main-thread startup only, O(path length). Design/implementation: cpp-guidelines
// R.1 (#rr-raii) uses autorelease scope/owned path; cpp-performance COPY.4
// (guidelines/copy-move/COPY.4-rule-of-five-rule-of-zero.md) returns an owning value.
#import <Foundation/Foundation.h>
#include <stdexcept>
#include "bw/platform/save_storage.hpp"
namespace bw::platform {
std::filesystem::path default_save_directory() {
  @autoreleasepool {
    NSArray<NSURL*>* locations =
        [[NSFileManager defaultManager] URLsForDirectory:NSApplicationSupportDirectory
                                               inDomains:NSUserDomainMask];
    NSURL* location = [locations firstObject];
    if (location == nil) {
      throw std::runtime_error("Application Support directory unavailable");
    }
    return std::filesystem::path([[location path] fileSystemRepresentation]) /
           "Plainsight Systems" / "Bullet Witch";
  }
}
}  // namespace bw::platform
