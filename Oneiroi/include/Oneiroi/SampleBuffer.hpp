#ifndef __SAMPLEBUFFER_H__
#define __SAMPLEBUFFER_H__

#include <stdint.h>

class NTSampleBuffer : public AudioBuffer {
protected:
  const size_t channels;
  size_t blocksize;
  FloatArray* buffers;
public:
  NTSampleBuffer(int channels, size_t blocksize)
    :channels(channels), blocksize(blocksize) {
    buffers = new FloatArray[channels];
    for(size_t i=0; i<channels; ++i)
      buffers[i] = FloatArray::create(blocksize);
  }
  ~NTSampleBuffer(){
    for(size_t i=0; i<channels; ++i)
      FloatArray::destroy(buffers[i]);
    //delete[] buffers;
  }
  virtual void split(float* input){
    for(size_t j=0; j<channels; ++j){
      for (size_t i=0; i<blocksize; ++i)
        buffers[j][i] = input[i];
      input += blocksize;
      }
    }
  void clear(){
    for(size_t i=0; i<channels; ++i)
      buffers[i].clear();
  }
  inline FloatArray getSamples(int channel){
    if(channel < channels)
      return buffers[channel];
    return FloatArray();
  }
  inline int getChannels(){
    return channels;
  }
  inline int getSize(){
    return blocksize;
  }
  inline void setSize(size_t blocksize_){
    blocksize = blocksize_;
  }
  static AudioBuffer* create(int channels, int samples) {return new NTSampleBuffer(channels, samples);}
  static void destroy(NTSampleBuffer* obj){
    //delete obj;
  }
};

#endif // __SAMPLEBUFFER_H__
