/////////////////////////////////////////
//
//   OpenLieroX
//
//   Auxiliary Software class library
//
//   based on the work of JasonB
//   enhanced by Dark Charlie and Albert Zeyer
//
//   code under LGPL
//
/////////////////////////////////////////


// Auxiliary library
// Created 12/11/01
// By Jason Boettcher

#ifdef _MSC_VER
#pragma warning(disable: 4996)
#endif


#include <iomanip>
#include <time.h>
#include <SDL.h>
#define Font Font_Xlib // Hack to prevent name clash in precompiled header and system libs
#include <SDL_syswm.h>
#undef Font
#include <cstdlib>
#include <sstream>
#include <cstring>

#if defined(__APPLE__)
#include <mach/host_info.h>
#include <mach/mach_host.h>
#include <mach/mach_init.h>
#include <sys/sysctl.h>
#include <mach/mach_traps.h>
#elif defined(WIN32) || defined(WIN64)
#include <windows.h>
#else
#include <cstdio>
#include <unistd.h>
#endif

#ifdef __FREEBSD__
#include <sys/sysctl.h>
#include <vm/vm_param.h>
#include <sys/vmmeter.h>
#endif

#ifndef WIN32
#include <sys/types.h>
#include <sys/resource.h>
#include <unistd.h>
#endif

#include "LieroX.h"
#include "Cache.h"
#include "Debug.h"
#include "AuxLib.h"
#include "Error.h"
#include "DeprecatedGUI/Menu.h"
#include "GfxPrimitives.h"
#include "FindFile.h"
#include "InputEvents.h"
#include "StringUtils.h"
#include "sound/SoundsBase.h"
#include "Version.h"
#include "Timer.h"
#include "olx-types.h"
#include "CClient.h"
#include "CServer.h"
#include "Geometry.h"


Null null;	// Used in timer class



// TODO: is this the best format? why? comment that.
// Maybe SDL_PIXELFORMAT_ARGB8888 is better? My OpenGL renderer lists that as native format.
SDL_PixelFormat mainPixelFormat =
	{
		SDL_PIXELFORMAT_RGBA8888, // format
		NULL, //SDL_Palette *palette;
		32, //Uint8  BitsPerPixel;
		4, //Uint8  BytesPerPixel;
		{0, 0}, // padding
		0xff000000, 0xff0000, 0xff00, 0xff, //Uint32 Rmask, Gmask, Bmask, Amask;
		0, 0, 0, 0, //Uint8  Rloss, Gloss, Bloss, Aloss;
		24, 16, 8, 0, //Uint8  Rshift, Gshift, Bshift, Ashift;
		0, // refcount
		NULL // next ref
	};



