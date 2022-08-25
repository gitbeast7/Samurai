#pragma once
#pragma warning( disable : 4996 )		// _CRT_SECURE_NO_WARNINGS

#include <wx/glcanvas.h>
#include <wx/dcclient.h>
#include <wx/dcmemory.h>
#include <wx/anidecod.h> // wxImageArray

#include "MultiCube.h"

// the rendering context used by all GL canvases
class GLDisplayContext3D : public wxGLContext
{
public:
    GLDisplayContext3D(wxGLCanvas *canvas);

		// render the cube showing it at given angles and position
	bool DrawCubes(MultiCube* grid, CubeParams& params, CubePtrs& cubeList);
	void DrawAxes(float xp, float yp, float zp);
	void DrawBoundingBox(float xp, float yp, float zp);

	void SetView(float zdim, float* params);

private:
    void DrawCube(Info_t cubeInfo, float xp, float yp, float zp);

		// textures for the cube faces
    GLuint	m_textures[12];
	float	m_lastZdim;
	int		m_lastIndex;
};

class GLDisplayCanvas : public wxGLCanvas
{
public:
	GLDisplayCanvas(wxWindow *parent);
	~GLDisplayCanvas();

	void SetGrid(MultiCube* grid);
	bool createGif(std::string& gifPath, float frameRate);
	void saveFrames(std::string& imagePath, std::string& baseName, int count=-1, double scale=1.0);
	void clearImages();
	void getFrame();
	void snap(std::string& basename);

private:
	void OnSize(wxSizeEvent& event);
	void OnSelected(wxMouseEvent& event);
	void OnMouseWheel(wxMouseEvent& event);
	void OnStartDragging(wxMouseEvent& event);
	void OnPaint(wxPaintEvent& WXUNUSED(event));

	void collectFrames(wxBitmap& bitmap);

    wxPoint		lastpos;
	MultiCube*	m_grid;
	CubePtrs	m_cubeList;

	wxImageArray m_images;
	int			m_snapCount;
};
