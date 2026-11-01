// SPDX-FileCopyrightText: 2026 Ahmed Imamovic
// SPDX-License-Identifier: CC-BY-SA-4.0
#ifndef UBERCLOCK_HW_H
#define UBERCLOCK_HW_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Commit the pending CSR writes staged on the config-link (the
 *  gateware latches many registers together on this pulse). Call after
 *  one or more hw_set_*()/uberclock_channel_set_*() writes. */
void uberclock_commit_config(void);

/** @brief Convert a frequency in Hz to a 26-bit NCO phase increment.
 *  @param f_hz  Target frequency, Hz.
 *  @param fs_hz Sample rate the NCO runs at, Hz.
 *  @return Rounded phase increment, or 0 if fs_hz is 0. */
uint32_t uberclock_phase_inc_from_hz(uint32_t f_hz, uint32_t fs_hz);

/** @brief Convert a 26-bit NCO phase increment back to Hz.
 *  @param phase_inc Phase increment as written to a phase_inc_* CSR.
 *  @param fs_hz     Sample rate the NCO runs at, Hz.
 *  @return Frequency in Hz, rounded to the nearest integer. */
uint32_t uberclock_phase_inc_to_hz(uint32_t phase_inc, uint32_t fs_hz);

/** @brief Convert a 26-bit NCO phase increment to milli-Hz (Hz * 1000),
 *  for sub-Hz-resolution diagnostics.
 *  @param phase_inc Phase increment as written to a phase_inc_* CSR.
 *  @param fs_hz     Sample rate the NCO runs at, Hz.
 *  @return Frequency in milli-Hz, rounded to the nearest integer. */
int64_t uberclock_phase_inc_to_mhz(uint32_t phase_inc, uint32_t fs_hz);

/** @brief Set the input CORDIC NCO phase increment (main_phase_inc_nco). */
void hw_set_phase_nco(uint32_t phase_inc);

/** @brief Read back the input CORDIC NCO phase increment. */
uint32_t hw_get_phase_nco(void);

/** @brief Set the input NCO magnitude (signed 12-bit, main_nco_mag). */
void hw_set_nco_mag(int32_t magnitude);

/** @brief Set the reference-channel downconversion phase increment
 *  (main_phase_inc_down_ref). Not part of the indexed channel array:
 *  the reference channel has no CPU/gain/mag counterpart. */
void hw_set_phase_down_ref(uint32_t phase_inc);

/** @brief Select the main input source (main_input_select: 0=ADC, 1=NCO,
 *  2=SUM, 3=reserved). */
void hw_set_input_select(uint32_t value);

/** @brief Read back the main input source selector. */
uint32_t hw_get_input_select(void);

/** @brief Select the upsampler input mux source (0=Gain, 1=CPU,
 *  2=CPU NCO). */
void hw_set_upsampler_input_mux(uint32_t value);

/** @brief Select DAC1's output source (0..15; 14=ref_y, 15=sum). */
void hw_set_output_select_ch1(uint32_t value);

/** @brief Select DAC2's output source (0..15; 14=ref_y, 15=sum). */
void hw_set_output_select_ch2(uint32_t value);

/** @brief Select the low-speed debug mux source (0..7; 6=ref_y). */
void hw_set_lowspeed_dbg_select(uint32_t value);

/** @brief Select the high-speed debug mux source (0..3; 1=filter_in_1). */
void hw_set_highspeed_dbg_select(uint32_t value);

/** @brief Set the final output shift amount (0..7). */
void hw_set_final_shift(int32_t value);

/** @brief Select the DDR capture source mux: 0 = ramp generator,
 *  1 = live design data. Shared by the standalone cap_enable command and
 *  the UberDDR3/S2MM ub_setmode/ub_ramp/ub_cap commands, since it is the
 *  same physical register either way. */
void hw_set_cap_enable(uint32_t enable);

/** @brief Read back the DDR capture source mux (0=ramp, 1=capture). */
uint32_t hw_get_cap_enable(void);

/** @brief Set the DDR S2MM capture length in 256-bit beats. */
void hw_set_cap_beats(uint32_t beats);

/** @brief Write the same signed 16-bit value to upsampler_input_x1..x5. */
void hw_write_upsampler_inputs_all_x(int16_t value);

/** @brief Write the same signed 16-bit value to upsampler_input_y1..y5. */
void hw_write_upsampler_inputs_all_y(int16_t value);

/** @brief Read the latched CORDIC magnitude register. */
int16_t hw_read_magnitude(void);

#ifdef __cplusplus
}
#endif

#endif /* UBERCLOCK_HW_H */
