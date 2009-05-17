/*
 *  Debug.cpp
 *  OpenLieroX
 *
 *  Created by Albert Zeyer on 01.01.09.
 *  code under LGPL
 *
 */

#include "Debug.h"
#include "StringUtils.h"
#include "CrashHandler.h"
#include "Command.h"

#include <time.h>

#ifdef WIN32

void RaiseDebugger() {
#ifdef DEBUG
	// HINT: ignored when not in debugger
	// If it just does nothing then, remove the surrounding #ifdef DEBUG
	// I read about a Win32's IsDebuggerPresent() function, perhaps you should use that one here.
	__asm  { int 3 };
#endif
}

#else

#if defined(__APPLE__)

#include <cassert>
#include <stdbool.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/sysctl.h>

// Based on Apple's recommended method as described in
// http://developer.apple.com/qa/qa2004/qa1361.html
static bool AmIBeingDebugged()
    // Returns true if the current process is being debugged (either
    // running under the debugger or has a debugger attached post facto).
{
	// Initialize mib, which tells sysctl what info we want.  In this case,
	// we're looking for information about a specific process ID.
	int mib[] =
	{
		CTL_KERN,
		KERN_PROC,
		KERN_PROC_PID,
		getpid()
	};
	
	// Caution: struct kinfo_proc is marked __APPLE_API_UNSTABLE.  The source and
	// binary interfaces may change.
	struct kinfo_proc info;
	size_t info_size = sizeof ( info );
	
	int sysctl_result = sysctl ( mib, sizeof(mib) / sizeof(*mib), &info, &info_size, NULL, 0 );
	if ( sysctl_result != 0 )
		return false;
	
	// This process is being debugged if the P_TRACED flag is set.
	return ( info.kp_proc.p_flag & P_TRACED ) != 0;
}

#else

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <cstring>
#include <unistd.h>


static bool AmIBeingDebugged() {
	// We can look in /proc/self/status for TracerPid.  We are likely used in crash
	// handling, so we are careful not to use the heap or have side effects.
	int status_fd = open("/proc/self/status", O_RDONLY);
	if (status_fd == -1)
		return false;

	// We assume our line will be in the first 1024 characters and that we can
	// read this much all at once.  In practice this will generally be true.
	// This simplifies and speeds up things considerably.
	char buf[1024];

	ssize_t num_read = read(status_fd, buf, sizeof(buf));
	fix_markend(buf);
	close(status_fd);
	if (num_read <= 0) return false;

	const char* searchStr = "TracerPid:\t";
	const char* f = strstr(buf, searchStr);
	if(f == NULL) return false;
	
	// Our pid is 0 without a debugger, assume this for any pid starting with 0.
	f += strlen(searchStr);
	return f < &buf[num_read] && *f != '0';
}

#endif // OSX/LINUX

#include <signal.h>

void RaiseDebugger() {
	if(AmIBeingDebugged()) {
		printf("I am being debugged, raising debugger ...\n");
		CrashHandler::get()->disable();
		// TODO: We need a way to set another ucontext here. (And that should be specified via a parameter
		// to RaiseDebugger().) E.g. when we use this function in the debugger thread, we want to set the
		// ucontext of the main loop thread.
		raise(SIGABRT);
		CrashHandler::get()->enable();
	} else
		printf("I am not being debugged, ignoring debugger raise.\n");
}

#endif


#ifdef WIN32
void OlxWriteCoreDump(const char* file_postfix) {}

#else

#ifdef GCOREDUMPER
#include <google/coredumper.h>
#endif
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <cstring>
#include <cstdio>

#ifndef GCOREDUMPER
static void GdbWriteCoreDump(const char* fname) {
	// WARNING: this is terribly slow like this
	char gdbparam[1000];
	sprintf(gdbparam,
			"attach %i \n"
			"gcore %s \n"
			"detach \n"
			"quit \n",
			getpid(), fname);
	FILE* p = popen("gdb -q", "w");
	if(p) {
		fprintf(p, "%s", gdbparam);
		fflush(p);
		int status = 0; wait(&status);
		pclose(p);
	}
}
#endif

