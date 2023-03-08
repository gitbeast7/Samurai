#include <wx/dc.h>
#include <wx/dcclient.h>
#include <wx/dcmemory.h>

#include "PlotWindow.h"

PlotWindow::PlotWindow(wxWindow* win) : wxWindow(win, wxID_ANY)
{
	m_xv = NULL;
	m_yv = NULL;

	m_force = false;
	m_snapCount = 0;

	Bind(wxEVT_PAINT, &PlotWindow::OnPaint, this);
	Bind(wxEVT_SIZE, &PlotWindow::OnSize, this);
}

PlotWindow::~PlotWindow()
{

}

void PlotWindow::initialize()
{
	m_xmin = 0.0;// DBL_MAX;
	m_xmax = 1.0;// DBL_MIN;
	m_ymin = 0.0;// DBL_MAX;
	m_ymax = 1.0;// DBL_MIN;

	m_xamin = 0.0;
	m_xamax = 1.0;
	m_xadist = 1.0;

	m_yamin = 0.0;
	m_yamax = 1.0;
	m_yadist = 1.0;

	getScaling();
}

void PlotWindow::OnSize(wxSizeEvent& event)
{
	m_force = true;
	Refresh();
	event.Skip();
}

void PlotWindow::doDraw(wxDC& dc)
{
	wxSize sz = GetClientSize();
	sz.x -= XOFF;
	sz.y -= YOFF;

	dc.SetPen(*wxBLACK_PEN);
	drawAxes(dc, sz);

	// Update any scaling that may have changed due to new data
	if (update() || m_force)
	{
		m_force = false;
		dc.Clear();	// Only clear the display if scaling has changed
		drawAxes(dc, sz);
	}

	plotSaved(dc, sz);

	if ((m_xv == NULL) || m_xv->empty())
		return;	// Nothing to plot

	double x = ((*m_xv)[0] - m_xmin) * m_xscl;
	double y = ((*m_yv)[0] - m_ymin) * m_yscl;
	m_last_xi = (int)round(x) + XOFF;
	m_last_yi = sz.y - (int)round(y);

	int npts = (int)m_xv->size();
	for (int i = 1; i < npts; i++)
	{
		double x = ((*m_xv)[i] - m_xmin) * m_xscl;
		double y = ((*m_yv)[i] - m_ymin) * m_yscl;
		int xi = (int)round(x) + XOFF;
		int yi = sz.y - (int)round(y);
		if ((xi != m_last_xi) || (yi != m_last_yi))
		{
			dc.DrawLine(m_last_xi, m_last_yi, xi, yi);
			m_last_xi = xi; m_last_yi = yi;
		}
	}
}

void PlotWindow::OnPaint(wxPaintEvent& WXUNUSED(event))
{
	wxPaintDC dc(this);
	PrepareDC(dc);

	doDraw(dc);
}

void PlotWindow::snap(std::string& basename)
{
	wxClientDC dc(this);
	doDraw(dc);

	const wxSize ClientSize = GetSize();

	wxBitmap bitmap(ClientSize);
	wxMemoryDC mdc;
	mdc.SelectObject(bitmap);
	mdc.Blit(wxPoint(0, 0), ClientSize, &dc, wxPoint(0, 0));
	mdc.SelectObject(wxNullBitmap);
	wxImage simage = bitmap.ConvertToImage();
	if (simage.IsOk())
	{
		wxString name = wxString::Format("%s%02d.png", basename.c_str(), m_snapCount++);
		simage.SaveFile(name, wxBITMAP_TYPE_PNG);
	}
}

void PlotWindow::doRefresh(bool force)
{
	m_force = force;
	Refresh(false); Update();
}

