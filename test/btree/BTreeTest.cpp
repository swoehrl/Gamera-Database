#include "btree/BTree.hpp"
#include "btree/IndexKey.hpp"
#include "btree/IndexKeyComparator.hpp"
#include "btree/IndexKeySchema.hpp"
#include "harriet/VariableType.hpp"
#include "harriet/Value.hpp"
#include "buffer_manager/BufferManager.hpp"
#include "common/Config.hpp"
#include "segment_manager/BTreeSegment.hpp"
#include "segment_manager/SegmentManager.hpp"
#include "test/TestConfig.hpp"
#include "util/Utility.hpp"
#include <cassert>
#include <cstdint>
#include <gtest/gtest.h>
#include <sstream>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <thread>
#include <vector>
#include <utility>
#include "btree/Algorithms.hpp"

typedef dbi::TID TID;

const int CHARSIZE = 20;


struct MyCompare{
    bool less(int a, int b) {
        return a < b;
    }
    bool equal(int a, int b) {
        return a == b;
    }
};


TEST(SearchTest, Array) {
    typedef std::array<int, 21> A21;
    typedef std::array<int, 1> A1;
    typedef std::array<int, 2> A2;
    MyCompare c;
    A21 a{2, 3, 5, 6, 7, 8, 10, 11, 13, 14, 18, 20, 22, 25, 26, 27, 33, 50, 100, 112, 130};
    A21::iterator it = dbi::search(a.begin(), a.end(), c, 8);
    ASSERT_TRUE(it != a.end());
    it = dbi::search(a.begin(), a.end(), c, 22);
    ASSERT_TRUE(it != a.end());
    it = dbi::search(a.begin(), a.end(), c, 200);
    ASSERT_TRUE(it == a.end());
    it = dbi::search(a.begin(), a.end(), c, 1);
    ASSERT_TRUE(it == a.end());
    it = dbi::search(a.begin(), a.end(), c, 19);
    ASSERT_TRUE(it == a.end());
    
    A1 a1{0};
    A1::iterator it1 = dbi::search(a1.begin(), a1.end(), c, 0);
    ASSERT_TRUE(it1 != a1.end());
    A2 a2{0, 1};
    A2::iterator it2 = dbi::search(a2.begin(), a2.end(), c, 0);
    ASSERT_TRUE(it2 != a2.end());
    it2 = dbi::search(a2.begin(), a2.end(), c, 1);
    ASSERT_TRUE(it2 != a2.end());
}

typedef std::array<int, 1> A1;
typedef std::array<int, 2> A2;

TEST(UpperBoundTest, Array) {
    typedef std::array<int, 21> A21;
    MyCompare c;
    A21 a{2, 3, 5, 6, 7, 8, 10, 11, 13, 14, 18, 20, 22, 25, 26, 27, 33, 50, 100, 112, 130};
    //A1 a1{0};
    //A2 a2{0, 1};
    A21::iterator it = dbi::upper_bound(a.begin(), a.end(), c, 8);
    ASSERT_EQ(*it, 10);
    it = dbi::upper_bound(a.begin(), a.end(), c, 0);
    ASSERT_EQ(*it, 2);
    it = dbi::upper_bound(a.begin(), a.end(), c, 140);
    ASSERT_EQ(it, a.end());
    it = dbi::upper_bound(a.begin(), a.end(), c, 130);
    ASSERT_EQ(it, a.end());
    it = dbi::upper_bound(a.begin(), a.end(), c, 2);
    ASSERT_EQ(*it, 3);
}


/* Comparator functor for uint64_t*/
struct MyCustomUInt64Cmp {
   bool operator()(uint64_t a, uint64_t b) const {
      return a<b;
   }
};

template <unsigned len>
struct Char {
   char data[len];
};

/* Comparator functor for char */
template <unsigned len>
struct MyCustomCharCmp {
   bool operator()(const Char<len>& a, const Char<len>& b) const {
      return memcmp(a.data, b.data, len) < 0;
   }
};

typedef std::pair<uint32_t, uint32_t> IntPair;

/* Comparator for IntPair */
struct MyCustomIntPairCmp {
   bool operator()(const IntPair& a, const IntPair& b) const {
      if (a.first < b.first)
         return true;
      else
         return (a.first == b.first) && (a.second < b.second);
   }
};

template <class T>
const T& getKey(const uint64_t& i);

template <>
const uint64_t& getKey(const uint64_t& i) { return i; }

