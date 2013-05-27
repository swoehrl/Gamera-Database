#include "FunkeSlottedTest.hpp"
#include "util/Utility.hpp"
#include "common/Config.hpp"
#include "buffer_manager/BufferManager.hpp"
#include "segment_manager/SegmentManager.hpp"
#include "segment_manager/SPSegment.hpp"
#include "gtest/gtest.h"
#include "segment_manager/Record.hpp"
#include "operator/TableScanOperator.hpp"
#include <array>
#include <fstream>
#include <string>
#include <set>

using namespace std;
using namespace dbi;

TEST(SPSegment, SPSegmentSimple)
{
   const string fileName = "swap_file";
   const uint32_t pages = 100;

   // Create
   ASSERT_TRUE(util::createFile(fileName, pages * kPageSize));
   BufferManager bufferManager(fileName, pages / 2);
   SegmentManager segmentManager(bufferManager, true);
   SegmentId id = segmentManager.createSegment(SegmentType::SP, 10);
   SPSegment& segment = segmentManager.getSPSegment(id);

   // Insert and look up
   string data = "the clown is down";
   Record record(data);
   TId tid = segment.insert(record);
   ASSERT_EQ(record, segment.lookup(tid));

   // Update existing page with value not longer than existing
   string updatedData = "the clown is ?";
   Record updatedRecord(updatedData);
   segment.update(tid, updatedRecord);
   ASSERT_EQ(updatedRecord, segment.lookup(tid));

   // Update existing page with value longer than existing
   string longerUpdatedData = "the clown was revived";
   Record longerUpdatedRecord(longerUpdatedData);
   segment.update(tid, longerUpdatedRecord);
   ASSERT_EQ(longerUpdatedRecord, segment.lookup(tid));

   // TODO: update with value which must be placed on another page

   // Remove created page
   segment.remove(tid);

   remove(fileName.c_str());
}

TEST(SPSegment, SPSegmentManyPageUpdate)
{
   const string fileName = "swap_file";
   const uint32_t pages = 100;
   const Record smallRecord = Record("the tree of liberty must be refreshed from time to time with the blood of patriots and tyrants. it is it's natural manure.");
   const Record bigRecord1 = Record(string(kPageSize/2, 'a'));
   const Record bigRecord2 = Record(string(kPageSize/2, 'b'));
   const Record bigRecord3 = Record(string(kPageSize/2, 'c'));
   const Record memdiumRecord = Record(string(kPageSize/4, 'd'));

   // Create
   ASSERT_TRUE(util::createFile(fileName, pages * kPageSize));
   BufferManager bufferManager(fileName, pages / 2);
   SegmentManager segmentManager(bufferManager, true);
   SegmentId id = segmentManager.createSegment(SegmentType::SP, 10);
   SPSegment& segment = segmentManager.getSPSegment(id);

   // Trigger local update: p1:[1=small, 2=big] p2:[] p3:[]
   TId tid1 = segment.insert(smallRecord);
   TId tid2 = segment.insert(smallRecord);
   segment.update(tid2, bigRecord2);

   // Trigger a non page local update: p1:[2=big] p2:[1=big] p3:[]
   segment.update(tid1, bigRecord1);
   ASSERT_EQ(bigRecord1, segment.lookup(tid1));
   ASSERT_EQ(bigRecord2, segment.lookup(tid2));

   // Trigger update on reference value: p1:[2=big] p2:[1=big] p3:[]
   segment.update(tid1, bigRecord3);
   ASSERT_EQ(bigRecord3, segment.lookup(tid1));
   ASSERT_EQ(bigRecord2, segment.lookup(tid2));

   // Trigger update on reference value with overflow: p1:[2=big, 3=medium] p2:[1=big] p3:[4=big]
   TId tid3 = segment.insert(memdiumRecord);
   TId tid4 = segment.insert(smallRecord);
   segment.update(tid4, memdiumRecord);
   segment.update(tid4, bigRecord3);
   ASSERT_EQ(bigRecord3, segment.lookup(tid4));

   // Trigger collapse reference: p1:[2=big, 3=medium, 4=small] p2:[1=big] p3:[]
   segment.update(tid4, smallRecord);
   ASSERT_EQ(bigRecord3, segment.lookup(tid1));
   ASSERT_EQ(bigRecord2, segment.lookup(tid2));
   ASSERT_EQ(memdiumRecord, segment.lookup(tid3));
   ASSERT_EQ(smallRecord, segment.lookup(tid4));

   // Remove everything
   segment.remove(tid1);
   segment.remove(tid2);
   segment.remove(tid3);
   segment.remove(tid4);

   // Check that page is empty
   TableScanOperator scanner(segment);
   scanner.open();
   ASSERT_TRUE(!scanner.next());
   scanner.close();
}