///////////////////
// Initialize the standard Auxiliary Library
bool InitializeAuxLib()
{
	// We have already loaded all options from the config file at this time.

#ifdef linux
	//XInitThreads();	// We should call this before any SDL video stuff and window creation
#endif


	if(getenv("SDL_VIDEODRIVER"))
		notes << "SDL_VIDEODRIVER=" << getenv("SDL_VIDEODRIVER") << endl;

	// Solves problem with FPS in fullscreen
#ifdef WIN32
	if(!getenv("SDL_VIDEODRIVER")) {
		notes << "SDL_VIDEODRIVER not set, setting to directx" << endl;
		putenv((char*)"SDL_VIDEODRIVER=directx");
	}
#endif

	// Initialize SDL
	int SDLflags = SDL_INIT_TIMER | SDL_INIT_NOPARACHUTE;
	if(!bDedicated) {
		SDLflags |= SDL_INIT_VIDEO;
	} else {
		hints << "DEDICATED MODE" << endl;
		bDisableSound = true;
		bJoystickSupport = false;
	}

	if(SDL_Init(SDLflags) == -1) {
		errors << "Failed to initialize the SDL system!\nErrorMsg: " << std::string(SDL_GetError()) << endl;
#ifdef WIN32
		// retry it with any available video driver
		unsetenv("SDL_VIDEODRIVER");
		if(SDL_Init(SDLflags) != -1)
			hints << "... but we have success with the any driver" << endl;
		// retry with windib
		else if(putenv((char*)"SDL_VIDEODRIVER=windib") == 0 && SDL_Init(SDLflags) != -1)
			hints << "... but we have success with the windib driver" << endl;
		else
#endif
		return false;
	}

#ifndef DISABLE_JOYSTICK
	if(bJoystickSupport) {
		if(SDL_InitSubSystem(SDL_INIT_JOYSTICK) != 0) {
			warnings << "WARNING: couldn't init joystick subystem: " << SDL_GetError() << endl;
			bJoystickSupport = false;
		}
	}
#endif

	if(!bDedicated && !SetVideoMode())
		return false;

    // Enable the system events
    SDL_EventState(SDL_SYSWMEVENT, SDL_ENABLE);
	//SDL_EventState(SDL_VIDEOEXPOSE, SDL_ENABLE); // TODO: SDL2? it's SDL_WINDOWEVENT now

	// Enable unicode and key repeat
	//SDL_EnableUNICODE(1); // TODO: SDL2?
	//SDL_EnableKeyRepeat(200,20); // TODO: SDL2?

	
	/*
	Note about the different sound vars:
	  bDisableSound - if the sound system+driver is disabled permanentely
	  tLXOptions->bSoundOn - if the sound is enabled temporarely (false -> volume=0, nothing else)

	I.e., even with tLXOptions->bSoundOn=false (Audio.Enabled=false in config), the sound system
	will be loaded. To start OLX without the sound system, use the -nosound parameter.

	The console variable Audio.Enabled links to tLXOptions->bSoundOn.
	The console command 'sound' also wraps around tLXOptions->bSoundOn.

	tLXOptions->iSoundVolume will never be touched by OLX itself, only the user can modify it.
	tLXOptions->bSoundOn will also not be touched by OLX itself, only user actions can modify it.
	(Both points were somewhat broken earlier and kind of annoying.)
	*/
	
    if( !bDisableSound ) {
	    // Initialize sound
		//if(!InitSoundSystem(22050, 1, 512)) {
		if(!InitSoundSystem(44100, 1, 512)) {
		    warnings << "Failed the initialize the sound system" << endl;
			bDisableSound = true;
		}
    }
	if(bDisableSound) {
		notes << "soundsystem completly disabled" << endl;
		// NOTE: Don't change tLXOptions->bSoundOn here!
	}

	if( tLXOptions->bSoundOn ) {
		StartSoundSystem();
	}
	else
		StopSoundSystem();


	// Give a seed to the random number generator
	srand((unsigned int)time(NULL));

	if(!bDedicated) {
		//SmartPointer<SDL_Surface> bmpIcon = LoadGameImage("data/icon.png", true);
		//if(bmpIcon.get())
		// TODO use SDL_SetWindowIcon
		//	SDL_SetWindowIcon(bmpIcon.get(), NULL);
	}

	InitEventQueue();
	
	// Initialize the keyboard & mouse
	InitEventSystem();

	// Initialize timers
	InitializeTimers();

#ifdef DEBUG
	// Cache
	InitCacheDebug();
#endif


	return true;
}



///////////////////
// Set the video mode
bool SetVideoMode()
{
	if(bDedicated) {
		notes << "SetVideoMode: dedicated mode, ignoring" << endl;
		return true; // ignore this case
	}

	if (!tLXOptions)  {
		warnings << "SetVideoMode: Don't know what video mode to set, ignoring" << endl;
		return false;
	}

	return VideoPostProcessor::get()->initWindow();
}

#ifdef WIN32
//////////////////////
// Get the window handle
void *GetWindowHandle()
{
	SDL_SysWMinfo info;
	SDL_VERSION(&info.version);
	if(!SDL_GetWMInfo(&info))
		return 0;

	return (void *)info.window;
}
#endif


