// OpenLieroX

// Main entry point
// Created 28/6/02
// Jason Boettcher

// code under LGPL



#include <assert.h>
#include <setjmp.h>
#include <sstream> // for print_binary_string

#include "LieroX.h"
#include "IpToCountryDB.h"
#include "AuxLib.h"
#include "CClient.h"
#include "CServer.h"
#include "ConfigHandler.h"
#include "console.h"
#include "GfxPrimitives.h"
#include "FindFile.h"
#include "InputEvents.h"
#include "StringUtils.h"
#include "Entity.h"
#include "Error.h"
#include "DedicatedControl.h"
#include "Physics.h"
#include "Version.h"
#include "OLXG15.h"
#include "CrashHandler.h"
#include "Cursor.h"
#include "CssParser.h"
#include "FontHandling.h"
#include "Timer.h"
#include "CChannel.h"
#include "Cache.h"
#include "ProfileSystem.h"
#include "IRC.h"
#include "Music.h"
#include "Debug.h"
#include "TaskManager.h"
#include "CGameMode.h"
#include "ConversationLogger.h"
#include "StaticAssert.h"

#include "DeprecatedGUI/CBar.h"
#include "DeprecatedGUI/Graphics.h"
#include "DeprecatedGUI/Menu.h"
#include "DeprecatedGUI/CChatWidget.h"

#include "SkinnedGUI/CGuiSkin.h"


#ifndef WIN32
#include <dirent.h>
#include <sys/stat.h>
#endif

#include <libxml/parser.h>

// TODO: i hate globals ...
// we have to create a basic class Game or something

lierox_t	*tLX = NULL;

CInput		*cTakeScreenshot = NULL;
CInput		*cSwitchMode = NULL;
CInput		*cIrcChat = NULL;

bool        bDisableSound = false;
#ifdef DEDICATED_ONLY
bool		bDedicated = true;
bool		bJoystickSupport = false;
#else //DEDICATED_ONLY
bool		bDedicated = false;
bool		bJoystickSupport = true;
#endif //DEDICATED_ONLY
bool		bRestartGameAfterQuit = false;
TStartFunction startFunction = NULL;
void*		startFunctionData = NULL;
ConversationLogger *convoLogger = NULL;


keyboard_t	*kb = NULL;
IpToCountryDB *tIpToCountryDB = NULL;


//
// Loading screen info and functions
//
class LoadingScreen  {
public:
	LoadingScreen() {
		iBackgroundX = iBackgroundY = 0;
		bmpBackground = NULL;
		cBar = NULL;
	}

	int iBackgroundX;
	int iBackgroundY;
	int iLabelX;
	int iLabelY;
	Uint32 clLabel;
	SmartPointer<SDL_Surface> bmpBackground;
	DeprecatedGUI::CBar *cBar;
};

LoadingScreen cLoading;

void InitializeLoading();
void DrawLoading(byte percentage, const std::string &text);
void ShutdownLoading();




// TODO: move this out here
void print_binary_string(const std::string& txt) {
	std::string buf;
	std::stringstream str(buf);
	for(std::string::const_iterator it = txt.begin(); it != txt.end(); it++) {
		str << std::hex << (ushort)(uchar)(*it) << " ";
	}
	notes << buf << endl;
}

static void DoSystemChecks() {
	// sadly, these sizeof are directly used in CGameScript.cpp/CMap.cpp
	// TODO: fix this issue
	static_assert(sizeof(char) == 1, sizeof_char__equals1);
	static_assert(sizeof(short) == 2, sizeof_short__equals2);
	static_assert(sizeof(int) == 4, sizeof_int__equals4);
	static_assert(sizeof(float) == 4, sizeof_float__equals4);
	// sometimes the return value of SendMessage is used as a pointer
	static_assert(sizeof(DWORD) == sizeof(void*), sizeof_dword__equals_p);
}


char* apppath = NULL;

char* GetAppPath() { return apppath; }

#ifndef WIN32
sigjmp_buf longJumpBuffer;
#endif

static SDL_mutex* videoFrameMutex = NULL;
static ThreadPoolItem* mainLoopThread = NULL;
static int MainLoopThread(void*);


static SDL_Event QuitEventThreadEvent() {
	SDL_Event ev;
	ev.type = SDL_USEREVENT;
	ev.user.code = UE_QuitEventThread;	
	return ev;
}


