#include <limits.h>
#include <math.h>

#include <wx/imaggif.h>
#include <wx/wfstream.h>
#include <wx/quantize.h>

#include "GLDisplay.h"
#include "ControlsPanel.h"

extern ControlsPanel*	GlobalControlsPanel;

GLDisplayContext3D	*GlobalGL3dContext = NULL;

	// Create a single device 3D context for the main frame
	// All subsequent requests will use the single context
GLDisplayContext3D* Get3DContext(wxGLCanvas *canvas)
{
    if ( !GlobalGL3dContext )
    {
        // Create the OpenGL context for the first window which needs it:
        // subsequently created windows will all share the same context.
        GlobalGL3dContext = new GLDisplayContext3D(canvas);
    }

    GlobalGL3dContext->SetCurrent(*canvas);

    return GlobalGL3dContext;
}

static void CheckGLError()
{
    GLenum errLast = GL_NO_ERROR;

    for ( ;; )
    {
        GLenum err = glGetError();
        if ( err == GL_NO_ERROR )
            return;

        // normally the error is reset by the call to glGetError() but if
        // glGetError() itself returns an error, we risk looping forever here
        // so check that we get a different error than the last time
        if ( err == errLast )
        {
            wxLogError(wxT("OpenGL error state couldn't be reset."));
            return;
        }

        errLast = err;

        wxLogError(wxT("OpenGL error %d"), err);
    }
}

unsigned char colormap[6][3]= {
	{0x00, 0x00, 0xFF},
	{0x00, 0xCC, 0xCC},
	{0x00, 0xFF, 0x00},
	{0xFF, 0xFF, 0x00},
	{0xFF, 0x80, 0x00},
	{0xFF, 0x00, 0x00}
};

unsigned char colormap2[6][3] = {
	{0xa6, 0x36, 0x03},
	{0xe6, 0x55, 0x0d},
	{0xfd, 0x8d, 0x3c},
	{0xfd, 0xae, 0x6b},
	{0xfd, 0xd0, 0xa2},
	{0xfe, 0xed, 0xde}
};

unsigned char colormap3[6][3] = {
	{0x6d, 0x41, 0x1e},
	{0x89, 0x5b, 0x32},
	{0xa6, 0x76, 0x47},
	{0xc4, 0x93, 0x5d},
	{0xe1, 0xb0, 0x74},
	{0xff, 0xcf, 0x8c}
};

unsigned char colormap4[6][3] = {
	{0x00, 0x5e, 0x89},
	{0x5d, 0x65, 0xab},
	{0xb0, 0x60, 0xad},
	{0xf2, 0x5b, 0x8e},
	{0xff, 0x74, 0x57},
	{0xff, 0xa6, 0x00}
};

// function to draw the texture for cube faces
static wxImage DrawCubeFace(int size, unsigned num)
{
//    wxASSERT_MSG( num >= 0 && num <= 5, wxT("invalid face index") );

    wxBitmap bmp(size, size);
    wxMemoryDC dc;
    dc.SelectObject(bmp);

	bool noLines = (num > 5) ? true : false;
	if (noLines)
		num -= 6;

	unsigned char* color = colormap[num];
	wxBrush brush(wxColour(color[0], color[1], color[2]));
	dc.SetBackground(brush);
	dc.Clear();

	if (!noLines)
	{
		dc.SetPen(*wxWHITE);
		dc.DrawLine(0, 0, 0, size-1);
		dc.DrawLine(0, size-1, size, size-1);
		dc.DrawLine(size-1, size-1, size-1, 0);
		dc.DrawLine(size-1, 0, 0, 0);
	}

    dc.SelectObject(wxNullBitmap);

    return bmp.ConvertToImage();
}