void CapFPS() {
	const TimeDiff fMaxFrameTime = TimeDiff( (tLXOptions->nMaxFPS > 0) ? (1.0f / (float)tLXOptions->nMaxFPS) : 0.0f );
	const AbsTime currentTime = GetTime();
	// tLX->currentTime is old time

	// Cap the FPS
	if(currentTime - tLX->currentTime < fMaxFrameTime)
		SDL_Delay((Uint32)(fMaxFrameTime - (currentTime - tLX->currentTime)).milliseconds());
	else
		// do at least one small break, else it's possible that we never receive signals from our OS
		SDL_Delay(1);
}


// Screenshot structure
struct screenshot_t {
	std::string sDir;
	std::string	sData;
};

struct ScreenshotQueue {
	std::list<screenshot_t> queue;
	SDL_mutex* mutex;
	ScreenshotQueue() : mutex(NULL) { mutex = SDL_CreateMutex(); }
	~ScreenshotQueue() { SDL_DestroyMutex(mutex); mutex = NULL; }
};

static ScreenshotQueue screenshotQueue;

void PushScreenshot(const std::string& dir, const std::string& data) {
	screenshot_t scr; scr.sDir = dir; scr.sData = data;
	ScopedLock lock(screenshotQueue.mutex);
	screenshotQueue.queue.push_back(scr);
}

static void TakeScreenshot(const std::string& scr_path, const std::string& additional_data);

////////////////
// Process any screenshots
void ProcessScreenshots()
{
	std::list<screenshot_t> scrs;
	{
		ScopedLock lock(screenshotQueue.mutex);
		scrs.swap(screenshotQueue.queue);
	}
	
	// Process all the screenhots in the queue
	for (std::list<screenshot_t>::iterator it = scrs.begin(); it != scrs.end(); it++)  {
		TakeScreenshot(it->sDir, it->sData);
	}
}







// ---------------- VideoPostProcessor ---------------------------------------------------------

VideoPostProcessor VideoPostProcessor::instance;


