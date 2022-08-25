#pragma once

#include <string>

// Status event
wxDECLARE_EVENT(wxEVT_STATUS_EVENT, wxCommandEvent);

class MultiCube;
class ControlsPanel;

class StatusInfo
{
public:
	StatusInfo(int type, std::string text=std::string(), int progress=0, void* payload=NULL) :
		m_text(text), m_progress(progress), m_type(type), m_payload(payload) {}

	enum { REFRESH, MESSAGE, START, UPDATE, DONE };

	std::string m_text;
	int			m_progress;
	int			m_type;
	void*		m_payload;
};

class RefreshInfo
{
public:
	RefreshInfo(bool enable, MultiCube* grid) : m_enable(enable), m_grid(grid) {}
	
	bool		m_enable;
	MultiCube*	m_grid;
};

// Define a new application type
class MyApp : public wxApp
{
public:
	MyApp() { ; }

	// virtual wxApp methods
	virtual bool OnInit() wxOVERRIDE;
	virtual int OnExit() wxOVERRIDE;

private:
};

// Define a new frame type
class MyFrame : public wxFrame
{
public:
	MyFrame();

private:
	void OnClose(wxCloseEvent& event);
	void OnQuit(wxCommandEvent& event);
	void OnReset(wxCommandEvent& event);
	void OnAbout(wxCommandEvent& event);
	void OnStatus(wxCommandEvent& event);

	ControlsPanel*	m_cp;

	wxDECLARE_EVENT_TABLE();
};

DECLARE_APP(MyApp)	// Need declaration for Main Application
