/*
Copyright (C) 2001 StrmnNrmn

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

// Display stuff like registers, instructions, memory usage and so on
#include "Base/Daedalus.h"
#include "Debug/Dump.h"

#include "Config/ConfigOptions.h"
#include "Debug/Console.h"
#include "System/IO.h"
#include "System/Paths.h"
static IO::Filename gDumpDir = "";

// Initialise the directory where files are dumped
// Appends subdir to the global dump base. Stores in rootdir
void Dump_GetDumpDirectory(char *rootdir, const char* subdir)
{
	if (gDumpDir[0] == '\0')
	{
#if defined(DAEDALUS_DEBUG_DISPLAYLIST) || !defined(DAEDALUS_SILENT)
		IO::Path::Combine(gDumpDir, gDaedalusExePath, "Dumps");
#else
		IO::Path::Combine(gDumpDir, gDaedalusExePath, "ms0:/PICTURE/");
#endif
	}

	// If a subdirectory was specified, append
	if (subdir[0] != '\0')
	{
		IO::Path::Combine(rootdir, gDumpDir, subdir);
	}
	else
	{
		IO::Path::Assign(rootdir, gDumpDir);
	}

#ifdef DAEDALUS_DEBUG_CONSOLE
	if (CDebugConsole::IsAvailable())
	{
		// Console_Print( "Dump dir: [C%s]", rootdir );
	}
#endif
	IO::Directory::EnsureExists(rootdir);
}

	void Dump_GetCacheDirectory(char * rootdir, const char * rom_filename, const char * extension)
	{
		// If the Save path has not yet been set up, prompt user
		if (strlen(g_DaedalusConfig.mCacheDir) == 0)
		{
				// Default to rom path
				IO::Path::Assign(g_DaedalusConfig.mCacheDir, rom_filename);
				IO::Path::RemoveFileSpec(g_DaedalusConfig.mCacheDir);
		}


	IO::Directory::EnsureExists(g_DaedalusConfig.mCacheDir);

	// Form the filename from the file spec (i.e. strip path and replace the extension)
	IO::Filename file_name;
	IO::Path::Assign(file_name, rom_filename);
	IO::Path::SetExtension(file_name, extension);

	IO::Path::Combine(rootdir, g_DaedalusConfig.mCacheDir, file_name);
}