GLDisplayContext3D::GLDisplayContext3D(wxGLCanvas *canvas) : wxGLContext(canvas)
{
    SetCurrent(*canvas);

	    // set up the parameters we want to use
    glEnable(GL_DEPTH_TEST);
    //glEnable(GL_LIGHTING);
    //glEnable(GL_LIGHT0);
    glEnable(GL_TEXTURE_2D);
	//glEnable(GL_CULL_FACE);

		// add slightly more light, the default lighting is rather dark
    //GLfloat ambient[] = { .80f, .80f, .80f, .80f };
    //glLightfv(GL_LIGHT0, GL_AMBIENT, ambient);

		// set viewing projection
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustum(-1.0f, 1.0f, -1.0f, 1.0f, 8.0f, 10000.0f);
	glRotatef(90.0f, 1.0f, 0.0f, 0.0f);

    // create the textures to use for cube sides: they will be reused by all
    // canvases (which is probably not critical in the case of simple textures
    // we use here but could be really important for a real application where
    // each texture could take many megabytes)
    glGenTextures(WXSIZEOF(m_textures), m_textures);

    for ( unsigned i = 0; i < WXSIZEOF(m_textures); i++ )
    {
        glBindTexture(GL_TEXTURE_2D, m_textures[i]);

        glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);

        const wxImage img(DrawCubeFace(16, i));

        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, img.GetWidth(), img.GetHeight(),
                     0, GL_RGB, GL_UNSIGNED_BYTE, img.GetData());
    }

	m_lastIndex = -1;
	m_lastZdim = 0;

    CheckGLError();
}

void GLDisplayContext3D::SetView(float zdim, float* params)
{
	if (m_lastZdim != zdim)
	{
		if (m_lastZdim != 0)
		{
			float scale = zdim / m_lastZdim;
			params[3] *= scale;
		}
		m_lastZdim = zdim;
	}

	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
	float zoom = (zdim+params[3]);
	if (zoom < 15)
	{	zoom = 15;
		params[3] = zoom-zdim;
	}
	glTranslatef(0.0f, -zoom, 0.0f);
	
	glRotatef(30.0f+params[0], 1.0f, 0.0f, 0.0f);
	glRotatef(0.0f+params[1], 0.0f, 1.0f, 0.0f);
	glRotatef(-45.0f+params[2], 0.0f, 0.0f, 1.0f);
}

bool GLDisplayContext3D::DrawCubes(MultiCube* grid, CubeParams& params, CubePtrs& cubeList)
{
	float xoff = params.xdim / 2.0 - 0.5;
	float yoff = params.ydim / 2.0 - 0.5;
	float zoff = params.zdim / 2.0 - 0.5;
	bool noLines = !params.showOutlines;

	CubePtrs::iterator lit = cubeList.begin();
	while (lit != cubeList.end())
	{
		Cube* cube = *lit++;
		int x, y, z;
		int index = grid->getColorIndexAndPosition(cube, x, y, z);
		if (index < 0)	// if < 0 we ignore this cube
			continue;

		if (noLines)
			index += 6;
		if (index != m_lastIndex)
		{
			glBindTexture(GL_TEXTURE_2D, m_textures[index]);
			m_lastIndex = index;
		}

		DrawCube(cube->info, x-xoff, y-yoff, z-zoff);
		if ((z - zoff) > zoff)
			z = zoff;
	}

	glBindTexture(GL_TEXTURE_2D, 0);
	m_lastIndex = -1;

	return(true);
}

void GLDisplayContext3D::DrawAxes(float xp, float yp, float zp)
{
	xp *= 1.5; xp /= 2;
	yp *= 1.5; yp /= 2;
	zp *= 1.5; zp /= 2;

	glColor3f(1.0f, 1.0f, 1.0f);
	// x-axis
	glBegin(GL_LINES);
	glVertex3f(-xp, 0, 0);
	glVertex3f(xp, 0, 0);
	glEnd();
	// y-axis
	glBegin(GL_LINES);
	glVertex3f(0, -yp, 0);
	glVertex3f(0, yp, 0);
	glEnd();
	// z-axis
	glBegin(GL_LINES);
	glVertex3f(0, 0, -zp);
	glVertex3f(0, 0, zp);
	glEnd();
}

void GLDisplayContext3D::DrawBoundingBox(float xp, float yp, float zp)
{
	xp /= 2.0; yp /= 2; zp /= 2;

	glColor3f(1.0f, 1.0f, 1.0f);

	glBegin(GL_LINES);
	glVertex3f(-xp, -yp, -zp);
	glVertex3f(xp, -yp, -zp);

	glVertex3f(xp, -yp, -zp);
	glVertex3f(xp, yp, -zp);

	glVertex3f(xp, yp, -zp);
	glVertex3f(-xp, yp, -zp);

	glVertex3f(-xp, yp, -zp);
	glVertex3f(-xp, -yp, -zp);

	glVertex3f(-xp, -yp, zp);
	glVertex3f(xp, -yp, zp);

	glVertex3f(xp, -yp, zp);
	glVertex3f(xp, yp, zp);

	glVertex3f(xp, yp, zp);
	glVertex3f(-xp, yp, zp);

	glVertex3f(-xp, yp, zp);
	glVertex3f(-xp, -yp, zp);
	glEnd();
}