void PlotWindow::savePlot()
{
	if ((m_xv == NULL) || m_xv->empty())
		return;	// Nothing to plot

	update();

	wxSize sz = GetClientSize();
	sz.x -= XOFF;
	sz.y -= YOFF;

	double x = ((*m_xv)[0] - m_xmin) * m_xscl;
	double y = ((*m_yv)[0] - m_ymin) * m_yscl;
	m_last_xi = (int)round(x) + XOFF;
	m_last_yi = sz.y - (int)round(y);

	std::vector<int> index;

	int npts = (int)m_xv->size();
	for (int i = 1; i < npts; i++)
	{
		double x = ((*m_xv)[i] - m_xmin) * m_xscl;
		double y = ((*m_yv)[i] - m_ymin) * m_yscl;
		int xi = (int)round(x) + XOFF;
		int yi = sz.y - (int)round(y);
		if ((xi != m_last_xi) || (yi != m_last_yi))
		{
			m_last_xi = xi; m_last_yi = yi;
			index.push_back(i);
		}
	}

	if (!index.empty())
	{
		int i = 0;
		gPoint gp;
		gp.x = (*m_xv)[i];
		gp.y = (*m_yv)[i];
		GPoints gpts;
		gpts.push_back(gp);
		std::vector<int>::iterator it = index.begin();
		while (it != index.end())
		{
			i = *it++;
			gp.x = (*m_xv)[i];
			gp.y = (*m_yv)[i];
			gpts.push_back(gp);
		}
		m_lines.push_back(gpts);
	}
}

void PlotWindow::drawAxes(wxDC& dc, wxSize& sz)
{
	if (m_yadist != 0.0)
	{
		double yval = m_yamin;
		while (yval <= m_yamax)
		{
			double y = (yval - m_ymin) * m_yscl;
			int yi = sz.y - (int)round(y);

			wxString ystr = wxString::Format("%g", yval);
			wxSize textSz = dc.GetTextExtent(ystr);
			int xoff = XOFF - textSz.x - 5;
			if (xoff < 0)
				xoff = 0;
			dc.DrawText(ystr, wxPoint(xoff, yi - (textSz.y / 2)));
			dc.DrawLine(wxPoint(XOFF, yi), wxPoint(XOFF + 10, yi));
			yval += m_yadist;
		}
	}
	if (m_xadist != 0.0)
	{
		double xval = m_xamin;
		while (xval <= m_xamax)
		{
			double x = (xval - m_xmin) * m_xscl;
			int xi = (int)round(x);

			wxString xstr = wxString::Format("%g", xval);
			wxSize textSz = dc.GetTextExtent(xstr);
			dc.DrawText(xstr, wxPoint(xi - (textSz.x / 2) + XOFF, sz.y + 5));
			dc.DrawLine(wxPoint(xi + XOFF, sz.y), wxPoint(xi + XOFF, sz.y - 10));
			xval += m_xadist;
		}
	}
	dc.DrawLine(wxPoint(XOFF, 0), wxPoint(XOFF, sz.y));
	dc.DrawLine(wxPoint(XOFF, sz.y), wxPoint(sz.x + XOFF, sz.y));

	wxFont font = dc.GetFont();
	int pointSize = font.GetPointSize();
	font.SetPointSize(11);
	dc.SetFont(font);

	wxString label = wxString::Format("Cubes Removed / Total Cubes");
	wxSize textSz = dc.GetTextExtent(label);
	dc.DrawText(label, wxPoint((sz.x - textSz.x) / 2 + XOFF, sz.y + textSz.y));
	label = wxString::Format("Surface Area / Initial SA");
	textSz = dc.GetTextExtent(label);
	dc.DrawRotatedText(label, wxPoint(0, (sz.y + textSz.x) / 2), 90.0);

	font.SetPointSize(pointSize);
	dc.SetFont(font);
}

void PlotWindow::setData(std::vector<double>* xv, std::vector<double>* yv)
{
	m_last_xi = -1;
	m_last_yi = 0;
	m_last_index = 0;

	m_xv = xv;
	m_yv = yv;
}

void PlotWindow::plotSaved(wxDC& dc, wxSize& sz)
{
	wxPen pen = dc.GetPen();
	wxColour fg = dc.GetTextForeground();

	dc.SetPen(*wxRED_PEN);
	dc.SetTextForeground(*wxRED);

	int lineCt = 0;
	GLines::iterator it = m_lines.begin();
	while (it != m_lines.end())
	{
		GPoints& gpts = *it++;

		double x = (gpts[0].x - m_xmin) * m_xscl;
		double y = (gpts[0].y - m_ymin) * m_yscl;
		m_last_xi = (int)round(x) + XOFF;
		m_last_yi = sz.y - (int)round(y);
		wxString label = wxString::Format("%d", lineCt++);
		dc.DrawText(label, m_last_xi, m_last_yi);

		int npts = (int)gpts.size();
		for (int i = 1; i < npts; i++)
		{
			double x = (gpts[i].x - m_xmin) * m_xscl;
			double y = (gpts[i].y - m_ymin) * m_yscl;
			int xi = (int)round(x) + XOFF;
			int yi = sz.y - (int)round(y);
			dc.DrawLine(m_last_xi, m_last_yi, xi, yi);
			m_last_xi = xi; m_last_yi = yi;
		}
	}

	dc.SetPen(pen);
	dc.SetTextForeground(fg);
}

