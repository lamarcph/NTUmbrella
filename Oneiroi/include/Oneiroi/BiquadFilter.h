#ifndef __BiquadFilter_h__
#define __BiquadFilter_h__

#include <string.h> // for memcpy
#include "FloatArray.h"
#include "SignalProcessor.h"

class FilterStage {
public:
  FloatArray coefficients;
  FloatArray state;
  static constexpr float BESSEL_Q = 0.57735026919f; // 1/sqrt(3)
  static constexpr float SALLEN_KEY_Q = 0.5f; // 1/2
  static constexpr float BUTTERWORTH_Q = 0.70710678118f; // 1/sqrt(2)

  FilterStage(FloatArray co, FloatArray st) : coefficients(co), state(st){}

  void setLowPass(float fc, float q, float sr){
    setLowPass(coefficients, fc*M_PI/sr, q);
  }

  void setHighPass(float fc, float q, float sr){
    setHighPass(coefficients, fc*M_PI/sr, q);
  }
  
  void setBandPass(float fc, float q, float sr){
    setBandPass(coefficients, fc*M_PI/sr, q);
  }
  
  void setAllPass(float fc, float q, float sr){
    setAllPass(coefficients, fc*M_PI/sr, q);
  }
  
  void setNotch(float fc, float q, float sr){
    setNotch(coefficients, fc*M_PI/sr, q);
  }
  
  void setPeak(float fc, float q, float gain, float sr){
    setPeak(coefficients, fc*M_PI/sr, q, gain);
  }
  void setLowShelf(float fc, float gain, float sr){
    setLowShelf(coefficients, fc*M_PI/sr, gain);
  }
  void setHighShelf(float fc, float gain, float sr){
    setHighShelf(coefficients, fc*M_PI/sr, gain);
  }
  void copyCoefficients(FloatArray newCoefficients){
    coefficients.copyFrom(newCoefficients);
  }  

  FloatArray getCoefficients(){
    return coefficients;
  }
  FloatArray getState(){
    return state;
  }

  static void setLowPass(float* coefficients, float omega, float q){
    float K = tanf(omega);
    float norm = 1 / (1 + K / q + K * K);
    coefficients[0] = K * K * norm;
    coefficients[1] = 2 * coefficients[0];
    coefficients[2] = coefficients[0];
    coefficients[3] = - 2 * (K * K - 1) * norm;
    coefficients[4] = - (1 - K / q + K * K) * norm;
  }

  static void setHighPass(float* coefficients, float omega, float q){
    float K = tanf(omega);
    float norm = 1 / (1 + K / q + K * K);
    coefficients[0] = 1 * norm;
    coefficients[1] = -2 * coefficients[0];
    coefficients[2] = coefficients[0];
    coefficients[3] = - 2 * (K * K - 1) * norm;
    coefficients[4] = - (1 - K / q + K * K) * norm;
  }

  static void setBandPass(float* coefficients, float omega, float q){
    float K = tanf(omega);
    float norm = 1 / (1 + K / q + K * K);
    coefficients[0] = K / q * norm;
    coefficients[1] = 0;
    coefficients[2] = -coefficients[0];
    coefficients[3] = - 2 * (K * K - 1) * norm;
    coefficients[4] = - (1 - K / q + K * K) * norm;
  }

  static void setAllPass(float* coefficients, float omega, float q){
    float K = tanf(omega);
    float norm = 1 / (1 + K / q + K * K);
    coefficients[0] = (1 - K / q + K * K) * norm;
    coefficients[1] = 2 * (K * K - 1) * norm;
    coefficients[2] = 1;
    coefficients[3] = coefficients[1];
    coefficients[4] = coefficients[0];
  }

  static void setNotch(float* coefficients, float omega, float q){
    float K = tanf(omega);
    float norm = 1 / (1 + K / q + K * K);
    coefficients[0] = (1 + K * K) * norm;
    coefficients[1] = 2 * (K * K - 1) * norm;
    coefficients[2] = coefficients[0];
    coefficients[3] = - coefficients[1];
    coefficients[4] = - (1 - K / q + K * K) * norm;
  }