void GLDisplayContext3D::DrawCube(Info_t info, float xp, float yp, float zp)
{
	float xpp = xp + 0.5f;
	float xpm = xp - 0.5f;
	float ypp = yp + 0.5f;
	float ypm = yp - 0.5f;
	float zpp = zp + 0.5f;
	float zpm = zp - 0.5f;

	// draw six faces of a cube of size 1 centered at (xp, yp, zp)
/* BOTTOM */
	if (isExposed(info, 5))
	{
		glBegin(GL_QUADS);
		glNormal3f(0.0f, 0.0f, 1.0f);
		glTexCoord2f(0, 0); glVertex3f(xpp, ypp, zpp);
		glTexCoord2f(1, 0); glVertex3f(xpm, ypp, zpp);
		glTexCoord2f(1, 1); glVertex3f(xpm, ypm, zpp);
		glTexCoord2f(0, 1); glVertex3f(xpp, ypm, zpp);
		glEnd();
	}
/* FRONT */
	if (isExposed(info, 4))
	{
		glBegin(GL_QUADS);
		glNormal3f(1.0f, 0.0f, 0.0f);
		glTexCoord2f(0, 0); glVertex3f(xpp, ypp, zpp);
		glTexCoord2f(1, 0); glVertex3f(xpp, ypm, zpp);
		glTexCoord2f(1, 1); glVertex3f(xpp, ypm, zpm);
		glTexCoord2f(0, 1); glVertex3f(xpp, ypp, zpm);
		glEnd();
	}
/* RIGHT */
	if (isExposed(info, 3))
	{
		glBegin(GL_QUADS);
		glNormal3f(0.0f, 1.0f, 0.0f);
		glTexCoord2f(0, 0); glVertex3f(xpp, ypp, zpp);
		glTexCoord2f(1, 0); glVertex3f(xpp, ypp, zpm);
		glTexCoord2f(1, 1); glVertex3f(xpm, ypp, zpm);
		glTexCoord2f(0, 1); glVertex3f(xpm, ypp, zpp);
		glEnd();
	}
/* LEFT */
	if (isExposed(info, 2))
	{
		glBegin(GL_QUADS);
		glNormal3f(0.0f, -1.0f, 0.0f);
		glTexCoord2f(0, 0); glVertex3f(xpm, ypm, zpm);
		glTexCoord2f(1, 0); glVertex3f(xpp, ypm, zpm);
		glTexCoord2f(1, 1); glVertex3f(xpp, ypm, zpp);
		glTexCoord2f(0, 1); glVertex3f(xpm, ypm, zpp);
		glEnd();
	}
/* BACK */
	if (isExposed(info, 1))
	{
		glBegin(GL_QUADS);
		glNormal3f(-1.0f, 0.0f, 0.0f);
		glTexCoord2f(0, 0); glVertex3f(xpm, ypm, zpm);
		glTexCoord2f(1, 0); glVertex3f(xpm, ypm, zpp);
		glTexCoord2f(1, 1); glVertex3f(xpm, ypp, zpp);
		glTexCoord2f(0, 1); glVertex3f(xpm, ypp, zpm);
		glEnd();
	}
/* TOP */
	if (isExposed(info, 0))
	{
		glBegin(GL_QUADS);
		glNormal3f(0.0f, 0.0f, -1.0f);
		glTexCoord2f(0, 0); glVertex3f(xpm, ypm, zpm);
		glTexCoord2f(1, 0); glVertex3f(xpm, ypp, zpm);
		glTexCoord2f(1, 1); glVertex3f(xpp, ypp, zpm);
		glTexCoord2f(0, 1); glVertex3f(xpp, ypm, zpm);
		glEnd();
	}
}

