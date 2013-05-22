#include <stdint.h>
#include "gtest/gtest.h"
#include "segment_manager/SlottedPage.hpp"
#include "common/Config.hpp"
#include <cstdlib>

TEST(SlottedPage, SlotReuseAfterDelete){
    dbi::SlottedPage* slottedPage = static_cast<dbi::SlottedPage*>(malloc(dbi::kPageSize));
    slottedPage->initialize();
    
    std::string data = "Hello World!";
    dbi::Record dataRecord(data);
    
    dbi::RecordId dataRecordId1 = slottedPage->insert(dataRecord);
    ASSERT_TRUE(slottedPage->remove(dataRecordId1));
    dbi::RecordId dataRecordId2 = slottedPage->insert(dataRecord);
    ASSERT_EQ(dataRecordId1, dataRecordId2);
    
    free(slottedPage);
}

TEST(SlottedPage, DefragmentationBasic){
    
    std::string fragmentationData1 = "Hello!";
    std::string fragmentationData2 = "World!";
    std::string staticData = "block";
    std::string newData = "Hello World!";
    dbi::Record fragmentationRecord1(fragmentationData1);
    dbi::Record fragmentationRecord2(fragmentationData2);
    dbi::Record staticRecord(staticData);
    dbi::Record newDataRecord(newData);
    
    dbi::SlottedPage* slottedPage = static_cast<dbi::SlottedPage*>(malloc(dbi::kPageSize));
    slottedPage->initialize();
    
    dbi::RecordId fragmentationRecordId1 = slottedPage->insert(fragmentationRecord1);    
    dbi::RecordId staticRecordId = slottedPage->insert(staticRecord);  
    dbi::RecordId fragmentationRecordId2 = slottedPage->insert(fragmentationRecord2);
    
    // Fill page with ... CRAP!
    while(slottedPage->getBytesFreeForRecord() >= staticData.length()){
        slottedPage->insert(dbi::Record(staticData));
    }
    
    uint16_t freeSpaceBeforeNewDataInsert = slottedPage->getBytesFreeForRecord();
    ASSERT_TRUE(slottedPage->getBytesFreeForRecord() < newData.length());  
    ASSERT_TRUE(slottedPage->remove(fragmentationRecordId1));
    ASSERT_TRUE(slottedPage->remove(fragmentationRecordId2));
    ASSERT_TRUE(slottedPage->getBytesFreeForRecord() >= newData.length());
    
    // The page should now have a structure like: <freeSpace> <staticData> <freeSpace> (whereby both free space areas are to short for the new record)    
    slottedPage->defragment();
    
    // No side effects on sample record
    ASSERT_EQ(slottedPage->lookup(staticRecordId).data(), staticData);
    //ASSERT_EQ(slottedPage->getBytesFreeForRecord(), newData.length() + freeSpaceBeforeNewDataInsert);
    
    
    ASSERT_TRUE(slottedPage->getBytesFreeForRecord() >= newData.length());
    dbi::RecordId newDataRecordId = slottedPage->insert(newDataRecord);
    ASSERT_EQ(slottedPage->lookup(newDataRecordId).data(), newData);
    // New record is as long as the two removed ones
    ASSERT_EQ(freeSpaceBeforeNewDataInsert, slottedPage->getBytesFreeForRecord());
    
    free(slottedPage);
}