bool VideoPostProcessor::initWindow() {
	bool resetting = false;
	
	// Check if already running
	if (m_videoSurface.get())  {
		resetting = true;
		notes << "resetting video mode" << endl;
	} else {
		notes << "setting video mode" << endl;
	}
	
	// uninit first to ensure that the video thread is not running
	VideoPostProcessor::uninit();
	
	int vidflags = 0;
	
	// Check that the bpp is valid
	switch (tLXOptions->iColourDepth) {
		case 0:
		case 16:
		case 24:
		case 32:
			break;
		default: tLXOptions->iColourDepth = 16;
	}
	notes << "ColorDepth: " << tLXOptions->iColourDepth << endl;
	
	// BlueBeret's addition (2007): OpenGL support
	bool opengl = tLXOptions->bOpenGL;
	
	// Initialize the video
	if(tLXOptions->bFullscreen)  {
		//vidflags |= SDL_WINDOW_FULLSCREEN;
		vidflags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
	}
	
	if (opengl) {
		vidflags |= SDL_WINDOW_OPENGL;
		//#ifndef MACOSX
		/*
		 short colorbitsize = (tLXOptions->iColourDepth==16) ? 5 : 8;
		 SDL_GL_SetAttribute (SDL_GL_RED_SIZE,   colorbitsize);
		 SDL_GL_SetAttribute (SDL_GL_GREEN_SIZE, colorbitsize);
		 SDL_GL_SetAttribute (SDL_GL_BLUE_SIZE,  colorbitsize);
		 SDL_GL_SetAttribute (SDL_GL_ALPHA_SIZE, colorbitsize);
		 //SDL_GL_SetAttribute (SDL_GL_DEPTH_SIZE, tLXOptions->iColourDepth);
		 */
		//#endif
		//SDL_GL_SetAttribute (SDL_GL_ALPHA_SIZE,  8);
		//SDL_GL_SetAttribute (SDL_GL_DEPTH_SIZE, 24);
		//SDL_GL_SetAttribute (SDL_GL_BUFFER_SIZE, 32);
		SDL_GL_SetAttribute (SDL_GL_DOUBLEBUFFER, 1); // always use double buffering in OGL mode
	}
	
#ifdef WIN32
	UnSubclassWindow();  // Unsubclass before doing anything with the window
#endif
	
	
#ifdef WIN32
	// Reset the video subsystem under WIN32, else we get a "Could not reset OpenGL context" error when switching mode
	if (opengl && tLX)  {  // Don't reset when we're setting up the mode for first time (OpenLieroX not yet initialized)
		SDL_QuitSubSystem(SDL_INIT_VIDEO);
		SDL_InitSubSystem(SDL_INIT_VIDEO);
	}
#endif
	
setvideomode:
	m_window = SDL_CreateWindow(GetGameVersion().asHumanString().c_str(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, screenWidth(), screenHeight(), vidflags);
	
	if(m_window.get() == NULL) {
		if (resetting)  {
			errors << "Failed to reset video mode"
			<< " (ErrorMsg: " << SDL_GetError() << "),"
			<< " let's wait a bit and retry" << endl;
			SDL_Delay(500);
			resetting = false;
			goto setvideomode;
		}
		
		if(tLXOptions->iColourDepth != 0) {
			errors << "Failed to use " << tLXOptions->iColourDepth << " bpp"
			<< " (ErrorMsg: " << SDL_GetError() << "),"
			<< " trying automatic bpp detection ..." << endl;
			tLXOptions->iColourDepth = 0;
			goto setvideomode;
		}
		
		if(vidflags & SDL_WINDOW_OPENGL) {
			errors << "Failed to use OpenGL"
			<< " (ErrorMsg: " << SDL_GetError() << "),"
			<< " trying without ..." << endl;
			vidflags &= ~SDL_WINDOW_OPENGL;
			goto setvideomode;
		}
		
		if(vidflags & SDL_WINDOW_FULLSCREEN) {
			errors << "Failed to set full screen video mode "
			<< screenWidth() << "x" << screenHeight() << "x" << tLXOptions->iColourDepth
			<< " (ErrorMsg: " << SDL_GetError() << "),"
			<< " trying window mode ..." << endl;
			vidflags &= ~SDL_WINDOW_FULLSCREEN;
			goto setvideomode;
		}
		
		SystemError("Failed to set the video mode " + itoa(screenWidth()) + "x" + itoa(screenHeight()) + "x" + itoa(tLXOptions->iColourDepth) + "\nErrorMsg: " + std::string(SDL_GetError()));
		return false;
	}
	
	SDL_ShowCursor(SDL_DISABLE);
	
#ifdef WIN32
	// Hint: Reset the mouse state - this should avoid the mouse staying pressed
	GetMouse()->Button = 0;
	GetMouse()->Down = 0;
	GetMouse()->FirstDown = 0;
	GetMouse()->Up = 0;
	
	if (!tLXOptions->bFullscreen)  {
		SubclassWindow();
	}
#endif
	
	// Set the change mode flag
	if (tLX)
		tLX->bVideoModeChanged = true;

	if(!VideoPostProcessor::get()->resetVideo())
		return false;
		
	// Clear screen to blank
	SDL_SetRenderDrawColor(m_renderer.get(), 0, 0, 0, 255);
	SDL_RenderClear(m_renderer.get());
	SDL_RenderPresent(m_renderer.get());
	
	notes << "video mode was set successfully" << endl;
	return true;
}

static void dumpRenderInfo(const SDL_RendererInfo& info) {
	notes << "Renderer '" << info.name << "':" << endl;
	notes << "  software fallback: " << bool(info.flags & SDL_RENDERER_SOFTWARE) << endl;
	notes << "  hardware accelerated: " << bool(info.flags & SDL_RENDERER_ACCELERATED) << endl;
	notes << "  vsync: " << bool(info.flags & SDL_RENDERER_PRESENTVSYNC) << endl;
	notes << "  rendering to texture: " << bool(info.flags & SDL_RENDERER_TARGETTEXTURE) << endl;
	notes << "  max texture size (WxH): " <<
		info.max_texture_width << " x " << info.max_texture_height << endl;
	notes << "  formats (" << info.num_texture_formats << "):" << endl;
	for(uint32_t i = 0;
		i < info.num_texture_formats &&
		i < sizeof(info.texture_formats)/sizeof(info.texture_formats[0]);
		++i) {
		notes << "    " << i << ": " << SDL_GetPixelFormatName(info.texture_formats[i]) << endl;
	}
}

static void dumpRenderInfo(SDL_Renderer* renderer) {
	SDL_RendererInfo info;
	if(SDL_GetRendererInfo(renderer, &info) != 0)
		warnings << "Error getting renderer info: " << SDL_GetError() << endl;
	else
		dumpRenderInfo(info);
}

bool VideoPostProcessor::resetVideo() {
	m_renderer = SDL_CreateRenderer(m_window.get(), -1, 0);
	if(!m_renderer.get()) {
		errors << "failed to init renderer: " << SDL_GetError() << endl;
		return false;
	}
	
	dumpRenderInfo(m_renderer.get());
		
	//SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");  // make the scaled rendering look smoother.
	SDL_RenderSetLogicalSize(m_renderer.get(), screenWidth(), screenHeight());

	m_videoTexture = SDL_CreateTexture
	(
		m_renderer.get(),
		getMainPixelFormat()->format,
		SDL_TEXTUREACCESS_STREAMING,
		screenWidth(), screenHeight()
	);
	if(!m_videoTexture.get()) {
		errors << "failed to init video texture: " << SDL_GetError() << endl;
		return false;
	}
	
	// IMPORTANT: Don't reallocate if we already have the buffers.
	// If we would do, the old surfaces would get deleted. This is bad
	// because other threads could use it right now.
	if(!m_videoSurface.get()) {
		m_videoSurface = gfxCreateSurface(screenWidth(), screenHeight());
		if(!m_videoSurface.get()) {
			errors << "failed to init video surface: " << SDL_GetError() << endl;
			return false;
		}
	}
	DumpSurfaceInfo(m_videoSurface.get(), "main video surface");
	
	// No need to reinit this.
	if(!m_videoBufferSurface.get()) {
		m_videoBufferSurface = gfxCreateSurface(screenWidth(), screenHeight());
		if(!m_videoBufferSurface.get()) {
			errors << "failed to init video backbuffer surface: " << SDL_GetError() << endl;
			return false;
		}
	}
	
	return true;
}


void VideoPostProcessor::flipBuffers() {
	std::swap(get()->m_videoBufferSurface, get()->m_videoSurface);
}


// IMPORTANT: this has to be called from main thread!

void VideoPostProcessor::process() {
	ProcessScreenshots();
	
	void* pixels = get()->m_videoBufferSurface->pixels;
	SDL_UpdateTexture(get()->m_videoTexture.get(), NULL, pixels, get()->screenWidth() * sizeof (uint32_t));
}

void VideoPostProcessor::render() {
	//TestCircleDrawing(psScreen);
	//TestPolygonDrawing(psScreen);
	//DrawLoadingAni(psScreen, 320, 260, 50, 50, Color(128,128,128), Color(128,128,128,128), LAT_CIRCLES);
	//DrawLoadingAni(psScreen, 320, 260, 10, 10, Color(255,0,0), Color(0,255,0), LAT_CAKE);
	
	if(!get()->m_renderer.get()) return;
	
	SDL_RenderClear(get()->m_renderer.get());
	SDL_RenderCopy(get()->m_renderer.get(), get()->m_videoTexture.get(), NULL, NULL);
	SDL_RenderPresent(get()->m_renderer.get());
}

void VideoPostProcessor::cloneBuffer() {
	DrawImageAdv(get()->m_videoBufferSurface.get(), get()->m_videoSurface.get(), 0, 0, 0, 0, get()->m_videoSurface->w, get()->m_videoSurface->h);
}

void VideoPostProcessor::uninit() {
	instance.m_videoSurface = NULL; // should never be used before resetVideo() is called
	instance.m_videoTexture = NULL;
	instance.m_renderer = NULL;
	instance.m_window = NULL;
}



void VideoPostProcessor::transformCoordinates_ScreenToVideo( int& x, int& y ) {
	x = (int)((float)x * 640.0f / (float)get()->screenWidth());
	y = (int)((float)y * 480.0f / (float)get()->screenHeight());
}


// ---------------------------------------------------------------------------------------------





///////////////////
// Shutdown the standard Auxiliary Library
void ShutdownAuxLib()
{
#ifdef WIN32
	UnSubclassWindow();
#endif

	// free all cached stuff like surfaces and sounds
	// HINT: we have to do it before we uninit the specific engines
	cCache.Clear();

	VideoPostProcessor::uninit();
	// quit video at this point to not get stuck in a fullscreen not responding game in case that it crashes in further quitting
	// in the case it wasn't inited at this point, this also doesn't hurt
	SDL_QuitSubSystem( SDL_INIT_VIDEO );

	QuitSoundSystem();

	// Shutdown the error system
	EndError();

#ifdef WIN32
	UnSubclassWindow();
#endif

	SDL_Quit();
}






//////////////////
// Helper funtion for screenshot taking
static std::string GetPicName(const std::string& prefix, size_t i, const std::string& ext)
{
	return prefix + (i == 0 ? std::string("") : itoa(i)) + ext;
}


////////////////////
// Helper function for TakeScreenshot
static std::string GetScreenshotFileName(const std::string& scr_path, const std::string& extension)
{
	std::string path = scr_path;

	// Append a slash if not present
	if (path[path.size() - 1] != '/' && path[path.size() - 1] != '\\')  {
		path += '/';
	}
	
	
	std::string filePrefix = GetDateTimeFilename();
	filePrefix += "-";
	if( tLX )
	{
		if( game.isLocalGame() )
			filePrefix += "local";
		else if( game.isServer() )
			filePrefix += tLXOptions->sServerName;
		else if( cClient )
			filePrefix += cClient->getServerName();
	}
	
	// Make filename more fileststem-friendly
	if( filePrefix.size() > 64 )
		filePrefix.resize(64);

#define S_LETTER_UPPER "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
#define S_LETTER_LOWER "abcdefghijklmnopqrstuvwxyz"
#define S_LETTER S_LETTER_UPPER S_LETTER_LOWER
#define S_NUMBER "0123456789"
#define S_SYMBOL ". -_&+"	// No "\\" symbol, no tab.
#define S_VALID_FILENAME S_LETTER_UPPER S_LETTER_LOWER S_NUMBER S_SYMBOL
	while( filePrefix.find_first_not_of(S_VALID_FILENAME) != std::string::npos )
		filePrefix[ filePrefix.find_first_not_of(S_VALID_FILENAME) ] = '-';

	static const size_t step = 256; // Step; after how many files we check if the filename still exists

	// We start at range from 1 to step
	size_t lower_bound = 0;
	size_t upper_bound = step;

	std::string fullname(path + GetPicName(filePrefix, upper_bound, extension));

	// Find a raw range of where the screenshot filename could be
	// For example: between lierox1000.png and lierox1256.png
	while (IsFileAvailable(fullname, false))  {
		lower_bound = upper_bound;
		upper_bound += step;

		fullname = path + GetPicName(filePrefix, upper_bound, extension);
	}

	// First file?
	if (!IsFileAvailable(path + GetPicName(filePrefix, lower_bound, extension)))
		return path + GetPicName(filePrefix, lower_bound, extension);

	// Use binary search on the given range to find the exact file name
	size_t i = (lower_bound + upper_bound) / 2;
	while (true)  {
		if (IsFileAvailable(path + GetPicName(filePrefix, i, extension), false))  {
			// If the current (i) filename exists, but the i+1 does not, we're done
			if (!IsFileAvailable(path + GetPicName(filePrefix, i + 1, extension)))
				return path + GetPicName(filePrefix, i + 1, extension);
			else  {
				// The filename is somewhere in the interval (i, upper_bound)
				lower_bound = i;
				i = (lower_bound + upper_bound) / 2;
			}
		} else {
			// The filename is somewhere in the interval (lower_bound, i)
			upper_bound = i;
			i = (lower_bound + upper_bound) / 2;
		}
	}

	return ""; // Should not happen
}

///////////////////
// Take a screenshot
// This should run on the main thread.
static void TakeScreenshot(const std::string& scr_path, const std::string& additional_data)
{
	if (scr_path.empty()) // Check
		return;

	notes << "Save screenshot to " << scr_path << endl;

	std::string	extension;

	// Set the extension
	switch (tLXOptions->iScreenshotFormat)  {
	case FMT_BMP: extension = ".bmp"; break;
	case FMT_PNG: extension = ".png"; break;
	case FMT_JPG: extension = ".jpg"; break;
	case FMT_GIF: extension = ".gif"; break;
	default: extension = ".png";
	}

	// Save the surface
	SaveSurface(VideoPostProcessor::videoBufferSurface(), GetScreenshotFileName(scr_path, extension),
		tLXOptions->iScreenshotFormat, additional_data);
}

#ifdef WIN32
LONG wpOriginal;
bool Subclassed = false;

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

////////////////////
// Subclass the window (control the incoming Windows messages)
void SubclassWindow()
{
	if (Subclassed)
		return;

#pragma warning(disable:4311)  // Temporarily disable, the typecast is OK here
	wpOriginal = SetWindowLong((HWND)GetWindowHandle(),GWL_WNDPROC,(LONG)(&WindowProc));
#pragma warning(default:4311) // Enable the warning
	Subclassed = true;
}

////////////////////
// Remove the subclassing
void UnSubclassWindow()
{
	if (!Subclassed)
		return;

	SetWindowLong((HWND)GetWindowHandle(),GWL_WNDPROC, wpOriginal);

	Subclassed = false;
}

/////////////////////
// Subclass callback
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	// Ignore the unwanted messages
	switch (uMsg)  {
	case WM_ENTERMENULOOP:
		return 0;
	case WM_INITMENU:
		return 0;
	case WM_MENUSELECT:
		return 0;
	case WM_SYSKEYUP:
		return 0;
	}

#pragma warning(disable:4312)
	return CallWindowProc((WNDPROC)wpOriginal,hwnd,uMsg,wParam,lParam);
#pragma warning(default:4312)
}

