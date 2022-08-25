#include "ProcThread.h"
#include "ControlsPanel.h"

ProcThread::ProcThread(ControlsPanel* cp, bool prerun/*=false*/) :
	wxThread(wxTHREAD_JOINABLE),
	mCP(cp), m_prerun(prerun)
{
	Create();	// Go ahead and create the thread.
	Run();
}

wxThread::ExitCode ProcThread::Entry()
{	wxThread::ExitCode result=0;
		// One-shot - just runs until complete
	mCP->run(m_prerun);

		// Notify main GUI thread of completion
	wxThreadEvent* event = new wxThreadEvent(wxEVT_THREAD_DONE_EVENT);
	event->SetEventObject((wxObject* )this);
	wxQueueEvent(mCP, event);
	return result;
}
