#ifndef __SimpleArray_h__
#define __SimpleArray_h__

#include <cstddef>
#include <string.h> /* for memcpy and memmov */

/**
 * SimpleArray holds a pointer to an array and the array size, and is designed to 
 * be passed by value. It does not define any virtual methods to prevent overheads 
 * in its subclasses.
 */
template<typename T>
class SimpleArray {
protected:
  T* data;
  size_t size;
public:
  SimpleArray() : data(NULL), size(0){}
  SimpleArray(T* data, size_t size) : data(data), size(size){}
  /* virtual ~SimpleArray(){} No virtual destructor to prevent adding a vtable to the object size */
  
  /**
   * Get the data stored in the Array.
   * @return a T* pointer to the data stored in the Array
  */
  T* getData(){
    return data;
  }
    
  size_t getSize() const {
    return size;
  }
    
  bool isEmpty() const {
    return size == 0;
  }

  /**
   * Get a single value stored in the array.
   * @return the value stored at index @param index
  */
  T getElement(size_t index){
    return data[index];
  }

  /**
   * Set a single value in the array.
  */
  void setElement(size_t index, T value){
    data[index] = value;
  }
    
  /**
   * Compares two arrays.
   * Performs an element-wise comparison of the values contained in the arrays.
   * @param other the array to compare against.
   * @return **true** if the arrays have the same size and the value of each of the elements of the one 
   * match the value of the corresponding element of the other, or **false** otherwise.
  */
  bool equals(const SimpleArray<T>& other) const {
    if(size != other.size)
      return false;
    for(size_t n=0; n<size; n++){
      if(data[n] != other.data[n])
        return false;
    }
    return true;
  }
  
  /**
   * Copies the content of this array to another array.
   * The other array needs to be at least as big as this array.
   * @param[out] destination the destination array
   */
  void copyTo(SimpleArray<T> destination){
      memcpy((void*)destination.data, (void*)data, size*sizeof(T));
  }

  /**
   * Copies the content of another array into this array.
   * This array needs to be at least as big as the other array.
   * @param[in] source the source array
   */
  void copyFrom(SimpleArray<T> source){
    memcpy((void*)data, (void*)source.data, source.size*sizeof(T));
  }

  /**
   * Copies the content of an array into a subset of the array.
   * Copies **len** elements from **source** to **destinationOffset** in the current array.
   * @param[in] source the source array
   * @param[in] destinationOffset the offset into the destination array 
   * @param[in] len the number of samples to copy
   *
  */
  void insert(SimpleArray<T> source, int destinationOffset, size_t len){
    insert(source, 0, destinationOffset, len);
  }

  /**
   * Copies the content of an array into a subset of the array.
   * Copies **len** elements starting from **sourceOffset** of **source** to **destinationOffset** in the current array.
   * @param[in] source the source array
   * @param[in] sourceOffset the offset into the source array
   * @param[in] destinationOffset the offset into the destination array
   * @param[in] len the number of samples to copy
  */
  void insert(SimpleArray<T> source, int sourceOffset, int destinationOffset, size_t len){
    memcpy((void*)(data+destinationOffset), (void*)(source.data+sourceOffset), len*sizeof(T));
  }

  /**
   * Copies values within an array.
   * Copies **length** values starting from index **fromIndex** to locations starting with index **toIndex**
   * @param[in] fromIndex the first element to copy
   * @param[in] toIndex the destination of the first element
   * @param[in] len the number of elements to copy
   */
  void move(int fromIndex, int toIndex, size_t len){
    memmove((void*)(data+toIndex), (void*)(data+fromIndex), len*sizeof(T));
  }
    
  /**
   * Optimised array copy for datatype T.
   * Copy four at a time to minimise loop overheads and allow SIMD optimisations.
   * This performs well on external RAM but is slower on internal memory compared to memcpy.
   */
  static void copy(T* dst, T* src, size_t len){
    size_t blocks = len >> 2u;
    T a, b, c, d;
    while(blocks--){
      a = *src++;
      b = *src++;
      c = *src++;
      d = *src++;
      *dst++ = a;
      *dst++ = b;
      *dst++ = c;
      *dst++ = d;
    }
    blocks = len & 0x3;
    while(blocks--){
      *dst++ = *src++;
    }
  }

  /**
   * Casting operator to T*
   * @return a T* pointer to the data stored in the Array
  */
  operator T*(){
    return data;
  }

  // /**
  //  * Allows to index the array using array-style brackets.
  //  * @param index the index of the element
  //  * @return the value of the **index** element of the array
  // */
  // T& operator [](size_t index){
  //   return data[index];
  // }
  
  // /**
  //  * Allows to index the array using array-style brackets.
  //  * **const** version of operator[]
  // */
  // const T& operator [](size_t index) const {
  //   return data[index];
  // }

};


#endif // __SimpleArray_h__