  static void setPeak(float* coefficients, float omega, float q, float gain){
    float K = tanf(omega);
    float V = exp10f(fabsf(gain)/20);
    float norm;
    if (gain >= 0) {
      norm = 1 / (1 + 1/q * K + K * K);
      coefficients[0] = (1 + V/q * K + K * K) * norm;
      coefficients[1] = 2 * (K * K - 1) * norm;
      coefficients[2] = (1 - V/q * K + K * K) * norm;
      coefficients[3] = - coefficients[1];
      coefficients[4] = - (1 - 1/q * K + K * K) * norm;
    }
    else {
      norm = 1 / (1 + V/q * K + K * K);
      coefficients[0] = (1 + 1/q * K + K * K) * norm;
      coefficients[1] = 2 * (K * K - 1) * norm;
      coefficients[2] = (1 - 1/q * K + K * K) * norm;
      coefficients[3] = - coefficients[1];
      coefficients[4] = - (1 - V/q * K + K * K) * norm;
    }
  }

  static void setLowShelf(float* coefficients, float omega, float gain){
    float K = tanf(omega);
    float V = exp10f(fabsf(gain)/20);
    float norm;
    if(gain >= 0) {
      norm = 1 / (1 + M_SQRT2 * K + K * K);
      coefficients[0] = (1 + sqrtf(2*V) * K + V * K * K) * norm;
      coefficients[1] = 2 * (V * K * K - 1) * norm;
      coefficients[2] = (1 - sqrtf(2*V) * K + V * K * K) * norm;
      coefficients[3] = - 2 * (K * K - 1) * norm;
      coefficients[4] = - (1 - M_SQRT2 * K + K * K) * norm;
    } else {
      norm = 1 / (1 + sqrtf(2*V) * K + V * K * K);
      coefficients[0] = (1 + M_SQRT2 * K + K * K) * norm;
      coefficients[1] = 2 * (K * K - 1) * norm;
      coefficients[2] = (1 - M_SQRT2 * K + K * K) * norm;
      coefficients[3] = - 2 * (V * K * K - 1) * norm;
      coefficients[4] = - (1 - sqrtf(2*V) * K + V * K * K) * norm;
    }
  }

  static void setHighShelf(float* coefficients, float omega, float gain){
    float K = tanf(omega);
    float V = exp10f(fabsf(gain)/20);
    float norm;
    if(gain >= 0) {
      norm = 1 / (1 + M_SQRT2 * K + K * K);
      coefficients[0] = (V + sqrtf(2*V) * K + K * K) * norm;
      coefficients[1] = 2 * (K * K - V) * norm;
      coefficients[2] = (V - sqrtf(2*V) * K + K * K) * norm;
      coefficients[3] = - 2 * (K * K - 1) * norm;
      coefficients[4] = - (1 - M_SQRT2 * K + K * K) * norm;
    } else {
      norm = 1 / (V + sqrtf(2*V) * K + K * K);
      coefficients[0] = (1 + M_SQRT2 * K + K * K) * norm;
      coefficients[1] = 2 * (K * K - 1) * norm;
      coefficients[2] = (1 - M_SQRT2 * K + K * K) * norm;
      coefficients[3] = - 2 * (K * K - V) * norm;
      coefficients[4] = - (V - sqrtf(2*V) * K + K * K) * norm;
    }
  }
};

/** 
 * Cascaded Biquad Filter.
 * Implemented using CMSIS DSP Library, Direct Form 2 Transposed.
 * Each cascaded stage implements a second order filter.
 */
