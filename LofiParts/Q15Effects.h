#include <cstdint>
// Q15 fixed-point representation of -1/3 (approx -0.33333).
// Calculation: round(-1/3 * 32767) = -10922
const int16_t COEFF_NEG_ONE_THIRD_Q15 = -10922;

/**
 * @brief Performs fixed-point Q1.15 multiplication (A * B) >> 15.
 * * Takes two Q15 inputs, multiplies them into a Q31/Q30 space (int32_t),
 * and shifts the result back to Q15.
 * * @param a First Q15 factor.
 * @param b Second Q15 factor.
 * @return The product in Q15 format.
 */
inline int16_t q15_mult(int16_t a, int16_t b) {
    // Cast to int32_t for the multiplication to prevent overflow
    // The result is implicitly Q30 or Q31 depending on implementation,
    // which is why we shift right by 15 bits.
    return (int16_t)(((int32_t)a * b) >> 15);
}


/**
 * @brief Applies soft clipping using a cubic polynomial approximation, controlled by a mix parameter.
 * * The function approximates the saturation curve y = x + mix * (-(1/3)x^3).
 * The mix parameter scales the strength of the compression term.
 * * @param input The audio sample in Q1.15 fixed-point format (-32768 to 32767).
 * @param mix_q15 The clipping depth/mix factor in Q1.15 format (0 = dry/no clipping, 32767 = max clipping).
 * @return The soft-clipped output sample in Q1.15 format.
 */
int16_t softClipCubicQ15(int16_t input, int16_t mix_q15) {
    // 1. Calculate x^2 (Q15 * Q15 -> Q15)
    int16_t x_squared = q15_mult(input, input);

    // 2. Calculate x^3 (x^2 * x -> Q15)
    int16_t x_cubed = q15_mult(x_squared, input);

    // 3. Calculate (-(1/3) * x^3) (Q15 * Q15 -> Q15)
    // This is the full, unmixed compression term.
    int16_t cubic_term_unmixed = q15_mult(COEFF_NEG_ONE_THIRD_Q15, x_cubed);
    
    // 4. Calculate (Mix * (-(1/3)x^3)) (Q15 * Q15 -> Q15)
    // Scale the compression term by the user's mix parameter.
    int16_t final_cubic_term = q15_mult(mix_q15, cubic_term_unmixed);

    // 5. Final result: y = x + (final_cubic_term)
    int16_t output = input + final_cubic_term;

    // We do not need a safety clamp here, as the final_cubic_term always acts
    // to pull the signal closer to zero, preventing standard Q15 overflow.
    return output;
}