GLDisplayCanvas::GLDisplayCanvas(wxWindow *parent)
    // With perspective OpenGL graphics, the wxFULL_REPAINT_ON_RESIZE style
    // flag should always be set, because even making the canvas smaller should
    // be followed by a paint event that updates the entire canvas with new
    // viewport settings.
    : wxGLCanvas(parent, wxID_ANY, NULL /* attribs */, wxDefaultPosition, wxDefaultSize, wxFULL_REPAINT_ON_RESIZE | wxBORDER_SUNKEN)
{
	GlobalGL3dContext = Get3DContext(this);

		// Used to distinguish first use
	lastpos.x = INT_MAX;	
	lastpos.y = INT_MAX;	
	
	m_grid = NULL;

	m_snapCount = 0;

		// Set the OpenGL viewport according to the client size of this canvas.
		// This is done here rather than in a wxSizeEvent handler because our
		// OpenGL rendering context (and thus viewport setting) is used with
		// multiple canvases: If we updated the viewport in the wxSizeEvent
		// handler, changing the size of one canvas causes a viewport setting that
		// is wrong when next another canvas is repainted.
	const wxSize ClientSize = GetClientSize();
	int size = ClientSize.x < ClientSize.y ? ClientSize.y : ClientSize.x;
	int diff = ClientSize.y - ClientSize.x;
	int x = 0;
	int y = 0;
	if (diff < 0)
		y = diff / 2;
	else if (diff > 0)
		x = -diff / 2;
	glViewport(x, y, size, size);

	Bind(wxEVT_PAINT,		&GLDisplayCanvas::OnPaint, this);
	Bind(wxEVT_MOTION,		&GLDisplayCanvas::OnStartDragging, this);
	Bind(wxEVT_LEFT_DOWN,	&GLDisplayCanvas::OnSelected, this);
	Bind(wxEVT_MOUSEWHEEL,	&GLDisplayCanvas::OnMouseWheel, this);
	Bind(wxEVT_SIZE,		&GLDisplayCanvas::OnSize, this);
}

GLDisplayCanvas::~GLDisplayCanvas()
{
	if (GlobalGL3dContext)
	{
		delete GlobalGL3dContext;
		GlobalGL3dContext = NULL;
	}
}

void GLDisplayCanvas::OnSize(wxSizeEvent& event)
{
	const wxSize ClientSize = GetClientSize();
	int size = ClientSize.x < ClientSize.y ? ClientSize.y : ClientSize.x;
	int diff = ClientSize.y - ClientSize.x;
	int x = 0;
	int y = 0;
	if (diff < 0)
		y = diff / 2;
	else if (diff > 0)
		x = -diff / 2;

	glViewport(x, y, size, size);

	event.Skip();
}

void GLDisplayCanvas::OnSelected(wxMouseEvent& event)
{
	CubeParams& params = GlobalControlsPanel->getParams();

	if (!(m_grid && params.displayEnable))
		return;

    wxClientDC dc(this);
    PrepareDC(dc);

    lastpos = event.GetLogicalPosition(dc);

	event.Skip();
}

void GLDisplayCanvas::OnMouseWheel(wxMouseEvent& event)
{
	ControlsPanel* cp = GlobalControlsPanel;
	CubeParams& params = cp->getParams();

	if (! (m_grid && params.displayEnable))
		return;

	int diffz = round(event.GetWheelRotation()/event.GetWheelDelta());
	float adjust = diffz*10;

	cp->mParams[3] += adjust;

	Refresh(false);
}

void GLDisplayCanvas::OnStartDragging(wxMouseEvent& event)
{
	ControlsPanel* cp = GlobalControlsPanel;
	CubeParams& params = cp->getParams();

	if (!(m_grid && params.displayEnable))
		return;

	if ((event.Dragging() == FALSE) || (event.LeftIsDown()==FALSE))
	{	if (HasCapture())
			ReleaseMouse();
		Refresh(false);
		event.Skip();
		return;
	}

    wxClientDC dc(this);
    PrepareDC(dc);

	if (!HasCapture())
		CaptureMouse();
    wxPoint pos = event.GetLogicalPosition(dc);
	if (lastpos.x == INT_MAX)
		lastpos.x = pos.x;
	if (lastpos.y == INT_MAX)
		lastpos.y = pos.y;
		// For Type_EULER_ANG
	int diffx = (pos.x - lastpos.x);
	int diffy = (pos.y - lastpos.y);
	if (abs(diffx) > abs(diffy))
	{	
		int newVal = ((int)cp->mParams[2] - diffx) % 360;
		cp->mParams[2] = newVal;
	}
	else
	{	
		if (event.ShiftDown())
		{
			int newVal = ((int)cp->mParams[1] - diffy) % 360;
			cp->mParams[1] = newVal;
		}
		else
		{
			int newVal = ((int)cp->mParams[0] + diffy) % 360;
			cp->mParams[0] = newVal;
		}
	}
    lastpos = pos;
	Refresh(false);
}