#define BIQUAD_COEFFICIENTS_PER_STAGE    5
#define BIQUAD_STATE_VARIABLES_PER_STAGE 2 // 4 for df1, 2 for df2
class BiquadFilter : public SignalProcessor {
private:
  float pioversr;
#ifdef ARM_CORTEX
  // arm_biquad_casd_df1_inst_f32 df1;
  arm_biquad_cascade_df2T_instance_f32 df2;
#endif /* ARM_CORTEX */
protected:
  float* coefficients; // stages*5
  float* state; // stages*4 for df1, stages*2 for df2
  size_t stages;
  /*
   * The coefficients are stored in the array <code>coefficients</code> in the following order:
   * <pre>
   *     {b10, b11, b12, a11, a12, b20, b21, b22, a21, a22, ...}
   * </pre>
   * where <code>b1x</code> and <code>a1x</code> are the coefficients for the first stage,
   * <code>b2x</code> and <code>a2x</code> are the coefficients for the second stage,
   * and so on.  The <code>coeffs</code> array must contain a total of <code>5*stages</code> values.   
   */
  void copyCoefficients(){
    for(size_t i=1; i<stages; ++i)
      memcpy(coefficients+i*BIQUAD_COEFFICIENTS_PER_STAGE, coefficients, BIQUAD_COEFFICIENTS_PER_STAGE*sizeof(float));
  }
  void init(){
#ifdef ARM_CORTEX
    // arm_biquad_cascade_df1_init_f32(&df1, stages, coefficients, state);
    arm_biquad_cascade_df2T_init_f32(&df2, stages, coefficients, state);
#else
    if(state)
      memset(state, 0, stages*BIQUAD_STATE_VARIABLES_PER_STAGE*sizeof(float));
#endif /* ARM_CORTEX */
  }
public:
  BiquadFilter()
    : pioversr(0), coefficients(NULL), state(NULL), stages(0) {}

  BiquadFilter(float sr, float* coefs, float* ste, size_t sgs) :
    pioversr(M_PI/sr), coefficients(coefs), state(ste), stages(sgs) {
    init();
  }
  virtual ~BiquadFilter(){}

  void setSampleRate(float sr){
    pioversr = M_PI/sr;
  }

  float getSampleRate(){
    return M_PI / pioversr;
  }

  size_t getStages(){
    return stages;
  }

  void setStages(size_t newStages){
    stages = newStages;
  }

  static size_t getCoefficientsPerStage(){
    return BIQUAD_COEFFICIENTS_PER_STAGE;
  }

  FloatArray getCoefficients(){
    return FloatArray(coefficients, BIQUAD_COEFFICIENTS_PER_STAGE*stages);
  }

  FloatArray getState(){
    return FloatArray(state, BIQUAD_STATE_VARIABLES_PER_STAGE*stages);
  }

  /**
   * Sets state to point to a different set of values
   */
  void setState(FloatArray newState){
    state = newState.getData();
  }
  
  /**
   * Copies state values from an array.
   */
  void copyState(FloatArray newState){
    getState().copyFrom(newState);
  }

  FilterStage getFilterStage(size_t stage){
    FloatArray c(coefficients+BIQUAD_COEFFICIENTS_PER_STAGE*stage, BIQUAD_COEFFICIENTS_PER_STAGE);
    FloatArray s(state+BIQUAD_STATE_VARIABLES_PER_STAGE*stage, BIQUAD_STATE_VARIABLES_PER_STAGE);
    return FilterStage(c, s);
  }

  /* process into output, leaving input intact */
  void process(float* input, float* output, size_t size){
#ifdef ARM_CORTEX
    // arm_biquad_cascade_df1_f32(&df1, input, output, size);
    arm_biquad_cascade_df2T_f32(&df2, input, output, size);
#else
    for(size_t k=0; k<stages; k++){
      float* coeffs = getFilterStage(k).getCoefficients();
      float b0 = *coeffs++;
      float b1 = *coeffs++;
      float b2 = *coeffs++;
      float a1 = *coeffs++;
      float a2 = *coeffs++;
      float d1 = state[k*BIQUAD_STATE_VARIABLES_PER_STAGE];
      float d2 = state[k*BIQUAD_STATE_VARIABLES_PER_STAGE+1];
      for(size_t n=0; n<size; n++){ //manually apply filter, one stage
        float out=b0 * input[n] + d1; 
        d1 = b1 * input[n] + a1 * out + d2;
        d2 = b2 * input[n] + a2 * out;
        output[n]=out;
        state[k*BIQUAD_STATE_VARIABLES_PER_STAGE]=d1;
        state[k*BIQUAD_STATE_VARIABLES_PER_STAGE+1]=d2;
      }
    }
#endif /* ARM_CORTEX */
  }

