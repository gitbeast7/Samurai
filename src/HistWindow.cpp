#include <wx/dc.h>
#include <wx/dcclient.h>
#include <wx/dcmemory.h>

#include "HistWindow.h"
#include "PlotWindow.h"
#include "MultiCube.h"

HistWindow::HistWindow(wxWindow* win) : wxWindow(win, wxID_ANY)
{
	m_hist = NULL;
	m_hmax.assign(6, 0);
	m_min = 0;
	m_max = 0;
	m_force = false;
	m_snapCount = 0;

	Bind(wxEVT_PAINT, &HistWindow::OnPaint, this);
	Bind(wxEVT_SIZE, &HistWindow::OnSize, this);
}

HistWindow::~HistWindow()
{

}

void HistWindow::initialize()
{
	m_min = 0.0;// DBL_MAX;
	m_max = 1.0;// DBL_MIN;
	m_hmax.assign(6, 0);

	m_amin = 0.0;
	m_amax = 1.0;
	m_adist = 0.25;

	getScaling();
}

void HistWindow::OnSize(wxSizeEvent& event)
{
	m_force = true;
	Refresh();
	event.Skip();
}

extern unsigned char colormap[6][3];

void HistWindow::doDraw(wxDC& dc)
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

	if ((m_hist == NULL) || m_hist->empty())
		return;	// Nothing to plot

	int npts = (int)m_hist->size();
	int xwidth = (sz.x - XOFF) / 5;
	for (int i = 0; i < npts; i++)
	{
		double histVal = (*m_hist)[i];
		if (m_hmax[i] > histVal)
		{
			double count = (m_hmax[i] - m_min) * m_scl;
			int yi = (int)round(count);
			dc.SetBrush(*wxWHITE_BRUSH);
			dc.DrawRectangle(i*xwidth + XOFF, sz.y - yi, xwidth, yi);
		}
		else
			m_hmax[i] = histVal;
		double count = (histVal - m_min) * m_scl;
		int yi = (int)round(count);
		unsigned char* color = colormap[MAX_COLOR_INDEX-i];
		wxBrush brush(wxColour(color[0], color[1], color[2]));
		dc.SetBrush(brush);
		dc.DrawRectangle(i*xwidth+XOFF, sz.y-yi, xwidth, yi);
	}
}

void HistWindow::OnPaint(wxPaintEvent& WXUNUSED(event))
{
	wxPaintDC dc(this);
	PrepareDC(dc);

	doDraw(dc);
}

void HistWindow::snap(std::string& basename)
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

void HistWindow::doRefresh(bool force)
{
	m_force = force;
	Refresh(false); Update();
}

void HistWindow::savePlot()
{
}

void HistWindow::drawAxes(wxDC& dc, wxSize& sz)
{
	if (m_adist != 0.0)
	{
		double yval = m_amin;
		while (yval <= m_amax)
		{
			double y = (yval - m_min) * m_scl;
			int yi = sz.y - (int)round(y);

			wxString ystr = wxString::Format("%g", yval);
			wxSize textSz = dc.GetTextExtent(ystr);
			int xoff = XOFF - textSz.x - 5;
			if (xoff < 0)
				xoff = 0;
			dc.DrawText(ystr, wxPoint(xoff, yi - (textSz.y / 2)));
			//dc.DrawLine(wxPoint(XOFF, yi), wxPoint(XOFF + 10, yi));
			yval += m_adist;
		}
	}
	int npts = 6;
	int xwidth = (sz.x - XOFF) / 5;
	int halfxwidth = xwidth / 2;
	for (int i = 0; i < npts; i++)
	{
		wxString xstr = wxString::Format("%d", i);
		wxSize textSz = dc.GetTextExtent(xstr);
		dc.DrawText(xstr, wxPoint(xwidth*i+halfxwidth - (textSz.x / 2) + XOFF, sz.y + 5));
	}
/*
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
*/
	dc.DrawLine(wxPoint(XOFF, 0), wxPoint(XOFF, sz.y));
	dc.DrawLine(wxPoint(XOFF, sz.y), wxPoint(sz.x + XOFF, sz.y));
	wxFont font = dc.GetFont();
	int pointSize = font.GetPointSize();
	font.SetPointSize(11);
	dc.SetFont(font);

	wxString label = wxString::Format("log(Fragment Size)");
	wxSize textSz = dc.GetTextExtent(label);
	dc.DrawText(label, wxPoint((sz.x - textSz.x) / 2 + XOFF, sz.y + textSz.y));
	label = wxString::Format("log(Fragment Count)");
	textSz = dc.GetTextExtent(label);
	dc.DrawRotatedText(label, wxPoint(0, (sz.y + textSz.x) / 2), 90.0);

	font.SetPointSize(pointSize);
	dc.SetFont(font);
}

void HistWindow::setData(std::vector<double>* hist)
{
	m_last_yi = 0;

	m_hist = hist;
}


bool HistWindow::checkMinMax(int y)
{
	bool needRefresh = false;

	if (y < m_min)
	{
		m_min = y;
		needRefresh = true;
	}
	if (y > m_max)
	{
		m_max = y;
		needRefresh = true;
	}

	return(needRefresh);
}

void HistWindow::clear()
{
	if (m_hist)
	{
		m_hist->clear();
		initialize();
		Refresh();
	}
}

bool HistWindow::getScaling()
{
	bool needRefresh = false;
	scalf1(m_min, m_max+1, 4, m_amin, m_amax, m_adist);

	m_rng = m_amax - m_amin;

	wxSize sz = GetClientSize();
	sz.y -= YOFF + 5;
	double new_scl = (double)sz.y / m_rng;
	if (new_scl != m_scl)
	{
		m_scl = new_scl;
		needRefresh = true;
	}

	return(needRefresh);
}

bool HistWindow::update()
{
	if (!m_hist)
		return(false);

	bool needRefresh = false;

	int index = m_hist->size();
	for (int i = 0; i < index; i++)
	{
		double count = (*m_hist)[i];
		needRefresh |= checkMinMax(count);
	}

	if (needRefresh)
		needRefresh = getScaling();

	return(needRefresh);
}

void HistWindow::scalf1(double xmin, double xmax, int n, double& xminp, double& xmaxp, double& dist)
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
