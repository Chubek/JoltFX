# MemTKX is header-only. Stage its headers so the upstream checkout is never
# modified. These narrowly checked fixes make the free list failure-atomic and
# make reclamation allocation-free, which a C free callback must guarantee.
set(MemTKX_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(MemTKX_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
set(MemTKX_ENABLE_WARNINGS OFF CACHE BOOL "" FORCE)
if(NOT EXISTS "${PROJECT_SOURCE_DIR}/third_party/memtkx/include/memtkx/space/free_list.hpp")
    message(FATAL_ERROR "JoltFX requires the MemTKX source checkout at third_party/memtkx")
endif()
add_subdirectory("${PROJECT_SOURCE_DIR}/third_party/memtkx" "${PROJECT_BINARY_DIR}/third_party/memtkx" EXCLUDE_FROM_ALL)
set(JFX_MEMTKX_INCLUDE "${CMAKE_CURRENT_BINARY_DIR}/memtkx-include")
file(COPY "${PROJECT_SOURCE_DIR}/third_party/memtkx/include/memtkx" DESTINATION "${JFX_MEMTKX_INCLUDE}")
set(source "${PROJECT_SOURCE_DIR}/third_party/memtkx/include/memtkx/space/free_list.hpp")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${source}")
file(READ "${source}" header)
function(jfx_memtkx_fix before after)
    string(FIND "${header}" "${before}" found)
    if(found EQUAL -1)
        message(FATAL_ERROR "MemTKX free-list contract changed; review cmake/MemTKX.cmake before upgrading")
    endif()
    string(REPLACE "${before}" "${after}" header "${header}")
    set(header "${header}" PARENT_SCOPE)
endfunction()
jfx_memtkx_fix("    cells_.clear();" "    if (end > start) cells_.reserve(1);\n    cells_.clear();\n    live_allocations_ = 0;")
jfx_memtkx_fix("      if (padding + bytes > cell.size) {" "      if (bytes > cell.size - padding) {")
jfx_memtkx_fix("      cells_.erase(cells_.begin() + static_cast<std::ptrdiff_t>(i));" [=[      // Reserve for splits and every outstanding allocation's future free.
      // No mutation occurs until this potentially throwing operation succeeds.
      if (cells_.size() > cells_.max_size() - 2 ||
          live_allocations_ > cells_.max_size() - cells_.size() - 2) {
        return AllocResult<Address>::from_err(AllocError::OutOfMemory);
      }
      const std::size_t required = cells_.size() + live_allocations_ + 2;
      if (required > cells_.capacity()) {
        const std::size_t growth = cells_.capacity() <= cells_.max_size() / 2
            ? cells_.capacity() * 2 : cells_.max_size();
        cells_.reserve(std::max(required, growth));
      }
      cells_.erase(cells_.begin() + static_cast<std::ptrdiff_t>(i));]=])
jfx_memtkx_fix("      ++allocations_;" "      ++allocations_;\n      ++live_allocations_;")
jfx_memtkx_fix([=[    cells_.push_back(FreeCell{address, bytes});
    std::sort(cells_.begin(), cells_.end(),
              [](const FreeCell& lhs, const FreeCell& rhs) {
                return lhs.address < rhs.address;
              });
    coalesce();]=] [=[    // Cells remain sorted. Validate only the adjacent ranges, and merge
    // without a whole-list sort or any free-time metadata allocation.
    auto at = std::lower_bound(cells_.begin(), cells_.end(), address,
        [](const FreeCell& cell, Address key) { return cell.address < key; });
    if ((at != cells_.end() && at->address < address + bytes) ||
        (at != cells_.begin() && (at - 1)->end() > address) ||
        !live_allocations_ || cells_.size() == cells_.capacity()) return false;
    if (at != cells_.begin() && (at - 1)->end() == address) {
      auto previous = at - 1;
      previous->size += bytes;
      if (at != cells_.end() && previous->end() == at->address) {
        previous->size += at->size;
        cells_.erase(at);
      }
    } else if (at != cells_.end() && address + bytes == at->address) {
      at->address = address;
      at->size += bytes;
    } else {
      cells_.insert(at, FreeCell{address, bytes});
    }
    --live_allocations_;]=])
# Splitting a coalesced cell preserves sort order and cannot create adjacent
# free cells. Avoid re-sorting/scanning the whole list on every allocation.
jfx_memtkx_fix([=[      cells_.erase(cells_.begin() + static_cast<std::ptrdiff_t>(i));
      if (padding != 0) {
        cells_.push_back(FreeCell{cell.address, padding});
      }

      const std::size_t remainder = cell.size - padding - bytes;
      if (remainder != 0) {
        cells_.push_back(
            FreeCell{object_address + bytes, remainder});
      }

      std::sort(cells_.begin(), cells_.end(),
                [](const FreeCell& lhs, const FreeCell& rhs) {
                  return lhs.address < rhs.address;
                });
      coalesce();]=] [=[      const std::size_t remainder = cell.size - padding - bytes;
      auto at = cells_.begin() + static_cast<std::ptrdiff_t>(i);
      if (padding != 0) {
        *at = FreeCell{cell.address, padding};
        if (remainder != 0) cells_.insert(at + 1, FreeCell{object_address + bytes, remainder});
      } else if (remainder != 0) {
        *at = FreeCell{object_address + bytes, remainder};
      } else {
        cells_.erase(at);
      }]=])
jfx_memtkx_fix([=[    std::vector<FreeCell> merged;
    merged.reserve(cells_.size());
    FreeCell current = cells_.front();
    for (std::size_t i = 1; i < cells_.size(); ++i) {
      const FreeCell next = cells_[i];
      if (current.end() == next.address) {
        current.size += next.size;
      } else {
        merged.push_back(current);
        current = next;
      }
    }
    merged.push_back(current);
    cells_ = std::move(merged);]=] [=[    std::size_t output = 0;
    for (std::size_t i = 1; i < cells_.size(); ++i) {
      if (cells_[output].end() == cells_[i].address) {
        cells_[output].size += cells_[i].size;
      } else {
        cells_[++output] = cells_[i];
      }
    }
    cells_.resize(output + 1);]=])
jfx_memtkx_fix("  std::size_t allocations_{0};" "  std::size_t live_allocations_{0};\n  std::size_t allocations_{0};")
file(WRITE "${JFX_MEMTKX_INCLUDE}/memtkx/space/free_list.hpp" "${header}")
# Use only the reviewed copy. A normal -I from the upstream interface takes
# precedence over an -isystem path, even when that path is marked BEFORE.
set_property(TARGET MemTKX PROPERTY INTERFACE_INCLUDE_DIRECTORIES
    "$<BUILD_INTERFACE:${JFX_MEMTKX_INCLUDE}>")
set_property(TARGET MemTKX PROPERTY SYSTEM TRUE)