static void startMainLockDetector() {
	struct MainLockDetector : Action {
		bool wait(Uint32 time) {
			if(!tLX) return false;
			AbsTime oldTime = tLX->currentTime;
			for(Uint32 t = 0; t < time; t += 100) {
				if(!tLX) return false;
				if(tLX->bQuitGame) return false;						
				if(oldTime != tLX->currentTime) return true;
				SDL_Delay(100);
			}
			return true;
		}
		int handle() {
			// We should always have tLX!=NULL here as we uninit it after the thread-shutdown now.
			while(tLX && !tLX->bQuitGame) {
				AbsTime oldTime = tLX->currentTime;
				if(!wait(1000)) return 0;
				if(!tLX) return 0;
				if(tLX->bQuitGame) return 0;
				if(IsWaitingForEvent()) continue;
				
				// HINT: Comment that out and you'll find a lot of things in OLX which could be improved.
				// WARNING: This is the only code here which could lead to very rare crashes.
				//if(!cClient || cClient->getStatus() != NET_PLAYING) continue;
				
				// check if the mainthread is hanging
				if(oldTime == tLX->currentTime) {
					warnings << "possible lock of game thread detected" << endl;
					//OlxWriteCoreDump("mainlock");
					//RaiseDebugger();
					
					// pause for a while, don't be so hard
					if(!wait(30*1000)) return 0;
					if(tLX && !tLX->bQuitGame && oldTime == tLX->currentTime) {
						warnings << "we still are locked after 30 seconds" << endl;
						if(tLXOptions && tLXOptions->bFullscreen) {
							notes << "we are in fullscreen, going to window mode now" << endl;
							tLXOptions->bFullscreen = false;
							doSetVideoModeInMainThread();
							notes << "setting window mode sucessfull" << endl;
						}
					}
				}
			}
			return 0;
		}
	};
	threadPool->start(new MainLockDetector(), "main lock detector", true);
}


static bool menu_startgame = false;

static bool videoModeReady = true;
static int needVideoFrame = 0;

