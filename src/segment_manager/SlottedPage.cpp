#include "SlottedPage.hpp"
#include "common/Config.hpp"
#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <iostream>
#include <stdint.h>

using namespace std;

namespace dbi {

void SlottedPage::initialize()
{
   assert(kPageSize < (1 << 16));
   assert(sizeof(SlottedPage) == kPageSize);
   LSN = -1;
   slotCount = 0;
   firstFreeSlot = 0;
   dataBegin = data.size(); // data.data() + dataBegin == first byte of used data
   freeBytes = data.size();
}

RecordId SlottedPage::insert(const Record& record)
{
   Slot* slot = aquireSlot(record.size());
   *slot = Slot(slot->getOffset(), record.size(), Slot::Type::kNormal);
   memcpy(data.data()+slot->getOffset(), record.data(), slot->getLength());
   return slot - slotBegin();
}

pair<TId, Record> SlottedPage::lookup(RecordId id) const
{
   const Slot* result = slotBegin() + id;
   assert(toRecordId(id) < slotCount);
   assert(result->getOffset() != 0);
   assert(result->getLength() > 0);

   // Record was originally on another page => the first 8 byte of the TID
   if(result->isRedirectedFromOtherPage()) {
     assert(result->getLength() > sizeof(TId));
     return make_pair(kInvalidTupleID, Record(data.data()+result->getOffset()+sizeof(TId), result->getLength()-sizeof(TId)));
   }

   // Actual record is on another page => the record contains the id
   if(result->isRedirectedToOtherPage()) {
      assert(result->getLength() == sizeof(TId));
      return make_pair(*reinterpret_cast<const TId*>(data.data()+result->getOffset()), Record(data.data()+result->getOffset(), result->getLength()));
   }

   // Normal record
   return make_pair(kInvalidTupleID, Record(data.data()+result->getOffset(), result->getLength()));
}

void SlottedPage::remove(RecordId rId)
{
   assert(dataBegin >= sizeof(Slot) * slotCount);
   assert(rId < slotCount);
   uint32_t count = countAllRecords();

   Slot* target = slotBegin() + rId;
   // Check if tId leads to valid slot
   assert(target->getOffset() != 0);
   assert(target->getLength() != 0);
   freeBytes += target->getLength();
   *target = Slot(target->getOffset(), target->getLength(), Slot::Type::kUnusedMemory);
   firstFreeSlot = min(firstFreeSlot, rId);

   assert(countAllRecords() == count - 1);
   assert(dataBegin >= sizeof(Slot) * slotCount);
}

void SlottedPage::update(RecordId recordId, const Record& newRecord)
{
   // Get and check everything .. *pretty paranoid*
   Slot* slot = slotBegin() + recordId;
   assert(canUpdateRecord(recordId, newRecord));
   assert(dataBegin >= sizeof(Slot) * slotCount);

   // In place update -- Re-use old space if new data fit into it
   if(newRecord.size() <= slot->getLength()) {
      memcpy(data.data() + slot->getOffset(), newRecord.data(), newRecord.size());
      freeBytes += slot->getLength();
      freeBytes -= newRecord.size();
      slot->setLength(newRecord.size());
      return;
   }

   // Remove record (so that de-fragment can recycle it) (keep slot .. does not matter)
   freeBytes += slot->getLength();
   *slot = Slot(0, 0, Slot::Type::kNormal);

   // Ensure there is enough
   if(dataBegin - sizeof(Slot) * slotCount < newRecord.size())
      defragment();
   assert(dataBegin - sizeof(Slot) * slotCount >= newRecord.size());

   // Insert it
   freeBytes -= newRecord.size();
   dataBegin -= newRecord.size();
   *slot = Slot(dataBegin, newRecord.size(), Slot::Type::kNormal);
   memcpy(data.data()+slot->getOffset(), newRecord.data(), slot->getLength());
}

void SlottedPage::updateToReference(RecordId, TId)
{
  throw;
}

RecordId SlottedPage::insertForeigner(const Record&, TId)
{
  throw;
}

bool SlottedPage::canHoldRecord(const Record& record) const
{
  return getBytesFreeForRecord() >= record.size();
}

bool SlottedPage::canUpdateRecord(RecordId rid, const Record& newRecord) const
{
  const Slot* result = slotBegin() + rid;
  assert(toRecordId(rid) < slotCount);
  assert(result->getOffset() != 0);
  assert(result->getLength() > 0);
  return freeBytes + result->getLength() >= newRecord.size();
}

vector<pair<TId, Record>> SlottedPage::getAllRecords(PageId thisPageId) const
{
  // Find all slots with data
  vector<pair<TId, Record>> result;
  result.reserve(slotCount);
  for(auto slot = slotBegin(); slot != slotEnd(); slot++) {
    // Normal record
    if(slot->isNormal())
      result.emplace_back(make_pair(toTID(thisPageId, slot-slotBegin()), Record(data.data() + slot->getOffset(), slot->getLength())));
    // Strip TID from foreign records
    if(slot->isRedirectedFromOtherPage())
      result.emplace_back(make_pair(*reinterpret_cast<const TId*>(data.data()+slot->getOffset()), Record(data.data()+slot->getOffset(), slot->getLength())));
    // Skip references
  }
  return result;
}

uint16_t SlottedPage::getBytesFreeForRecord() const
{
  // Update first free slot
  for(;firstFreeSlot!=slotCount; firstFreeSlot++)
    if((slotBegin()+firstFreeSlot)->getOffset() == 0 || (slotBegin()+firstFreeSlot)->isMemoryUnused())
      break;

  if(firstFreeSlot != slotCount)
    return freeBytes; else
    return freeBytes > sizeof(Slot) ? freeBytes - sizeof(Slot) : 0;
}

uint16_t SlottedPage::maximumRecordSize()
{
  return kPageSize - 16 - 4;
}

uint32_t SlottedPage::countAllRecords() const
{
  uint32_t result = 0;
  for(auto slot = slotBegin(); slot != slotEnd(); slot++)
    if(!slot->isEmpty() && !slot->isMemoryUnused())
      result++;
  return result;
}

/// Defragments the page. Any slot with a record length <= 0 (deleted/ not used) is set to its initial state during the process. NOT THREADSAFE!
void SlottedPage::defragment()
{
    uint32_t count = countAllRecords();
    vector<uint16_t> slotIndices(slotCount);
    std::iota(slotIndices.begin(), slotIndices.end(), 0);
    // Sort indices so that slot with greatest offset is first entry
    std::sort(slotIndices.begin(), slotIndices.end(), [this](uint16_t x, uint16_t y){ return (slotBegin() + x)->getOffset() > (slotBegin() + y)->getOffset(); });

    // Start at max index of data
    dataBegin = data.size();
    for(uint16_t slotIndex : slotIndices) {
        Slot* currentSlot = slotBegin() + slotIndex;
        if(currentSlot->isMemoryUnused() || currentSlot->getOffset() == 0) {
            // Deleted/ not used records can be ignored; Only reset its slot
            *currentSlot = Slot(0, 0, Slot::Type::kNormal);
            firstFreeSlot = min(firstFreeSlot, slotIndex);
        } else {
            assert(currentSlot->getOffset() > 0);
            assert(currentSlot->getLength() > 0);

            // Move the entry 
            uint16_t newOffset = dataBegin - currentSlot->getLength();
            std::memmove(data.data() + newOffset, data.data() + currentSlot->getOffset(), currentSlot->getLength());
            currentSlot->setOffset(newOffset);

            // Advance (towards beginning) in index by length of currently processed record
            dataBegin -= currentSlot->getLength();
            assert(dataBegin >= sizeof(Slot) * slotCount);
        }
    }
    assert(isValid());
    assert(countAllRecords() == count);
}

void SlottedPage::dump() const
{
  for(const Slot* slot=slotBegin(); slot!=slotEnd(); slot++)
    cout << "slot: (" << slot->getOffset() << ", " << slot->getLength() << ") " << slot->isNormal() << " " << slot->isMemoryUnused() << " " << slot->isRedirectedFromOtherPage() << " " << slot->isRedirectedToOtherPage() << " " << slot->isEmpty() << endl;
  cout << "LSN: " << LSN << "  ";
  cout << "slotCount: " << slotCount << "  ";
  cout << "firstFreeSlot: " << firstFreeSlot << "  ";
  cout << "dataBegin: " << dataBegin << "  ";
  cout << "freeBytes: " << freeBytes << "  " << endl;
}

bool SlottedPage::isValid() const
{
  uint32_t usedBytes = 0;
  for(const Slot* slot=slotBegin(); slot!=slotEnd(); slot++)
    if(slot->isNormal() || slot->isRedirectedFromOtherPage() || slot->isRedirectedToOtherPage())
      usedBytes += slot->getLength();
  return usedBytes + slotCount*sizeof(Slot) + freeBytes + 16 == kPageSize;
}

Slot* SlottedPage::aquireSlot(uint16_t length)
{
   assert(getBytesFreeForRecord() >= length);
   assert(dataBegin >= sizeof(Slot) * slotCount);
   assert(length > 0);
   uint32_t count = countAllRecords();

   // Try to reuse old slot including its space
   for(Slot* slot=slotBegin() + firstFreeSlot; slot!=slotEnd(); slot++)
      if(slot->isMemoryUnused() && slot->getLength() >= length) {
         freeBytes -= length;
         *slot = Slot(slot->getOffset(), length, Slot::Type::kNormal);
         assert(countAllRecords() == count+1);
         assert(dataBegin >= sizeof(Slot) * slotCount);
         return slot;
      }

   // Otherwise: We have to insert it into the free space: ensure there is enough
   if(dataBegin - sizeof(Slot) * slotCount < length + sizeof(Slot))
      defragment();
   assert(dataBegin - sizeof(Slot) * slotCount >= length);

   // Now we know that the new record will fit into the free space: find a slot
   Slot* slot;
   for(slot = slotBegin() + firstFreeSlot; slot != slotEnd(); slot++)
      if(slot->getOffset() == 0)
         break;

   // The slot variable points to the slot to be used
   dataBegin -= length;
   *slot = Slot(dataBegin, length, Slot::Type::kNormal);
   freeBytes -= length;
   freeBytes -= slotEnd() == slot ? sizeof(Slot) : 0;
   slotCount = max(slotCount, static_cast<uint16_t>(1 + slot - slotBegin()));
   firstFreeSlot = slot - slotBegin(); // No free slot before this one

   assert(countAllRecords() == count+1);
   assert(dataBegin >= sizeof(Slot) * slotCount);
   return slot;
}

const Slot* SlottedPage::slotBegin() const
{
  return reinterpret_cast<const Slot*>(data.data());
}

const Slot* SlottedPage::slotEnd() const
{
  return reinterpret_cast<const Slot*>(data.data()) + slotCount;
}

Slot* SlottedPage::slotBegin()
{
  return reinterpret_cast<Slot*>(data.data());
}

Slot* SlottedPage::slotEnd()
{
  return reinterpret_cast<Slot*>(data.data()) + slotCount;
}

}