  /* perform in-place processing */
  void process(FloatArray in){
    process(in, in, in.getSize());
  }

  void process(FloatArray in, FloatArray out){
    process(in, out, in.getSize());
  }

  void processLowPass(FloatArray in, FloatArray fc, float q, FloatArray out){
    for(size_t i = 0; i < in.getSize(); i++){
      setLowPass(fc[i], q);
      out[i] = process(in[i]);
    }
  }

  void processHighPass(FloatArray in, FloatArray fc, float q, FloatArray out){
    for(size_t i = 0; i < in.getSize(); i++){
      setHighPass(fc[i], q);
      out[i] = process(in[i]);
    }
  }

  void processBandPass(FloatArray in, FloatArray fc, float q, FloatArray out){
    for(size_t i = 0; i < in.getSize(); i++){
      setBandPass(fc[i], q);
      out[i] = process(in[i]);
    }
  }

  void processAllPass(FloatArray in, FloatArray fc, float q, FloatArray out){
    for(size_t i = 0; i < in.getSize(); i++){
      setAllPass(fc[i], q);
      out[i] = process(in[i]);
    }
  }

  /* process a single sample and return the result */
  float process(float input){
    float output;
    process(&input, &output, 1);
    return output;
  }

  void setLowPass(float fc, float q){
    FilterStage::setLowPass(coefficients, fc*pioversr, q);
    copyCoefficients();
  }

  void setHighPass(float fc, float q){
    FilterStage::setHighPass(coefficients, fc*pioversr, q);
    copyCoefficients();
  }

  void setBandPass(float fc, float q){
    FilterStage::setBandPass(coefficients, fc*pioversr, q);
    copyCoefficients();
  }

  void setAllPass(float fc, float q){
    FilterStage::setAllPass(coefficients, fc*pioversr, q);
    copyCoefficients();
  }

  void setNotch(float fc, float q){
    FilterStage::setNotch(coefficients, fc*pioversr, q);
    copyCoefficients();
  }

  /**
   * Configure a peaking filter with resonance and variable gain.
   * @param gain in dB
   */
  void setPeak(float fc, float q, float gain){
    FilterStage::setPeak(coefficients, fc*pioversr, q, gain);
    copyCoefficients();
  }

  /**
   * Configure a low shelf filter with variable gain.
   * @param gain in dB
   */
  void setLowShelf(float fc, float gain){
    FilterStage::setLowShelf(coefficients, fc*pioversr, gain);
    copyCoefficients();
  }

  /**
   * Configure a high shelf filter with variable gain.
   * @param gain in dB
   */
  void setHighShelf(float fc, float gain){
    FilterStage::setHighShelf(coefficients, fc*pioversr, gain);
    copyCoefficients();
  }

  /**
   * Sets coefficients to point to a different set of values
   */
  void setCoefficients(FloatArray newCoefficients){
    coefficients = newCoefficients.getData();
    init();
  }

  /**
   * Copies coefficient values from an array.
   */
  void copyCoefficients(FloatArray newCoefficients){
    getFilterStage(0).copyCoefficients(newCoefficients);
    copyCoefficients(); //set all the other stages
  }

  static BiquadFilter* create(float sr, size_t stages=1){
    return new BiquadFilter(sr, new float[stages*BIQUAD_COEFFICIENTS_PER_STAGE], new float[stages*BIQUAD_STATE_VARIABLES_PER_STAGE], stages);
  }