bool PlotWindow::checkMinMax(double x, double y)
{
	bool needRefresh = false;

	if (x < m_xmin)
	{
		m_xmin = x;
		needRefresh = true;
	}
	if (x > m_xmax)
	{
		m_xmax = x;
		needRefresh = true;
	}
	if (y < m_ymin)
	{
		m_ymin = y;
		needRefresh = true;
	}
	if (y > m_ymax)
	{
		m_ymax = y;
		needRefresh = true;
	}

	return(needRefresh);
}

void PlotWindow::clear()
{
	m_xv->clear();
	m_yv->clear();
	m_lines.clear();
	Refresh();
}

bool PlotWindow::minmaxSaved()
{
	bool needRefresh = false;
	GLines::iterator it = m_lines.begin();
	while (it != m_lines.end())
	{
		GPoints& gpts = *it++;

		int npts = (int)gpts.size();
		for (int i = 0; i < npts; i++)
		{
			double x = gpts[i].x;
			double y = gpts[i].y;
			needRefresh |= checkMinMax(x, y);
		}
	}
	return(needRefresh);
}

bool PlotWindow::getScaling()
{
	bool needRefresh = false;
	scalf1(m_xmin, m_xmax, 4, m_xamin, m_xamax, m_xadist);
	scalf1(m_ymin, m_ymax, 4, m_yamin, m_yamax, m_yadist);

	m_xrng = m_xamax - m_xamin;
	m_yrng = m_yamax - m_yamin;

	wxSize sz = GetClientSize();
	sz.x -= XOFF + 5;
	sz.y -= YOFF + 5;
	double new_xscl = (double)sz.x / m_xrng;
	double new_yscl = (double)sz.y / m_yrng;
	if ((new_xscl != m_xscl) || (new_yscl != m_yscl))
	{
		m_xscl = new_xscl;
		m_yscl = new_yscl;
		needRefresh = true;
	}

	return(needRefresh);
}

bool PlotWindow::update()
{
	if (!m_xv || (m_last_index == m_xv->size()))
		return(false);

	bool needRefresh = false;

	int index = m_xv->size();
	for (int i = m_last_index; i < index; i++)
	{
		double x = (*m_xv)[i];
		double y = (*m_yv)[i];
		needRefresh |= checkMinMax(x, y);
	}

	if (needRefresh)
		needRefresh = getScaling();

	return(needRefresh);
}

void PlotWindow::scalf1(double xmin, double xmax, int n, double& xminp, double& xmaxp, double& dist)
{
	double vint[4] = { 1.0, 2.0, 5.0, 10.0 };
	double sqr[3] = { 1.414214, 3.162278, 7.071068 };

	double del = .00002;
	double fn = n;

	double a = (xmax - xmin) / fn;
	double al = log10(a);
	int nal = al;
	if (a < 1.0)
		nal = nal - 1;

	double b = a / pow(10, nal);
	int i;
	for (i = 0; i < 3; i++)
	{
		if (b < sqr[i])
			break;
	}
	dist = vint[i] * pow(10, nal);
	double fm1 = xmin / dist;
	int m1 = fm1;
	if (fm1 < 0)
		m1 = m1 - 1;
	if (abs((double)m1 + 1 - fm1) < del)
		m1 = m1 + 1;
	xminp = dist * (double)m1;
	double fm2 = xmax / dist;
	int m2 = fm2 + .5;// 1.0;
	if (fm2 < -1.0)
		m2 = m2 - 1;
	xmaxp = dist * (double)m2;
	if (xminp > xmin)
		xminp = xmin;
	if (xmaxp < xmax)
		xmaxp = xmax;
}
