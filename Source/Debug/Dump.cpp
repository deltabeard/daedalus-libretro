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
#include "stdafx.h"

#include <ctype.h>



#include "Config/ConfigOptions.h"
#include "Core/CPU.h"
#include "Core/Interrupt.h"
#include "Core/Memory.h"
#include "Core/ROMBuffer.h"
#include "Debug/Dump.h"
#include "Debug/DebugLog.h"
#include "Debug/DBGConsole.h"
#include "OSHLE/patch.h"		// For GetCorrectOp
#include "OSHLE/ultra_R4300.h"
#include "System/Paths.h"
#include "System/IO.h"
#include "Core/PrintOpCode.h"

static IO::Filename gDumpDir = "";

// Initialise the directory where files are dumped
// Appends subdir to the global dump base. Stores in rootdir
void Dump_GetDumpDirectory(char * rootdir, const char * subdir)
{
	if (gDumpDir[0] == '\0')
	{
		// Initialise
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
	if(CDebugConsole::IsAvailable())
	{
		//DBGConsole_Msg( 0, "Dump dir: [C%s]", rootdir );
	}
#endif
	IO::Directory::EnsureExists(rootdir);

}


// E.g. Dump_GetSaveDirectory([out], "c:\roms\test.rom", ".sra")
// would first try to find the save in g_DaedalusConfig.mSaveDir. If this is not
// found, g_DaedalusConfig.mRomsDir is checked.
void Dump_GetSaveDirectory(char * rootdir, const char * rom_filename, const char * extension)
{
	// If the Save path has not yet been set up, prompt user
	if (strlen(g_DaedalusConfig.mSaveDir) == 0)
	{
		// FIXME: missing prompt here!

		// User may have cancelled
		if (strlen(g_DaedalusConfig.mSaveDir) == 0)
		{
			// Default to rom path
			IO::Path::Assign(g_DaedalusConfig.mSaveDir, rom_filename);
			IO::Path::RemoveFileSpec(g_DaedalusConfig.mSaveDir);
#ifndef DAEDALUS_PSP
			// FIXME(strmnnrmn): for OSX I generate savegames in a subdir Save, to make it easier to clean up.
			IO::Path::Append(g_DaedalusConfig.mSaveDir, "Save");
#endif

#ifdef DAEDALUS_DEBUG_CONSOLE
			if(CDebugConsole::IsAvailable())
			{
				DBGConsole_Msg(0, "SaveDir is still empty - defaulting to [C%s]", g_DaedalusConfig.mSaveDir);
			}
#endif
		}
	}

	IO::Directory::EnsureExists(g_DaedalusConfig.mSaveDir);

	// Form the filename from the file spec (i.e. strip path and replace the extension)
	IO::Filename file_name;
	IO::Path::Assign(file_name, IO::Path::FindFileName(rom_filename));
	IO::Path::SetExtension(file_name, extension);

	IO::Path::Combine(rootdir, g_DaedalusConfig.mSaveDir, file_name);
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
	#ifdef DAEDALUS_PSP // For some reason this doesn't work on Posix so we swap it out
	IO::Path::Assign(file_name, IO::Path::FindFileName(rom_filename));
	#else
	IO::Path::Assign(file_name, rom_filename);
	#endif
	IO::Path::SetExtension(file_name, extension);

	IO::Path::Combine(rootdir, g_DaedalusConfig.mCacheDir, file_name);
}

#ifndef DAEDALUS_SILENT
//*****************************************************************************
//
//*****************************************************************************
void Dump_DisassembleMIPSRange(FILE * fh, u32 address_offset, const OpCode * b, const OpCode * e)
{
	u32 address( address_offset );
	const OpCode * p( b );
	while( p < e )
	{
		char opinfo[400];

		OpCode op( GetCorrectOp( *p ) );
#if 0
		if (translate_patches)
		{
			///TODO: We need a way to know this
			if (IsJumpTarget( op ))
			{
				fprintf(fp, "\n");
				fprintf(fp, "\n");
				fprintf(fp, "// %s():\n", Patch_GetJumpAddressName(current_pc));
			}
		}
#endif

		SprintOpCodeInfo( opinfo, address, op );
		fprintf(fh, "0x%08x: <0x%08x> %s\n", address, op._u32, opinfo);

		address += 4;
		++p;
	}
}

void Dump_Disassemble(u32 start, u32 end, const char * p_file_name)
{
	IO::Filename file_path;

	// Cute hack - if the end of the range is less than the start,
	// assume it is a length to disassemble
	if (end < start)
		end = start + end;

	if (p_file_name == NULL || strlen(p_file_name) == 0)
	{
		Dump_GetDumpDirectory(file_path, "");
		IO::Path::Append(file_path, "dis.txt");
	}
	else
	{
		IO::Path::Assign(file_path, p_file_name);
	}

	u8 * p_base;
	if (!Memory_GetInternalReadAddress(start, (void**)&p_base))
	{
		DBGConsole_Msg(0, "[Ydis: Invalid base 0x%08x]", start);
		return;
	}

	FILE * fp( fopen(file_path, "w") );
	if (fp == NULL)
		return;

	DBGConsole_Msg(0, "Disassembling from 0x%08x to 0x%08x ([C%s])", start, end, file_path);

	const OpCode * op_start( reinterpret_cast< const OpCode * >( p_base ) );
	const OpCode * op_end(   reinterpret_cast< const OpCode * >( p_base + (end-start) ) );

	Dump_DisassembleMIPSRange(fp, start, op_start, op_end);

	fclose(fp);
}
#endif

#ifndef DAEDALUS_SILENT
//*****************************************************************************
//
//	N.B. This assumbes that b/e are 4 byte aligned (otherwise endianness is broken)
//
//*****************************************************************************
void Dump_MemoryRange(FILE * fh, u32 address_offset, const u32 * b, const u32 * e)
{
	u32 address( address_offset );
	const u32 * p( b );
	while( p < e )
	{
		fprintf(fh, "0x%08x: %08x %08x %08x %08x ", address, p[0], p[1], p[2], p[3]);

		const u8 * p8( reinterpret_cast< const u8 * >( p ) );
		for (u32 i = 0; i < 16; i++)
		{
			u8 c( p8[i ^ U8_TWIDDLE] );
			if (c >= 32 && c < 128)
				fprintf(fh, "%c", c);
			else
				fprintf(fh, ".");

			if ((i%4)==3)
				fprintf(fh, " ");
		}
		fprintf(fh, "\n");

		address += 16;
		p += 4;
	}
}

void Dump_DisassembleRSPRange(FILE * fh, u32 address_offset, const OpCode * b, const OpCode * e)
{
	u32 address( address_offset );
	const OpCode * p( b );
	while( p < e )
	{
		char opinfo[400];
		SprintRSPOpCodeInfo( opinfo, address, *p );
		fprintf(fh, "0x%08x: <0x%08x> %s\n", address, p->_u32, opinfo);

		address += 4;
		++p;
	}
}

void Dump_RSPDisassemble(const char * p_file_name)
{
	u8 * base;
	u32 start = 0xa4000000;
	u32 end = 0xa4002000;

	if (!Memory_GetInternalReadAddress(start, (void**)&base))
	{
		DBGConsole_Msg(0, "[Yrdis: Invalid base 0x%08x]", start);
		return;
	}

	IO::Filename file_path;

	if (p_file_name == NULL || strlen(p_file_name) == 0)
	{
		Dump_GetDumpDirectory(file_path, "");
		IO::Path::Append(file_path, "rdis.txt");
	}
	else
	{
		IO::Path::Assign(file_path, p_file_name);
	}

	DBGConsole_Msg(0, "Disassembling from 0x%08x to 0x%08x ([C%s])", start, end, file_path);

	FILE * fp( fopen(file_path, "w") );
	if (fp == NULL)
		return;

	const u32 * mem_start( reinterpret_cast< const u32 * >( base + 0x0000 ) );
	const u32 * mem_end(   reinterpret_cast< const u32 * >( base + 0x1000 ) );

	Dump_MemoryRange( fp, start, mem_start, mem_end );

	const OpCode * op_start( reinterpret_cast< const OpCode * >( base + 0x1000 ) );
	const OpCode * op_end(   reinterpret_cast< const OpCode * >( base + 0x2000 ) );

	Dump_DisassembleRSPRange( fp, start + 0x1000, op_start, op_end );

	fclose(fp);
}
#endif

#ifndef DAEDALUS_SILENT
//*****************************************************************************
//
//*****************************************************************************
void Dump_Strings( const char * p_file_name )
{
	IO::Filename file_path;
	FILE * fp;

	static const u32 MIN_LENGTH = 5;

	if (p_file_name == NULL || strlen(p_file_name) == 0)
	{
		Dump_GetDumpDirectory(file_path, "");
		IO::Path::Append(file_path, "strings.txt");
	}
	else
	{
		IO::Path::Assign(file_path, p_file_name);
	}

	DBGConsole_Msg(0, "Dumping strings in rom ([C%s])", file_path);

	// Overwrite here
	fp = fopen(file_path, "w");
	if (fp == NULL)
		return;

	// Memory dump
	u32 ascii_start = 0;
	u32 ascii_count = 0;
	for ( u32 i = 0; i < RomBuffer::GetRomSize(); i ++)
	{
		if ( RomBuffer::ReadValueRaw< u8 >( i ^ 0x3 ) >= ' ' &&
			 RomBuffer::ReadValueRaw< u8 >( i ^ 0x3 ) < 200 )
		{
			if ( ascii_count == 0 )
			{
				ascii_start = i;
			}
			ascii_count++;
		}
		else
		{
			if ( ascii_count >= MIN_LENGTH )
			{
				fprintf( fp, "0x%08x: ", ascii_start );

				for ( u32 j = 0; j < ascii_count; j++ )
				{
					fprintf( fp, "%c", RomBuffer::ReadValueRaw< u8 >( (ascii_start + j ) ^ 0x3 ) );
				}

				fprintf( fp, "\n");
			}

			ascii_count = 0;
		}
	}
	fclose(fp);
}
#endif