void OlxWriteCoreDump(const char* file_postfix) {
	char corefile[PATH_MAX + 100];
	if(getcwd(corefile, PATH_MAX) == NULL) strcpy(corefile, "");
	strcat(corefile, "/core.OpenLieroX");
	if(file_postfix) { strcat(corefile, "."); strcat(corefile, file_postfix); }
	printf("writing coredump to %s\n", corefile);
	
	printf("dumping core ... "); fflush(0);
#ifdef GCOREDUMPER
	WriteCoreDump(corefile);
#else
	GdbWriteCoreDump(corefile);
#endif
	printf("ready\n");
}

#endif

// ----------------- core dump code end -------------


// --------------- backtrace stuff start --------------

#ifndef HAVE_EXECINFO
#	if defined(__linux__)
#		define HAVE_EXECINFO 1
#	elif defined(__DARWIN_VERS_1050)
#		define HAVE_EXECINFO 1
#	else
#		define HAVE_EXECINFO 0
#	endif
#endif

#if HAVE_EXECINFO

#include <execinfo.h>
#include <stdio.h>
#include <stdlib.h>

void DumpCallstackPrintf(void* callpnt) {
	void *callstack[128];
	int framesC = backtrace(callstack, sizeof(callstack));
	printf("backtrace() returned %d addresses\n", framesC);
	if(callpnt != NULL && framesC > 3) callstack[3] = callpnt; // expected to be called from signal handler
	char** strs = backtrace_symbols(callstack, framesC);
	for(int i = 0; i < framesC; ++i) {
		if(strs[i])
			printf("%s\n", strs[i]);
		else
			break;
	}
	free(strs);
}

void DumpCallstack(void (*PrintOutFct) (const std::string&)) {
	void *callstack[128];
	int framesC = backtrace(callstack, sizeof(callstack));
	(*PrintOutFct) ("DumpCallstack: " + itoa(framesC) + " addresses:");
	char** strs = backtrace_symbols(callstack, framesC);
	for(int i = 0; i < framesC; ++i) {
		if(strs[i])
			(*PrintOutFct) (std::string(" ") + strs[i] + "\n");
		else
			break;
	}
	free(strs);
}

#elif defined(WIN32)

#include "StackWalker.h"  // Call Luke Stackwalker for help

typedef void (*PrintOutFct) (const std::string&);

// Override the default stackwalker with our own print functions
class PrintStackWalker : public StackWalker  {
private:
	PrintOutFct m_print;

public:
	PrintStackWalker(PrintOutFct fct = NULL) : StackWalker(RetrieveVerbose) { m_print = fct; }
	void OnLoadModule(LPCSTR img, LPCSTR mod, DWORD64 baseAddr, DWORD size, DWORD result, LPCSTR symType, LPCSTR pdbName, ULONGLONG fileVersion)
	{

	}

	void OnOutput(LPCSTR szText)  
	{
		if (m_print == NULL)
			printf(szText);
		else
			m_print(std::string(szText));
		StackWalker::OnOutput(szText);
	}
};

void DumpCallstackPrintf(void* callpnt) 
{
	PrintStackWalker sw;
	sw.ShowCallstack();
	
}
void DumpCallstack(void (*LineOutFct) (const std::string&)) 
{  
	PrintStackWalker sw(LineOutFct);
	sw.ShowCallstack();
}

#else

#warning No DumpCallstack implementation for this arch/sys

void DumpCallstackPrintf(void* callpnt) {
	printf("DumpCallstackPrintf not implemented\n");
}

void DumpCallstack(void (*PrintOutFct) (const std::string&)) {
	printf("DumpCallstack not implemented\n");
}

#endif

std::string GetLogTimeStamp()
{
	// TODO: please recode this, don't use C-strings!
	char buf[64];
	const time_t unif_time = time(NULL);
	struct tm *t = localtime(&unif_time);
	if (t == NULL)
		return "";
	
	strftime(buf, sizeof(buf), "[%H:%M:%S] ", t);
	fix_markend(buf);
	return std::string(buf);
}

