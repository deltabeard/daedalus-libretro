#include "Base/Daedalus.h"

#include <stdio.h>

#include "Core/Memory.h"
#include "Debug/DBGConsole.h"
#include "Graphics/GraphicsContext.h"
#include "HLEGraphics/BaseRenderer.h"
#include "HLEGraphics/DisplayListDebugger.h"
#include "HLEGraphics/DLParser.h"
#include "HLEGraphics/TextureCache.h"
#include "Interface/Preferences.h"
#include "HLEGraphics/GraphicsPlugin.h"
#include "SysGL/GL.h"
#include "System/Timing.h"

CGraphicsPlugin* gGraphicsPlugin = NULL;
extern SDL_Window * gWindow;

EFrameskipValue     gFrameskipValue = FV_DISABLED;
bool                gTakeScreenshot = false;

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
	FILE *				gFramerateFile = NULL;
#endif

static void	UpdateFramerate()
{
#ifdef DAEDALUS_FRAMERATE_ANALYSIS
	gTotalFrames++;
#endif
	gFlipCount++;

	u64			now;
	NTiming::GetPreciseTime( &now );

	if(gLastFramerateCalcTime == 0)
	{
		u64		freq;
		gLastFramerateCalcTime = now;

		NTiming::GetPreciseFrequency( &freq );
		gTicksPerSecond = freq;
	}

#ifdef DAEDALUS_FRAMERATE_ANALYSIS
	if( gFramerateFile == NULL )
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
		if( gFramerateFile != NULL )
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
	if (!CreateRenderer())
	{
		return false;
	}

	if (!CTextureCache::Create())
	{
		return false;
	}

	if (!DLParser_Initialise())
	{
		return false;
	}
RSP_HLE_RegisterDisplayListEventHandler(this);
Memory_RegisterVIOriginChangedEventHandler(this);
	return true;
}

void CGraphicsPlugin::Finalise()
{
	DBGConsole_Msg(0, "Finalising GLGraphics");
	Memory_UnregisterVIOriginChangedEventHandler(this);
	RSP_HLE_UnregisterDisplayListProcessor(this);
	DLParser_Finalise();
	CTextureCache::Destroy();
	DestroyRenderer();
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


void CGraphicsPlugin::UpdateScreen()
{
	u32 current_origin = Memory_VI_GetRegister(VI_ORIGIN_REG);

	if (current_origin != LastOrigin)
	{
		UpdateFramerate();

		// FIXME: safe printf
		char string[22];
		sprintf(string, "Daedalus | FPS %#.1f", gCurrentFramerate);

	SDL_SetWindowTitle(gWindow, string);

		if (gTakeScreenshot)
		{
			CGraphicsContext::Get()->DumpNextScreen();
			gTakeScreenshot = false;
		}

		CGraphicsContext::Get()->UpdateFrame( false );

		LastOrigin = current_origin;
	}
}
