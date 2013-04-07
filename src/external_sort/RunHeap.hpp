#ifndef __MinHeap
#define __MinHeap

#include "InputRun.hpp"
#include <vector>
#include <memory>
#include <cassert>

namespace dbi {

/// Fast RunHeap -- keeps track of the run with minimal next value
class RunHeap {
public:
	RunHeap()
	{
	}

	void push(std::unique_ptr<InputRun> run)
	{
		// Just add
		data.push_back(move(run));
	}

	uint64_t getMin()
	{
		// Locate
		assert(hasMore());
		uint32_t minIndex = 0;
		for (uint32_t i = 1; i < data.size(); ++i)
			if(data[i]->peekNext() < data[minIndex]->peekNext())
				minIndex = i;

      // Remove
      uint64_t value = data[minIndex]->getNext();
      if (!data[minIndex]->hasNext())
         data.erase(data.begin() + minIndex);

      return value;
	}

	bool hasMore()
	{
		return !data.empty();
	}

	uint32_t size()
	{
		return data.size();
	}

private:
	std::vector<std::unique_ptr<InputRun>> data;
};

}

#endif