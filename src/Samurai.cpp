// For compilers that support precompilation, includes "wx.h".
#include "wx/wxprec.h"
#ifndef WX_PRECOMP
#include "wx/wx.h"
#endif

#include "Samurai.h"
#include "FrameStatusBar.h"
#include "ControlsPanel.h"
#include "GLDisplay.h"
#include "MultiCube.h"

extern std::string format(const char *fmt, ...);

MyFrame* GlobalFrame = NULL;
FrameStatusBar* GlobalStatusBar = NULL;
ControlsPanel* GlobalControlsPanel = NULL;
extern wxTextCtrl* GlobalMessageControl;
extern GLDisplayCanvas* GlobalGLDisplayCanvas;

// Status event
wxDEFINE_EVENT(wxEVT_STATUS_EVENT, wxCommandEvent);

// ----------------------------------------------------------------------------
// MyApp: the application object
// ----------------------------------------------------------------------------
wxIMPLEMENT_APP(MyApp);

bool MyApp::OnInit()
{
	if (!wxApp::OnInit())
		return false;

	std::string appName = "Samurai";
	SetAppName(appName);
	new MyFrame();

	return true;
}

int MyApp::OnExit()
{
	return wxApp::OnExit();
}

// ----------------------------------------------------------------------------
// MyFrame: main application window
// ----------------------------------------------------------------------------

wxBEGIN_EVENT_TABLE(MyFrame, wxFrame)
EVT_MENU(wxID_EXIT, MyFrame::OnQuit)
EVT_MENU(wxID_RESET, MyFrame::OnReset)
EVT_MENU(wxID_ABOUT, MyFrame::OnAbout)
wxEND_EVENT_TABLE()

MyFrame::MyFrame() : wxFrame(NULL, wxID_ANY, "Samurai (Version 2.0)")
{
	GlobalFrame = this;

	SetIcon(wxICON(MYICON));

		// Make a menubar
	wxMenuBar *menuBar = new wxMenuBar;

		// Make the File menu
	wxMenu *menu = new wxMenu;
	menu->Append(wxID_RESET, "Restore Defaults");
	menu->Append(wxID_EXIT, "&Quit\tCtrl+Q");
	menuBar->Append(menu, "&File");

		// Make the File menu
	menu = new wxMenu;
	//menu->Append(wxID_HELP, "View Help");
	menu->Append(wxID_ABOUT, "About");
	menuBar->Append(menu, "&Help");

	SetMenuBar(menuBar);

		// Remove the old status bar
	wxStatusBar *statbarOld = GetStatusBar();
	if ( statbarOld )
	{	SetStatusBar(NULL);
		delete statbarOld;
	}

		// Create a Status Bar for messages and progress bar use
	SetStatusBar(new FrameStatusBar(this));
	FrameStatusBar* statusBar = (FrameStatusBar*)GetStatusBar();
	GlobalStatusBar = statusBar;
	statusBar->setStatusText("Ready");

		// Create the main panel
	m_cp = new ControlsPanel(this);

	Bind(wxEVT_STATUS_EVENT,	&MyFrame::OnStatus, this);
	Bind(wxEVT_CLOSE_WINDOW,	&MyFrame::OnClose, this);

	Show();
}

void MyFrame::OnQuit(wxCommandEvent& event)
{
	m_cp->shutdown();	// Clean up before shutting down
	
	Close();

	event.Skip();
}

void MyFrame::OnReset(wxCommandEvent& event)
{
	m_cp->setCurrentParams();

	event.Skip();
}

void MyFrame::OnClose(wxCloseEvent& event)
{
	m_cp->shutdown();	// Clean up before shutting down

	event.Skip();
}

void MyFrame::OnAbout(wxCommandEvent& WXUNUSED(event))
{
		// Show the about message
	wxString caption = wxString::Format("About %s", "SAMURAI");
	wxString message;
	message.Printf("SAMURAI : Surface Area Model Using Rubik As Inspriation\n");
	message += wxT("Written By Bob Eastwood (09/2021)");
	wxMessageBox(message, caption, wxICON_NONE, this);
}

void MyFrame::OnStatus(wxCommandEvent& event)
{
	StatusInfo* statusInfo = (StatusInfo*)event.GetClientData();

	switch (statusInfo->m_type) {
		case StatusInfo::REFRESH:
		{	
			MultiCube* grid = (MultiCube*)statusInfo->m_payload;
			GlobalGLDisplayCanvas->SetGrid(grid);
			GlobalGLDisplayCanvas->Update(); GlobalGLDisplayCanvas->Refresh(false);	// Update the display
		}
		break;
		case StatusInfo::MESSAGE:
		{	
			if (GlobalMessageControl != NULL)
				GlobalMessageControl->AppendText(statusInfo->m_text.c_str());
		}
		break;
		case StatusInfo::START:
		{
			if (GlobalStatusBar)
				GlobalStatusBar->Start();
		}
		break;
		case StatusInfo::UPDATE:
		{
			if (GlobalStatusBar)
			{	GlobalStatusBar->SetProgress(statusInfo->m_progress, statusInfo->m_text);
			}
		}
		break;
		case StatusInfo::DONE:
		{
			if (GlobalStatusBar)
				GlobalStatusBar->Done();
		}
		break;
	}

	delete statusInfo;
}
