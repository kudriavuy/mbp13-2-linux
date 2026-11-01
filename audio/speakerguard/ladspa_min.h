/*
 * The subset of the LADSPA 1.1 plugin ABI used by mbp_speakerguard, so it
 * builds without the ladspa package. Layout and constants follow the public
 * LADSPA specification (www.ladspa.org); hosts load the plugin by it.
 */
#ifndef LADSPA_MIN_H
#define LADSPA_MIN_H

typedef float LADSPA_Data;
typedef int LADSPA_Properties;
#define LADSPA_PROPERTY_REALTIME	0x1
#define LADSPA_PROPERTY_INPLACE_BROKEN	0x2
#define LADSPA_PROPERTY_HARD_RT_CAPABLE	0x4

typedef int LADSPA_PortDescriptor;
#define LADSPA_PORT_INPUT	0x1
#define LADSPA_PORT_OUTPUT	0x2
#define LADSPA_PORT_CONTROL	0x4
#define LADSPA_PORT_AUDIO	0x8

typedef int LADSPA_PortRangeHintDescriptor;
#define LADSPA_HINT_BOUNDED_BELOW	0x1
#define LADSPA_HINT_BOUNDED_ABOVE	0x2
#define LADSPA_HINT_DEFAULT_MINIMUM	0x40
#define LADSPA_HINT_DEFAULT_MIDDLE	0xC0
#define LADSPA_HINT_DEFAULT_MAXIMUM	0x140
#define LADSPA_HINT_DEFAULT_0		0x200

typedef struct _LADSPA_PortRangeHint {
	LADSPA_PortRangeHintDescriptor HintDescriptor;
	LADSPA_Data LowerBound;
	LADSPA_Data UpperBound;
} LADSPA_PortRangeHint;

typedef void *LADSPA_Handle;

typedef struct _LADSPA_Descriptor {
	unsigned long UniqueID;
	const char *Label;
	LADSPA_Properties Properties;
	const char *Name;
	const char *Maker;
	const char *Copyright;
	unsigned long PortCount;
	const LADSPA_PortDescriptor *PortDescriptors;
	const char *const *PortNames;
	const LADSPA_PortRangeHint *PortRangeHints;
	void *ImplementationData;
	LADSPA_Handle (*instantiate)(const struct _LADSPA_Descriptor *Descriptor, unsigned long SampleRate);
	void (*connect_port)(LADSPA_Handle Instance, unsigned long Port, LADSPA_Data *DataLocation);
	void (*activate)(LADSPA_Handle Instance);
	void (*run)(LADSPA_Handle Instance, unsigned long SampleCount);
	void (*run_adding)(LADSPA_Handle Instance, unsigned long SampleCount);
	void (*set_run_adding_gain)(LADSPA_Handle Instance, LADSPA_Data Gain);
	void (*deactivate)(LADSPA_Handle Instance);
	void (*cleanup)(LADSPA_Handle Instance);
} LADSPA_Descriptor;

const LADSPA_Descriptor *ladspa_descriptor(unsigned long Index);

#endif
