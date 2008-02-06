/*
	OpenLieroX
	
	threadsafe variable
	
	code under LGPL
	by Albert Zeyer
*/

#ifndef __THREADVAR_H__
#define __THREADVAR_H__

#include "ReadWriteLock.h"

template< typename _T >
class ThreadVar {

private:
	_T data;
	ReadWriteLock locker;

public:
	class Reader {
	private:
		ThreadVar<_T>& tvar;
	public:
		Reader(ThreadVar<_T>& var) : tvar(var) { var.locker.startReadAccess(); }
		~Reader() { tvar.locker.endReadAccess(); }
		const _T& get() const { return tvar.data; }
	};

	class Writer {
	private:
		ThreadVar<_T>& tvar;
	public:
		Writer(ThreadVar<_T>& var) : tvar(var) { var.locker.startWriteAccess(); }
		~Writer() { tvar.locker.endWriteAccess(); }
		_T& get() { return tvar.data; }
		const _T& get() const { return tvar.data; }
	};
	
};

#endif
