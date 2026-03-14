/*
 * envelope.h — ADSR envelope generator and LFO.
 */

#ifndef SQ_ENVELOPE_H
#define SQ_ENVELOPE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "engine/engine.h"

/* Initialize an envelope to idle state */
void envelope_init(sq_envelope_t *env);

/* Trigger the envelope (start attack phase) */
void envelope_trigger(sq_envelope_t *env, const sq_adsr_params_t *params,
                      uint32_t sample_rate);

/* Release the envelope (start release phase) */
void envelope_release(sq_envelope_t *env, const sq_adsr_params_t *params,
                      uint32_t sample_rate);

/* Process one sample, returns current level (0.0 - 1.0) */
float envelope_process(sq_envelope_t *env, const sq_adsr_params_t *params,
                       uint32_t sample_rate);

/* Process one LFO sample, returns -1.0 to 1.0 */
float lfo_process(sq_lfo_t *lfo, uint32_t sample_rate,
                  const sq_wavetables_t *wt);

/* Initialize LFO phase */
void lfo_init(sq_lfo_t *lfo);

#ifdef __cplusplus
}
#endif

#endif /* SQ_ENVELOPE_H */