///////////////////
// Main entry point
int main(int argc, char *argv[])
{
	setCurThreadName("Main Thread");
	setCurThreadPriority(0.5f);
	
	hints << GetFullGameName() << " is starting ..." << endl;
#ifdef DEBUG
	hints << "This is a DEBUG build." << endl;
#endif
#ifdef DEDICATED_ONLY
	hints << "This is a DEDICATED_ONLY build." << endl;
#endif

#ifdef DEBUG
	// TODO: any reason for this?
	//setvbuf(stdout, NULL, _IOLBF, 1024);
#endif

	DoSystemChecks();
	InitThreadPool();
	
	apppath = argv[0];
	binary_dir = argv[0];
	size_t slashpos = findLastPathSep(binary_dir);
	if(slashpos != std::string::npos)  {
		binary_dir.erase(slashpos);
		binary_dir = SystemNativeToUtf8(binary_dir);
	} else
		binary_dir = "."; // TODO get exact path of binary

	// this has to be done before GameOptions::Init
	InitGameModes();

startpoint:

	InitTaskManager();
	
	// Load options and other settings
	if(!GameOptions::Init()) {
		SystemError("Could not load options");
		return -1;
	}

	CrashHandler::init();

	if(!NetworkTexts::Init()) {
		SystemError("Could not load network strings.");
		return -1;
	}

	CSSParser::test_css();

    // Parse the arguments
	// do it after the loading of the options as this can
	// overwrite the default options
	ParseArguments(argc, argv);

	// Start the G15 support, it's suitable that the display is showing while loading.
#ifdef WITH_G15
	OLXG15 = new OLXG15_t;
	if (!OLXG15->init()) {
		// Error when initialising, can't use it.
		delete OLXG15;
		OLXG15 = NULL;
	}
#endif //WITH_G15

	// Start loading the IP to country database
	// HINT: we do it as soon as possible because the loading has more time then which means better results
	// HINT: the database won't load if it is disabled in options
	tIpToCountryDB = new IpToCountryDB("ip_to_country.csv");

	// Initialize LX
	if(!InitializeLieroX())  {
		SystemError("Could not initialize LieroX.");
		return -1;
	}
	
	kb = GetKeyboard();
	if (!bDedicated && !VideoPostProcessor::videoSurface()) {
		SystemError("Could not find screen.");
		return -1;
	}

	// Music
	if( !bDedicated && tLXOptions->iMusicVolume > 0 )
		InitializeBackgroundMusic();

	tLX->currentTime = GetTime();

	if( tLXOptions->bNewSkinnedGUI )
	{
		// Just for test - it's not real handler yet
		SkinnedGUI::cMainSkin->Load("default");
		SkinnedGUI::cMainSkin->OpenLayout("test.skn");
		while (!tLX->bQuitGame)  {
			tLX->fDeltaTime = GetTime() - tLX->currentTime;
			tLX->currentTime = GetTime();

			WaitForNextEvent();
			SkinnedGUI::cMainSkin->Frame();
		}

		ShutdownLieroX();
		return 0;
	}

	DrawLoading(60, "Initializing menu system");

	// Initialize menu
	if(!DeprecatedGUI::Menu_Initialize(&menu_startgame)) {
        SystemError("Error: Could not initialize the menu system.\nError when loading graphics files");
		return -1;
	}

	// Initialize chat logging
	convoLogger = new ConversationLogger();
	if (tLXOptions->bLogConvos)
		convoLogger->startLogging();

	// Setup the global keys	
	cTakeScreenshot = new CInput();
	cSwitchMode = new CInput();
	cIrcChat = new CInput();
	cTakeScreenshot->Setup(tLXOptions->sGeneralControls[SIN_SCREENSHOTS]);
	cSwitchMode->Setup(tLXOptions->sGeneralControls[SIN_SWITCHMODE]);
	cIrcChat->Setup(tLXOptions->sGeneralControls[SIN_IRCCHAT]);


	DrawLoading(99, "Loading Physics Engine");
	PhysicsEngine::Init();

	DrawLoading(100, "Done! Starting menu");

	// Everything loaded, this is not needed anymore
	ShutdownLoading();

	if(startFunction != NULL) {
		if(!startFunction(startFunctionData)) {
			SystemError("ERROR: startfunction not successfull");
			return -1;
		}
		// reset the data; the startfunction is intended to be called only once
		startFunction = NULL;
		startFunctionData = NULL;
	}

	needVideoFrame = false;
	videoFrameMutex = SDL_CreateMutex();
	mainLoopThread = threadPool->start(MainLoopThread, NULL, "mainloop");
	
	startMainLockDetector();
	
	if(!bDedicated) {
		// Get all SDL events and push them to our event queue.
		// We have to do that in the same thread where we inited the video because of SDL.
		SDL_Event ev;
		memset( &ev, 0, sizeof(ev) );
		while(true) {
			while( SDL_WaitEvent(&ev) ) {
				if(ev.type == SDL_USEREVENT) {
					switch(ev.user.code) {
						case UE_QuitEventThread: goto quit;
						case UE_DoVideoFrame:
							{
								ScopedLock lock(videoFrameMutex);
								if(!needVideoFrame && !ev.user.data1) continue; // data1!=0 -> forceRedraw
								if(needVideoFrame > 0) needVideoFrame--;
								if(!videoModeReady) continue;
								VideoPostProcessor::process();
							}
							flipRealVideo();
							continue;
						case UE_DoSetVideoMode:
							if(!videoModeReady) {
								SDL_mutexP(videoFrameMutex);
								SetVideoMode();
								videoModeReady = true;
								SDL_mutexV(videoFrameMutex);
							}
							continue;
						case UE_DoActionInMainThread:
							((Action*)ev.user.data1)->handle();
							delete (Action*)ev.user.data1;
							continue;
					}
				}
				mainQueue->push(ev);
			}
			
			notes << "error while waiting for next event" << endl;
			SDL_Delay(200);
		}
	}
	
quit:
	threadPool->wait(mainLoopThread, NULL);
	mainLoopThread = NULL;
	SDL_DestroyMutex(videoFrameMutex);
	videoFrameMutex = NULL;
	
	PhysicsEngine::UnInit();

	ShutdownLieroX();
	delete cSwitchMode; cSwitchMode = NULL;
	delete cTakeScreenshot; cTakeScreenshot = NULL;	
	delete cIrcChat; cIrcChat = NULL;
	
	notes << "waiting for all left threads and tasks" << endl;
	taskManager->finishQueuedTasks();
	threadPool->waitAll(); // do that before uniniting task manager because some threads could access it

	// do that after shutting down the timers and other threads
	ShutdownEventQueue();
	
	UnInitTaskManager();

	// LieroX structure
	// HINT: must be after end of all threads because we could access it
	if(tLX) {
		delete tLX;
		tLX = NULL;
	}
	
	if(bRestartGameAfterQuit) {
		bRestartGameAfterQuit = false;
		hints << "-- Restarting game --" << endl;
		goto startpoint;
	}

	UnInitThreadPool();
	
	notes << "Good Bye and enjoy your day..." << endl;

	// Uninit the crash handler after all other code
	CrashHandler::uninit();

	return 0;
}

