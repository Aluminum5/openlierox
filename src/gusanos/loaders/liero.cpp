#include "liero.h"
#include "../gfx.h"
#include "FindFile.h"
#include <string>
#include <vector>
#include <cstring>
#include <iostream>
using std::cerr;
using std::endl;

#include <boost/filesystem/path.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/filesystem/convenience.hpp>
namespace fs = boost::filesystem;

LieroLevelLoader LieroLevelLoader::instance;
#ifndef DEDICATED_ONLY
LieroFontLoader LieroFontLoader::instance;
#endif

bool LieroLevelLoader::canLoad(std::string const& path, std::string& name)
{
	if(fs::extension(path) == ".lev")
	{
		name = GetBaseFilenameWithoutExt(path);
		return true;
	}
	return false;
}

#ifndef DEDICATED_ONLY

static array<unsigned char, 256*3> const lieroPalette = 
{0x0,0x0,0x0,0x6c,0x38,0x0,0x6c,0x50,0x0,0xa4,0x94,0x80,0x0,0x90,0x0,0x3c,0xac,0x3c,0xfc,0x54,0x54,0xa8,0xa8,0xa8,0x54,
0x54,0x54,0x54,0x54,0xfc,0x54,0xd8,0x54,0x54,0xfc,0xfc,0x78,0x40,0x8,0x80,0x44,0x8,0x88,0x48,0xc,0x90,0x50,0x10,0x98,
0x54,0x14,0xa0,0x58,0x18,0xac,0x60,0x1c,0x4c,0x4c,0x4c,0x54,0x54,0x54,0x5c,0x5c,0x5c,0x64,0x64,0x64,0x6c,0x6c,0x6c,
0x74,0x74,0x74,0x7c,0x7c,0x7c,0x84,0x84,0x84,0x8c,0x8c,0x8c,0x94,0x94,0x94,0x9c,0x9c,0x9c,0x38,0x38,0x88,0x50,0x50,
0xc0,0x68,0x68,0xf8,0x90,0x90,0xf4,0xb8,0xb8,0xf4,0x6c,0x6c,0x6c,0x90,0x90,0x90,0xb4,0xb4,0xb4,0xd8,0xd8,0xd8,0x20,
0x60,0x20,0x2c,0x84,0x2c,0x3c,0xac,0x3c,0x70,0xbc,0x70,0xa4,0xd4,0xa4,0x6c,0x6c,0x6c,0x90,0x90,0x90,0xb4,0xb4,0xb4,
0xd8,0xd8,0xd8,0xa8,0xa8,0xf8,0xd0,0xd0,0xf4,0xfc,0xfc,0xf4,0x3c,0x50,0x0,0x58,0x70,0x0,0x74,0x90,0x0,0x94,0xb0,0x0,
0x78,0x48,0x34,0x9c,0x78,0x58,0xc4,0xa8,0x7c,0xec,0xd8,0xa0,0x9c,0x78,0x58,0xc4,0xa8,0x7c,0xec,0xd8,0xa0,0xc8,0x64,
0x0,0xa0,0x50,0x0,0x48,0x48,0x48,0x6c,0x6c,0x6c,0x90,0x90,0x90,0xb4,0xb4,0xb4,0xd8,0xd8,0xd8,0xfc,0xfc,0xfc,0xc4,0xc4,
0xc4,0x90,0x90,0x90,0x98,0x3c,0x0,0xb4,0x64,0x0,0xd0,0x8c,0x0,0xec,0xb4,0x0,0xa8,0x54,0x0,0xd8,0x0,0x0,0xbc,0x0,0x0,
0xa4,0x0,0x0,0xc8,0x0,0x0,0xac,0x0,0x0,0xd8,0x0,0x0,0xbc,0x0,0x0,0xa4,0x0,0x0,0xd8,0x0,0x0,0xbc,0x0,0x0,0xa4,0x0,0x0,
0x50,0x50,0xc0,0x68,0x68,0xf8,0x90,0x90,0xf4,0x50,0x50,0xc0,0x68,0x68,0xf8,0x90,0x90,0xf4,0x94,0x88,0x0,0x88,0x7c,0x0,
0x7c,0x70,0x0,0x74,0x64,0x0,0x84,0x5c,0x28,0xa0,0x84,0x48,0xbc,0xb0,0x68,0xd8,0xdc,0x88,0xf8,0xf8,0xbc,0xf4,0xf4,0xfc,
0xfc,0x0,0x0,0xf8,0x18,0x4,0xf8,0x34,0x8,0xf8,0x50,0x10,0xf8,0x6c,0x14,0xf8,0x88,0x18,0xf8,0xa4,0x20,0xf8,0xc0,0x24,
0xf8,0xdc,0x28,0xf4,0xe8,0x3c,0xf4,0xf4,0x50,0xf4,0xf4,0x70,0xf4,0xf4,0x94,0xf0,0xf0,0xb4,0xf0,0xf0,0xd4,0xf0,0xf0,
0xf8,0x2c,0x84,0x2c,0x3c,0xac,0x3c,0x70,0xbc,0x70,0x2c,0x84,0x2c,0x3c,0xac,0x3c,0x70,0xbc,0x70,0xf8,0x3c,0x3c,0xf4,
0x7c,0x7c,0xf4,0xbc,0xbc,0x68,0x68,0xf8,0x90,0x90,0xf4,0xb8,0xb8,0xf4,0x90,0x90,0xf4,0x3c,0xac,0x3c,0x70,0xbc,0x70,
0xa4,0xd4,0xa4,0x70,0xbc,0x70,0x94,0x88,0x0,0x88,0x74,0x0,0x7c,0x60,0x0,0x70,0x4c,0x0,0x64,0x38,0x0,0x58,0x28,0x0,
0x68,0x68,0x88,0x90,0x90,0xc0,0xbc,0xbc,0xf8,0xc8,0xc8,0xf4,0xdc,0xdc,0xf4,0x28,0x70,0x28,0x2c,0x84,0x2c,0x34,0x98,
0x34,0x3c,0xac,0x3c,0xfc,0xc8,0xc8,0xf4,0xa4,0xa4,0xf8,0x5c,0x5c,0xf4,0x4c,0x4c,0xf4,0x3c,0x3c,0xf4,0x4c,0x4c,0xf4,
0x5c,0x5c,0xf4,0xa4,0xa4,0x54,0x28,0x0,0x58,0x28,0x0,0x5c,0x2c,0x0,0x60,0x30,0x0,0x3c,0x1c,0x0,0x40,0x1c,0x0,0x44,
0x20,0x0,0x48,0x24,0x0,0xfc,0xfc,0xfc,0xdc,0xdc,0xdc,0xbc,0xbc,0xbc,0x9c,0x9c,0x9c,0x7c,0x7c,0x7c,0x9c,0x9c,0x9c,
0xbc,0xbc,0xbc,0xdc,0xdc,0xdc,0x6c,0x4c,0x2c,0x7c,0x54,0x30,0x8c,0x60,0x38,0x9c,0x6c,0x40,0xac,0x78,0x48,0x0,0x0,
0x0,0x28,0x24,0x8,0x50,0x4c,0x14,0x78,0x74,0x1c,0xa0,0x98,0x28,0xc8,0xc0,0x30,0xf4,0xe8,0x3c,0x0,0x0,0x0,0x0,0x0,
0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0xfc,0x0,0x0,0xfc,0x24,0x0,
0xfc,0x48,0x0,0xfc,0x6c,0x0,0xfc,0x90,0x0,0xfc,0xb4,0x0,0xfc,0xd8,0x0,0xfc,0xfc,0x0,0xa8,0xf0,0x0,0x54,0xe8,0x0,
0x0,0xe0,0x0,0xfc,0x0,0x0,0xe8,0x4,0x14,0xd8,0xc,0x2c,0xc4,0x14,0x44,0xb4,0x18,0x58,0xa0,0x20,0x70,0x90,0x28,0x88,
0x7c,0x2c,0x9c,0x6c,0x34,0xb4,0x58,0x3c,0xcc,0x48,0x44,0xe4};