//////////////////////
// unsetenv for WIN32, taken from libc source
static int _unsetenv(const char *name)
{
  size_t len;
  char **ep;

  if (name == NULL || *name == '\0' || strchr (name, '=') != NULL)
    {
      return -1;
    }

  len = strlen (name);

  ep = environ;
  while (*ep != NULL)
    if (!strncmp (*ep, name, len) && (*ep)[len] == '=')
      {
	/* Found it.  Remove this pointer by moving later ones back.  */
	char **dp = ep;

	do
	  dp[0] = dp[1];
	while (*dp++);
	/* Continue the loop in case NAME appears again.  */
      }
    else
      ++ep;

  return 0;
}

#if 0
static int _unsetenv(const wchar_t *name)
{
  size_t len;
  wchar_t **ep;

  if (name == NULL || *name == '\0' || wcschr (name, '=') != NULL)
    {
      return -1;
    }

  len = wcslen (name);

 // ep = _wenviron;
  while (*ep != NULL)
    if (!wcsncmp (*ep, name, len) && (*ep)[len] == '=')
      {
	/* Found it.  Remove this pointer by moving later ones back.  */
	wchar_t **dp = ep;

	do
	  dp[0] = dp[1];
	while (*dp++);
	// Continue the loop in case NAME appears again.  */
      }
    else
      ++ep;

  return 0;
}
#endif