// note: important to have const char* here because std::string is too dynamic, could be screwed up when returning
void SetCrashHandlerReturnPoint(const char* name) {
#ifndef WIN32
	if(sigsetjmp(longJumpBuffer, true) != 0) {
		hints << "returned from sigsetjmp in " << name << endl;
		if(!tLXOptions) {
			notes << "we already have tLXOptions uninitialised, exiting now" << endl;
			exit(10);
			return;
		}
		if(tLXOptions->bFullscreen) {
			notes << "we are in fullscreen, going to window mode now" << endl;
			tLXOptions->bFullscreen = false;
			doSetVideoModeInMainThread();
			notes << "back in window mode" << endl;
		}
	}
#endif	
}

void doVideoFrameInMainThread(bool forceRedraw) {
	if(bDedicated) return;
	
	// don't push too much events if we have not drawn yet
	if(needVideoFrame >= 2) return;
	
	{
		// wait for current drawing
		ScopedLock lock(videoFrameMutex);
		if(needVideoFrame >= 2) return; // again the check (now we have for sure a correct value)
		needVideoFrame++;
		VideoPostProcessor::flipBuffers();		
	}

	SDL_Event ev;
	ev.type = SDL_USEREVENT;
	ev.user.code = UE_DoVideoFrame;
	ev.user.data1 = forceRedraw ? (void*)1 : NULL;
	if(SDL_PushEvent(&ev) != 0) {
		warnings << "failed to push videoframeevent" << endl;
	}
}

void doSetVideoModeInMainThread() {
	if(bDedicated) return;
	
	SDL_Event ev;
	ev.type = SDL_USEREVENT;
	ev.user.code = UE_DoSetVideoMode;
	videoModeReady = false;
	if(SDL_PushEvent(&ev) == 0) {
		while(!videoModeReady) {
			SDL_Delay(10);
			// put this thread into sleep (other thread should have locked this mutex)
			SDL_mutexP(videoFrameMutex);
			SDL_mutexV(videoFrameMutex);
		}
	}
}

void doActionInMainThread(Action* act) {
	if(bDedicated) {
		warnings << "doActionInMainThread cannot work correctly in dedicated mode" << endl;
		// we will just put it to the main queue instead
		mainQueue->push(act);
		return;
	}
	
	SDL_Event ev;
	ev.type = SDL_USEREVENT;
	ev.user.code = UE_DoActionInMainThread;
	ev.user.data1 = act;
	if(SDL_PushEvent(&ev) != 0) {
		errors << "failed to push custom action event" << endl;
	}
}



static int MainLoopThread(void*) {
	setCurThreadPriority(0.5f);
	tLX->bQuitGame = false;
	ResetQuitEngineFlag();
	while(!tLX->bQuitGame) {
		SetCrashHandlerReturnPoint("MainLoopThread before lobby");
		
		menu_startgame = false; // the menu has a reference to this variable
		
		DeprecatedGUI::Menu_Start();	// Start and run the menu, won't return 'till user joins game / exits
		
		if(!menu_startgame) {
			// Quit
			tLX->bQuitGame = true;
			break;
		}
		
		// Pre-game initialization
		if(!bDedicated) FillSurface(VideoPostProcessor::videoSurface(), tLX->clBlack);
		
		ClearEntities();
		
		ProcessEvents();
		notes << "MaxFPS is " << tLXOptions->nMaxFPS << endl;
		
		//cCache.ClearExtraEntries(); // Do not clear anything before game started, it may be slow
		
		notes << "GameLoopStart" << endl;
		if( DedicatedControl::Get() )
			DedicatedControl::Get()->GameLoopStart_Signal();
		
		//
        // Main game loop
        //
		ResetQuitEngineFlag();
		AbsTime oldtime = GetTime();
		while(!tLX->bQuitEngine) {
			
			tLX->currentTime = GetTime();
			SetCrashHandlerReturnPoint("main game loop");
			
			// Timing
			tLX->fDeltaTime = tLX->currentTime - oldtime;
			tLX->fRealDeltaTime = tLX->fDeltaTime;
			oldtime = tLX->currentTime;
			
			// cap the delta
			if(tLX->fDeltaTime.seconds() > 0.5f) {
				warnings << "deltatime " << tLX->fDeltaTime.seconds() << " is too high" << endl;
				tLX->fDeltaTime = 0.5f; // don't simulate more than 500ms, it could crash the game
			}
			
			ProcessEvents();
			
			// Main frame
			GameLoopFrame();
			
			doVideoFrameInMainThread(false);
			CapFPS();
		}
		
		PhysicsEngine::Get()->uninitGame();
		
		notes << "GameLoopEnd" << endl;
		if( DedicatedControl::Get() )
			DedicatedControl::Get()->GameLoopEnd_Signal();
		
		cCache.ClearExtraEntries(); // Game ended - clear cache
		
	}

	SDL_Event quitEv = QuitEventThreadEvent();
	if(!bDedicated)
		while(SDL_PushEvent(&quitEv) < 0) {}

	return 0;
}



