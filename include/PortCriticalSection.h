#pragma once

struct PortCriticalSectionImpl;

class PortCriticalSection
{
public:
	PortCriticalSection();
	~PortCriticalSection();
	
	void Enter();		// wait for access to critical section			
	bool TryEnter();	// doesn't wait - returns false immediately if blocked. 
	void Leave();

	class AutoLock
	{
	public:
		AutoLock(PortCriticalSection& csect);
		~AutoLock() { Leave(); }
	protected:
		PortCriticalSection& m_csect;	// critSect supplied as ctor argument
		bool m_locked;			// still in locked mode?
		void Leave()	// for early exit
		{
			if (m_locked)
			{
				m_locked = false;
				m_csect.Leave();
			}
		}
	};

private:
	PortCriticalSectionImpl* m_impl;
};
