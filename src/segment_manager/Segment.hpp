#pragma once

#include "common/Config.hpp"
#include "Extent.hpp"
#include <cstdint>
#include <vector>
#include <algorithm>
#include <iostream>
#include <cassert>

namespace dbi {

class BufferManager;
class BufferFrame;

class Segment {
public:
   Segment(SegmentId id, BufferManager& bufferManager);
   virtual ~Segment() {}

   SegmentId getId() const {return id;}

   uint64_t getNumPages() const {return numPages;}

   /// Adds extend and updates the numPages count
   virtual void assignExtent(const Extent& extent); // New pages (these pages need to be initialized for proper use)
   virtual void restoreExtents(const std::vector<Extent>& alreadyUsedExtents); // Old pages (this means that this pages belonged to this segment before the restart of the database)

private:
   const SegmentId id;
   std::vector<Extent> extents;
   uint64_t numPages;

protected:
   /// Assumes internal address space (i.E. extents[0].begin + offset)
   BufferFrame& fixPage(uint64_t offset, bool exclusive) const;
   void unfixPage(BufferFrame& bufferFrame, bool dirty) const;
   /// Or use buffer manager to directly fix the page
   BufferManager& bufferManager;

public:
   /// Define PageID iterator -- use to iterate over all page ids of this segment
   /// Changing the extents of the segment potentially breaks the iterator
   class PageIDIterator : public std::iterator<std::random_access_iterator_tag, PageIDIterator> {
      uint32_t extent;
      PageId pageID;
      const std::vector<Extent>* extents;
      PageIDIterator(std::vector<Extent>& extents, PageId pageID) : extent(0), pageID(pageID), extents(&extents) {}
      void inc() {assert(pageID!=kInvalidPageID); pageID++; if(pageID>(*extents)[extent].end) {extent++; if(extent>extents->size()) pageID=kInvalidPageID; else pageID=(*extents)[extent].begin;}}
      friend class Segment;
   public:
      const PageIDIterator operator++() {PageIDIterator result=*this; inc(); return result;}
      const PageIDIterator& operator++(int) {inc(); return *this;}
      bool operator==(const PageIDIterator& other) const {return pageID==other.pageID;}
      bool operator!=(const PageIDIterator& other) const {return pageID!=other.pageID;}
      uint32_t operator-(const PageIDIterator& other) const {return pageID-other.pageID;}
      PageId operator*() const {return pageID;}
      PageIDIterator(const PageIDIterator& other) : extent(other.extent), pageID(other.pageID), extents(other.extents) {}
      PageIDIterator& operator=(const PageIDIterator& other) {extent=other.extent; pageID=other.pageID; extents=other.extents; return *this;}
   };

   PageIDIterator beginPageID() {return PageIDIterator(extents, extents.size()==0?kInvalidPageID:extents[0].begin);}
   PageIDIterator endPageID() {return PageIDIterator(extents, kInvalidPageID);}
};

}