///////////////////
// Parse the arguments
void ParseArguments(int argc, char *argv[])
{
    // Parameters passed to OpenLieroX overwrite the loaded options
    char *a;
    for(int i=1; i<argc; i++) {
        a = argv[i];

        // -opengl
        // Turns OpenGL on
        if( stricmp(a, "-opengl") == 0 ) {
            tLXOptions->bOpenGL = true;
        } else

        // -nojoystick
        // Turns joystick off
        if( stricmp(a, "-nojoystick") == 0 ) {
            bJoystickSupport = false;
        } else


        // -noopengl
        // Turns OpenGL off
        if( stricmp(a, "-noopengl") == 0 ) {
            tLXOptions->bOpenGL = false;
        } else

        // -nosound
        // Turns off the sound
        if( stricmp(a, "-nosound") == 0 ) {
            bDisableSound = true;
            tLXOptions->bSoundOn = false;
        } else

        // -dedicated
        // Turns on dedicated mode (no gfx, no sound)
        if( stricmp(a, "-dedicated") == 0 ) {
            bDedicated = true;
			bDisableSound = true;
			// this setting will be temporarly because we don't save options at end in dedicated mode
            tLXOptions->bSoundOn = false;
        } else

		// -dedscript
		// set dedicated script (next param)
		if( stricmp(a, "-script") == 0 ) {
			bDedicated = true;
			bDisableSound = true;
			// these settings will be temporarly because we don't save options at end in dedicated mode
			tLXOptions->bSoundOn = false;
			tLXOptions->sDedicatedScript = argv[++i];
		} else
				
		// -connect
		// connect to server (next param)
		if( stricmp(a, "-connect") == 0 ) {
			// TODO ...
			// argv[++i];
			++i;
		} else

        // -window
        // Turns fullscreen off
        if( stricmp(a, "-window") == 0 ) {
            tLXOptions->bFullscreen = false;
        } else

        // -fullscreen
        // Turns fullscreen on
        if( stricmp(a, "-fullscreen") == 0 ) {
            tLXOptions->bFullscreen = true;
        } else

        // -skin
        // Turns new skinned GUI on
        if( stricmp(a, "-skin") == 0 ) {
            tLXOptions->bNewSkinnedGUI = true;
        } else

        // -noskin
        // Turns new skinned GUI off
        if( stricmp(a, "-noskin") == 0 ) {
            tLXOptions->bNewSkinnedGUI = false;
        } else

#ifdef WIN32
		// -console
		// Attaches a console window to the main LX window
		if (stricmp(a, "-console") == 0)  {
			if (AllocConsole())  {
			  FILE *con = freopen("CONOUT$", "w", stdout);
			  if (con) {
				*stdout = *con;
				setvbuf(stdout, NULL, _IONBF, 0);
			  }
			  SetConsoleTitle("OpenLieroX Console");
			}
		} else
#endif

		// -help
		// Displays help and quits
        if( !stricmp(a, "-h") || !stricmp(a, "-help") || !stricmp(a, "--help") || !stricmp(a, "/?")) {
        	printf("available parameters:\n");
     		printf("   -opengl       OpenLieroX will use OpenGL for drawing\n");
     		printf("   -noopengl     Explicitly disable using OpenGL\n");
     		printf("   -dedicated    Dedicated mode\n");
     		printf("   -nojoystick   Disable Joystick support\n");
     		printf("   -nosound      Disable sound\n");
     		printf("   -window       Run in window mode\n");
     		printf("   -fullscreen   Run in fullscreen mode\n");
			#ifdef WIN32
			printf("   -console      Attach a console window to the main OpenLieroX window\n");
			#endif
			#ifdef DEBUG
     		printf("   -nettest      Test CChannel reliability\n");
			#endif
     		printf("   -skin         Turns on new skinned GUI - it's unfinished yet\n");
     		printf("   -noskin       Turns off new skinned GUI\n");

			// Shutdown and quit
			// ShutdownLieroX() works only correct when everything was inited because ProcessEvents() is used.
			// Therefore we just ignore a good clean up and just quit here.
			// This is not nice but still nicer than getting a segfault.
			// It is not worth to fix this in a nicer way as it is fixed anyway when we use the new engine system.
     		exit(0);
        }
		#ifdef DEBUG
		if( !stricmp(a, "-nettest") )
		{
			InitializeLieroX();
			TestCChannelRobustness();
			ShutdownLieroX();
     		exit(0);
		}
		#endif
    }
}