Logger notes(0,2,1000, "n: ");
Logger hints(0,1,100, "H: ");
Logger warnings(0,0,10, "W: ");
Logger errors(-1,-1,1, "E: ");

#include <iostream>
#include <sstream>
#include "ThreadPool.h"
#include "Options.h"
#include "console.h"
#include "StringUtils.h"

static SDL_mutex* globalCoutMutex = NULL;

Logger::Logger(int o, int ingame, int callst, const std::string& p)
: minCoutVerb(o), minIngameConVerb(ingame), minCallstackVerb(callst), prefix(p), lastWasNewline(true), mutex(NULL) {
	mutex = SDL_CreateMutex();
	if(!globalCoutMutex)
		globalCoutMutex = SDL_CreateMutex();
}

Logger::~Logger() {
	SDL_DestroyMutex(mutex); mutex = NULL;
	if(globalCoutMutex) {
		SDL_DestroyMutex(globalCoutMutex);
		globalCoutMutex = NULL;
	}
}

void Logger::lock() {
	SDL_mutexP(mutex);
}

void Logger::unlock() {
	SDL_mutexV(mutex);
}

static void CoutPrint(const std::string& str) {
	// TODO: We have used std::cout here before but it doesn't seem to work after a while for some reason.
	printf("%s", str.c_str());
}

template<int col> void ConPrint(const std::string& str) {
	// TODO: Con_AddText adds a line but we only want to add str
	std::string buf = str;
	if(buf.size() > 0 && buf[buf.size()-1] == '\n') buf.erase(buf.size()-1);
	Con_AddText(col, buf, false);
}

// true if last was newline
static bool logger_output(Logger& log, const std::string& buf) {
	bool ret = true;

	std::string prefix = log.prefix;
	if (tLXOptions && tLXOptions->bLogTimestamps)
		prefix = GetLogTimeStamp() + prefix;

	if(!tLXOptions || tLXOptions->iVerbosity >= log.minCoutVerb) {
		SDL_mutexP(globalCoutMutex);
		ret = PrettyPrint(prefix, buf, CoutPrint, log.lastWasNewline);
		//std::cout.flush();
		SDL_mutexV(globalCoutMutex);
	}
	if(tLXOptions && tLXOptions->iVerbosity >= log.minCallstackVerb) {
		DumpCallstackPrintf();
	}
	if(tLXOptions && Con_IsInited() && tLXOptions->iVerbosity >= log.minIngameConVerb) {
		// the check is a bit hacky (see Con_AddText) but I really dont want to overcomplicate this
		if(!strStartsWith(buf, "Ingame console: ")) {
			// we are not safing explicitly a color in the Logger, thus we try to assume a good color from the verbosity level
			if(log.minIngameConVerb < 0)
				ret = PrettyPrint(prefix, buf, ConPrint<CNC_ERROR>, log.lastWasNewline);
			else if(log.minIngameConVerb == 0)
				ret = PrettyPrint(prefix, buf, ConPrint<CNC_WARNING>, log.lastWasNewline);
			else if(log.minIngameConVerb == 1)
				ret = PrettyPrint(prefix, buf, ConPrint<CNC_NOTIFY>, log.lastWasNewline);
			else if(log.minIngameConVerb < 5)
				ret = PrettyPrint(prefix, buf, ConPrint<CNC_NORMAL>, log.lastWasNewline);
			else // >=5
				ret = PrettyPrint(prefix, buf, ConPrint<CNC_DEV>, log.lastWasNewline);
		}
		if(tLXOptions->iVerbosity >= log.minCallstackVerb) {
			DumpCallstack(ConPrint<CNC_DEV>);
		}
	}
	return ret;
}

Logger& Logger::flush() {
	lock();
	lastWasNewline = logger_output(*this, buffer);
	buffer = "";
	unlock();
	return *this;
}