int unsetenv(const char *name) {
	return _unsetenv(name);
//	return _unsetenv(Utf8ToUtf16(name).c_str());
}

#endif




#ifdef DEBUG
#include "CClient.h"

// TODO: move this to console (it was only nec. via chat because we didn't had the console globally before)
// HINT: This is called atm from CClientNetEngine::SendText().
// HINT: This is just a hack to do some testing in lobby or whatever.
// WARNING: These stuff is not intended to be stable, it's only for testing!
// HINT: Don't rely on this. If we allow the console later somehow in the lobby,
// this debug stuff will probably move there.
bool HandleDebugCommand(const std::string& text) {
	if(text.size() >= 3 && text.substr(0,3) == "///") {
		cClient->getChatbox()->AddText("DEBUG COMMAND", tLX->clNotice, TXT_NOTICE, tLX->currentTime);

		std::string cmd = text.substr(3);
		stringlwr(cmd);

		if(cmd == "reconnect") {
			notes << "DEBUG CMD: reconnect local client to " << cClient->getServerAddress() << endl;
			cClient->Connect(cClient->getServerAddress());
		} else if(cmd == "msgbox") {
			Menu_MessageBox("Test",
							"This is a very long text, a very long text, a very long text, a very long text, "
							"a very long text, a very long text, a very long text, a very long text, a very long text, "
							"a very long text, a very long text, a very long text, a very long text, a very long text, "
							"a very long text, a very long text, a very long text, a very long text, a very long text.\n"
							"Yes really, this text is very long, very long, very long, very long, very long, "
							"very very long, very very long, very very long, very very long, very very long.",
							DeprecatedGUI::LMB_OK);
		} else if(cmd == "register") {
			cServer->RegisterServer();
		} else
			notes << "DEBUG CMD unknown" << endl;

		return true;
	}
	return false;
}
#endif