///////////////////
// Initialize the game
int InitializeLieroX()
{
	notes << "Hello there, I am initializing me now..." << endl;

	LIBXML_TEST_VERSION;

	// Initialize the aux library
	if(!InitializeAuxLib("config.cfg",16,0)) {
        SystemError("strange problems with the aux library");
		return false;
	}

	// Setup the HTTP proxy
	AutoSetupHTTPProxy();

	// Initialize the LieroX structure
	tLX = new lierox_t;
    if(!tLX) {
        SystemError("Error: InitializeLieroX() Out of memory on creating lierox_t");
		return false;
    }
	tLX->bVideoModeChanged = false;
	tLX->bQuitGame = false;
	tLX->bQuitCtrlC = false;
	tLX->debug_string = "";
	tLX->currentTime = 0;
	tLX->fDeltaTime = 0;
	tLX->bHosted = false;

	if (!SkinnedGUI::InitializeGuiSkinning())
		return false;

	// Initialize the game colors (must be called after SDL_GetVideoSurface is not NULL and tLX is not NULL)
	DeprecatedGUI::InitializeColors();

	// Load the fonts (must be after colors, because colors are used inside CFont::Load)
	if (!DeprecatedGUI::LoadFonts())  {
		SystemError("Could not load the fonts");
		return false;
	}

	// Initialize the loading screen
	InitializeLoading();

	DrawLoading(0, "Initializing network");

	// Initialize the network
	if(!InitNetworkSystem()) {
		SystemError("Error: Failed to initialize the network library");
		return false;
	}

	DrawLoading(5, "Initializing client and server");

	// Allocate the client & server
	cClient = new CClient;
	if(cClient == NULL) {
		SystemError("Error: InitializeLieroX() Out of memory");
		return false;
	}
	cClient->Clear();
	cClient->setLocalClient(true);



	cServer = new GameServer;
	if(cServer == NULL) {
		SystemError("Error: InitializeLieroX() Out of memory on creating GameServer");
		return false;
	}

	DrawLoading(10, "Initializing game entities");

	// Initialize the entities
	if(!InitializeEntities()) {
		SystemError("Error: InitializeEntities() Out of memory on initializing the entities");
		return false;
	}

	DrawLoading(15, "Loading graphics");


	// Load the graphics
	if(!DeprecatedGUI::LoadGraphics()) {
		SystemError("Error: Error loading graphics");
		return false;
	}

	DrawLoading(40, "Initializing console");

	// Initialize the console GUI
	if(!Con_Initialize()) {
		SystemError("Error: Could not initialize the console");
		return false;
	}

	Cmd_Initialize();	// Initialize console commands
	
	DrawLoading(45, "Loading sounds");

	// Load the sounds
	LoadSounds();

	DrawLoading(55, "Loading player profiles");

	// Load the profiles
	LoadProfiles();

	if(bDedicated)
		if(!DedicatedControl::Init()) {
			errors << "couldn't init dedicated control" << endl;
			return false;
		}

	notes << "Initializing ready" << endl;

	return true;
}


///////////////////
// Game loop
void GameLoopFrame()
{
	if(bDedicated)
		DedicatedControl::Get()->GameLoop_Frame();

    if(tLX->bQuitEngine)
        return;

	// Switch between window and fullscreen mode
	// Switch only if delta time is low enough. This is because when the game does not
	// respond for >30secs and the user presses cSwitchMode in the meantime, the mainlock-detector
	// would switch to window and here we would switch again to fullscreen which is stupid.
	if( cSwitchMode->isUp() && tLX && tLX->fRealDeltaTime < 1.0f )  {
		// Set to fullscreen
		tLXOptions->bFullscreen = !tLXOptions->bFullscreen;

		// Set the new video mode
		doSetVideoModeInMainThread();

		cSwitchMode->reset();
	}

#ifdef WITH_G15
	if (OLXG15)
		OLXG15->gameFrame();
#endif //WITH_G15

	if(tLXOptions->bEnableChat)
		ProcessIRC();

	// Local
	switch (tLX->iGameType)  {
	case GME_LOCAL:
		cClient->Frame();
		cServer->Frame();

		// If we are connected, just start the game straight away (bypass lobby in local)
		if(cClient->getStatus() == NET_CONNECTED) {
			if(cServer->getState() == SVS_LOBBY) {
				if(!cServer->StartGame()) {
					errors << "starting game in local game failed for some reason" << endl;
					GotoLocalMenu();
					return;
				}
			}
		}

		if(tLX && !tLX->bQuitEngine)
			cClient->Draw(VideoPostProcessor::videoSurface());
		break;


	// Hosting
	case GME_HOST:
		cClient->Frame();
		cServer->Frame();

		if(tLX && !tLX->bQuitEngine)
			cClient->Draw(VideoPostProcessor::videoSurface());
		break;

	// Joined
	case GME_JOIN:
		cClient->Frame();
		if(tLX && !tLX->bQuitEngine)
			cClient->Draw(VideoPostProcessor::videoSurface());
		break;

	} // SWITCH

	// We put it here, so the mouse never displays
    SDL_ShowCursor(SDL_DISABLE);
}


