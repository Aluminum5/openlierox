/*
 *  ThreadPool.cpp
 *  OpenLieroX
 *
 *  Created by Albert Zeyer on 08.02.09.
 *  code under LGPL
 *
 */

#include <SDL_thread.h>
#include "ThreadPool.h"
#include "Debug.h"
#include "AuxLib.h"
#include "ReadWriteLock.h" // for ScopedLock
#include "OLXCommand.h"
#include "util/macros.h"

#if !defined(WIN32) || defined(HAVE_PTHREAD)
#include <pthread.h>
#ifndef HAVE_PTHREAD
#define HAVE_PTHREAD
#endif
#endif

static bool isThreadIdValid(ThreadId id) {
	if(id == 0) return false;
	if(id == (ThreadId)-1) return false;
	return true;
}

ThreadId mainThreadId = -1;

bool isMainThread() {
	return mainThreadId == getCurrentThreadId();
}

ThreadId gameloopThreadId = -1;

bool isGameloopThread() {
	if(gameloopThreadId == (ThreadId)-1) {
		errors << "isGameloopThread: Gameloop thread is not running" << endl;
		return false;
	}
	return gameloopThreadId == getCurrentThreadId();
}

void getAllThreads(std::set<ThreadId>& ids) {
	if(isThreadIdValid(mainThreadId))
		ids.insert(mainThreadId);
	if(isThreadIdValid(gameloopThreadId))
		ids.insert(gameloopThreadId);

	if(threadPool) {
		std::map<ThreadId, std::string> threads;
		threadPool->getAllWorkingThreads(threads);
		foreach(t, threads) {
			ids.insert(t->first);
		}
	}
}

std::string getThreadName(ThreadId tid) {
#ifdef HAVE_PTHREAD
	char buf[128] = "\0";
	if(pthread_getname_np((pthread_t) tid, buf, sizeof(buf)) == 0)
		if(strlen(buf) > 0)
			return buf;
#endif

	if(threadPool) {
		std::map<ThreadId, std::string> threads;
		threadPool->getAllWorkingThreads(threads);
		foreach(t, threads) {
			if(t->first == tid)
				return t->second;
		}
	}

	return "";
}

ThreadId getCurrentThreadId() {
#ifdef HAVE_PTHREAD
	return (ThreadId) pthread_self();
#else
	// SDL_ThreadID returns a Uint32.
	// On 64bit systems, this might not be enough information about the current thread.
	// Win64 thread HANDLE is 64bit, aswell as pthread_t.
	return (ThreadId) SDL_ThreadID();
#endif
}

ThreadPool::ThreadPool(unsigned int size) {
	nextAction = NULL; nextIsHeadless = false; nextData = NULL;
	quitting = false;	
	mutex = SDL_CreateMutex();
	awakeThread = SDL_CreateCond();
	threadStartedWork = SDL_CreateCond();
	threadStatusChanged = SDL_CreateCond();
	startMutex = SDL_CreateMutex();
	
	notes << "ThreadPool: creating " << size << " threads ..." << endl;
	while(availableThreads.size() < size)
		prepareNewThread();
}

ThreadPool::~ThreadPool() {
	waitAll();

	// this is the hint for all available threads to break
	SDL_mutexP(mutex); // lock to be sure that every thread is outside that region, we could get crashes otherwise
	nextAction = NULL;
	quitting = true;
	SDL_CondBroadcast(awakeThread);
	for(std::set<ThreadPoolItem*>::iterator i = availableThreads.begin(); i != availableThreads.end(); ++i) {
		SDL_mutexV(mutex);
		SDL_WaitThread((*i)->thread, NULL);
		SDL_mutexP(mutex);
		SDL_DestroyCond((*i)->finishedSignal);
		SDL_DestroyCond((*i)->readyForNewWork);
		delete *i;
	}
	availableThreads.clear();
	SDL_mutexV(mutex);
	
	SDL_DestroyMutex(startMutex);
	SDL_DestroyCond(threadStartedWork);
	SDL_DestroyCond(threadStatusChanged);
	SDL_DestroyCond(awakeThread);
	SDL_DestroyMutex(mutex);
}

void ThreadPool::prepareNewThread() {
	ThreadPoolItem* t = new ThreadPoolItem();
	t->pool = this;
	t->finishedSignal = SDL_CreateCond();
	t->readyForNewWork = SDL_CreateCond();
	t->finished = false;
	t->working = false;
	availableThreads.insert(t);
	t->nativeThreadId = 0;
	t->thread = SDL_CreateThread(threadWrapper, t);
}

int ThreadPool::threadWrapper(void* param) {
	ThreadPoolItem* data = (ThreadPoolItem*)param;
	data->nativeThreadId = getCurrentThreadId();

	SDL_mutexP(data->pool->mutex);
	while(true) {
		while(data->pool->nextAction == NULL && !data->pool->quitting)
			SDL_CondWait(data->pool->awakeThread, data->pool->mutex);
		if(data->pool->quitting) break;
		data->pool->usedThreads.insert(data);
		data->pool->availableThreads.erase(data);
		
		Action* act = data->pool->nextAction; data->pool->nextAction = NULL;
		data->headless = data->pool->nextIsHeadless;
		data->name = data->pool->nextName;
		data->finished = false;
		data->working = true;
		data->pool->nextData = data;
		SDL_mutexV(data->pool->mutex);
		
		SDL_CondSignal(data->pool->threadStartedWork);
		setCurThreadName(data->name);
		data->ret = act->handle();
		delete act;
		setCurThreadName(data->name + " [finished]");
		SDL_mutexP(data->pool->mutex);
		data->finished = true;
		SDL_CondSignal(data->pool->threadStatusChanged);
		
		if(!data->headless) { // headless means that we just can clean it up right now without waiting
			SDL_CondSignal(data->finishedSignal);
			while(data->working) SDL_CondWait(data->readyForNewWork, data->pool->mutex);
		} else
			data->working = false;
		data->pool->usedThreads.erase(data);
		data->pool->availableThreads.insert(data);
		SDL_CondSignal(data->pool->threadStatusChanged);
		setCurThreadName("");
	}

	SDL_mutexV(data->pool->mutex);
		
	return 0;
}

