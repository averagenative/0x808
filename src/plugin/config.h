/*
 * config.h — CPLUG plugin configuration for Sequencer_C.
 *
 * This file is force-included by the compiler (-include / /FI) so that
 * CPLUG's wrapper sources can read the #defines without any include chain.
 */

#ifndef SQ_PLUGIN_CONFIG_H
#define SQ_PLUGIN_CONFIG_H

#define CPLUG_IS_INSTRUMENT    1
#define CPLUG_WANT_GUI         1
#define CPLUG_GUI_RESIZABLE    1
#define CPLUG_WANT_MIDI_INPUT  1
#define CPLUG_WANT_MIDI_OUTPUT 0

#define CPLUG_COMPANY_NAME   "Sequencer_C"
#define CPLUG_COMPANY_EMAIL  ""
#define CPLUG_PLUGIN_NAME    "Sequencer_C"
#define CPLUG_PLUGIN_URI     "https://github.com/sequencer-c"
#define CPLUG_PLUGIN_VERSION "0.9.0"

/* VST3 categories — instrument + stereo */
#define CPLUG_VST3_CATEGORIES "Instrument|Drum|Stereo"

/* VST3 TUIDs — unique 128-bit IDs (four 32-bit chunks) */
#define CPLUG_VST3_TUID_COMPONENT  'SqCe', 'ngne', 'comp', 0x01
#define CPLUG_VST3_TUID_CONTROLLER 'SqCe', 'ngne', 'edit', 0x01

/* CLAP identification */
#define CPLUG_CLAP_ID          "com.sequencer-c.plugin"
#define CPLUG_CLAP_DESCRIPTION "Drum sequencer and synthesizer"
#define CPLUG_CLAP_FEATURES    CLAP_PLUGIN_FEATURE_INSTRUMENT, CLAP_PLUGIN_FEATURE_STEREO, CLAP_PLUGIN_FEATURE_DRUM_MACHINE

#endif /* SQ_PLUGIN_CONFIG_H */