  static void destroy(BiquadFilter* filter){
    ////delete[] filter->coefficients;
    ////delete[] filter->state;
    ////delete filter;
  }
};

class MultiBiquadFilter : public BiquadFilter, public MultiSignalProcessor {
private:
  BiquadFilter* filters;
  size_t channels;
protected:
public:
  MultiBiquadFilter(float sr, float* coefs, float* states, size_t stages, BiquadFilter* filters, size_t len) :
    BiquadFilter(sr, coefs, states, stages), filters(filters), channels(len){}
  virtual ~MultiBiquadFilter(){}
  static MultiBiquadFilter* create(float sr, size_t channels, size_t stages=1){
    BiquadFilter* filters = new BiquadFilter[channels-1];
    float* coefs = new float[stages*BIQUAD_COEFFICIENTS_PER_STAGE];
    float* states = new float[stages*BIQUAD_STATE_VARIABLES_PER_STAGE*channels];
    FloatArray coefficients(coefs, stages*BIQUAD_COEFFICIENTS_PER_STAGE);
    float* mystate = states;
    for(size_t ch=1; ch<channels; ++ch){
      states += stages*BIQUAD_STATE_VARIABLES_PER_STAGE;
      filters[ch-1].setSampleRate(sr);
      filters[ch-1].setStages(stages);
      filters[ch-1].setState(FloatArray(states, stages*BIQUAD_STATE_VARIABLES_PER_STAGE));
      filters[ch-1].setCoefficients(coefficients); // shared coefficients
    }
    return new MultiBiquadFilter(sr, coefs, mystate, stages, filters, channels);
  }  
  static void destroy(MultiBiquadFilter* filter){
    ////delete[] filter->coefficients;
    ////delete[] filter->state;
    ////delete[] filter->filters;
    ////delete filter;
  }
  BiquadFilter* getFilter(size_t channel){
    if(channel == 0)
      return this;
    if(channel < channels)
      return &filters[channel-1];
    return NULL;
  }
  void process(AudioBuffer &input, AudioBuffer &output){
    size_t len = min((int)channels, min(input.getChannels(), output.getChannels()));
    BiquadFilter::process(input.getSamples(0), output.getSamples(0));
    for(size_t ch=1; ch<len; ++ch)
      filters[ch-1].process(input.getSamples(ch), output.getSamples(ch));
  }
};

class StereoBiquadFilter : public MultiBiquadFilter {
public:
  StereoBiquadFilter(float sr, float* coefs, float* states, size_t stages, BiquadFilter* filters) :
    MultiBiquadFilter(sr, coefs, states, stages, filters, 2) {}
  static StereoBiquadFilter* create(float sr, size_t stages=1){
    size_t channels = 2;
    BiquadFilter* filters = new BiquadFilter[channels-1];
    float* coefs = new float[stages*BIQUAD_COEFFICIENTS_PER_STAGE];
    float* states = new float[stages*BIQUAD_STATE_VARIABLES_PER_STAGE*channels];
    FloatArray coefficients(coefs, stages*BIQUAD_COEFFICIENTS_PER_STAGE);
    float* mystate = states;
    for(size_t ch=1; ch<channels; ++ch){
      states += stages*BIQUAD_STATE_VARIABLES_PER_STAGE;
      filters[ch-1].setSampleRate(sr);
      filters[ch-1].setStages(stages);
      filters[ch-1].setState(FloatArray(states, stages*BIQUAD_STATE_VARIABLES_PER_STAGE));
      filters[ch-1].setCoefficients(coefficients); // shared coefficients
    }
    return new StereoBiquadFilter(sr, coefs, mystate, stages, filters);
  }
  static void destroy(StereoBiquadFilter* filter){
    MultiBiquadFilter::destroy(filter);
  }
};

#endif // __BiquadFilter_h__
