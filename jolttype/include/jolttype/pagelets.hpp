#ifndef JOLTTYPE_PAGELETS_HPP
#define JOLTTYPE_PAGELETS_HPP
#include <cstddef>

namespace jolttype {
// Read-only built-in catalog; pointers live for the lifetime of the process.
// Copy source into an inventory Pagelet asset to edit/package an instance's
// library. Several entries may share an asset_id (the original basic module).
struct BundledPagelet {
    const char *name, *category, *description, *asset_id, *source;
};
const BundledPagelet *bundled_pagelets(size_t &count) noexcept;
}
#endif
