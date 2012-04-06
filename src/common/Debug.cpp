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
#include "OLXCommand.h"
#include "client/StdinCLISupport.h"
#include "util/macros.h"


#include <time.h>

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

/*
  1. param: minCoutVerb
  2. param: minIngameConVerb
  3. param: minCallstackVerb
  The verbosity is defined by GameOptions.Misc.Verbosity (default: 0).
  */

Logger notes(0,2,1000, "n: ");
Logger hints(0,1,100, "H: ");
Logger warnings(0,0,10, "W: ");
Logger errors(-1,-1,1, "E: ");

#include <iostream>
#include <sstream>
#include "ThreadPool.h"
#include "Options.h"
#include "OLXConsole.h"
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

struct CoutPrint : PrintOutFct {
	void print(const std::string& str) const {
		// TODO: We have used std::cout here before but it doesn't seem to work after a while for some reason.
		printf("%s", str.c_str());
	}
};

template<int col>
struct ConPrint : PrintOutFct {
	void print(const std::string& str) const {
		// TODO: Con_AddText adds a line but we only want to add str
		std::string buf = str;
		if(buf.size() > 0 && buf[buf.size()-1] == '\n') buf.erase(buf.size()-1);
		Con_AddText(col, buf, false);
	}
};

// true if last was newline
static bool logger_output(Logger& log, const std::string& buf) {
	bool ret = true;

	std::string prefix = log.prefix;
	if (tLXOptions && tLXOptions->bLogTimestamps)
		prefix = GetLogTimeStamp() + prefix;

	if(!tLXOptions || tLXOptions->iVerbosity >= log.minCoutVerb) {
		SDL_mutexP(globalCoutMutex);
		StdinCLI_StdoutScope stdoutScope;
		ret = PrettyPrint(prefix, buf, CoutPrint(), log.lastWasNewline);
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
				ret = PrettyPrint(prefix, buf, ConPrint<CNC_ERROR>(), log.lastWasNewline);
			else if(log.minIngameConVerb == 0)
				ret = PrettyPrint(prefix, buf, ConPrint<CNC_WARNING>(), log.lastWasNewline);
			else if(log.minIngameConVerb == 1)
				ret = PrettyPrint(prefix, buf, ConPrint<CNC_NOTIFY>(), log.lastWasNewline);
			else if(log.minIngameConVerb < 5)
				ret = PrettyPrint(prefix, buf, ConPrint<CNC_NORMAL>(), log.lastWasNewline);
			else // >=5
				ret = PrettyPrint(prefix, buf, ConPrint<CNC_DEV>(), log.lastWasNewline);
		}
		if(tLXOptions->iVerbosity >= log.minCallstackVerb) {
			DumpCallstack(ConPrint<CNC_DEV>());
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