TEST(SPSegment, Randomized)
{
   const uint32_t kTestScale = 1;
   const uint32_t kIterations = 10000;
   const string kFileName = "swap_file";
   const uint32_t kPages = 1000;
   const uint32_t kMaxWordSize = 512;
   ASSERT_TRUE(util::createFile(kFileName, kPages * kPageSize));

   for(uint32_t j=0; j<kTestScale; j++) {
      /// Create structure
      BufferManager bufferManager(kFileName, kPages / 2);
      auto segmentManager = util::make_unique<SegmentManager>(bufferManager, true);
      SegmentId id = segmentManager->createSegment(SegmentType::SP, 10);
      SPSegment* segment = &segmentManager->getSPSegment(id);
      unordered_map<TId, string> reference;

      // Add some initial data
      for(uint32_t i=0; i<kPageSize/3/32; i++) {
         string data = util::randomWord(8, kMaxWordSize);
         TId id = segment->insert(Record(data));
         ASSERT_TRUE(reference.count(id) == 0);
         reference.insert(make_pair(id, data));
         // cout << "initial insert " << id << " -> " << data << endl;
      }

     // Work on it
     for(uint32_t i=0; i<kIterations; i++) {
         int32_t operation = random() % 100;

         // Do insert
         if(operation <= 40) {
            string data = util::randomWord(8, kMaxWordSize);
            TId id = segment->insert(Record(data));
            ASSERT_TRUE(reference.count(id) == 0);
            reference.insert(make_pair(id, data));
            // cout << "insert " << id << " -> " << data << endl;
         }

         // Do remove
         else if(operation <= 60) {
            if(reference.empty())
               continue;
            auto iter = reference.begin();
            advance(iter, random()%reference.size());
            TId id = iter->first;
            Record record = segment->lookup(id);
            ASSERT_EQ(string(record.data(), record.size()), iter->second);
            segment->remove(id);
            reference.erase(iter);
            // cout << "remove " << id << endl;
         }

         // Do update
         else if(operation <= 97) {
            if(reference.empty())
               continue;
            auto iter = reference.begin();
            advance(iter, random()%reference.size());
            TId id = iter->first;
            ASSERT_EQ(string(segment->lookup(id).data(), segment->lookup(id).size()), iter->second);
            string data = util::randomWord(kMaxWordSize, 4*kMaxWordSize);
            segment->update(id, Record(data));
            ASSERT_EQ(string(segment->lookup(id).data(), segment->lookup(id).size()), data);
            reference.erase(iter);
            reference.insert(make_pair(id, data));
            // cout << "update " << id << " to " << data << endl;
         }

         // Do restart of the database
         else if(operation<=98) {
            segmentManager = util::make_unique<SegmentManager>(bufferManager, false);
            segment = &segmentManager->getSPSegment(id);
         }

         // Do consistency check
         else if(operation<=99 || i==kIterations-1 || i==0) {
            // Do scan empty
            dbi::TableScanOperator scanner(*segment);
            scanner.open();
            while(scanner.next()) {
               const std::pair<dbi::TId, dbi::Record>& record = scanner.getOutput();
               ASSERT_TRUE(reference.count(record.first) > 0);
               ASSERT_EQ(string(record.second.data(), record.second.size()), reference.find(record.first)->second);
            }
            scanner.close();
         }
      }
   }
}
