#ifndef __ExternalSort
#define __ExternalSort

#include <string>
#include <cstdint>
#include <memory>
#include <fstream>

struct ExternalSort {

  ExternalSort(const std::string& fileName);

  std::unique_ptr<uint64_t*> read(uint32_t begin, uint32_t count);

private:

  std::string fileName;
  std::ifstream input;
};

#endif
