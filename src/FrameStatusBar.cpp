#include <wx/gauge.h>
#include <wx/textctrl.h>

#include "FrameStatusBar.h"

FrameStatusBar::FrameStatusBar(wxWindow *parent)
	: wxStatusBar(parent, wxID_ANY, wxSTB_DEFAULT_STYLE)
{
    static const int widths[Field_Max] = { -1, 240};
	static const int styles[Field_Max] = { wxSB_FLAT, wxSB_FLAT };

    SetFieldsCount(Field_Max, widths);
    SetStatusWidths(Field_Max, widths);
	SetStatusStyles(Field_Max, styles);

	mStaticText = new wxTextCtrl(this, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxTE_READONLY | wxBORDER_NONE);

	mGauge = new wxGauge(this, wxID_ANY, 100, wxDefaultPosition, wxSize(widths[Field_Gauge],-1));

	mGauge->Hide();

	Bind(wxEVT_SIZE,	&FrameStatusBar::OnSize, this);
}

FrameStatusBar::~FrameStatusBar()
{
	Unbind(wxEVT_SIZE,	&FrameStatusBar::OnSize, this);
}

void FrameStatusBar::OnSize(wxSizeEvent& event)
{
	wxSize size = GetClientSize();
    wxSize gaugeSize = mGauge->GetSize();
	gaugeSize.x += 16;	// Pad

    mGauge->Move(size.x - gaugeSize.x, (size.y-gaugeSize.y)/2);

	if (size.x>gaugeSize.x)
	{	mStaticText->SetSize(size.x-gaugeSize.x, -1);
		wxSize textSize = mStaticText->GetSize();
	    mStaticText->Move(0, (size.y-textSize.y)/2);
	}
	event.Skip();
}

void FrameStatusBar::setStatusText(std::string text)
{
	mStaticText->SetValue(text);
}

/*
* Set progress bar value
*/
void FrameStatusBar::SetProgress(int progress, std::string text)
{
	mGauge->SetValue(progress);
	setStatusText(text);
	Refresh(); Update();
}

void FrameStatusBar::Start()
{	
	SetProgress(0, "");
	mGauge->Show();
}

void FrameStatusBar::Done()
{
	setStatusText(std::string());
	mGauge->Hide();
}