ThreadPoolItem* ThreadPool::start(Action* act, const std::string& name, bool headless) {
	SDL_mutexP(startMutex); // If start() method will be called from different threads without mutex, hard-to-find crashes will occur
	SDL_mutexP(mutex);
	if(availableThreads.size() == 0) {
#ifndef SINGLETHREADED
		warnings << "no available thread in ThreadPool for " << name << ", creating new one..." << endl;
#endif
		prepareNewThread();
	}
	assert(nextAction == NULL);
	assert(nextData == NULL);
	nextAction = act;
	nextIsHeadless = headless;
	nextName = name;
	
	SDL_CondSignal(awakeThread);
	while(nextData == NULL) SDL_CondWait(threadStartedWork, mutex);
	ThreadPoolItem* data = nextData; nextData = NULL;
	SDL_mutexV(mutex);
		
	SDL_mutexV(startMutex);
	return data;
}

ThreadPoolItem* ThreadPool::start(ThreadFunc fct, void* param, const std::string& name) {
	struct StaticAction : Action {
		ThreadFunc fct; void* param;
		Result handle() { return (*fct) (param); }
	};
	StaticAction* act = new StaticAction();
	act->fct = fct;
	act->param = param;
	ThreadPoolItem* item = start(act, name);
	if(item) return item;
	delete act;
	return NULL;
}

ThreadPoolItem* ThreadPool::start(boost::function<Result()> fct, const std::string& name, bool headless) {
	struct FctPtrAction : Action {
		boost::function<Result()> fct;
		Result handle() { return fct(); }
	};
	FctPtrAction* act = new FctPtrAction();
	act->fct = fct;
	return start(act, name, headless);
}

bool ThreadPool::wait(ThreadPoolItem* thread, int* status) {
	if(!thread) return false;
	SDL_mutexP(mutex);
	if(!thread->working) {
		warnings << "given thread " << thread->name << " is not working anymore" << endl;
		SDL_mutexV(mutex);
		return false;
	}
	while(!thread->finished) SDL_CondWait(thread->finishedSignal, mutex);
	if(status) *status = thread->ret;
	thread->working = false;
	SDL_mutexV(mutex);
	
	SDL_CondSignal(thread->readyForNewWork);
	return true;
}

bool ThreadPool::waitAll() {
	SDL_mutexP(mutex);
	while(usedThreads.size() > 0) {		
		warnings << "ThreadPool: waiting for " << usedThreads.size() << " threads to finish:" << endl;
		for(std::set<ThreadPoolItem*>::iterator i = usedThreads.begin(); i != usedThreads.end(); ++i) {
			if((*i)->working && (*i)->finished) {
				warnings << "  thread " << (*i)->name << " is ready but was not cleaned up" << endl;
				(*i)->working = false;
				SDL_CondSignal((*i)->readyForNewWork);
			}
			else if((*i)->working && !(*i)->finished) {
				warnings << "  thread " << (*i)->name << " is still working" << endl;
			}
			else if(!(*i)->working && !(*i)->headless && (*i)->finished) {
				warnings << "  thread " << (*i)->name << " is cleaning itself up right now" << endl;
			}
			else {
				warnings << "  thread " << (*i)->name << " is in an invalid state" << endl;
			}
		}
		SDL_CondWait(threadStatusChanged, mutex);
	}
	SDL_mutexV(mutex);
	
	return true;
}

void ThreadPool::dumpState(CmdLineIntf& cli) const {
	ScopedLock lock(mutex);
	for(std::set<ThreadPoolItem*>::const_iterator i = usedThreads.begin(); i != usedThreads.end(); ++i) {
		if((*i)->working && (*i)->finished)
			cli.writeMsg("thread '" + (*i)->name + "': ready but was not cleaned up");
		else if((*i)->working && !(*i)->finished)
			cli.writeMsg("thread '" + (*i)->name + "': working");
		else if(!(*i)->working && !(*i)->headless && (*i)->finished)
			cli.writeMsg("thread '" + (*i)->name + "': cleanup");
		else
			cli.writeMsg("thread '" + (*i)->name + "': invalid");
	}
}

void ThreadPool::getAllWorkingThreads(std::map<ThreadId, std::string>& threads) {
	ScopedLock lock(mutex);
	for(std::set<ThreadPoolItem*>::const_iterator i = usedThreads.begin(); i != usedThreads.end(); ++i) {
		if((*i)->working && !(*i)->finished)
			threads[(*i)->nativeThreadId] = (*i)->name;
	}
}


ThreadPool* threadPool = NULL;

void InitThreadPool(unsigned int size) {
#ifdef SINGLETHREADED
	size = 0;
#endif
	if(!threadPool)
		threadPool = new ThreadPool(size);
	else
		errors << "ThreadPool inited twice" << endl;
}

void UnInitThreadPool() {
	if(threadPool) {
		delete threadPool;
		threadPool = NULL;
	} else
		errors << "ThreadPool already uninited" << endl;
}


