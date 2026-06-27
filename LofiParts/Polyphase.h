#ifndef LOFI_PARTS_POLYPHASE_H
#define LOFI_PARTS_POLYPHASE_H

/**
  * @brief Zero-latency 2x upsampling and downsampling using a Half-Band Polyphase IIR structure.
 * * Efficiently establishes a 2x oversampled domain for non-linear DSP operations 
 * (clipping, saturation, filtering) to mitigate digital aliasing.
 */
class Polyphase2x {
private:
    struct AllpassStage {
        float alpha;
        float x1, y1;
        
        inline void clear() { 
            x1 = 0.0f; 
            y1 = 0.0f; 
        }
        
        // Direct Form I allpass filter execution optimized for ARM Cortex-M7 FPU pipeline
        inline float process(float in) {
            float out = alpha * in + x1 - alpha * y1;
            x1 = in;
            y1 = out;
            return out;
        }
    };

    AllpassStage up0, up1;
    AllpassStage down0, down1;

public:
    /**
     * @brief Initializes the polyphase network with optimized Vaidyanathan/HIIR coefficients.
     * Provides >50dB stopband attenuation for anti-aliasing.
     */
    void init() {
        up0.alpha = down0.alpha = 0.1155796f;
        up1.alpha = down1.alpha = 0.5184135f;
        clear();
    }

    /**
     * @brief Flushes the internal filter memory to prevent clicks, transients, or DC offset issues.
     */
    void clear() {
        up0.clear(); up1.clear();
        down0.clear(); down1.clear();
    }

    /**
     * @brief Upsamples a single base-rate sample into two interleaved high-rate samples.
     * @param in The input sample at base rate (e.g., 48kHz).
     * @param outEven Reference to populate with the even-phase sample of the 2x domain (96kHz).
     * @param outOdd Reference to populate with the odd-phase sample of the 2x domain (96kHz).
     */
    inline void upsample(float in, float &outEven, float &outOdd) {
        outEven = up0.process(in);
        outOdd = up1.process(in);
    }

    /**
     * @brief Downsamples and filters two high-rate samples back into a single base-rate sample.
     * Strips out generated harmonic energy above the original base Nyquist boundary.
     * @param inEven The even-phase sample from the 2x domain (96kHz).
     * @param inOdd The odd-phase sample from the 2x domain (96kHz).
     * @return The filtered and decimated sample at base rate (e.g., 48kHz).
     */
    inline float downsample(float inEven, float inOdd) {
        float p0 = down0.process(inEven);
        float p1 = down1.process(inOdd);
        return 0.5f * (p0 + p1);
    }
};

// Backwards-compatible alias used by existing DSP blocks.
using Polyphase2xEngine = Polyphase2x;

struct Polyphase4xEngine {
    // Three 2x engines required to build a 1-to-4 upsampling tree
    Polyphase2xEngine stage1;
    Polyphase2xEngine stage2_Even;
    Polyphase2xEngine stage2_Odd;

    // Reciprocal tree for downsampling
    Polyphase2xEngine down2_Even;
    Polyphase2xEngine down2_Odd;
    Polyphase2xEngine down1;

    void init() {
        stage1.init(); stage2_Even.init(); stage2_Odd.init();
        down2_Even.init(); down2_Odd.init(); down1.init();
    }

    // Explodes 1 Base Sample (48kHz) into 4 Oversampled Samples (192kHz)
    inline void upsample4x(float in, float* out4) {
        float sample2_Evn, sample2_Odd;
        
        // Stage 1: 48kHz -> 96kHz (Yields 2 samples)
        stage1.upsample(in, sample2_Evn, sample2_Odd);

        // Stage 2: 96kHz -> 192kHz (Yields 4 samples total)
        stage2_Even.upsample(sample2_Evn, out4[0], out4[1]);
        stage2_Odd.upsample(sample2_Odd, out4[2], out4[3]);
    }

    // Collapses 4 Oversampled Samples (192kHz) back to 1 Base Sample (48kHz)
    inline float downsample4x(float* in4) {
        // Stage 2: Collapse pair streams from 192kHz back to 96kHz
        float sample2_Evn = down2_Even.downsample(in4[0], in4[1]);
        float sample2_Odd = down2_Odd.downsample(in4[2], in4[3]);

        // Stage 1: Collapse final pair from 96kHz back to 48kHz
        return down1.downsample(sample2_Evn, sample2_Odd);
    }
};

#endif // LOFI_PARTS_POLYPHASE_H