std::vector<std::string> char20;
template <>
const Char<CHARSIZE>& getKey(const uint64_t& i) {
   std::stringstream ss;
   ss << i;
   std::string s(ss.str());
   char20.push_back(std::string(CHARSIZE-s.size(), '0')+s);
   return *reinterpret_cast<const Char<CHARSIZE>*>(char20.back().data());
}

std::vector<IntPair> intPairs;
template <>
const IntPair& getKey(const uint64_t& i) {
   intPairs.push_back(std::make_pair(i/3, 3-(i%3)));
   return intPairs.back();
}


const dbi::IndexKey getKey(const uint64_t& i) {
    std::vector<harriet::Value> values;
    values.emplace_back(harriet::Value::createInteger(i));
    return dbi::IndexKey{std::move(values)};
}

template<typename T, typename CMP, typename Schema>
::testing::AssertionResult Lookup(dbi::BTree<T, CMP, Schema>& tree, const dbi::IndexKey& k, dbi::TupleId& tid, uint64_t i) {
    tid = tree.lookup(k);
    if (tid != dbi::kInvalidTupleId)
        return ::testing::AssertionSuccess();
    else
        return ::testing::AssertionFailure() << "Failed at : " << i << "";
}

template<typename T, typename CMP, typename Schema>
::testing::AssertionResult Erase(dbi::BTree<T, CMP, Schema>& tree, const dbi::IndexKey& k, uint64_t i) {
    if (tree.erase(k))
        return ::testing::AssertionSuccess();
    else
        return ::testing::AssertionFailure() << "Failed at : " << i << "";
}

template <typename T, typename CMP, typename Schema>
void test(uint64_t n, Schema schema) {
    typedef dbi::TID TID;
    const uint32_t pages = 1000;
    assert(kSwapFilePages>=pages);

    // Create
    dbi::BufferManager bufferManager(kSwapFileName, pages / 2);
    dbi::SegmentManager segmentManager(bufferManager, true);
    dbi::SegmentId id = segmentManager.createSegment(dbi::SegmentType::BT, 10);
    dbi::BTreeSegment& segment = segmentManager.getBTreeSegment(id);

    // Set up stuff, you probably have to change something here to match to your interfaces
    dbi::BTree<T, CMP, Schema> bTree(segment, CMP(schema), schema);
    //std::cout << "LeafnodeSize: " << bTree.getLeafNodeSize() << std::endl;
    //std::cout << "Starting test\n";
    // Insert values
    TID tid;
    for (uint64_t i=0; i<n; ++i) {
        //std::cout << i << std::endl;
        ASSERT_TRUE(bTree.insert(getKey(i),TID(i)));
        ASSERT_TRUE(Lookup(bTree, getKey(i), tid, i));
        //ASSERT_TRUE(bTree.lookup(getKey(i),tid));
    }
    //ASSERT_EQ(bTree.size(), n);
    // Check if they can be retrieved
    for (uint64_t i=0; i<n; ++i) {
        //std::cout << i << std::endl;
        TID tid;
        ASSERT_TRUE(Lookup(bTree, getKey(i), tid, i));
        //ASSERT_TRUE(bTree.lookup(getKey<T>(i),tid));
        ASSERT_EQ(tid, TID(i));
    }
   
    // Delete some values
    for (uint64_t i=0; i<n; ++i)
        if ((i%7)==0) {
            ASSERT_TRUE(Erase(bTree, getKey(i), i));
        }
    // Check if the right ones have been deleted
    for (uint64_t i=0; i<n; ++i) {
        TID tid;
        if ((i%7)==0) {
            ASSERT_FALSE(Lookup(bTree, getKey(i), tid, i));
        } else {
            ASSERT_TRUE(Lookup(bTree, getKey(i), tid, i));
            ASSERT_EQ(tid, TID(i));
        }
    }
    // Delete everything
    for (uint64_t i=0; i<n; ++i)
        bTree.erase(getKey(i));
    //ASSERT_EQ(bTree.size(), (uint64_t)0);
}

TEST(BTreeTest, FunkeTest) {
    std::vector<harriet::VariableType> columns;
    columns.push_back(harriet::VariableType::createIntegerType());
    uint64_t n = 10*1000ul;
    // Test index with 64bit unsigned integers
    dbi::IndexKeySchema s{columns};
    test<dbi::IndexKey, dbi::IndexKeyComparator, dbi::IndexKeySchema>(n, s);
}

