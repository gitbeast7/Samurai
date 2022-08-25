#pragma once

#include <wx/statusbr.h>

class wxGauge;
class wxTextCtrl;

// ----------------------------------------------------------------------------
// FrameStatusBar
// ----------------------------------------------------------------------------
// A custom status bar which contains controls, icons &c
class FrameStatusBar : public wxStatusBar
{
public:
	FrameStatusBar(wxWindow *parent);
	~FrameStatusBar();

	void setStatusText(std::string text);
	void SetProgress(int progress, std::string text);
	void Start();
	void Done();

private:
		// event handlers
	void OnSize(wxSizeEvent& event);

	wxTextCtrl*	mStaticText;
	wxGauge*	mGauge;

	enum { Field_Text, Field_Gauge, Field_Max };
};
