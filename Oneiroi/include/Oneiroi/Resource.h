#ifndef __RESOURCE_STORAGE_H__
#define __RESOURCE_STORAGE_H__

#include <cstddef>
#include <stdint.h>
#include "FloatArray.h"

class Resource {
public:
  /**
   * Check if data is available
   */
  bool hasData() const {
    return data != NULL;
  }

  /**
   * Get pointer to data. This may be NULL if no buffer is assigned yet.
   */
  void* getData() {
    return data;
  }

  /**
   * Get buffer size in bytes
   */
  size_t getSize() const {
    return size;
  }    

  bool exists() const {
    return size != 0;
  }

  bool isMutable() const {
    return allocated;
  }

  /**
   * Get resource name
   */
  const char* getName() const {
    return name;
  }

  /**
   * Array conversion.
   *
   * @param offset: offset in bytes
   * @param max_size maximum size, actual size can be smaller depending on object size
   */
  template<typename Array, typename Element>
  Array asArray(size_t offset = 0, size_t max_size = 0xFFFFFFFF);

  FloatArray asFloatArray(size_t offset = 0, size_t max_size = 0xFFFFFFFF){
    return asArray<FloatArray, float>(offset, max_size);
  }
  
  /**
   * Get resource from storage.
   * Returned object must be garbage collected with Resource::destroy()
   * 
   * @param name resource name
   * @return NULL if resource does not exist or can't be read.
   */
  static Resource* open(const char* name);

  /**
   * Open resource and load data.
   * Allocates extra memory to hold the resource if required.
   * Returned object must be garbage collected with Resource::destroy()
   * 
   * @param name resource name
   * @return NULL if resource does not exist or can't be read.
   */
  static Resource* load(const char* name);

  /**
   * Clean up used memory resources.
   */
  static void destroy(Resource* resource);

  /**
   * Read data from resource into memory
   *
   * @param len maximum number of bytes to read
   * @param offset index of first byte to read from
   *
   * @return number of bytes actually read
   */
  size_t read(void* dest, size_t len, size_t offset=0); 

  Resource(): name(NULL), size(0), data(NULL), allocated(false) {}
  ~Resource(){}
protected:
  Resource(const char* name, size_t size, void* data)
    : name(name), size(size), data((uint8_t*)data), allocated(false) {}
  const char* name;
  size_t size;
  uint8_t* data;
  bool allocated;
};

#include "OpenWareMidiControl.h"
#include "ServiceCall.h"
#include "ProgramVector.h"

template<typename Array, typename Element>
Array Resource::asArray(size_t offset, size_t max_size) {
    // Data is expected to be aligned
    if (max_size > size - offset)
        max_size = size - offset;
    return Array((Element*)(data + offset), max_size / sizeof(Element));
}

template FloatArray Resource::asArray<FloatArray, float>(size_t offset, size_t max_size);

void Resource::destroy(Resource* resource) {
  if(resource && resource->allocated)
    //delete[] resource->data;
  //delete resource;
}

Resource* Resource::open(const char* name){
  uint8_t* data = NULL;
  size_t offset = 0;
  size_t size = 0;
  void* args[] = {
		  (void*)name, (void*)&data, (void*)&offset, (void*)&size
  };
  if(getProgramVector()->serviceCall(OWL_SERVICE_LOAD_RESOURCE, args, 4) == OWL_SERVICE_OK)
    return new Resource(name, size, data);
  return NULL;
}

Resource* Resource::load(const char* name){
  Resource* resource = Resource::open(name);
  if(resource && !resource->hasData()){
    size_t offset = 0;
    size_t size = resource->size;
    uint8_t* data = new uint8_t[size];
    void* args[] = {
      (void*)name, (void*)&data, (void*)&offset, (void*)&size
    };
    if (getProgramVector()->serviceCall(OWL_SERVICE_LOAD_RESOURCE, args, 4) == OWL_SERVICE_OK){
      resource->data = data;
      resource->size = size;
      resource->allocated = true;
    }else{
      resource->size = 0;
      //delete[] data;
    }
  }
  return resource;
}

size_t Resource::read(void* dest, size_t len, size_t offset){
  if(this->data == NULL){
    void* args[] = {
		    (void*)this->name, (void*)&dest, (void*)&offset, (void*)&len
    };
    if (getProgramVector()->serviceCall(OWL_SERVICE_LOAD_RESOURCE, args, 4) == OWL_SERVICE_OK){
      return len;
    }else{
      return 0;
    }
  }else{
    len = std::min(this->size-offset, len);
    memcpy(dest, (uint8_t*)this->data+offset, len);
    return len;
  }
}


#endif
