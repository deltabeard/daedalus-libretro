#include "Base/Daedalus.h"
#include "HLEAudio/AudioPlugin.h"
#include "Config/ConfigOptions.h"

EAudioPluginMode gAudioPluginEnabled = APM_DISABLED;

CAudioPlugin * CreateAudioPlugin()
{
	return NULL;
}
