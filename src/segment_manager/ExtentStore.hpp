#pragma once

#include "Extent.hpp"
#include <vector>

namespace dbi {

/// Takes care of storing extents. This version will preserve the order in which the extents were added:
/// [3,4[ and [4,5[ => [3,5[ works, but [4,5[ and [3,4[ does not.
/// Illegal instructs will be prevented:
/// -> Its not OK to add [50,100[ to [1,100[
class ExtentStore {
public:
   /// Constructor and move operations -- the rest is default
   ExtentStore();
   ExtentStore(ExtentStore&& other);
   const ExtentStore& operator=(ExtentStore&& other);

   /// Merge given extent into existing ones
   void add(const Extent& extent);
   /// Access the ordered extents
   const std::vector<Extent>& get() const;
   /// Sum of all extents
   uint64_t numPages() const;

private:
   uint64_t pageCount = 0;
   std::vector<Extent> extents;
};

}