void lierox_t::setupInputs() {
	if(!tLXOptions) {
		errors << "lierox_t::setupInputs: tLXOptions not set" << endl;
		return;
	}
	
	// Setup global keys
	cTakeScreenshot.Setup(tLXOptions->sGeneralControls[SIN_SCREENSHOTS]);
	cSwitchMode.Setup(tLXOptions->sGeneralControls[SIN_SWITCHMODE]);
	cIrcChat.Setup(tLXOptions->sGeneralControls[SIN_IRCCHAT]);
	cConsoleToggle.Setup(tLXOptions->sGeneralControls[SIN_CONSOLETOGGLE]);
	
	if(cClient)
		cClient->SetupGameInputs();
	else
		warnings << "lierox_t::setupInputs: cClient not set" << endl;
}

bool lierox_t::isAnyControlKeyDown() const {
	return cTakeScreenshot.isDown() || cSwitchMode.isDown() || cIrcChat.isDown() || cConsoleToggle.isDown();
}


void EnableSystemMouseCursor(bool enable)
{
	if( bDedicated )
		return;
	struct EnableMouseCursor: public Action
	{
		bool Enable;
		
		EnableMouseCursor(bool b): Enable(b) {};
		Result handle()
		{
			SDL_ShowCursor(Enable ? SDL_ENABLE : SDL_DISABLE ); // Should be called from main thread, or you'll get race condition with libX11
			return true;
		} 
	};
	doActionInMainThread( new EnableMouseCursor(enable) );
};