void GLDisplayCanvas::SetGrid(MultiCube* grid)
{
	m_grid = grid;
}

void GLDisplayCanvas::OnPaint(wxPaintEvent& WXUNUSED(event))
{
	   // This is required even though dc is not used otherwise.
    wxPaintDC dc(this);
    PrepareDC(dc);

	ControlsPanel* cp = GlobalControlsPanel;
	CubeParams& params = cp->getParams();

	if (m_grid && params.displayEnable)
	{
		float zscale = cp->getScale() * 8;
		GlobalGL3dContext->SetView(zscale, cp->mParams);

		if (m_grid->loadCubeListFromExposedList(m_cubeList))
			GlobalGL3dContext->DrawCubes(m_grid, params, m_cubeList);

		if (params.showAxes)
		{
			GlobalGL3dContext->DrawAxes(params.xdim, params.ydim, params.zdim);
			//GlobalGL3dContext->DrawBoundingBox(params.xdim, params.ydim, params.zdim);
		}
	}
	else
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	SwapBuffers();
}

void GLDisplayCanvas::getFrame()
{
	wxClientDC dc(this);

	const wxSize ClientSize = GetClientSize();

	wxBitmap bitmap(ClientSize);
	wxMemoryDC mdc;
	mdc.SelectObject(bitmap);
	mdc.Blit(wxPoint(0, 0), ClientSize, &dc, wxPoint(0, 0));
	mdc.SelectObject(wxNullBitmap);
	wxImage simage = bitmap.ConvertToImage();
	if (simage.IsOk())
		m_images.push_back(simage);
}

void GLDisplayCanvas::snap(std::string& basename)
{
	wxClientDC dc(this);

	const wxSize ClientSize = GetClientSize();

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

void GLDisplayCanvas::collectFrames(wxBitmap& bitmap)
{
	if (bitmap.IsOk())
	{
		wxImage simage = bitmap.ConvertToImage();
		wxImage dimage;
		wxQuantize::Quantize(simage, dimage);
		if (dimage.IsOk())
		{
			m_images.push_back(dimage);
		}
	}
}

void GLDisplayCanvas::saveFrames(std::string& imagePath, std::string& baseName, int count/*=-1*/, double scale/*=1.0*/)
{
	if (!m_images.empty())
	{
		int nImages = m_images.GetCount();			// Default: Save all image frames
		if ((nImages > count)&&(count != -1))
			nImages = count;
		int imgSizeX = m_images[0].GetSize().x;		// Default: Full image size
		int imgSizeY = m_images[0].GetSize().y;		// Default: Full image size

		for (int i = 0; i < nImages; i++)
		{
			wxString name = wxString::Format("%s\\%s%03d.png", imagePath.c_str(), baseName.c_str(), i);
			wxImage image = m_images[i];
			if (scale != 1.0)
				image.Rescale(scale*imgSizeX, scale*imgSizeY);
			if (image.IsOk())
				image.SaveFile(name, wxBITMAP_TYPE_PNG);
		}
	}
}

bool GLDisplayCanvas::createGif(std::string& gifPath, float frameRate)
{
	if (!m_images.empty())
	{
		int index = 0;
		int nImages = m_images.GetCount();
		while(nImages--)
		{	// Palettize all the images (in place)
			wxImage& image = m_images.Item(index++);
			wxQuantize::Quantize(image, image);
		}

		int delayms = (int)(frameRate * 1000);
		wxGIFHandler* gifHandler = new wxGIFHandler();
		wxFileOutputStream outstream(gifPath);
		gifHandler->SaveAnimation(m_images, &outstream, false, delayms);
		delete gifHandler;
		return(true);
	}
	return(false);
}

void GLDisplayCanvas::clearImages()
{
	m_images.Clear();
}