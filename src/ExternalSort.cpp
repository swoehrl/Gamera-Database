#include "ExternalSort.hpp"
#include "Page.hpp"
#include "BufferManager.hpp"
#include "Run.hpp"
#include "OutputRun.hpp"
#include "RunHeap.hpp"
#include "util/Utility.hpp"
#include <stdio.h>
#include <fstream>
#include <iostream>
#include <cassert>
#include <list>
#include <memory>
#include <cstdio>
#include <chrono>
#include <set>
#include <queue>

namespace dbi {

using namespace std;

ExternalSort::ExternalSort()
{
}

template<class T>
void simpleSortImpl(const string& inputFileName, const string& outputFileName)
{
   // Open file
   ifstream in(inputFileName, ios::binary);
   if (!in.is_open() || !in.good())
      throw;

   // Figure out file length and entry count
   in.seekg(0, ios::end);
   uint64_t fileLength = static_cast<uint64_t>(in.tellg());
   in.seekg(0, ios::beg);
   if (fileLength % sizeof(T) != 0)
      throw;
   uint64_t entryCount = fileLength / sizeof(T);

   // Read into buffer
   vector<T> buffer(entryCount);
   in.read(reinterpret_cast<char*>(buffer.data()), fileLength);

   // Sort
   sort(buffer.begin(), buffer.end());

   // Open output and write data
   ofstream out(outputFileName, ios::binary);
   if (!out.is_open() || !out.good())
      throw;
   out.write(reinterpret_cast<char*>(buffer.data()), fileLength);
}

template<class T>
void createRunsPhase(const string& inputFileName, const string& outputFileName, BufferManager<T>& buffer, list<unique_ptr<Run<T>>>& runs)
{
   // Phase I: Create runs
   uint64_t ioTime = 0;
   string runFileName = outputFileName + "run";
   fstream inputFile(inputFileName, ios::binary | ios::in);
   fstream outputFile(runFileName, ios::binary | ios::out);
   for (uint64_t runId=0; true; runId++) {
      // Read data
      int64_t position = inputFile.tellg();
      auto start = chrono::high_resolution_clock::now();
      inputFile.read(buffer.begin(), buffer.size());
      inputFile.peek(); // Detect end of file
      bool readSuccessfull = inputFile.eof();
      auto end = chrono::high_resolution_clock::now();
      ioTime += chrono::duration_cast<chrono::nanoseconds>(end-start).count();
      inputFile.clear();
      int64_t readBytes = inputFile.tellg() - position;

      // End of file
      if(readSuccessfull) {
         // Trivial case -- buffer is larger than file
         if(runId == 0) {
            sort(reinterpret_cast<uint64_t*>(buffer.begin()), reinterpret_cast<uint64_t*>(buffer.begin()) + readBytes / sizeof(T));
            fstream resultFile(outputFileName, ios::binary | ios::out);
            resultFile.write(buffer.begin(), readBytes);
            return;
         }

         // Terminate
         if(readBytes <= 0)
            break;
      }

      // Sort and write
      sort(reinterpret_cast<uint64_t*>(buffer.begin()), reinterpret_cast<uint64_t*>(buffer.begin()) + readBytes / sizeof(T));
      auto run = dbiu::make_unique<Run<T>>(outputFile.tellg(), readBytes, runFileName);
      runs.push_back(move(run));
      outputFile.write(buffer.begin(), readBytes);
   }
}

template<class T>
void singleMergePhase(BufferManager<T>& buffer, list<unique_ptr<Run<T>>>& inputRuns, uint32_t numJoins, OutputRun<T>& targetRun)
{
   RunHeap<T> runHeap;
   uint64_t totalBytes = 0;
   for (uint64_t i = 0; i < numJoins && !inputRuns.empty(); i++) {
      auto run = move(inputRuns.front());
      inputRuns.pop_front();
      run->assignPage(buffer.getPage(i));
      run->prepareForReading();
      totalBytes += run->size();
      runHeap.push(move(run));
   }

   // Set up output stream
   targetRun.assignPage(buffer.getPage(numJoins));
   targetRun.prepareForWriting();

   // Merge selected inputRuns
   while(runHeap.hasMore())
      targetRun.add(runHeap.getMin());

   // Add target run back to all inputRuns
   targetRun.flush();
}

template<class T>
void mergeRuns(const string& outputFileName, BufferManager<T>& buffer, list<unique_ptr<Run<T>>>& runs, uint64_t availablePages)
{
   int fileIndex = 0;
   while(!runs.empty()) {
      // Find nice merge strategy
      uint32_t minimalNumberOfMerges = ceil(runs.size() / (availablePages-1));
      uint32_t unusedSlots = (availablePages-1) - (runs.size() % (availablePages-1));
      if(minimalNumberOfMerges <= unusedSlots) {
         // Postpone as much work as possible
         unusedSlots -= minimalNumberOfMerges;
         OutputRun<T> targetRun1(outputFileName + "_merge_" + to_string(fileIndex), true);
         singleMergePhase(buffer, runs, (availablePages-1)-unusedSlots, targetRun1);
         runs.push_back(targetRun1.createRun());

         // We can finish in this merge
         list<unique_ptr<Run<T>>> nextLevelRuns;
         while(runs.size() >= (availablePages-1)) {
            OutputRun<T> targetRun(outputFileName + "_merge_" + to_string(fileIndex), true);
            singleMergePhase(buffer, runs, (availablePages-1), targetRun);
            runs.push_back(targetRun.createRun());
         }

         // Final merge pass
         OutputRun<T> targetRun2(outputFileName, false);
         singleMergePhase(buffer, runs, (availablePages-1), targetRun2);
         runs.clear();
         remove((outputFileName + "_merge_" + to_string(fileIndex)).c_str());
      } else {
         // Just create the next level
         list<unique_ptr<Run<T>>> nextLevelRuns;
         string fileName = runs.front()->getFileName();
         while(!runs.empty()) {
            assert(fileName == runs.front()->getFileName());
            OutputRun<T> targetRun(outputFileName + "_merge_" + to_string(fileIndex), true);
            singleMergePhase(buffer, runs, (availablePages-1), targetRun);
            nextLevelRuns.push_back(targetRun.createRun());
         }
         fileIndex++;
         runs = move(nextLevelRuns);
         remove(fileName.c_str());
      }
   }
}

template<class T>
void complexSortImpl(const string& inputFileName, const string& outputFileName, uint64_t pageSize, uint64_t maxMemory, bool showPerformance)
{
   // Check input and calculate constants
   assert(maxMemory % pageSize == 0);
   assert(maxMemory > 2*pageSize);
   const uint64_t availablePages = maxMemory / pageSize;

   // Set up buffers
   BufferManager<T> buffer(maxMemory, pageSize);
   list<unique_ptr<Run<T>>> runs;

   // Phase I: Create runs
   auto startRunPhase = chrono::high_resolution_clock::now();
   createRunsPhase(inputFileName, outputFileName, buffer, runs);
   uint64_t initialRunCount = runs.size();
   auto endRunPhase = chrono::high_resolution_clock::now();

   // Phase II: Merge runs
   auto startMergePhase = chrono::high_resolution_clock::now();
   mergeRuns(outputFileName, buffer, runs, availablePages);
   auto endMergePhase = chrono::high_resolution_clock::now();

   if(showPerformance) {
      cout << "Run count: " << initialRunCount << endl;
      cout << "Run phase: " << chrono::duration_cast<chrono::milliseconds>(endRunPhase-startRunPhase).count() << "ms"  << endl;
      cout << "Merge phase: " << chrono::duration_cast<chrono::milliseconds>(endMergePhase-startMergePhase).count() << "ms" << endl;
      cout << "Both phases: " << chrono::duration_cast<chrono::milliseconds>(endMergePhase-startRunPhase).count() << "ms" << endl;
   }
}

void ExternalSort::simpleSort(const string& inputFileName, const string& outputFileName)
{
   simpleSortImpl<uint64_t>(inputFileName, outputFileName);
}

void ExternalSort::complexSort(const string& inputFileName, const string& outputFileName, uint64_t pageSize, uint64_t maxMemory, bool showPerformance)
{
   complexSortImpl<uint64_t>(inputFileName, outputFileName, pageSize, maxMemory, showPerformance);
}

}