///////////////////
// Quit back to the menu
void QuittoMenu()
{
	SetQuitEngineFlag("QuittoMenu");
    DeprecatedGUI::Menu_SetSkipStart(false);
	cClient->Disconnect();
}

//////////////////
// Go to local menu
void GotoLocalMenu()
{
	if(tLX->iGameType == GME_HOST) {
		warnings << "called GotoLocalMenu as host, ignoring..." << endl;
		return;
	}

	SetQuitEngineFlag("GotoLocalMenu");
	cClient->Disconnect();
	DeprecatedGUI::Menu_SetSkipStart(true);
	DeprecatedGUI::Menu_LocalInitialize();
}

//////////////////
// Go to local menu
void GotoNetMenu()
{
	notes << "GotoNetMenu" << endl;
	SetQuitEngineFlag("GotoNetMenu");
	cClient->Disconnect();
	DeprecatedGUI::Menu_SetSkipStart(true);
	DeprecatedGUI::Menu_NetInitialize();
}

////////////////////
// Initialize the loading screen
void InitializeLoading()  {
	if(bDedicated) return; // ignore this case

	FillSurface(VideoPostProcessor::videoSurface(), MakeColour(0,0,0));

	int bar_x, bar_y, bar_label_x, bar_label_y,bar_dir;
	bool bar_visible;
	std::string bar_direction;

	// Load the details
	ReadInteger("data/frontend/frontend.cfg", "Loading", "LoadingBarX", &bar_x, 210);
	ReadInteger("data/frontend/frontend.cfg", "Loading", "LoadingBarY", &bar_y, 230);
	ReadString ("data/frontend/frontend.cfg", "Loading", "LoadingBarDirection", bar_direction, "lefttoright");
	ReadInteger("data/frontend/frontend.cfg", "Loading", "PercentageLabelX", &bar_label_x, 0);
	ReadInteger("data/frontend/frontend.cfg", "Loading", "PercentageLabelY", &bar_label_y, 0);
	ReadKeyword("data/frontend/frontend.cfg", "Loading", "PercentageLabelVisible", &bar_visible, false);
	ReadInteger("data/frontend/frontend.cfg", "Loading", "BackgroundX", &cLoading.iBackgroundX, 190);
	ReadInteger("data/frontend/frontend.cfg", "Loading", "BackgroundY", &cLoading.iBackgroundY, 170);
	ReadInteger("data/frontend/frontend.cfg", "Loading", "LabelX", &cLoading.iLabelX, 235);
	ReadInteger("data/frontend/frontend.cfg", "Loading", "LabelY", &cLoading.iLabelY, 190);

	// Convert the loading direction
	if (!stringcasecmp(bar_direction,"lefttoright"))
		bar_dir = DeprecatedGUI::BAR_LEFTTORIGHT;
	else if (!stringcasecmp(bar_direction,"righttoleft"))
		bar_dir = DeprecatedGUI::BAR_RIGHTTOLEFT;
	else if (!stringcasecmp(bar_direction,"toptobottom"))
		bar_dir = DeprecatedGUI::BAR_TOPTOBOTTOM;
	else if (!stringcasecmp(bar_direction,"bottomtotop"))
		bar_dir = DeprecatedGUI::BAR_BOTTOMTOTOP;
	else
		bar_dir = DeprecatedGUI::BAR_LEFTTORIGHT;


	// Allocate bar
	cLoading.cBar = new DeprecatedGUI::CBar(LoadGameImage("./data/frontend/loading_bar.png",true), bar_x, bar_y, bar_label_x, bar_label_y, bar_dir);
	if (cLoading.cBar)
		cLoading.cBar->SetLabelVisible(bar_visible);

	// Load the background
	cLoading.bmpBackground = LoadGameImage("./data/frontend/background_loading.png", true);
}

