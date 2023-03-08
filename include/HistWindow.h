#pragma once

#include <wx/window.h>
#include <vector>

class HistWindow : public wxWindow
{
public:
	HistWindow(wxWindow* win);
	~HistWindow();

	enum { XOFF = 42, YOFF = 40 };

	void initialize();
	void setData(std::vector<double>* hist);
	void clear();
	void savePlot();
	void doRefresh(bool force);
	void doDraw(wxDC& dc);
	void snap(std::string& basename);

private:
	void OnPaint(wxPaintEvent& WXUNUSED(event));
	void OnSize(wxSizeEvent& event);

	bool checkMinMax(int y);
	void drawAxes(wxDC& dc, wxSize& sz);
	bool update();
	bool getScaling();
	void scalf1(double xmin, double xmax, int n, double& xminp, double& xmaxp, double& dist);

	double m_min, m_max;
	double m_rng;
	double m_scl;
	double m_amin, m_amax, m_adist;

	std::vector<double>* m_hist;
	std::vector<double> m_hmax;

	int m_last_yi;

	bool	m_force;

	int		m_snapCount;
};

