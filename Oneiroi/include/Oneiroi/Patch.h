#ifndef __Patch_h__
#define __Patch_h__

#include "basicmaths.h"
#include "FloatArray.h"
#include "AudioBuffer.h"
#include "SampleBuffer.hpp"



enum PatchChannelId {
  LEFT_CHANNEL = 0,
  RIGHT_CHANNEL = 1
};


#include <cstddef>
#include <stdint.h>
static constexpr size_t _allocatableDTCMemorySize = 10000;
static constexpr uint32_t _allocatableMemorySize = 8000000; 


static uint8_t* _allocatableMemory;
static uint32_t _allocatedMemory;
static uint8_t* _allocatableDTCMemory;
static uint32_t _allocatedDTCMemory;


void* _new(std::size_t);

void* operator new (std::size_t sz)
{
    void *mem = _new(sz);
    return mem;
}

void* operator new[] (std::size_t sz)
{
    void *mem = _new(sz);
    return mem;
}

void* _new(std::size_t sz)
{
    void *ret = nullptr;
    if (sz < 1200 &&  (_allocatedDTCMemory + sz) <  _allocatableDTCMemorySize ){
      ret = _allocatableDTCMemory;
      _allocatableDTCMemory += sz;
      _allocatedDTCMemory += sz;
      return ret;
    }
    
    else{
     ret = _allocatableMemory;
      _allocatableMemory += sz;
      _allocatedMemory += sz;
      return ret;
  }
}

void operator delete (void *ptr)
{
 // nothing!
}

void operator delete (void *ptr, unsigned int arg)
{
    // nothing!
}

void operator delete[] (void *ptr)
{
    // nothing!
}


#endif /* __Patch_h__ */