/////////////////////
// Draw the loading
void DrawLoading(byte percentage, const std::string &text)  {
	if(bDedicated) {
		notes << "Loading: " << text << endl;
		return;
	}

	if (cLoading.bmpBackground.get() == NULL)
		return;

	// Update the repainted area
	int x = MIN(cLoading.iBackgroundX, cLoading.cBar->GetX());
	int y = MIN(cLoading.cBar->GetY(), cLoading.iBackgroundY);
	int w = MAX(cLoading.bmpBackground.get()->w, cLoading.cBar->GetWidth());
	int h = MAX(cLoading.bmpBackground.get()->h, cLoading.cBar->GetHeight());
	DrawRectFill(VideoPostProcessor::videoSurface(), x, y, x+w, y+h, MakeColour(0,0,0));

	if (cLoading.bmpBackground.get() != NULL)
		DrawImage(VideoPostProcessor::videoSurface(), cLoading.bmpBackground, cLoading.iBackgroundX, cLoading.iBackgroundY);

	if (cLoading.cBar)  {
		cLoading.cBar->SetPosition(percentage);
		cLoading.cBar->Draw( VideoPostProcessor::videoSurface() );
	}

	tLX->cFont.Draw(VideoPostProcessor::videoSurface(), cLoading.iLabelX, cLoading.iLabelY, tLX->clLoadingLabel, text);

	// we are in the main thread, so we can call this directly
	VideoPostProcessor::flipBuffers();
	VideoPostProcessor::process();
	flipRealVideo();
}

////////////////////
// Shutdown the loading screen
void ShutdownLoading()  {
	if (cLoading.cBar)
		delete cLoading.cBar;
	cLoading.cBar = NULL;
}

///////////////////
// Shutdown the game
void ShutdownLieroX()
{
	notes << "Shutting me down..." << endl;

	DeprecatedGUI::CChatWidget::GlobalDestroy();
	
	ShutdownIRC(); // Disconnect from IRC

	if(bDedicated)
		DedicatedControl::Uninit();

	if( ! bDedicated )
		ShutdownMusic();

	if( ! bDedicated )
		ShutdownBackgroundMusic();

    Con_Shutdown();

	ShutdownLoading();  // In case we're called when an error occured

	DeprecatedGUI::ShutdownGraphics();
	SkinnedGUI::ShutdownGuiSkinning();

	ShutdownFontCache();

	DeprecatedGUI::Menu_Shutdown();
	// Only do the deregistration for widgets if we are not restarting.
	// The problem is that we have registered most widgets globally (not by any init-function)
	// so we would not reinit them.
	if(!bRestartGameAfterQuit)
		DeprecatedGUI::CGuiSkin::DeInit();
	ShutdownProfiles();

	// Free the IP to Country DB
	if (tIpToCountryDB) {
		delete tIpToCountryDB;
		tIpToCountryDB = NULL;
	}

	// Free the client & server
	if(cClient) {
		delete cClient;
		cClient = NULL;
	}

	if(cServer) {
		cServer->Shutdown();
		delete cServer;
		cServer = NULL;
	}

	// End logging
	if (convoLogger)  {
		if (tLXOptions->bLogConvos)
			convoLogger->endLogging();
		delete convoLogger;
		convoLogger = NULL;
	}

	// Options
	if(!bDedicated) // only save if not in dedicated mode
		tLXOptions->SaveToDisc();

#ifdef WITH_G15
	if (OLXG15)
	{
		delete OLXG15;
		OLXG15 = NULL;
	}
#endif //WITH_G15
	// Entitites
	ShutdownEntities();

	// Console commands
	Cmd_Free();

	// Event system
	ShutdownEventSystem();

	// SDL, Cache and other small stuff
	ShutdownAuxLib();

	// HINT: must be after shutting down the event system to free the timer correctly
#ifdef DEBUG
	ShutdownCacheDebug();
#endif

	// Save and clear options
	ShutdownOptions();

	// Only do the deregistration for variables if we are not restarting.
	// The problem is that we have registered most vars globally (not by any init-function)
	// so we would not reinit them.
	// Multiple registration of a var is also not a problem because we are
	// using a map for all vars and with a new registration, we would just overwrite
	// the old registration.
	if(!bRestartGameAfterQuit)
	{
		CScriptableVars::DeInit();
	}

	// Shutdown the timers
	ShutdownTimers();

	xmlCleanupParser();

	// Network
	QuitNetworkSystem();

	notes << "Everything was shut down" << endl;
}

std::string quitEngineFlagReason;

void ResetQuitEngineFlag() {
	tLX->bQuitEngine = false;
}

void SetQuitEngineFlag(const std::string& reason) {
	Warning_QuitEngineFlagSet("SetQuitEngineFlag(" + reason + "): ");
	quitEngineFlagReason = reason;
	tLX->bQuitEngine = true;
}

bool Warning_QuitEngineFlagSet(const std::string& preText) {
	if(tLX->bQuitEngine) {
		hints << preText << endl;
		warnings << "bQuitEngine is set because: " << quitEngineFlagReason << endl;
		return true;
	}
	return false;
}
