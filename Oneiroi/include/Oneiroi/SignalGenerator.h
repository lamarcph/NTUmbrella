#ifndef __SignalGenerator_h__
#define __SignalGenerator_h__

#include "FloatArray.h"
#include "ComplexFloatArray.h"
#include "AudioBuffer.h"
#include "SampleBuffer.hpp"


/**
 * Base class for signal generators such as Oscillators.
 * A SignalGenerator produces samples from -1 to 1 unless
 * otherwise stated.
 */
class SignalGenerator {
public:
  virtual ~SignalGenerator(){}
  /**
   * Produce the next consecutive sample.
   */
  virtual float generate(){
    return 0;
  }
  /**
   * Produce a block of samples
   */
  virtual void generate(FloatArray output){
    for(size_t i=0; i<output.getSize(); ++i)
      output[i] = generate();
  }
};


class MultiSignalGenerator {
public:
  virtual ~MultiSignalGenerator(){}
  virtual void generate(AudioBuffer& output) = 0;
};


/**
 * Base class for stereo signal generators such as Oscillators.
 * A ComplexSignalGenerator produces complex numbers with each channel
 * containing samples in [-1..1] range unless otherwise stated.
 */
class ComplexSignalGenerator {
public:
  virtual ~ComplexSignalGenerator(){}
  /**
   * Produce the next consecutive sample.
   */
  virtual ComplexFloat generate() = 0;
  /**
   * Produce a block of samples
   */
  virtual void generate(ComplexFloatArray output) {
    size_t size = output.getSize();
    for(size_t i=0; i<size; ++i) {
      output[i] = generate();
    }
  }
};
#endif // __SignalGenerator_h__