//const uint64_t n = 10*1000ul;
/*
TEST(BTreeTest, FunkeTestUintKey) {
    uint64_t n = 10*1000ul;
    // Test index with 64bit unsigned integers
    test<uint64_t, MyCustomUInt64Cmp>(n);
}

TEST(BTreeTest, FunkeTestCharKey) {
    uint64_t n = 10*1000ul;
    // Test index with CHARSIZE character strings
    test<Char<CHARSIZE>, MyCustomCharCmp<CHARSIZE>>(n);
}

TEST(BTreeTest, FunkeTestCompoundKey) {
    uint64_t n = 10*1000ul;
    // Test index with compound key
    test<IntPair, MyCustomIntPairCmp>(n);
}

TEST(BTreeTest, SimpleTest) {
    const uint32_t pages = 100;
    assert(kSwapFilePages>=pages);

    // Create
    dbi::BufferManager bufferManager(kSwapFileName, pages / 2);
    dbi::SegmentManager segmentManager(bufferManager, true);
    dbi::SegmentId id = segmentManager.createSegment(dbi::SegmentType::BT, 10);
    dbi::BTreeSegment& segment = segmentManager.getBTreeSegment(id);

    dbi::BTree<uint64_t, std::less<uint64_t>> tree(segment);
    TID tid; 
    
    EXPECT_FALSE(tree.lookup((uint64_t)2, tid));
    
    for (uint64_t i=1; i <= tree.getLeafNodeSize()+2; i++) {
        EXPECT_TRUE(tree.insert(i, TID(i)));
        EXPECT_TRUE(tree.lookup(i, tid));
    }

    // Test Iterator 
    auto it = tree.lookupRange(1, tree.getLeafNodeSize()+2);
    for (uint64_t i=1; i <= tree.getLeafNodeSize()+2; i++) {
        EXPECT_TRUE(it.valid());
        std::pair<uint64_t, TID> p = *it;
        EXPECT_EQ(p.first, i); 
        it++;
    }

    EXPECT_FALSE(it.valid());
    tree.visualize();
}



void threadTestInsert(dbi::BTree<uint64_t>* tree, uint64_t n, uint64_t numthreads, uint64_t threadid) {
    dbi::TID tid;
    // Insert values into tree
    for (uint64_t i=threadid; i <= n; i+=numthreads) {
        ASSERT_TRUE(tree->insert(i, TID(i)));
        ASSERT_TRUE(tree->lookup(i, tid));
        ASSERT_EQ(TID(i), tid);
    }
}

void threadTestLookup(dbi::BTree<uint64_t>* tree, uint64_t n, bool* finished) {
    dbi::TID tid;
    srand(n);
    // Check values are all inserted
    while (!*finished) {
        tree->lookup(rand()%n, tid);
    }
}
    
void threadTestErase(dbi::BTree<uint64_t>* tree, uint64_t n, uint64_t numthreads, uint64_t threadid) {
    dbi::TID tid;
    // Delete values
    for (uint64_t i=(threadid+1)%numthreads; i <= n; i+=numthreads) {
        ASSERT_TRUE(tree->erase(i));
        ASSERT_FALSE(tree->lookup(i, tid));
    }
}



TEST(BTreeTest, ThreadTest) {
    return;
    const uint32_t pages = 1000;
    assert(kSwapFilePages>=pages);
    const uint64_t n = 5*1000ul;
    const uint64_t numthreads = 4;
    
    // Create
    dbi::BufferManager bufferManager(kSwapFileName, pages / 4);
    dbi::SegmentManager segmentManager(bufferManager, true);
    dbi::SegmentId id = segmentManager.createSegment(dbi::SegmentType::BT, 10);
    dbi::BTreeSegment& segment = segmentManager.getBTreeSegment(id);
    dbi::BTree<uint64_t> tree(segment);
    
    bool finished = false; 
    std::thread lookupThread(threadTestLookup, &tree, n, &finished);
    std::array<std::thread, numthreads> threads;
    
    // Start threads to insert values
    for (uint64_t i=0; i < numthreads; i++) {
        threads[i] = std::thread(threadTestInsert, &tree, n, numthreads, i);
    }
    for (uint64_t i=0; i < numthreads; i++) {
        threads[i].join();
    }
    
    dbi::TID tid;
    // Check that all values have been inserted
    for (uint64_t i=0; i <= n; i++) {
        ASSERT_TRUE(tree.lookup(i, tid));
        ASSERT_EQ(TID(i), tid);
    }
    
    // Start threads to delete keys
    for (uint64_t i=0; i < numthreads; i++) {
        threads[i] = std::thread(threadTestErase, &tree, n, numthreads, i);
    }
    for (uint64_t i=0; i < numthreads; i++) {
        threads[i].join();
    }
    
    // Make sure tree is empty
    ASSERT_EQ(tree.size(), (uint64_t)0);
    // Stop lookupthread
    finished = true;
    lookupThread.join();
}
*/