#endif

static unsigned char const lieroMaterials[1280]={
0,1,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,
0,0,1,1,1,0,0,0,1,1,1,0,0,0,0,0,0,0,1,1,
1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,
1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,1,1,
1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,1,1,1,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,
0,0,0,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,1,1,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,1,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
};

static unsigned char materialMappings[256];

enum LieroMaterialFlags
{
	Dirt = 0,
	Dirt2,
	Rock,
	Back,
	Shadow
};

bool getLieroMaterialFlag(LieroMaterialFlags type, int material)
{
	return lieroMaterials[material + int(type)*256];
}

void initMaterialMappings()
{
	for(int i = 0; i < 256; ++i)
	{
		bool dirt = getLieroMaterialFlag(Dirt, i);
		bool dirt2 = getLieroMaterialFlag(Dirt2, i);
		bool rock = getLieroMaterialFlag(Rock, i);
		bool back = getLieroMaterialFlag(Back, i);
		//bool shadow = getLieroMaterialFlag(Shadow, i); //unused
		
		int mapTo = 0;
		
		if(dirt || dirt2)
		{
			if(back)
				mapTo = 3; // Special dirt
			else
				mapTo = 2; // TODO: Change to real dirt
		}
		else if(rock)
		{
			mapTo = 0; // Rock
		}
		else if(back)
		{
			mapTo = 1; // Background
		}
		else
			mapTo = 4; // Special rock
			
		materialMappings[i] = mapTo;
	}
}

