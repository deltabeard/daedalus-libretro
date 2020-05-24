/*
Copyright (C) 2003 Azimer
Copyright (C) 2001,2006 StrmnNrmn

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

//
//	N.B. This source code is derived from Azimer's Audio plugin (v0.55?)
//	and modified by StrmnNrmn to work with Daedalus PSP. Thanks Azimer!
//	Drop me a line if you get chance :)
//

#include "Base/Daedalus.h"
#include <stdio.h>
#include <new>

#include <pspkernel.h>
#include <pspaudiolib.h>
#include <pspaudio.h>

#include "HLEAudio/AudioPlugin.h"
#include "HLEAudio/audiohle.h"

#include "Config/ConfigOptions.h"
#include "Core/CPU.h"
#include "Core/Interrupt.h"
#include "Core/Memory.h"
#include "Core/ROM.h"
#include "Core/RSP_HLE.h"
#include "Debug/Console.h"
#include "HLEAudio/AudioBuffer.h"
#include "SysPSP/Utility/CacheUtil.h"
#include "Core/FramerateLimiter.h"
#include "System/Thread.h"

CAudioPlugin* gAudioPlugin = nullptr;
EAudioMode gAudioMode( AM_DISABLED );

#define RSP_AUDIO_INTR_CYCLES     1
extern u32 gSoundSync;

static const u32	kOutputFrequency = 44100;
static const u32	MAX_OUTPUT_FREQUENCY = kOutputFrequency * 4;


static bool audio_open = false;


// Large kAudioBufferSize creates huge delay on sound //Corn
static const u32	kAudioBufferSize = 1024 * 2; // OSX uses a circular buffer length, 1024 * 1024

#ifdef DAEDALUS_PSP_USE_ME

#include "SysPSP/PRX/MediaEngine/me.h"
#include "SysPSP/Utility/ModulePSP.h"

bool gLoadedMediaEnginePRX {false};

volatile me_struct *mei;

bool InitialiseMediaEngine()
{

	if( CModule::Load("mediaengine.prx") < 0 )	return false;

	mei = (volatile struct me_struct *)malloc_64(sizeof(struct me_struct));
	mei = (volatile struct me_struct *)(MAKE_UNCACHED_PTR(mei));
	sceKernelDcacheWritebackInvalidateAll();

	if (InitME(mei) == 0)
	{
		gLoadedMediaEnginePRX = true;
		return true;
	}
	else
	{
		#ifdef DAEDALUS_DEBUG_CONSOLE
		printf(" Couldn't initialize MediaEngine Instance\n");
		#endif
		return false;
	}

}

#endif

class AudioPluginPSP : public CAudioPlugin
{
public:

 AudioPluginPSP();
	virtual ~AudioPluginPSP();
	virtual void			Stop();

	virtual void			DacrateChanged( ESystemType SystemType );
	virtual void			LenChanged();
	virtual EProcessResult	ProcessAList();

	//virtual void SetFrequency(u32 frequency);
	virtual void AddBuffer( u8 * start, u32 length);
	virtual void FillBuffer( Sample * buffer, u32 num_samples);
  	virtual void			UpdateOnVbl( bool wait ) {};

	virtual void StopAudio();
	virtual void StartAudio();

public:
  CAudioBuffer *		mAudioBufferUncached;

private:
	CAudioBuffer * mAudioBuffer;
	bool mKeepRunning;
	bool mExitAudioThread;
	u32 mFrequency;
	s32 mAudioThread;
	s32 mSemaphore;
//	u32 mBufferLenMs;
};

bool CreateAudioPlugin()
{
	DAEDALUS_ASSERT(gAudioPlugin == nullptr, "Why is there already an audio plugin?");
	gAudioPlugin = new AudioPluginPSP();
	return true;
}

void DestroyAudioPlugin()
{
	// Make a copy of the plugin, and set the global pointer to NULL;
	// This stops other threads from trying to access the plugin
	// while we're in the process of shutting it down.
	// TODO(strmnnrmn): Still looks racey.
	AudioPluginPSP* plugin = static_cast<AudioPluginPSP*>(gAudioPlugin);
	gAudioPlugin = nullptr;
	if (plugin != nullptr)
	{
		plugin->Stop();
    pspAudioEndPre();
    sceKernelDelayThread(100000);
    pspAudioEnd();
		delete plugin;
	}
}

static AudioPluginPSP * ac;

void AudioPluginPSP::FillBuffer(Sample * buffer, u32 num_samples)
{
	sceKernelWaitSema( mSemaphore, 1, nullptr );

	mAudioBufferUncached->Drain( buffer, num_samples );

	sceKernelSignalSema( mSemaphore, 1 );
}


AudioPluginPSP::AudioPluginPSP()
:mKeepRunning (false)
//: mAudioBuffer( kAudioBufferSize )
, mFrequency( 44100 )
,	mSemaphore( sceKernelCreateSema( "AudioPluginPSP", 0, 1, 1, nullptr ) )
//, mAudioThread ( kInvalidThreadHandle )
//, mKeepRunning( false )
//, mBufferLenMs ( 0 )
{
	// Allocate audio buffer with malloc_64 to avoid cached/uncached aliasing
	void * mem = malloc_64( sizeof( CAudioBuffer ) );
	mAudioBuffer = new( mem ) CAudioBuffer( kAudioBufferSize );
  mAudioBufferUncached = (CAudioBuffer*)MAKE_UNCACHED_PTR(mem);
	dcache_wbinv_range_unaligned( mAudioBuffer, mAudioBuffer+sizeof( CAudioBuffer ) );

  #ifdef DAEDALUS_PSP_USE_ME
InitialiseMediaEngine();
#endif
}

AudioPluginPSP::~AudioPluginPSP()
{
	mAudioBuffer->~CAudioBuffer();
  free(mAudioBuffer);

  pspAudioEnd();
}

void	AudioPluginPSP::Stop()
{
    Audio_Reset();
  	StopAudio();

}

void	AudioPluginPSP::DacrateChanged( ESystemType SystemType )
{
u32 clock = (SystemType == ST_NTSC) ? VI_NTSC_CLOCK : VI_PAL_CLOCK;
u32 dacrate = Memory_AI_GetRegister(AI_DACRATE_REG);
u32 frequency = clock / (dacrate + 1);

#ifdef DAEDALUS_DEBUG_CONSOLE
Console_Print( "Audio frequency: %d", frequency);
#endif
mFrequency = frequency;
}


void	AudioPluginPSP::LenChanged()
{
	if( gAudioMode > AM_DISABLED )
	{
		u32 address = Memory_AI_GetRegister(AI_DRAM_ADDR_REG) & 0xFFFFFF;
		u32	length = Memory_AI_GetRegister(AI_LEN_REG);

		AddBuffer( gu8RamBase + address, length );
	}
	else
	{
		StopAudio();
	}
}



EProcessResult	AudioPluginPSP::ProcessAList()
{
	Memory_SP_SetRegisterBits(SP_STATUS_REG, SP_STATUS_HALT);

	EProcessResult	result = PR_NOT_STARTED;

	switch( gAudioMode )
	{
		case AM_DISABLED:
			result = PR_COMPLETED;
			break;
		case AM_ENABLED_ASYNC:
			{
        sceKernelDcacheWritebackInvalidateAll();
				if(BeginME( mei, (int)&Audio_Ucode, (int)NULL, -1, NULL, -1, NULL) < 0)
        {
						Audio_Ucode();
				}
			}
			result = PR_COMPLETED;
			break;
		case AM_ENABLED_SYNC:
			Audio_Ucode();
			result = PR_COMPLETED;
			break;
	}

	return result;
}


void audioCallback( void * buf, unsigned int length, void * userdata )
{
	AudioPluginPSP * ac( reinterpret_cast< AudioPluginPSP * >( userdata ) );

	ac->FillBuffer( reinterpret_cast< Sample * >( buf ), length );
}


void AudioPluginPSP::StartAudio()
{
	if (mKeepRunning)
		return;

	mKeepRunning = true;

	ac = this;


pspAudioInit();
pspAudioSetChannelCallback( 0, audioCallback, this );

	// Everything OK
	audio_open = true;
}

void AudioPluginPSP::AddBuffer( u8 *start, u32 length )
{
	if (length == 0)
		return;

	if (!mKeepRunning)
		StartAudio();

	u32 num_samples = length / sizeof( Sample );

	switch( gAudioMode )
	{
	case AM_DISABLED:
		break;

	case AM_ENABLED_ASYNC:
		{
      mAudioBufferUncached->AddSamples( reinterpret_cast< const Sample * >( start ), num_samples, mFrequency, kOutputFrequency );

		}
		break;

	case AM_ENABLED_SYNC:
		{
			mAudioBufferUncached->AddSamples( reinterpret_cast< const Sample * >( start ), num_samples, mFrequency, kOutputFrequency );
		}
		break;
	}

	/*
	u32 remaining_samples = mAudioBuffer.GetNumBufferedSamples();
	mBufferLenMs = (1000 * remaining_samples) / kOutputFrequency);
	float ms = (float) num_samples * 1000.f / (float)mFrequency;
	#ifdef DAEDALUS_DEBUG_CONSOLE
	DPF_AUDIO("Queuing %d samples @%dHz - %.2fms - bufferlen now %d\n", num_samples, mFrequency, ms, mBufferLenMs);
	#endif
	*/
}

void AudioPluginPSP::StopAudio()
{
	if (!mKeepRunning)
		return;

	mKeepRunning = false;

	audio_open = false;
}
