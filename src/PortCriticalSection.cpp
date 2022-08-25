#include "PortCriticalSection.h"

#include <windows.h>	// For CRITICAL_SECTION

struct PortCriticalSectionImpl
{
	CRITICAL_SECTION critSect;
};

PortCriticalSection::PortCriticalSection()
{
	m_impl = new PortCriticalSectionImpl;
	::InitializeCriticalSection(&m_impl->critSect);
}

PortCriticalSection::~PortCriticalSection(void)
{
	::DeleteCriticalSection(&m_impl->critSect);
	delete (&m_impl->critSect);
}

void PortCriticalSection::Enter()
{
	::EnterCriticalSection(&m_impl->critSect);
}

bool  PortCriticalSection::TryEnter()
{	// OS method returns BOOL, which is typedef as an int
	return (::TryEnterCriticalSection(&m_impl->critSect) >0);
}


void PortCriticalSection::Leave()
{
	::LeaveCriticalSection(&m_impl->critSect);
}

PortCriticalSection::AutoLock::AutoLock(PortCriticalSection& csect) : m_csect(csect), m_locked(true) 
{ 
	m_csect.Enter(); 
}