bool LieroLevelLoader::load(Level* level, std::string const& path)
{
	std::ifstream f;
	OpenGameFileR(f, path, std::ios::binary);
	if(!f)
		return false;
		
	f.seekg(0, std::ios::end);
	std::streamoff fileSize = f.tellg();
	f.seekg(0, std::ios::beg);
	
	const int width = 504;
	const int height = 350;
	const std::streamoff regularFileSize = width*height;
	if(fileSize < regularFileSize)
		return false;
		
	array<unsigned char, 256*3> palette;
	
#ifndef DEDICATED_ONLY
	palette = lieroPalette;
		
	if(fileSize >= width*height+10+256*3)
	{
		char magic[10];
		f.seekg(width*height, std::ios::beg);
		f.read(magic, 10);
		if(!memcmp(magic, "POWERLEVEL", 10))
		{
			f.read((char *)&palette[0], 256*3);
			for(array<unsigned char, 256*3>::iterator i = palette.begin();
				i != palette.end();
				++i)
			{
				*i *= 4;
			}
		}
		f.seekg(0, std::ios::beg);
	}
#endif
	
	level->material = create_bitmap_ex(8, width, height);
#ifndef DEDICATED_ONLY
	level->image = create_bitmap(width, height);
	level->background = create_bitmap(width, height);
#endif
	
	initMaterialMappings();

	for (int y = 0; y < height; y++)
	{
		for (int x = 0; x < width; x++)
		{
			int c = f.get();
			
#ifndef DEDICATED_ONLY
			unsigned char const* entry = &palette[c * 3];
			int imagec = makecol(entry[0], entry[1], entry[2]);
			putpixel(level->image, x, y, imagec);
			
			entry = &palette[(160 + (rndgen() & 3)) * 3];
			int backgroundc = makecol(entry[0], entry[1], entry[2]);
			putpixel(level->background, x, y, backgroundc);
#endif
			
			putpixel(level->material, x, y, materialMappings[c]); //TODO
		}
	}

	level->loaderSucceeded();
	return true;
}

const char* LieroLevelLoader::getName()
{
	return "Liero level loader";
}

std::string LieroLevelLoader::format() { return "Liero level"; }
std::string LieroLevelLoader::formatShort() { return "Liero"; }


#ifndef DEDICATED_ONLY

bool LieroFontLoader::canLoad(std::string const& path, std::string& name)
{
	if(fs::extension(path) == ".lft")
	{
		name = GetBaseFilenameWithoutExt(path);
		return true;
	}
	return false;
}
	
bool LieroFontLoader::load(Font* font, std::string const& path)
{
	font->free();
	
	std::ifstream f;
	OpenGameFileR(f, path, std::ios::binary);
	if(!f)
		return false;
		
	long bitmapWidth = 7, bitmapHeight = 250 * 8;

	font->m_bitmap = create_bitmap_ex(8, bitmapWidth, bitmapHeight);
	if(!font->m_bitmap)
		return false;
		
	font->m_supportColoring = true;
		
	std::vector<char> buffer(16000);
	
	f.ignore(8); //First 8 bytes are useless
	f.read(&buffer[0], 16000);
	
	if(f.gcount() < 16000)
		return false;

	font->m_chars.assign(2, Font::CharInfo(Rect(0, 0, 1, 1), 0)); // Two empty slots

	int y = 0;
	for(int i = 0; i < 250; ++i)
	{
		int width = buffer[i * 8 * 8 + 64];
		if(width < 2)
			width = 2;
			
		int beginy = y;
		int endy = y;

		for(int y2 = 0; y2 < 8; ++y2, ++y)
		{
			for(int x = 0; x < 7; ++x)
			{
				char v = buffer[y*8 + x + 1];
				
				if(v)
					endy = y;
				
				int c = v ? 255 : 0;

				putpixel(font->m_bitmap, x, y, c);
			}
		}
		
		font->m_chars.push_back(Font::CharInfo(Rect(0, beginy, width, endy + 1), 0));
	}
	
	font->buildSubBitmaps();
	
	return true;
}

const char* LieroFontLoader::getName()
{
	return "Liero font loader";
}

std::string LieroFontLoader::format() { return "Liero font"; }
std::string LieroFontLoader::formatShort() { return "Liero"; }


#endif
