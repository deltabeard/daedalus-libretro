/*
Copyright (C) 2007 StrmnNrmn

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

#include "Base/Daedalus.h"

#include <pspdebug.h>

#include "Core/Memory.h"
#include "Core/FramerateLimiter.h"
#include "Debug/DBGConsole.h"
#include "Graphics/GraphicsContext.h"
#include "HLEGraphics/BaseRenderer.h"
#include "HLEGraphics/TextureCache.h"
#include "HLEGraphics/DLParser.h"
#include "HLEGraphics/DisplayListDebugger.h"
#include "HLEGraphics/GraphicsPlugin.h"
#include "Interface/Preferences.h"
#include "System/Timing.h"
#include "Utility/Profiler.h"


//#define DAEDALUS_FRAMERATE_ANALYSIS
extern void battery_warning();
extern void HandleEndOfFrame();

extern bool gFrameskipActive;



CGraphicsPlugin * 	gGraphicsPlugin = NULL;

u32		gSoundSync = 44100;
bool	gTakeScreenshot = false;
bool	gTakeScreenshotSS = false;

EFrameskipValue		gFrameskipValue = FV_DISABLED;

namespace
{
	//u32					gVblCount = 0;
	u32					gFlipCount = 0;
	//float				gCurrentVblrate = 0.0f;
	float				gCurrentFramerate = 0.0f;
	u64					gLastFramerateCalcTime = 0;
	u64					gTicksPerSecond = 0;

#ifdef DAEDALUS_FRAMERATE_ANALYSIS
	u32					gTotalFrames = 0;
	u64					gFirstFrameTime = 0;
	FILE *				gFramerateFile = nullptr;
#endif

static void	UpdateFramerate()
{
#ifdef DAEDALUS_FRAMERATE_ANALYSIS
	gTotalFrames++;
#endif
	gFlipCount++;

	u64			now = 0;
	NTiming::GetPreciseTime( &now );

	if(gLastFramerateCalcTime == 0)
	{
		u64		freq = 0;
		gLastFramerateCalcTime = now;

		NTiming::GetPreciseFrequency( &freq );
		gTicksPerSecond = freq;
	}

#ifdef DAEDALUS_FRAMERATE_ANALYSIS
	if( gFramerateFile == nullptr )
	{
		gFirstFrameTime = now;
		gFramerateFile = fopen( "framerate.csv", "w" );
	}
	fprintf( gFramerateFile, "%d,%f\n", gTotalFrames, f32(now - gFirstFrameTime) / f32(gTicksPerSecond) );
#endif

	// If 1 second has elapsed since last recalculation, do it now
	u64		ticks_since_recalc( now - gLastFramerateCalcTime );
	if(ticks_since_recalc > gTicksPerSecond)
	{
		//gCurrentVblrate = float( gVblCount * gTicksPerSecond ) / float( ticks_since_recalc );
		gCurrentFramerate = float( gFlipCount * gTicksPerSecond ) / float( ticks_since_recalc );

		//gVblCount = 0;
		gFlipCount = 0;
		gLastFramerateCalcTime = now;

#ifdef DAEDALUS_FRAMERATE_ANALYSIS
		if( gFramerateFile != nullptr )
		{
			fflush( gFramerateFile );
		}
#endif
	}

}
}


bool CreateGraphicsPlugin()
{
	DAEDALUS_ASSERT(gGraphicsPlugin == nullptr, "The graphics plugin should not be initialised at this point");
	DBGConsole_Msg( 0, "Initialising Graphics Plugin" );

	CGraphicsPlugin * plugin = new CGraphicsPlugin();
	if (!plugin->Initialise())
	{
		delete plugin;
		plugin = nullptr;
	}

	gGraphicsPlugin = plugin;
	return plugin != nullptr;
}

void DestroyGraphicsPlugin()
{
	if (gGraphicsPlugin != nullptr)
	{
		gGraphicsPlugin->Finalise();
		delete gGraphicsPlugin;
		gGraphicsPlugin = nullptr;
	}
}

CGraphicsPlugin::~CGraphicsPlugin()
{
}

bool CGraphicsPlugin::Initialise()
{
	if(!CreateRenderer())
	{
		return false;
	}

	if(!CTextureCache::Create())
	{
		return false;
	}

	if (!DLParser_Initialise())
	{
		return false;
	}
	RSP_HLE_RegisterDisplayListProcessor(this);
	Memory_RegisterVIOriginChangedEventHandler(this);
	return true;
}

void CGraphicsPlugin::Finalise()
{
	DBGConsole_Msg(0, "Finalising PSPGraphics");
	Memory_UnregisterVIOriginChangedEventHandler(this);
	RSP_HLE_UnregisterDisplayListProcessor(this);
	DLParser_Finalise();
	CTextureCache::Destroy();
}


void CGraphicsPlugin::ProcessDisplayList()
{
#ifdef DAEDALUS_DEBUG_DISPLAYLIST
	if (!DLDebugger_Process())
	{
		DLParser_Process();
	}
#else
	DLParser_Process();
#endif
}

extern void RenderFrameBuffer(u32);
extern u32 gRDPFrame;

void CGraphicsPlugin::OnOriginChanged(u32 origin)
{
	// NB: if no display lists executed, interpret framebuffer
	if( gRDPFrame == 0 )
	{
		RenderFrameBuffer(origin & 0x7FFFFF);
	}
	else
	{
		UpdateScreen();
	}
}

#ifdef DAEDALUS_DEBUG_DISPLAYLIST
extern u32 gNumInstructionsExecuted;
extern u32 gNumDListsCulled;
extern u32 gNumRectsClipped;
#endif


void CGraphicsPlugin::UpdateScreen()
{
	//gVblCount++;

	u32 current_origin = Memory_VI_GetRegister(VI_ORIGIN_REG);
	static bool Old_FrameskipActive = false;
	static bool Older_FrameskipActive =false;

	if( current_origin != LastOrigin )
	{
		//printf( "Flip (%08x, %08x)\n", current_origin, last_origin );
		if( gGlobalPreferences.DisplayFramerate )
			UpdateFramerate();

		const f32 Fsync = FramerateLimiter_GetSync();
		//Calc sync rates for audio and game speed //Corn
		const f32 inv_Fsync = 1.0f / Fsync;
		gSoundSync = (u32)(44100.0f * inv_Fsync);
		// gVISyncRate = (u32)(1500.0f * inv_Fsync);
		// if( gVISyncRate > 4000 ) gVISyncRate = 4000;
		// else if ( gVISyncRate < 1500 ) gVISyncRate = 1500;

		if(!gFrameskipActive)
		{
			if( gGlobalPreferences.DisplayFramerate )
			{
				pspDebugScreenSetTextColor( 0xffffffff );
				pspDebugScreenSetBackColor(0);
				pspDebugScreenSetXY(0, 0);

				switch(gGlobalPreferences.DisplayFramerate)
				{
					case 1:
						pspDebugScreenPrintf( "%#.1f  ", gCurrentFramerate );
						break;
					case 2:
						pspDebugScreenPrintf( "FPS[%#.1f] VB[%d/%d] Sync[%#.1f%%]   ", gCurrentFramerate, u32( Fsync * f32( FramerateLimiter_GetTvFrequencyHz() ) ), FramerateLimiter_GetTvFrequencyHz(), Fsync * 100.0f );
						break;
					case 3:
#ifdef DAEDALUS_DEBUG_DISPLAYLIST
						pspDebugScreenPrintf( "Dlist[%d] Cull[%d] | Tris[%d] Cull[%d] | Rect[%d] Clip[%d] ", gNumInstructionsExecuted, gNumDListsCulled, gRenderer->GetNumTrisRendered(), gRenderer->GetNumTrisClipped(), gRenderer->GetNumRect(), gNumRectsClipped);
#else
						pspDebugScreenPrintf( "%#.1f  ", gCurrentFramerate );
#endif
						break;
				}
			}
			if( gGlobalPreferences.BatteryWarning )
			{
				battery_warning();
			}
			if(gTakeScreenshot)
			{
				CGraphicsContext::Get()->DumpNextScreen();
				gTakeScreenshot = false;
			}

			CGraphicsContext::Get()->UpdateFrame( false );
			HandleEndOfFrame();
		}

		static u32 current_frame = 0;
		current_frame++;


		Older_FrameskipActive = Old_FrameskipActive;
		Old_FrameskipActive = gFrameskipActive;

		switch(gFrameskipValue)
		{
		case FV_DISABLED:
			gFrameskipActive = false;
			break;
		case FV_AUTO1:
			if(!Old_FrameskipActive && (Fsync < 0.965f)) gFrameskipActive = true;
			else gFrameskipActive = false;
			break;
		case FV_AUTO2:
			if((!Old_FrameskipActive | !Older_FrameskipActive) && (Fsync < 0.965f)) gFrameskipActive = true;
			else gFrameskipActive = false;
			break;
		default:
			gFrameskipActive = (current_frame % (gFrameskipValue - 1)) != 0;
			break;
		}

		LastOrigin = current_origin;
	}
}
