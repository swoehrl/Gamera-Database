#include "Persister.hpp"
#include "buffer_manager/BufferManager.hpp"
#include "ExtentStore.hpp"
#include "SlottedPage.hpp"

using namespace std;

namespace dbi {

Persister::Persister(BufferManager& bufferManager, ExtentStore& freePages)
: bufferManager(bufferManager)
, freePages(freePages)
{
}

void Persister::create()
{
   // Setup linked list structure on meta page
   auto& frame = bufferManager.fixPage(kMetaPageId, kExclusive);
   auto& sp = reinterpret_cast<SlottedPage&>(*frame.getData());
   sp.initialize();
   if(sp.insert(Record(reinterpret_cast<const char*>(&kMetaPageId), sizeof(PageId))) != 0) {
      assert(false&&"assuming record id zero for first insert");
      throw;
   }
   uint32_t freeBytes = sp.getBytesFreeForRecord();
   bufferManager.unfixPage(frame, kDirty);

   // Remember this page
   pages.push_back(PageReference{freeBytes, kMetaPageId});

   // Add remaining free pages to free page structure
   freePages.add(Extent {kFirstFreePageId, PageId(bufferManager.getNumDiscPages())});
}

void Persister::load(std::unordered_map<SegmentId, std::pair<TId, ExtentStore>>& segmentMap)
{
   freePages.add(Extent {kFirstFreePageId, PageId(bufferManager.getNumDiscPages())});
   PageId currentPageId = kMetaPageId;
   PageId nextPageId = kMetaPageId;

   // Load linked list structure
   do {
      // Load current page
      auto& frame = bufferManager.fixPage(kMetaPageId, kExclusive);
      auto& sp = reinterpret_cast<SlottedPage&>(*frame.getData());
      auto records = sp.getAllRecords(currentPageId);

      // Add to internal structure
      pages.push_back(PageReference{sp.getBytesFreeForRecord(), currentPageId});
      bufferManager.unfixPage(frame, kClean);

      // Parse all records
      for(auto& iter : records) {
         if(toRecordId(iter.first) == 0) {
            // Update page id
            nextPageId = *reinterpret_cast<const PageId*>(iter.second.data());
            assert(nextPageId == kMetaPageId);
         } else {
            // Otherwise it is a mapping entry
            pair<SegmentId, ExtentStore> mapping = unmarshall(iter.second);

            // Remove from free pages
            for(auto& extent : mapping.second.get())
               freePages.remove(extent);

            // Add to segment -> extent mapping
            segmentMap.insert(make_pair(mapping.first , make_pair(toTID(currentPageId, iter.first), move(mapping.second))));
         }
      }

      currentPageId = nextPageId;
   } while(nextPageId != kMetaPageId);
}

TId Persister::insert(SegmentId sid, const ExtentStore& extents)
{
   /// Find a nice spot and do the insert .. no magic here
   Record record = marshall(sid, extents);
   if(record.size() > SlottedPage::maximumRecordSize() - 12) { // need 12 byte for linked list structure
      assert(false&&"There is a limit on the record size");
      throw;
   }

   for(auto& page : pages) {
      if(page.freeBytes >= record.size()) {
         auto& frame = bufferManager.fixPage(page.pid, kExclusive);
         auto& sp = reinterpret_cast<SlottedPage&>(*frame.getData());
         assert(page.freeBytes == sp.getBytesFreeForRecord());
         RecordId rid = sp.insert(record);
         page.freeBytes = sp.getBytesFreeForRecord();
         bufferManager.unfixPage(frame, kDirty);
         return toTID(page.pid, rid);
      }
   }

   cout << "grow" << endl;

   // Otherwise structure is full => find new page
   assert(freePages.numPages() != 0);
   Extent singlePage(freePages.get()[0].begin(), freePages.get()[0].begin()+1);
   freePages.remove(singlePage);
   PageReference newPage{0, singlePage.begin()};

   // Add new page to linked list
   auto& lastElementInList = bufferManager.fixPage(pages.back().pid, kExclusive);
   auto& lastSp = reinterpret_cast<SlottedPage&>(*lastElementInList.getData());
   if(!lastSp.tryInPageUpdate(0, Record(reinterpret_cast<const char*>(&newPage.pid), sizeof(PageId))))
      throw; // Needs to work, because record has the same size
   bufferManager.unfixPage(lastElementInList, kDirty);

   // Setup new page
   auto& newFrame = bufferManager.fixPage(newPage.pid, kExclusive);
   auto& newSp = reinterpret_cast<SlottedPage&>(*newFrame.getData());
   newSp.initialize();
   if(newSp.insert(Record(reinterpret_cast<const char*>(&kMetaPageId), sizeof(PageId))) != 0)
      throw; // assuming record id zero for first insert

   // Insert record into new page
   RecordId rid = newSp.insert(record);
   newPage.freeBytes = newSp.getBytesFreeForRecord();
   bufferManager.unfixPage(newFrame, kDirty);

   // Remember the new page
   pages.push_back(newPage);

   return toTID(newPage.pid, rid);
}

TId Persister::update(TId tid, SegmentId sid, const ExtentStore& extents)
{
   remove(tid); // TODO optimize
   return insert(sid, extents);
}

void Persister::remove(TId tid)
{
   /// Find the corresponding page and remove .. no magic here either
   auto pid = toPageId(tid);
   for(auto& page : pages) {
      if(page.pid == pid) {
         auto& frame = bufferManager.fixPage(page.pid, kExclusive);
         auto& sp = reinterpret_cast<SlottedPage&>(*frame.getData());
         sp.remove(toRecordId(tid));
         page.freeBytes = sp.getBytesFreeForRecord();
         bufferManager.unfixPage(frame, kDirty);
         return;
      }
   }

   assert(false&&"removing unknown page");
   throw;
}

Record Persister::marshall(SegmentId sid, const ExtentStore& extents)
{
   vector<char> data(sizeof(SegmentId) + extents.get().size() * sizeof(Extent));
   memcpy(data.data(), &sid, sizeof(SegmentId));
   memcpy(data.data() + sizeof(SegmentId), extents.get().data(), extents.get().size() * sizeof(Extent));
   return Record(move(data));
}

pair<SegmentId, ExtentStore> Persister::unmarshall(const Record& record) // TODO: improve .. but don't chance Extent class if possible .. 
{
   SegmentId sid;
   memcpy(&sid, record.data(), sizeof(SegmentId));

   vector<char> extentsRaw(record.size()-sizeof(SegmentId));
   memcpy(extentsRaw.data(), record.data() + sizeof(SegmentId), extentsRaw.size());

   ExtentStore extents;
   for(Extent* extent=reinterpret_cast<Extent*>(extentsRaw.data()); extent!=reinterpret_cast<Extent*>(extentsRaw.data()+extentsRaw.size()); extent++)
      extents.add(*extent);

   return make_pair(sid, move(extents));
}

}
