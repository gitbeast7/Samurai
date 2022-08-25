#pragma once

#include <wx/window.h>
#include <vector>

struct gPoint
{
	double x, y;
};

typedef std::vector<gPoint> GPoints;
typedef std::vector<GPoints> GLines;

class PlotWindow : public wxWindow
{
public:
	PlotWindow(wxWindow* win);
	~PlotWindow();

	enum { XOFF = 42, YOFF = 40 };

	void initialize();
	void setData(std::vector<double>* xv, std::vector<double>* yv);
	void clear();
	void savePlot();
	void doRefresh(bool force);
	void doDraw(wxDC& dc);
	void snap(std::string& basename);

private:
	void OnPaint(wxPaintEvent& WXUNUSED(event));
	void OnSize(wxSizeEvent& event);

	void plotSaved(wxDC& dc, wxSize& sz);
	bool checkMinMax(double x, double y);
	bool minmaxSaved();
	void drawAxes(wxDC& dc, wxSize& sz);
	bool update();
	bool getScaling();
	void scalf1(double xmin, double xmax, int n, double& xminp, double& xmaxp, double& dist);

	double m_xmin, m_xmax;
	double m_ymin, m_ymax;
	double m_xrng, m_yrng;
	double m_xscl, m_yscl;
	double m_xamin, m_xamax, m_xadist;
	double m_yamin, m_yamax, m_yadist;

	std::vector<double>* m_xv;
	std::vector<double>* m_yv;

	int m_last_xi, m_last_yi;
	int m_last_index;

	bool	m_save, m_force;

	GLines	m_lines;

	int		m_snapCount;
};

