#pragma once

#include <wx/panel.h>
#include <wx/textctrl.h>
#include <wx/checkbox.h>
#include <wx/button.h>
#include <wx/radiobut.h>
#include <wx/sizer.h>
#include <wx/splitter.h>
#include <wx/notebook.h>
#include <wx/slider.h>
#include <wx/dc.h>

#include "MultiCube.h"
#include "ProcThread.h"

// State event
wxDECLARE_EVENT(wxEVT_THREAD_STATE_EVENT, wxCommandEvent);
// Done event
wxDECLARE_EVENT(wxEVT_THREAD_DONE_EVENT, wxCommandEvent);

class ProcThread;
class GLDisplayCanvas;
class PlotWindow;

#ifdef WANT_FRAGMENTATION
class HistWindow;
#endif// #ifdef WANT_FRAGMENTATION

class ControlsPanel : public wxPanel
{
public:
	ControlsPanel(wxWindow* win);
	~ControlsPanel();

	void run(bool is_prerun);
	void generatePores();

	bool saveConfig();
	void refreshDisplay(bool force=false);
	float getScale();
	float mParams[4];	// display parameters (e.g. rotation and position in 3D view)
	CubeParams& getParams() { return(m_params); }
	void shutdown();
	void setCurrentParams(bool reset=false);

private:
	void OnButton(wxCommandEvent& event);
	void OnTextEnter(wxCommandEvent& event);
	void OnText(wxCommandEvent& event);
	void OnRadioButton(wxCommandEvent& event);
	void OnCheckBox(wxCommandEvent& event);
	void OnThreadState(wxCommandEvent& event);
	void OnThreadDone(wxCommandEvent& event);
	void OnPageChanged(wxBookCtrlEvent& event);
#ifdef WANT_FRAGMENTATION
	void OnSlider(wxCommandEvent& event);
#endif// #ifdef WANT_FRAGMENTATION
	void displaySectionEnable(bool state);
	void porositySectionEnable(bool state);

	bool prerun(bool is_prerun);
	void destroyGrid();
	void loadDefaults();
	bool loadConfig();
	void getCurrentParams();
	void toggleThreadState();
	void updateSampling();
	void rescale(bool isCuboid);
//	void rescaleSA(bool isCuboid);

	wxBoxSizer* buildParamsSection(wxPanel* panel);

	PlotWindow*	m_plot;
#ifdef WANT_FRAGMENTATION
	HistWindow*	m_histWin;
#endif// #ifdef WANT_FRAGMENTATION

	wxSplitterWindow*	m_splitter;
	wxPanel*			lowerPanel;
	wxBoxSizer*			m_paramSizer;

	// GUI controls for samurai parameters
	wxRadioButton*	m_cuboid;
	wxRadioButton*	m_ellipsoid;
	wxTextCtrl*		m_xdim;
	wxTextCtrl*		m_ydim;
	wxTextCtrl*		m_zdim;
	wxTextCtrl*		m_porosity;
	wxTextCtrl*		m_poreSize;
	wxTextCtrl*		m_outputInc;
	wxTextCtrl*		m_outputEnd;
	wxTextCtrl*		m_nRuns;
	wxTextCtrl*		m_outputSubsamp;
	wxTextCtrl*		m_outputNSamps;
	wxTextCtrl*		m_outputDir;
	wxCheckBox*		m_outputSave;
	wxCheckBox*		m_outputSaveGrid;
#ifdef WANT_INPUT_CONTROL
	wxTextCtrl*		m_inputFile;
	wxButton*		m_chooseFileButton;
#endif //#ifdef WANT_INPUT_CONTROL
	wxCheckBox*		m_poreIsFixed;
	wxRadioButton*	m_poreCuboid;
	wxRadioButton*	m_poreSpheroid;
	wxCheckBox*		m_withReplacement;
#ifdef RANDOM_REMOVAL
	wxCheckBox*		m_naiveRemoval;
#endif //#ifdef RANDOM_REMOVAL
	wxCheckBox*		m_displayEnable;
	wxCheckBox*		m_showOutlines;
	wxCheckBox*		m_showAxes;
	wxCheckBox*		m_pauseOnInc;
	wxRadioButton*	m_displayFaces;
	wxRadioButton*	m_displayFrags;
	wxCheckBox*		m_saveGif;
	wxCheckBox*		m_saveFrames;
	wxTextCtrl*		m_fps;
	wxTextCtrl*		m_rotationAngle;
	wxButton*		m_clearButton;
	wxButton*		m_snapshotButton;
#ifdef WANT_FRAGMENTATION
	wxCheckBox*		m_outputSaveFrags;
	wxCheckBox*		m_enableFrag;
	wxCheckBox*		m_discardFrags;
	wxCheckBox*		m_animateFrags;
	wxCheckBox*		m_histFrags;
	wxCheckBox*		m_enableFragClass;
	wxSlider*		m_fragClass;
	wxSizer*		m_fragBoxSizer;
	wxRadioButton*	m_faces;
	wxRadioButton*	m_edges;
	wxRadioButton*	m_verts;
#endif// #ifdef WANT_FRAGMENTATION
	wxTextCtrl*		m_messages;
	wxButton*		m_stopButton;
	wxButton*		m_runButton;
	wxButton*		m_chooseDirButton;
	wxNotebook*		m_notebook;
	ProcThread*		m_thread;
	bool			m_done;
	bool			m_isRunning;
	bool			m_need_prerun;
	bool			m_terminating;
	bool			m_paused;
	bool			m_consuming;	// Flag; indicates that the cube consuming process is running
	bool			m_param_changed;
	int				m_run_count;

	GLDisplayCanvas* mGLCanvas;

	CubeParams		m_params;

	MultiCube*		m_grid;

	double			m_ellipse_scalar;
};
