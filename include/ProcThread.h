#pragma once
#include <wx/thread.h>

class ControlsPanel;

	// Samurai Run Thread
class ProcThread : public wxThread
{
public:
	ProcThread(ControlsPanel* cp, bool prerun=false);
	virtual wxThread::ExitCode Entry();

	bool m_prerun;

private:
	ControlsPanel* mCP;
};

