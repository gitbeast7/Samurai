#include <wx/statbox.h>
#include <wx/stattext.h>
#include <wx/frame.h>
#include <wx/filename.h>
#include <wx/xml/xml.h>
#include <wx/valnum.h>
#include <wx/dirdlg.h>
#include <wx/filedlg.h>

#include <chrono>

#include "ControlsPanel.h"
#include "PlotWindow.h"
#include "HistWindow.h"
#include "FrameStatusBar.h"
#include "ProcThread.h"
#include "GLDisplay.h"
#include "Samurai.h"

// State event
wxDEFINE_EVENT(wxEVT_THREAD_STATE_EVENT, wxCommandEvent);
// Done event
wxDEFINE_EVENT(wxEVT_THREAD_DONE_EVENT, wxCommandEvent);
// Frame Capture Event
wxDEFINE_EVENT(wxEVT_FRAME_CAPTURE_EVENT, wxCommandEvent);

extern std::string format(const char *fmt, ...);

extern MyFrame*			GlobalFrame;
extern FrameStatusBar*	GlobalStatusBar;
extern ControlsPanel*	GlobalControlsPanel;

wxTextCtrl*			GlobalMessageControl = NULL;
GLDisplayCanvas*	GlobalGLDisplayCanvas = NULL;
extern GLDisplayContext3D	*GlobalGL3dContext;

#ifdef HAS_WXWIDGETS

void sendMessage(std::string& message)
{
	wxCommandEvent* event = new wxCommandEvent(wxEVT_STATUS_EVENT);
	StatusInfo* statusInfo = new StatusInfo(StatusInfo::MESSAGE, message);
	event->SetClientData(statusInfo);

	wxQueueEvent(GlobalFrame, event);	// Send thread pause request event to the main GUI frame
}
#endif

void updateProgress(std::string& text, int progress)
{
	wxCommandEvent* event = new wxCommandEvent(wxEVT_STATUS_EVENT);
	StatusInfo* statusInfo = new StatusInfo(StatusInfo::UPDATE, text, progress);
	event->SetClientData(statusInfo);

	wxQueueEvent(GlobalFrame, event);	// Send update event to the main GUI frame
}

void startProgress()
{
	wxCommandEvent* event = new wxCommandEvent(wxEVT_STATUS_EVENT);
	StatusInfo* statusInfo = new StatusInfo(StatusInfo::START);
	event->SetClientData(statusInfo);

	wxQueueEvent(GlobalFrame, event);	// Send start event to the main GUI frame
}

void doneProgress()
{
	wxCommandEvent* event = new wxCommandEvent(wxEVT_STATUS_EVENT);
	StatusInfo* statusInfo = new StatusInfo(StatusInfo::DONE);
	event->SetClientData(statusInfo);

	wxQueueEvent(GlobalFrame, event);	// Send done progress event to the main GUI frame
}

#define TEXT_BOX_WIDTH		(36)
#define TEXT_BOX_WIDTH_WIDE	(44)
#define TEXT_BOX_HEIGHT		(20)

class ffTextCtrl : public wxTextCtrl
{
public:
	ffTextCtrl(wxWindow *parent, wxWindowID id,
		const wxString& value = wxEmptyString,
		const wxPoint& pos = wxDefaultPosition,
		const wxSize& size = wxDefaultSize,
		long style = 0,
		const wxValidator& validator = wxDefaultValidator,
		const wxString& name = wxTextCtrlNameStr);

private:
	void OnEraseBackground(wxEraseEvent& event);


};

ffTextCtrl::ffTextCtrl(wxWindow *parent, wxWindowID id,
	const wxString& value,
	const wxPoint& pos,
	const wxSize& size,
	long style,
	const wxValidator& validator,
	const wxString& name)
 : wxTextCtrl(parent, id, value, pos, size, style, validator, name)
{
	Bind(wxEVT_ERASE_BACKGROUND, &ffTextCtrl::OnEraseBackground, this);
}

void ffTextCtrl::OnEraseBackground(wxEraseEvent& event)
{

}

ControlsPanel::ControlsPanel(wxWindow* win) : wxPanel(win, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxWANTS_CHARS | wxRESIZE_BORDER | wxCLIP_CHILDREN)
{
	GlobalControlsPanel = this;

	wxInitAllImageHandlers();

	m_grid		= NULL;	// Used for display
	m_thread	= NULL;
	m_done		= true;
	m_paused	= false;
	m_need_prerun = true;
	m_terminating = false;
	m_consuming = false;
	m_param_changed = false;
	m_run_count = 0;
	m_ellipse_scalar = std::cbrt(6.0 / M_PI);

	loadDefaults();	// If no configuration; use defaults
	
	loadConfig();	// Try to load the configuration file

	wxBoxSizer* topSizer = new wxBoxSizer(wxHORIZONTAL);

	m_splitter = new wxSplitterWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxSP_THIN_SASH | wxSP_LIVE_UPDATE);

	wxPanel* upperPanel = new wxPanel(m_splitter);
	wxBoxSizer* upperPanelSizer = new wxBoxSizer(wxHORIZONTAL);

	m_notebook = new wxNotebook(upperPanel, wxID_ANY);
	mGLCanvas = new GLDisplayCanvas(m_notebook);

	GlobalGLDisplayCanvas = mGLCanvas;

	m_notebook->AddPage(mGLCanvas, "3D View");

	m_plot = new PlotWindow(m_notebook);
	m_notebook->AddPage(m_plot, "2D View");
	if (!m_params.cubeView)
		m_notebook->SetSelection(1);

	upperPanelSizer->Add(m_notebook, wxSizerFlags(1).Expand());

	m_paramSizer = buildParamsSection(upperPanel);

	m_runButton = new wxButton(upperPanel, wxID_OK, "run");
	m_stopButton = new wxButton(upperPanel, wxID_OK, "stop");
	wxSizer* buttonSizer = new wxBoxSizer(wxHORIZONTAL);

	buttonSizer->AddStretchSpacer();
	buttonSizer->Add(m_stopButton, wxSizerFlags().Expand());
	buttonSizer->Add(m_runButton, wxSizerFlags().Expand());
	m_paramSizer->AddStretchSpacer();
	m_paramSizer->Add(buttonSizer, wxSizerFlags(0).Expand());

	upperPanelSizer->Add(m_paramSizer, wxSizerFlags(0).Expand());
	upperPanel->SetSizerAndFit(upperPanelSizer);

	lowerPanel = new wxPanel(m_splitter);
	wxBoxSizer* lowerPanelSizer = new wxBoxSizer(wxHORIZONTAL);

	m_messages = new ffTextCtrl(lowerPanel, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxTE_READONLY | wxTE_MULTILINE | wxNO_FULL_REPAINT_ON_RESIZE);
	lowerPanelSizer->Add(m_messages, wxSizerFlags(3).Expand());

#ifdef WANT_FRAGMENTATION
	m_histWin = new HistWindow(lowerPanel);
	m_histWin->SetMinSize(wxSize(260, 200));
	lowerPanelSizer->Add(m_histWin, wxSizerFlags(1).Expand());
	m_histWin->Show(m_params.histFrags && m_params.enableFrag);

	m_fragBoxSizer->Show(m_params.enableFrag);
#endif //#ifdef WANT_FRAGMENTATION

	lowerPanel->SetSizerAndFit(lowerPanelSizer);

	m_splitter->SplitHorizontally(upperPanel, lowerPanel);

	GlobalMessageControl = m_messages;

	topSizer->Add(m_splitter, wxSizerFlags(1).Expand());

	SetSizerAndFit(topSizer);

	Bind(wxEVT_BUTTON,				&ControlsPanel::OnButton, this);
	Bind(wxEVT_TEXT_ENTER,			&ControlsPanel::OnTextEnter, this);
	Bind(wxEVT_TEXT,				&ControlsPanel::OnText, this);
	Bind(wxEVT_RADIOBUTTON,			&ControlsPanel::OnRadioButton, this);
	Bind(wxEVT_COMBOBOX,			&ControlsPanel::OnComboBox, this);
	Bind(wxEVT_CHECKBOX,			&ControlsPanel::OnCheckBox,	this);
	Bind(wxEVT_THREAD_STATE_EVENT,	&ControlsPanel::OnThreadState, this);
	Bind(wxEVT_THREAD_DONE_EVENT,	&ControlsPanel::OnThreadDone, this);
	Bind(wxEVT_FRAME_CAPTURE_EVENT, &ControlsPanel::OnCaptureFrame, this);
	Bind(wxEVT_NOTEBOOK_PAGE_CHANGED, &ControlsPanel::OnPageChanged, this);
#ifdef WANT_FRAGMENTATION
	Bind(wxEVT_SLIDER, &ControlsPanel::OnSlider, this);
#endif// #ifdef WANT_FRAGMENTATION

	win->SetClientSize(m_params.xsize, m_params.ysize);
	win->SetPosition(wxPoint(m_params.xpos, m_params.ypos));
	m_splitter->SetSashPosition(m_params.sashpos);

	if (m_plot)
		m_plot->initialize();
#ifdef WANT_FRAGMENTATION
	m_histWin->initialize();
#endif// #ifdef WANT_FRAGMENTATION

	Show();
	m_thread = new ProcThread(this, true);	// Display the initial object and do any preprocessing
}

ControlsPanel::~ControlsPanel()
{
	// Prevent messages on the main GUI thread
	GlobalStatusBar = NULL;		
	GlobalMessageControl = NULL;

	saveConfig();	// Save system configuration parameters

	if (m_thread)
	{
		// Destroy the consumer thread
		m_thread->Wait();
		delete m_thread;
		m_thread = NULL;
	}

	if (m_grid != NULL)
	{	
		// Free up grid object
		delete m_grid;
		m_grid = NULL;
	}
}

void ControlsPanel::shutdown()
{
	m_terminating = true;	// Notify system of termination
	
	// Force completion the consumer thread (if running)
	m_paused = false;
	m_done = true;
}

wxBoxSizer* ControlsPanel::buildParamsSection(wxPanel* panel)
{
	wxBoxSizer* paramSizer = new wxBoxSizer(wxVERTICAL);

	wxSizer *shapeSizer = new wxStaticBoxSizer(new wxStaticBox(panel, wxID_ANY, "Object Shape"), wxHORIZONTAL);
	m_cuboid = new wxRadioButton(panel, wxID_ANY, "Cuboid", wxDefaultPosition, wxDefaultSize, wxRB_GROUP);
	shapeSizer->Add(m_cuboid, wxSizerFlags(0).Border(wxALL, 3));
	m_ellipsoid = new wxRadioButton(panel, wxID_ANY, "Ellipsoid");
	shapeSizer->Add(m_ellipsoid, wxSizerFlags(0).Border(wxALL, 3));
	paramSizer->Add(shapeSizer, wxSizerFlags(0).Expand().Border(wxBOTTOM, 3));

	if (m_params.cuboid)
		m_cuboid->SetValue(true);
	else
		m_ellipsoid->SetValue(true);

	unsigned long dimVal;
	wxIntegerValidator<unsigned long> dim(&dimVal);
	dim.SetRange(1, 65535);
	unsigned long subsampVal;
	wxIntegerValidator<unsigned long> subsamp(&subsampVal);
	subsamp.SetMin(1);
	double pVal;
	wxFloatingPointValidator<double> percent(2, &pVal);
	percent.SetRange(0.0, 1.0);
	double sVal;
	wxFloatingPointValidator<double> step(4, &sVal);
	step.SetRange(0.0001, 1.0);
	double aVal;
	wxFloatingPointValidator<double> ang(1, &aVal);
	ang.SetRange(0.0, 360.0);
	unsigned long frameVal;
	wxIntegerValidator<unsigned long> frate(&frameVal);
	frate.SetRange(1, 50);

	m_xdim = new wxTextCtrl(panel, wxID_ANY, wxString::Format("%d", m_params.xdim), wxDefaultPosition, wxSize(TEXT_BOX_WIDTH, TEXT_BOX_HEIGHT), wxTE_PROCESS_ENTER | wxTE_CENTRE, dim);
	m_ydim = new wxTextCtrl(panel, wxID_ANY, wxString::Format("%d", m_params.ydim), wxDefaultPosition, wxSize(TEXT_BOX_WIDTH, TEXT_BOX_HEIGHT), wxTE_PROCESS_ENTER | wxTE_CENTRE, dim);
	m_zdim = new wxTextCtrl(panel, wxID_ANY, wxString::Format("%d", m_params.zdim), wxDefaultPosition, wxSize(TEXT_BOX_WIDTH, TEXT_BOX_HEIGHT), wxTE_PROCESS_ENTER | wxTE_CENTRE, dim);

	m_aggregateEnable = new wxCheckBox(panel, wxID_ANY, "Enable");
	m_aggregateEnable->SetToolTip("Enable Sub-Particle Aggregation");
	m_aggregateEnable->SetValue(m_params.aggregateEnable);
	m_particleSize = new wxTextCtrl(panel, wxID_ANY, wxString::Format("%d", m_params.particleSize), wxDefaultPosition, wxSize(TEXT_BOX_WIDTH, TEXT_BOX_HEIGHT), wxTE_PROCESS_ENTER | wxTE_CENTRE, dim);
	m_particleSize->SetToolTip("Sub-Particle size");
	m_replaceEnable = new wxCheckBox(panel, wxID_ANY, "Replace");
	m_replaceEnable->SetToolTip("Enable Collision Replacement");
	m_replaceEnable->SetValue(m_params.replaceEnable);

	m_porosity = new wxTextCtrl(panel, wxID_ANY, wxString::Format("%.2lf", m_params.porosity), wxDefaultPosition, wxSize(TEXT_BOX_WIDTH, TEXT_BOX_HEIGHT), wxTE_PROCESS_ENTER | wxTE_CENTRE, percent);
	m_porosity->SetToolTip("Fraction of cubes to randomly remove");
	m_poreSize = new wxTextCtrl(panel, wxID_ANY, wxString::Format("%d", m_params.poreSize), wxDefaultPosition, wxSize(TEXT_BOX_WIDTH, TEXT_BOX_HEIGHT), wxTE_PROCESS_ENTER | wxTE_CENTRE, dim);
	m_poreSize->SetToolTip("Maximum pore size");
	m_poreIsFixed = new wxCheckBox(panel, wxID_ANY, "Fixed Size");
	m_poreIsFixed->SetToolTip("Prevents pore size\nfrom randomly varying from 1 to pore size");
	m_poreIsFixed->SetValue(m_params.poreIsFixed);
	m_poreCuboid = new wxRadioButton(panel, wxID_ANY, "Cube", wxDefaultPosition, wxDefaultSize, wxRB_GROUP);
	m_poreSpheroid = new wxRadioButton(panel, wxID_ANY, "Sphere");
	if (m_params.poreIsCuboid)
		m_poreCuboid->SetValue(true);
	else
		m_poreSpheroid->SetValue(true);
	m_withReplacement = new wxCheckBox(panel, wxID_ANY, "Replace");
	m_withReplacement->SetToolTip("Any extra cubes removed during porosity phase are randomly replaced");
	m_withReplacement->SetValue(m_params.withReplacement);

#ifdef RANDOM_REMOVAL
	m_naiveRemoval = new wxCheckBox(panel, wxID_ANY, "Naive");
	m_naiveRemoval->SetToolTip("Remove cubes with regard to surface area");
	m_naiveRemoval->SetValue(m_params.naiveRemoval);
#endif //#ifdef RANDOM_REMOVAL

	m_outputInc = new wxTextCtrl(panel, wxID_ANY, wxString::Format("%.4lf", m_params.outputInc), wxDefaultPosition, wxSize(TEXT_BOX_WIDTH_WIDE, TEXT_BOX_HEIGHT), wxTE_PROCESS_ENTER | wxTE_CENTRE, step);
	m_outputInc->SetToolTip("Fraction of the consuming process completed before output is generated\ne.g. 0.05 will output data after every 5% of processing");
	m_outputEnd = new wxTextCtrl(panel, wxID_ANY, wxString::Format("%.4lf", m_params.outputEnd), wxDefaultPosition, wxSize(TEXT_BOX_WIDTH_WIDE, TEXT_BOX_HEIGHT), wxTE_PROCESS_ENTER | wxTE_CENTRE, step);
	m_outputEnd->SetToolTip("Fraction of the consuming process completed before processing terminates");
	m_nRuns = new wxTextCtrl(panel, wxID_ANY, wxString::Format("%d", m_params.nRuns), wxDefaultPosition, wxSize(TEXT_BOX_WIDTH, TEXT_BOX_HEIGHT), wxTE_PROCESS_ENTER | wxTE_CENTRE, frate);
	m_nRuns->SetToolTip("Number of simulations to run with current configuration");
	m_outputSubsamp = new wxTextCtrl(panel, wxID_ANY, wxString::Format("%d", m_params.outputSubsamp), wxDefaultPosition, wxSize(40, TEXT_BOX_HEIGHT), wxTE_PROCESS_ENTER | wxTE_CENTRE, subsamp);
	m_outputSubsamp->SetToolTip("Subsample the data. Only every subsample samples will be saved");
	m_outputNSamps = new wxTextCtrl(panel, wxID_ANY, wxString::Format("%d", m_params.outputNSamps), wxDefaultPosition, wxSize(40, TEXT_BOX_HEIGHT), wxTE_PROCESS_ENTER | wxTE_CENTRE, subsamp);
	m_outputNSamps->SetToolTip("Subsample the data. N total samples will be saved");
	m_outputDir = new wxTextCtrl(panel, wxID_ANY, wxString::Format("%s", m_params.outputDir.c_str()), wxDefaultPosition, wxSize(162, TEXT_BOX_HEIGHT), wxTE_PROCESS_ENTER);
	m_outputDir->SetInsertionPointEnd();
	m_outputDir->SetToolTip("Directory to store data files");
	m_outputSave = new wxCheckBox(panel, wxID_ANY, "SA");
	m_outputSave->SetToolTip("Save volume/surface area data");
	m_outputSave->SetValue(m_params.outputSave);
	m_outputSaveGrid = new wxCheckBox(panel, wxID_ANY, "Grid");
	m_outputSaveGrid->SetToolTip("Save 3d object data");
	m_outputSaveGrid->SetValue(m_params.outputSaveGrid);
	m_outputSaveInfo = new wxCheckBox(panel, wxID_ANY, "Info");
	m_outputSaveInfo->SetToolTip("Save Run Information");
	m_outputSaveInfo->SetValue(m_params.outputSaveInfo);

	m_chooseDirButton = new wxButton(panel, wxID_ANY, "...", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);

#ifdef WANT_FRAGMENTATION
	m_outputSaveFrags = new wxCheckBox(panel, wxID_ANY, "Frags");
	m_outputSaveFrags->SetToolTip("Save fragment data");
	m_outputSaveFrags->SetValue(m_params.outputSaveFrags);

	m_enableFrag = new wxCheckBox(panel, wxID_ANY, "Frag Detect");
	m_enableFrag->SetToolTip("Fragments generated during the\ncomsuming process are detected and output");
	m_enableFrag->SetValue(m_params.enableFrag);
	m_discardFrags = new wxCheckBox(panel, wxID_ANY, "Discard");
	m_discardFrags->SetToolTip("Fragments are removed so they don't contribute to consuming process");
	m_discardFrags->SetValue(m_params.discardFrags);
	m_animateFrags = new wxCheckBox(panel, wxID_ANY, "Animate");
	m_animateFrags->SetToolTip("Fragments are animated to simulate flow");
	m_animateFrags->SetValue(m_params.animateFrags);
	m_histFrags = new wxCheckBox(panel, wxID_ANY, "Hist");
	m_histFrags->SetToolTip("Show fragment histogram");
	m_histFrags->SetValue(m_params.histFrags);
	m_enableFragClass = new wxCheckBox(panel, wxID_ANY, "Fragment Class");
	m_enableFragClass->SetToolTip("Only show fragments of selected size class");
	m_enableFragClass->SetValue(m_params.enableFragClass);
	m_fragClass = new wxSlider(panel, wxID_ANY, m_params.fragClass, 0, 5, wxDefaultPosition, wxDefaultSize, wxSL_MIN_MAX_LABELS);
	m_fragClass->Enable(m_params.enableFragClass);
	m_faces = new wxRadioButton(panel, wxID_ANY, "Faces", wxDefaultPosition, wxDefaultSize, wxRB_GROUP);
	m_edges = new wxRadioButton(panel, wxID_ANY, "Edges");
	m_verts = new wxRadioButton(panel, wxID_ANY, "Verts");
	switch(m_params.fragmentAt) {
		case 0 : m_faces->SetValue(true); break;
		case 1 : m_edges->SetValue(true); break;
		case 2 : m_verts->SetValue(true); break;
	}
#endif //#ifdef WANT_FRAGMENTATION

#ifdef WANT_INPUT_CONTROL
	m_inputFile = new wxTextCtrl(panel, wxID_ANY, wxString::Format("%s", m_params.inputFile.c_str()), wxDefaultPosition, wxSize(162, TEXT_BOX_HEIGHT), wxTE_PROCESS_ENTER);
	m_inputFile->SetInsertionPointEnd();
	m_inputFile->SetToolTip("XYZ Input Data File");

	m_chooseFileButton = new wxButton(panel, wxID_ANY, "...", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
#endif //#ifdef WANT_INPUT_CONTROL

	m_displayEnable = new wxCheckBox(panel, wxID_ANY, "Enable");
	m_displayEnable->SetToolTip("Enable 3D visualization");
	m_displayEnable->SetValue(m_params.displayEnable);
	m_displayFaces = new wxRadioButton(panel, wxID_ANY, "Surface Area", wxDefaultPosition, wxDefaultSize, wxRB_GROUP);
	m_displayFrags = new wxRadioButton(panel, wxID_ANY, "Volume");
	if (m_params.displayFaces)
		m_displayFaces->SetValue(true);
	else
		m_displayFrags->SetValue(true);

	wxString ColorMapNames[N_COLORMAPS] = { "Jet", "Matter", "Copper", "Pink", "Spring" };
	m_colorMap = new wxComboBox(panel, wxID_ANY, ColorMapNames[m_params.colormapIndex], wxDefaultPosition, wxDefaultSize, N_COLORMAPS, ColorMapNames, wxCB_DROPDOWN | wxCB_READONLY);
	m_colorMap->SetToolTip("Colormap used for display");
	GlobalGL3dContext->GenerateTextures(m_params.colormapIndex);
	m_showOutlines = new wxCheckBox(panel, wxID_ANY, "Outlines");
	m_showOutlines->SetToolTip("Outline cube faces");
	m_showOutlines->SetValue(m_params.showOutlines);
	m_showAxes = new wxCheckBox(panel, wxID_ANY, "Axes");
	m_showAxes->SetToolTip("Display axes");
	m_showAxes->SetValue(m_params.showAxes);
	m_pauseOnInc = new wxCheckBox(panel, wxID_ANY, "Pause after pass");
	m_pauseOnInc->SetToolTip("Pause processing after each pass");
	m_pauseOnInc->SetValue(m_params.pauseOnInc);
	m_saveGif = new wxCheckBox(panel, wxID_ANY, "GIF");
	m_saveGif->SetToolTip("Create a gif animation");
	m_saveGif->SetValue(m_params.saveGif);
	m_saveFrames = new wxCheckBox(panel, wxID_ANY, "Frames");
	m_saveFrames->SetToolTip("Save individual image frames");
	m_saveFrames->SetValue(m_params.saveFrames);
	m_fps = new wxTextCtrl(panel, wxID_ANY, wxString::Format("%d", m_params.fps), wxDefaultPosition, wxSize(TEXT_BOX_WIDTH, TEXT_BOX_HEIGHT), wxTE_PROCESS_ENTER | wxTE_CENTRE, frate);
	m_fps->SetToolTip("Frames per second");
	m_rotationAngle = new wxTextCtrl(panel, wxID_ANY, wxString::Format("%.1lf", m_params.rotationAngle), wxDefaultPosition, wxSize(TEXT_BOX_WIDTH, TEXT_BOX_HEIGHT), wxTE_PROCESS_ENTER | wxTE_CENTRE, ang);
	m_rotationAngle->SetToolTip("Object rotation per increment (in degrees)");

	wxSizer *dimSizer = new wxStaticBoxSizer(new wxStaticBox(panel, wxID_ANY, "Dimensions"), wxHORIZONTAL);
	dimSizer->Add(new wxStaticText(panel, wxID_ANY, "XDim"), wxSizerFlags(0).Center().Border(wxALL, 3));
	dimSizer->Add(m_xdim, wxSizerFlags(0).Border(wxALL, 3));
	dimSizer->Add(new wxStaticText(panel, wxID_ANY, "YDim"), wxSizerFlags(0).Center().Border(wxALL, 3));
	dimSizer->Add(m_ydim, wxSizerFlags(0).Border(wxALL, 3));
	dimSizer->Add(new wxStaticText(panel, wxID_ANY, "ZDim"), wxSizerFlags(0).Center().Border(wxALL, 3));
	dimSizer->Add(m_zdim, wxSizerFlags(0).Border(wxALL, 3));

	wxSizer *aggSizer = new wxStaticBoxSizer(new wxStaticBox(panel, wxID_ANY, "Aggregate Control"), wxVERTICAL);
	wxSizer* aSizer0 = new wxBoxSizer(wxHORIZONTAL);
	aSizer0->Add(m_aggregateEnable, wxSizerFlags(0).Center().Border(wxALL, 3));
	aSizer0->Add(m_replaceEnable, wxSizerFlags(0).Center().Border(wxALL, 3));
	wxSizer* aSizer1 = new wxBoxSizer(wxHORIZONTAL);
	aSizer1->Add(new wxStaticText(panel, wxID_ANY, "Size"), wxSizerFlags(0).Center().Border(wxALL, 3));
	aSizer1->Add(m_particleSize, wxSizerFlags(0).Border(wxALL, 3));
	aggSizer->Add(aSizer0, wxSizerFlags(0));
	aggSizer->Add(aSizer1, wxSizerFlags(0));

	wxSizer *poreSizer = new wxStaticBoxSizer(new wxStaticBox(panel, wxID_ANY, "Initial Removal Control"), wxVERTICAL);
	wxSizer *pSizer0 = new wxBoxSizer(wxHORIZONTAL);
	pSizer0->Add(new wxStaticText(panel, wxID_ANY, L"\u03A9"), wxSizerFlags(0).Center().Border(wxALL, 3));
	pSizer0->Add(m_porosity, wxSizerFlags(0).Border(wxALL, 3));
	pSizer0->Add(new wxStaticText(panel, wxID_ANY, "Size"), wxSizerFlags(0).Center().Border(wxALL, 3));
	pSizer0->Add(m_poreSize, wxSizerFlags(0).Border(wxALL, 3));
	wxSizer *pSizer0a = new wxBoxSizer(wxHORIZONTAL);
	pSizer0a->Add(m_poreIsFixed, wxSizerFlags(0).Center().Border(wxALL, 3));
	pSizer0a->Add(m_withReplacement, wxSizerFlags(0).Border(wxALL, 3));
#ifdef RANDOM_REMOVAL
	pSizer0a->Add(m_naiveRemoval, wxSizerFlags(0).Border(wxALL, 3));
#endif //#ifdef RANDOM_REMOVAL

	wxSizer *pSizer1 = new wxBoxSizer(wxHORIZONTAL);
	pSizer1->Add(new wxStaticText(panel, wxID_ANY, "Shape"), wxSizerFlags(0).Center().Border(wxALL, 3));
	pSizer1->Add(m_poreCuboid, wxSizerFlags(0).Center().Border(wxLEFT, 6));
	pSizer1->Add(m_poreSpheroid, wxSizerFlags(0).Center().Border(wxALL, 3));
	poreSizer->Add(pSizer0, wxSizerFlags(0).Expand());
	poreSizer->Add(pSizer0a, wxSizerFlags(0).Expand());
	poreSizer->Add(pSizer1, wxSizerFlags(0).Expand());
	
	porositySectionEnable((m_params.porosity > 0.0), !m_params.aggregateEnable);

	wxSizer *dispSizer = new wxStaticBoxSizer(new wxStaticBox(panel, wxID_ANY, "Display Control"), wxVERTICAL);
	wxSizer* dSizer0 = new wxBoxSizer(wxHORIZONTAL);
	dSizer0->Add(m_displayEnable, wxSizerFlags(0).Border(wxALL, 3));
#ifdef WANT_FRAGMENTATION
	dSizer0->Add(m_enableFrag, wxSizerFlags(0).Border(wxALL, 3));
#endif// #ifdef WANT_FRAGMENTATION
	dSizer0->Add(m_pauseOnInc, wxSizerFlags(0).Center().Border(wxBOTTOM, 3));
	wxSizer* dSizer1 = new wxBoxSizer(wxHORIZONTAL);
	dSizer1->Add(new wxStaticText(panel, wxID_ANY, "Colour By"), wxSizerFlags(0).Center().Border(wxALL, 3));
	dSizer1->Add(m_displayFaces, wxSizerFlags(0).Center().Border(wxLEFT, 6));
	dSizer1->Add(m_displayFrags, wxSizerFlags(0).Center().Border(wxALL, 3));
	wxSizer* dSizer1a = new wxBoxSizer(wxHORIZONTAL);
	dSizer1a->Add(new wxStaticText(panel, wxID_ANY, "Colour Map"), wxSizerFlags(0).Center());
	dSizer1a->Add(m_colorMap, wxSizerFlags(0).Border(wxLEFT, 3));
	wxSizer* dSizer1b = new wxBoxSizer(wxHORIZONTAL);
	dSizer1b->Add(new wxStaticText(panel, wxID_ANY, "Show"), wxSizerFlags(0).Center().Border(wxRIGHT, 3));
	dSizer1b->Add(m_showOutlines, wxSizerFlags(0).Border(wxALL, 3));
	dSizer1b->Add(m_showAxes, wxSizerFlags(0).Border(wxALL, 3));
	wxSizer* dSizer2 = new wxBoxSizer(wxHORIZONTAL);
	dSizer2->Add(new wxStaticText(panel, wxID_ANY, "Save"), wxSizerFlags(0).Center().Border(wxRIGHT, 3));
	dSizer2->Add(m_saveFrames, wxSizerFlags(0).Center().Border(wxLEFT | wxBOTTOM, 3));
	dSizer2->Add(m_saveGif, wxSizerFlags(0).Center().Border(wxLEFT | wxBOTTOM, 3));
	wxSizer* dSizer2a = new wxBoxSizer(wxHORIZONTAL);
	dSizer2a->Add(new wxStaticText(panel, wxID_ANY, "FPS"), wxSizerFlags(0).Center().Border(wxBOTTOM, 3));
	dSizer2a->Add(m_fps, wxSizerFlags(0).Border(wxLEFT | wxRIGHT | wxBOTTOM, 3));
	dSizer2a->Add(new wxStaticText(panel, wxID_ANY, "Rotation Angle"), wxSizerFlags(0).Center().Border(wxLEFT | wxBOTTOM, 3));
	dSizer2a->Add(m_rotationAngle, wxSizerFlags(0).Border(wxLEFT | wxBOTTOM, 3));
	wxSizer* dSizer3 = new wxBoxSizer(wxHORIZONTAL);
	m_clearButton = new wxButton(panel, wxID_CLEAR, "Clear Plots", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
	m_clearButton->SetToolTip("Clear Plot Area");
	dSizer3->Add(m_clearButton, wxSizerFlags(0).Border(wxLEFT, 3));
	m_snapshotButton = new wxButton(panel, wxID_PREVIEW, "Snapshot", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
	m_snapshotButton->SetToolTip("Save Snapshot of Current Display Area");
	dSizer3->Add(m_snapshotButton, wxSizerFlags(0).Border(wxLEFT, 3));
	dispSizer->Add(dSizer0, wxSizerFlags(0));
	dispSizer->Add(dSizer1, wxSizerFlags(0).Border(wxTOP, 3));
	dispSizer->Add(dSizer1a, wxSizerFlags(0).Border(wxALL, 3));
	dispSizer->Add(dSizer1b, wxSizerFlags(0).Border(wxALL, 3));
	dispSizer->Add(dSizer2, wxSizerFlags(0).Border(wxALL, 3));
	dispSizer->Add(dSizer2a, wxSizerFlags(0).Border(wxALL, 3));
	dispSizer->Add(dSizer3, wxSizerFlags(0).Border(wxALL, 3));
	
	displaySectionEnable(m_params.displayEnable);

	wxSizer *outputSizer = new wxStaticBoxSizer(new wxStaticBox(panel, wxID_ANY, "Output Control"), wxVERTICAL);
	wxSizer *oSizer0 = new wxBoxSizer(wxHORIZONTAL);
	oSizer0->Add(new wxStaticText(panel, wxID_ANY, "Interval"), wxSizerFlags(0).Center().Border(wxALL, 3));
	oSizer0->Add(m_outputInc, wxSizerFlags(0).Border(wxALL, 3));
	oSizer0->Add(new wxStaticText(panel, wxID_ANY, "End"), wxSizerFlags(0).Center().Border(wxALL, 3));
	oSizer0->Add(m_outputEnd, wxSizerFlags(0).Border(wxALL, 3));
	oSizer0->Add(new wxStaticText(panel, wxID_ANY, "Runs"), wxSizerFlags(0).Center().Border(wxALL, 3));
	oSizer0->Add(m_nRuns, wxSizerFlags(0).Border(wxALL, 3));
	wxSizer *oSizer3 = new wxBoxSizer(wxHORIZONTAL);
	oSizer3->Add(new wxStaticText(panel, wxID_ANY, "Sub-sample"), wxSizerFlags(0).Center().Border(wxALL, 3));
	oSizer3->Add(m_outputSubsamp, wxSizerFlags(0).Border(wxALL, 3));
	oSizer3->Add(new wxStaticText(panel, wxID_ANY, "N-samples"), wxSizerFlags(0).Center().Border(wxALL, 3));
	oSizer3->Add(m_outputNSamps, wxSizerFlags(0).Border(wxALL, 3));
	wxSizer *oSizer1 = new wxBoxSizer(wxHORIZONTAL);
	oSizer1->Add(new wxStaticText(panel, wxID_ANY, "Directory"), wxSizerFlags(0).Center().Border(wxALL, 3));
	oSizer1->Add(m_outputDir, wxSizerFlags(0).Border(wxALL, 3));
	oSizer1->Add(m_chooseDirButton, wxSizerFlags(0).Border(wxALL, 3));
	wxSizer *oSizer2 = new wxBoxSizer(wxHORIZONTAL);
	oSizer2->Add(new wxStaticText(panel, wxID_ANY, "Save"), wxSizerFlags(0).Center().Border(wxALL, 3));
	oSizer2->Add(m_outputSave, wxSizerFlags(0).Center().Border(wxALL, 3));
	oSizer2->Add(m_outputSaveGrid, wxSizerFlags(0).Center().Border(wxALL, 3));
	oSizer2->Add(m_outputSaveInfo, wxSizerFlags(0).Center().Border(wxALL, 3));
#ifdef WANT_FRAGMENTATION
	oSizer2->Add(m_outputSaveFrags, wxSizerFlags(0).Center().Border(wxALL, 3));
#endif //#ifdef WANT_FRAGMENTATION
	outputSizer->Add(oSizer0, wxSizerFlags(0).Expand());
	outputSizer->Add(oSizer3, wxSizerFlags(0).Expand());
	outputSizer->Add(oSizer2, wxSizerFlags(0).Expand());
	outputSizer->Add(oSizer1, wxSizerFlags(0).Expand());

#ifdef WANT_INPUT_CONTROL
	wxSizer *inputSizer = new wxStaticBoxSizer(new wxStaticBox(panel, wxID_ANY, "Input Control"), wxVERTICAL);
	wxSizer *iSizer0 = new wxBoxSizer(wxHORIZONTAL);
	iSizer0->Add(new wxStaticText(panel, wxID_ANY, "XYZ File"), wxSizerFlags(0).Center().Border(wxALL, 3));
	iSizer0->Add(m_inputFile, wxSizerFlags(0).Border(wxALL, 3));
	iSizer0->Add(m_chooseFileButton, wxSizerFlags(0).Border(wxALL, 3));
	inputSizer->Add(iSizer0, wxSizerFlags(0).Expand());
#endif //#ifdef WANT_INPUT_CONTROL

#ifdef WANT_FRAGMENTATION
	m_fragBoxSizer = new wxStaticBoxSizer(new wxStaticBox(panel, wxID_ANY, "Fragmentation Control"), wxVERTICAL);
	wxSizer *fragSizer = new wxBoxSizer(wxHORIZONTAL);
	fragSizer->Add(m_discardFrags, wxSizerFlags(0).Border(wxALL, 3));
	fragSizer->Add(m_animateFrags, wxSizerFlags(0).Border(wxALL, 3));
	fragSizer->Add(m_histFrags, wxSizerFlags(0).Border(wxALL, 3));
	wxSizer *fragSizer2 = new wxBoxSizer(wxHORIZONTAL);
	fragSizer2->Add(m_enableFragClass, wxSizerFlags(0).Center().Border(wxALL, 3));
	fragSizer2->Add(m_fragClass, wxSizerFlags(0).Border(wxALL, 3));
	wxSizer* fragSizer3 = new wxBoxSizer(wxHORIZONTAL);
	fragSizer3->Add(new wxStaticText(panel, wxID_ANY, "Fragment at"), wxSizerFlags(0).Center().Border(wxALL, 3));
	fragSizer3->Add(m_faces, wxSizerFlags(0).Border(wxALL, 3));
	fragSizer3->Add(m_edges, wxSizerFlags(0).Border(wxALL, 3));
	fragSizer3->Add(m_verts, wxSizerFlags(0).Border(wxALL, 3));
	m_fragBoxSizer->Add(fragSizer, wxSizerFlags(0));
	m_fragBoxSizer->Add(fragSizer2, wxSizerFlags(0));
	m_fragBoxSizer->Add(fragSizer3, wxSizerFlags(0));
#endif// #ifdef WANT_FRAGMENTATION

	paramSizer->Add(dimSizer, wxSizerFlags(0).Expand().Border(wxBOTTOM, 3));
	paramSizer->Add(aggSizer, wxSizerFlags(0).Expand().Border(wxBOTTOM, 3));
	paramSizer->Add(poreSizer, wxSizerFlags(0).Expand().Border(wxBOTTOM, 3));
	paramSizer->Add(dispSizer, wxSizerFlags(0).Expand().Border(wxBOTTOM, 3));
	paramSizer->Add(outputSizer, wxSizerFlags(0).Expand().Border(wxBOTTOM, 3));
#ifdef WANT_INPUT_CONTROL
	paramSizer->Add(inputSizer, wxSizerFlags(0).Expand().Border(wxBOTTOM, 3));
#endif //#ifdef WANT_INPUT_CONTROL
#ifdef WANT_FRAGMENTATION
	paramSizer->Add(m_fragBoxSizer, wxSizerFlags(0).Expand().Border(wxBOTTOM, 3).ReserveSpaceEvenIfHidden());
#endif// #ifdef WANT_FRAGMENTATION

	return(paramSizer);
}
/*
void ControlsPanel::rescaleSA(bool isCuboid)
{
	double x = m_params.xdim;
	double y = m_params.ydim;
	double z = m_params.zdim;

	double n0 = (1.0 / (M_PI * 2.0)) * (x*y + y*z + x*z);
	double n1 = 3.0 * pow(n0, 1.6);
	double d0 = pow(x*y, 1.6) + pow(y*z, 1.6) + pow(x*z, 1.6);
	double v0 = n1 / d0;
	double v1 = 4.0 * pow(v0, .625);
	double scalar = sqrt(v1);

	if (isCuboid)
	{
		m_params.xdim = (uint32_t)round((double)m_params.xdim / scalar);
		m_params.ydim = (uint32_t)round((double)m_params.ydim / scalar);
		m_params.zdim = (uint32_t)round((double)m_params.zdim / scalar);
	}
	else
	{
		m_params.xdim = (uint32_t)round((double)m_params.xdim*scalar);
		m_params.ydim = (uint32_t)round((double)m_params.ydim*scalar);
		m_params.zdim = (uint32_t)round((double)m_params.zdim*scalar);
	}

}
*/
void ControlsPanel::displaySectionEnable(bool state)
{
	m_showOutlines->Enable(state);
	m_showAxes->Enable(state);
	m_pauseOnInc->Enable(state);
	m_displayFaces->Enable(state);
	m_displayFrags->Enable(state);
	m_saveGif->Enable(state);
	m_saveFrames->Enable(state);
	m_fps->Enable(state);
	m_rotationAngle->Enable(state);
	m_clearButton->Enable(state);
	m_snapshotButton->Enable(state);

	Refresh(false);
}

void ControlsPanel::porositySectionEnable(bool state, bool enable/*=true*/)
{
	m_porosity->Enable(enable);

	bool mstate = state & enable;
	m_poreSize->Enable(mstate);
	m_poreIsFixed->Enable(mstate);
	m_poreCuboid->Enable(mstate);
	m_poreSpheroid->Enable(mstate);
	m_withReplacement->Enable(mstate);

	Refresh(false);
}

void ControlsPanel::rescale(bool isCuboid)
{
	if (isCuboid)
	{
		m_params.xdim = (uint32_t)round((double)m_params.xdim / m_ellipse_scalar);
		m_params.ydim = (uint32_t)round((double)m_params.ydim / m_ellipse_scalar);
		m_params.zdim = (uint32_t)round((double)m_params.zdim / m_ellipse_scalar);
	}
	else
	{
		m_params.xdim = (uint32_t)round((double)m_params.xdim * m_ellipse_scalar);
		m_params.ydim = (uint32_t)round((double)m_params.ydim * m_ellipse_scalar);
		m_params.zdim = (uint32_t)round((double)m_params.zdim * m_ellipse_scalar);
	}
}

void ControlsPanel::refreshDisplay(bool force/*=false*/)
{
	if (m_grid)
		m_grid->m_params = m_params;

	if (!(m_params.displayEnable || force) || m_terminating)
		return;

	if (mGLCanvas && mGLCanvas->IsShownOnScreen())
	{
		mGLCanvas->SetGrid(m_grid);

		if (m_consuming && (m_params.rotationAngle != 0.0))
		{
			double angle = mParams[2] + m_params.rotationAngle;
			while (angle > 360.0)
				angle -= 360.0;
			mParams[2] = angle;
		}

		mGLCanvas->Refresh(); mGLCanvas->Update();	// Redraw the canvas

		if (m_consuming && (m_params.saveGif || m_params.saveFrames))
		{
			captureFrame();
		}

	}
	if (m_plot && m_plot->IsShownOnScreen())
	{
		m_plot->doRefresh(force);
	}

#ifdef WANT_FRAGMENTATION
	if (m_histWin->IsShownOnScreen())
	{
		m_histWin->doRefresh(force);
	}
#endif// #ifdef WANT_FRAGMENTATION

	//wxCommandEvent* event = new wxCommandEvent(wxEVT_STATUS_EVENT);
	//StatusInfo* statusInfo = new StatusInfo(StatusInfo::REFRESH, std::string(), 0, m_params.displayEnable ? m_grid : NULL);
	//event->SetClientData(statusInfo);

	//wxQueueEvent(GlobalFrame, event);	// Send start event to the main GUI frame
}

float ControlsPanel::getScale()
{		// Return max dimension
	float dim = (m_params.zdim > m_params.ydim) ? m_params.zdim : m_params.ydim;
	dim = (dim > m_params.xdim) ? dim : m_params.xdim;
	return(dim);
}

#ifdef WANT_FRAGMENTATION
void ControlsPanel::OnSlider(wxCommandEvent& event)
{
	m_params.fragClass = m_fragClass->GetValue();
	refreshDisplay(true);

	event.Skip();
}
#endif //#ifdef WANT_FRAGMENTATION

void ControlsPanel::OnCheckBox(wxCommandEvent& event)
{
	wxCheckBox* checkbox = (wxCheckBox*)event.GetEventObject();

	saveConfig();	// Update the settings file
#ifdef WANT_FRAGMENTATION
	if (checkbox == m_histFrags)
	{
		m_histWin->Show(checkbox->IsChecked());
		lowerPanel->Layout();
	}
	else if (checkbox == m_animateFrags)
	{
		refreshDisplay(true);
	}
	else if (checkbox == m_enableFrag)
	{
		m_histWin->Show(m_params.histFrags && checkbox->IsChecked());
		lowerPanel->Layout();
		m_fragBoxSizer->Show(checkbox->IsChecked());
		m_paramSizer->Layout();
	}
	else if (checkbox == m_enableFragClass)
	{
		m_fragClass->Enable(checkbox->IsChecked());
		refreshDisplay(true);
	}
#endif //#ifdef WANT_FRAGMENTATION

	if (m_thread)	// If there is currently a thread return
	{	
		refreshDisplay(true);
		return;
	}

	if ((checkbox == m_poreIsFixed) || (checkbox == m_withReplacement) || 
		(checkbox == m_aggregateEnable) || (checkbox == m_replaceEnable))
	{
		if (checkbox == m_aggregateEnable)	// Toggle the Porosity control section if Aggregation is toggled
		{
			porositySectionEnable((m_params.porosity > 0.0), !checkbox->GetValue());
		}
		m_thread = new ProcThread(this, true);	// Launch with preprocessing only
	}
	else if (checkbox == m_displayEnable)
	{
		displaySectionEnable(m_params.displayEnable);
		refreshDisplay(true);
	}
	else if	((checkbox == m_showOutlines) || 
			(checkbox == m_showAxes)) 			// If Display was off or display option changed
	{
		refreshDisplay(true);
	}

	event.Skip(false);
}

void ControlsPanel::OnComboBox(wxCommandEvent& event)
{
	wxComboBox* cmpicker = (wxComboBox*)event.GetEventObject();
	int colormapIndex = cmpicker->GetCurrentSelection();
	if (colormapIndex == m_params.colormapIndex)
		return;

	GlobalGL3dContext->GenerateTextures(colormapIndex);
	refreshDisplay(true);

	if (m_thread)	// If there is currently a thread return
		return;

	saveConfig();	// Update the settings file

	m_thread = new ProcThread(this, true);	// Launch with preprocessing only

	event.Skip(false);
}

void ControlsPanel::OnRadioButton(wxCommandEvent& event)
{	
	saveConfig();	// Update the settings file

	wxRadioButton* rbutton = (wxRadioButton*)event.GetEventObject();
	if ((rbutton != m_displayFaces) && (rbutton != m_displayFrags))
	{
		if (m_thread)	// If there is currently a thread return
			return;
		m_thread = new ProcThread(this, true);	// Launch with preprocessing only
	}
	if ((rbutton == m_cuboid) || ( rbutton == m_ellipsoid))
	{
		rescale(rbutton == m_cuboid);
		//rescaleSA(rbutton == m_cuboid);
		m_xdim->SetValue(wxString::Format("%ld", m_params.xdim));
		m_ydim->SetValue(wxString::Format("%ld", m_params.ydim));
		m_zdim->SetValue(wxString::Format("%ld", m_params.zdim));
		refreshDisplay();
	}
	else
		refreshDisplay();

	event.Skip(false);
}

void ControlsPanel::updateSampling()
{
	m_outputNSamps->GetValue().ToULong(&m_params.outputNSamps);
	if (m_params.outputNSamps > 0)
	{
		m_params.outputSubsamp = m_grid->getInitialVolume() / m_params.outputNSamps;
		if (m_params.outputSubsamp == 0)
			m_params.outputSubsamp = 1;
		m_outputSubsamp->SetValue(wxString::Format("%ld", m_params.outputSubsamp));
	}
}

void ControlsPanel::OnPageChanged(wxBookCtrlEvent& event)
{
	m_params.cubeView = (m_notebook->GetSelection() == 0);

	refreshDisplay(true);

	event.Skip();
}

void ControlsPanel::OnText(wxCommandEvent& event)
{
	wxTextCtrl* textCtrl = (wxTextCtrl*)event.GetEventObject();
	if ((textCtrl == m_xdim) || (textCtrl == m_ydim) || (textCtrl == m_zdim) ||
		(textCtrl == m_porosity) || (textCtrl == m_poreSize) ||
		(textCtrl == m_particleSize))
	{
		m_param_changed = true;
	}
}

void ControlsPanel::OnTextEnter(wxCommandEvent& event)
{
	m_param_changed = false;	// Reset flag. We'll commit any change that might have been made here

	wxTextCtrl* textCtrl = (wxTextCtrl*)event.GetEventObject();
	if ((textCtrl == m_rotationAngle) || (textCtrl == m_fps))
	{
		saveConfig();
		return;
	}

	// Because the following parameters require a reset of the multicube class,
	// if a thread is currently running we abort the update
	if (m_thread)	// If there is currently a thread return
		return;

	saveConfig();	// Update the settings file

	if ((textCtrl == m_xdim) || (textCtrl == m_ydim) || (textCtrl == m_zdim) ||
		(textCtrl == m_porosity) || (textCtrl == m_poreSize) ||
		(textCtrl == m_particleSize))
	{
		if (textCtrl == m_porosity)
		{
			porositySectionEnable((m_params.porosity > 0.0));
		}
		updateSampling();	// Initial volume may have changed.  Need to update the sampling.
		m_thread = new ProcThread(this, true);	// Launch with preprocessing only
	}
	else if (textCtrl == m_outputSubsamp)
	{
		m_params.outputNSamps = 0;
		m_outputNSamps->SetValue("0");
	}
	else if (textCtrl == m_outputNSamps)
	{	
		updateSampling();
	}
#ifdef WANT_INPUT_CONTROL
	else if (textCtrl == m_inputFile)
	{
		m_thread = new ProcThread(this, true);	// Launch with preprocessing only
	}
#endif //#ifdef WANT_INPUT_CONTROL
	event.Skip(false);
}

void ControlsPanel::toggleThreadState()
{
	if (m_thread)
	{
		if (m_thread->IsRunning())
		{
			m_runButton->SetLabel("resume");
			m_paused = true;
			m_thread->Pause();
		}
		else if (m_thread->IsPaused())
		{
			m_runButton->SetLabel("pause");
			m_thread->Resume();
			m_paused = false;
		}
	}
}

void ControlsPanel::captureFrame()
{
	wxCommandEvent* event = new wxCommandEvent(wxEVT_FRAME_CAPTURE_EVENT);
	wxQueueEvent(this, event);	// Send update event to Controls Panel
}

void ControlsPanel::OnCaptureFrame(wxCommandEvent& event)
{
	mGLCanvas->getFrame();
}


void ControlsPanel::OnThreadState(wxCommandEvent& event)
{
	toggleThreadState();
}

void ControlsPanel::OnThreadDone(wxCommandEvent& event)
{
	m_runButton->SetLabel("run");

	ProcThread* thread = (ProcThread*)event.GetEventObject();
	thread->Wait();

	m_thread = NULL;
	bool isPrerun = thread->m_prerun;
	if (mGLCanvas && !isPrerun && (m_params.saveGif || m_params.saveFrames))
	{
		std::string outputDir = ".";
		if (!m_params.outputDir.empty())
			outputDir = m_params.outputDir;
		outputDir.append(format("\\Run%03d", m_run_count));
		if (!wxFileName::DirExists(outputDir))
			wxFileName::Mkdir(outputDir);

		if (m_params.saveFrames)
		{
			std::string baseName = "snap";
			mGLCanvas->saveFrames(outputDir, baseName);
		}
		if (m_params.saveGif)
		{
			std::string fileName = format("%s\\anim.gif", outputDir.c_str());
			float fps = 1.0f / (float)m_params.fps;
			mGLCanvas->createGif(fileName, fps);
		}
		mGLCanvas->clearImages();
	}

	delete thread;

	if (!isPrerun)
	{
		++m_run_count;			// Increment run counter
		if (m_run_count < (int)m_params.nRuns)
		{
			m_runButton->SetLabel("pause");
			m_thread = new ProcThread(this);	// Launch the thread
		}
		else
			m_run_count = 0;	// Reset run counter
	}
}

void ControlsPanel::OnButton(wxCommandEvent& event)
{
	wxButton* button = (wxButton*)event.GetEventObject();
	wxString label = button->GetLabel();
	int id = event.GetId();

	if (button == m_runButton)
	{
		if (m_done)
		{
			//updateSampling();
			saveConfig();
			button->SetLabel("pause");
			if (m_param_changed)
			{
				m_param_changed = false;	// reset the flag
				m_need_prerun = true;		// make sure we reconfig the multicube using the modified params before running
			}
			m_thread = new ProcThread(this);	// Launch the thread
		}
		else
		{
			toggleThreadState();
		}
	}
	else if (button == m_stopButton)
	{
		m_runButton->SetLabel("run");
		if (m_paused)
			toggleThreadState();
		m_done = true;
		m_run_count = m_params.nRuns;
	}
	else if (id == wxID_CLEAR)
	{
		if (m_plot)
		{
			m_plot->clear();
		}
#ifdef WANT_FRAGMENTATION
		if (m_histWin)
		{
			m_histWin->clear();
		}
#endif //#ifdef WANT_FRAGMENTATION
	}
	else if (id == wxID_PREVIEW)
	{
		if (!m_params.outputDir.empty())
		{
			wxString outputDir = m_params.outputDir;
			// Create the output directory if necessary
			if (!wxFileName::DirExists(outputDir))
				wxFileName::Mkdir(outputDir);
			outputDir.append(format("\\Run%03d", m_run_count));
			if (!wxFileName::DirExists(outputDir))
				wxFileName::Mkdir(outputDir);

			if (m_params.cubeView)
			{ 
				if (mGLCanvas)
				{
					std::string basename = format("%s\\ObjectSnap", outputDir.ToStdString().c_str());
					mGLCanvas->snap(basename);
				}
			}
			else
			{
				if (m_plot)
				{
					std::string basename = format("%s\\PlotSnap", outputDir.ToStdString().c_str());
					m_plot->snap(basename);
				}
			}
#ifdef WANT_FRAGMENTATION
			if (m_params.histFrags)
			{
				std::string basename = format("%s\\HistSnap", outputDir.ToStdString().c_str());
				m_histWin->snap(basename);
			}
#endif //#ifdef WANT_FRAGMENTATION
		}
	}
	else if ((button == m_chooseDirButton) && !m_thread)
	{
		wxDirDialog dlg(NULL, "Choose Output Directory");
		int result = dlg.ShowModal();
		if (result == wxID_OK)
		{
			wxString path = dlg.GetPath();
			m_outputDir->SetValue(path);
			m_outputDir->SetInsertionPointEnd();
		}
	}
#ifdef WANT_INPUT_CONTROL
	else if ((button == m_chooseFileButton) && !m_thread)
	{
		wxFileDialog dlg(NULL, "Choose Input File");
		int result = dlg.ShowModal();
		if (result == wxID_OK)
		{
			wxString path = dlg.GetPath();
			m_inputFile->SetValue(path);
			m_inputFile->SetInsertionPointEnd();
		}
	}
#endif //#ifdef WANT_INPUT_CONTROL

	event.Skip(false);
}

void ControlsPanel::getCurrentParams()
{
	wxSize clientSize		= GetClientSize();
	wxPoint pos				= GetParent()->GetPosition();
	m_params.xsize			= clientSize.x;
	m_params.ysize			= clientSize.y;
	m_params.xpos			= pos.x;
	m_params.ypos			= pos.y;
	m_params.sashpos		= m_splitter->GetSashPosition();
	m_params.cuboid			= m_cuboid->GetValue();
	m_xdim->GetValue().ToULong(&m_params.xdim);
	m_ydim->GetValue().ToULong(&m_params.ydim);
	m_zdim->GetValue().ToULong(&m_params.zdim);
	m_params.outputDir		= m_outputDir->GetValue();
#ifdef WANT_INPUT_CONTROL
	m_params.inputFile		= m_inputFile->GetValue();
#endif //#ifdef WANT_INPUT_CONTROL
	m_params.poreIsFixed	= m_poreIsFixed->GetValue();
	m_porosity->GetValue().ToDouble(&m_params.porosity);
	m_poreSize->GetValue().ToULong(&m_params.poreSize);
	m_params.poreIsCuboid	= m_poreCuboid->GetValue();
	m_params.withReplacement= m_withReplacement->GetValue();

	m_params.aggregateEnable = m_aggregateEnable->GetValue();
	m_particleSize->GetValue().ToULong(&m_params.particleSize);
	m_params.replaceEnable = m_replaceEnable->GetValue();
#ifdef RANDOM_REMOVAL
	m_params.naiveRemoval	= m_naiveRemoval->GetValue();
#endif //#ifdef RANDOM_REMOVAL
	m_outputInc->GetValue().ToDouble(&m_params.outputInc);
	m_outputEnd->GetValue().ToDouble(&m_params.outputEnd);
	m_nRuns->GetValue().ToULong(&m_params.nRuns);
	m_outputSubsamp->GetValue().ToULong(&m_params.outputSubsamp);
	m_outputNSamps->GetValue().ToULong(&m_params.outputNSamps);
	m_params.outputSave		= m_outputSave->GetValue();
	m_params.outputSaveGrid = m_outputSaveGrid->GetValue();
	m_params.outputSaveInfo = m_outputSaveInfo->GetValue();
	m_params.displayEnable	= m_displayEnable->GetValue();
	m_params.displayFaces	= m_displayFaces->GetValue();
	m_params.colormapIndex	= m_colorMap->GetCurrentSelection();
	m_params.showOutlines	= m_showOutlines->GetValue();
	m_params.showAxes		= m_showAxes->GetValue();
	m_params.pauseOnInc		= m_pauseOnInc->GetValue();
	m_params.saveGif		= m_saveGif->GetValue();
	m_params.saveFrames		= m_saveFrames->GetValue();
	m_fps->GetValue().ToULong(&m_params.fps);
	m_params.cubeView		= m_notebook ? (m_notebook->GetSelection() == 0) : true;
	m_rotationAngle->GetValue().ToDouble(&m_params.rotationAngle);
#ifdef WANT_FRAGMENTATION
	m_params.outputSaveFrags = m_outputSaveFrags->GetValue();
	m_params.enableFrag = m_enableFrag->GetValue();
	m_params.discardFrags = m_discardFrags->GetValue();
	m_params.animateFrags = m_animateFrags->GetValue();
	m_params.histFrags = m_histFrags->GetValue();
	m_params.enableFragClass = m_enableFragClass->GetValue();
	m_params.fragClass = m_fragClass->GetValue();
	m_params.fragmentAt = m_faces->GetValue() ? 0 : m_edges->GetValue() ? 1 : 2;
#endif //#ifdef WANT_FRAGMENTATION
}

void ControlsPanel::setCurrentParams(bool reset/*=true*/)
{
	loadDefaults();

	// GUI settings (only change on reset)
	if (reset)
	{
		wxSize clientSize(m_params.xsize, m_params.ysize);
		SetClientSize(clientSize);
		wxPoint pos(m_params.xpos, m_params.ypos);
		GetParent()->SetPosition(pos);
		m_splitter->SetSashPosition(m_params.sashpos);
		if (m_notebook)
		{	if (m_params.cubeView)
				m_notebook->SetSelection(0);
			else
				m_notebook->SetSelection(1);
		}
	}

	m_cuboid->SetValue(m_params.cuboid);
	wxString val = wxString::Format("%u",m_params.xdim);
	m_xdim->SetValue(val);
	val = wxString::Format("%u",m_params.ydim);
	m_ydim->SetValue(val);
	val = wxString::Format("%u",m_params.zdim);
	m_zdim->SetValue(val);
	m_outputDir->SetValue(m_params.outputDir);
#ifdef WANT_INPUT_CONTROL
	m_inputFile->SetValue(m_params.inputFile);
#endif //#ifdef WANT_INPUT_CONTROL
	m_poreIsFixed->SetValue(m_params.poreIsFixed);
	val = wxString::Format("%lf",m_params.porosity);
	m_porosity->SetValue(val);
	val = wxString::Format("%u",m_params.poreSize);
	m_poreSize->SetValue(val);
	m_poreCuboid->SetValue(m_params.poreIsCuboid);
	m_withReplacement->SetValue(m_params.withReplacement);

	m_aggregateEnable->SetValue(m_params.aggregateEnable);
	val = wxString::Format("%u", m_params.particleSize);
	m_particleSize->SetValue(val);
	m_replaceEnable->SetValue(m_params.replaceEnable);
#ifdef RANDOM_REMOVAL
	m_naiveRemoval->SetValue(m_params.naiveRemoval);
#endif //#ifdef RANDOM_REMOVAL
	val = wxString::Format("%lf",m_params.outputInc);
	m_outputInc->SetValue(val);
	val = wxString::Format("%lf",m_params.outputEnd);
	m_outputEnd->SetValue(val);
	val = wxString::Format("%u",m_params.nRuns);
	m_nRuns->SetValue(val);
	val = wxString::Format("%u",m_params.outputSubsamp);
	m_outputSubsamp->SetValue(val);
	val = wxString::Format("%u",m_params.outputNSamps);
	m_outputNSamps->SetValue(val);
	m_outputSave->SetValue(m_params.outputSave);
	m_outputSaveGrid->SetValue(m_params.outputSaveGrid);
	m_outputSaveInfo->SetValue(m_params.outputSaveInfo);
	m_displayEnable->SetValue(m_params.displayEnable);
	m_colorMap->SetSelection(m_params.colormapIndex);
	m_displayFaces->SetValue(m_params.displayFaces);
	m_showOutlines->SetValue(m_params.showOutlines);
	m_showAxes->SetValue(m_params.showAxes);
	m_pauseOnInc->SetValue(m_params.pauseOnInc);
	m_saveGif->SetValue(m_params.saveGif);
	m_saveFrames->SetValue(m_params.saveFrames);
	val = wxString::Format("%u",m_params.fps);
	m_fps->SetValue(val);
	val = wxString::Format("%lf",m_params.rotationAngle);
	m_rotationAngle->SetValue(val);
#ifdef WANT_FRAGMENTATION
	m_outputSaveFrags->SetValue(m_params.outputSaveFrags);
	m_enableFrag->SetValue(m_params.enableFrag);
	m_discardFrags->SetValue(m_params.discardFrags);
	m_animateFrags->SetValue(m_params.animateFrags);
	m_histFrags->SetValue(m_params.histFrags);
	m_enableFragClass->SetValue(m_params.enableFragClass);
	m_fragClass->SetValue(m_params.fragClass);
	switch(m_params.fragmentAt) {
		case 0 : m_faces->SetValue(true); break;
		case 1 : m_edges->SetValue(true); break;
		case 2 : m_verts->SetValue(true); break;
	}
#endif //#ifdef WANT_FRAGMENTATION

	updateSampling();	// Initial volume may have changed.  Need to update the sampling.
	m_thread = new ProcThread(this, true);	// Launch with preprocessing only

	refreshDisplay();
}

bool ControlsPanel::saveConfig()
{
	FILE* fp = fopen("Samurai.xml","w+");
	if (fp == NULL)
		return(false);

	getCurrentParams();		// Read the current control panel settings

	fprintf(fp,"<?xml version=\"1.0\" encoding=\"utf-8\" ?>\n");
	fprintf(fp,"<settings>\n");
	fprintf(fp, "\t<app w=\"%d\" h=\"%d\" x=\"%d\" y=\"%d\" sashpos=\"%d\" />\n", m_params.xsize, m_params.ysize, m_params.xpos, m_params.ypos, m_params.sashpos);
	fprintf(fp,"\t<object isCuboid=\"%d\" />\n", m_params.cuboid);
	fprintf(fp,"\t<dimensions x=\"%d\" y=\"%d\" z=\"%d\" />\n", m_params.xdim, m_params.ydim, m_params.zdim);
#ifdef RANDOM_REMOVAL
	fprintf(fp,"\t<porectrl porosity=\"%.5lf\" size=\"%d\" fixed=\"%d\" isCuboid=\"%d\" replace=\"%d\" naive=\"%d\" />\n", m_params.porosity, m_params.poreSize, m_params.poreIsFixed, m_params.poreIsCuboid, m_params.withReplacement, m_params.naiveRemoval);
#else
	fprintf(fp,"\t<porectrl porosity=\"%.5lf\" size=\"%d\" fixed=\"%d\" isCuboid=\"%d\" replace=\"%d\" />\n", m_params.porosity, m_params.poreSize, m_params.poreIsFixed, m_params.poreIsCuboid, m_params.withReplacement);
#endif //#ifdef RANDOM_REMOVAL
	fprintf(fp, "\t<aggctrl enable=\"%d\" size=\"%d\" replace=\"%d\"/>\n", m_params.aggregateEnable, m_params.particleSize, m_params.replaceEnable);
	fprintf(fp,"\t<outputctrl inc=\"%.5lf\" end=\"%.5lf\" runs=\"%d\" subsamp=\"%d\" nsamps=\"%d\" dir=\"%s\" save=\"%d\" grid=\"%d\" info=\"%d\" />\n", m_params.outputInc, m_params.outputEnd, m_params.nRuns, m_params.outputSubsamp, m_params.outputNSamps, m_params.outputDir.c_str(), m_params.outputSave, m_params.outputSaveGrid, m_params.outputSaveInfo);
#ifdef WANT_INPUT_CONTROL
	fprintf(fp,"\t<inputctrl file=\"%s\" />\n", m_params.inputFile.c_str());
#endif //#ifdef WANT_INPUT_CONTROL
	fprintf(fp,"\t<displayopts enable=\"%d\" cmindex=\"%d\" faces=\"%d\" outline=\"%d\" axes=\"%d\" pause=\"%d\" save=\"%d\" frames=\"%d\" cubeview=\"%d\" />\n", m_params.displayEnable, m_params.colormapIndex, m_params.displayFaces, m_params.showOutlines, m_params.showAxes, m_params.pauseOnInc, m_params.saveGif, m_params.saveFrames, m_params.cubeView);
	fprintf(fp,"\t<displayctrl fps=\"%d\" rotangle=\"%.5lf\" zoom=\"%.5lf\" xangle=\"%.5lf\" yangle=\"%.5lf\" zangle=\"%.5lf\" />\n", m_params.fps, m_params.rotationAngle, mParams[3], mParams[0], mParams[1], mParams[2]);
#ifdef WANT_FRAGMENTATION
	fprintf(fp, "\t<fragmentctrl enable=\"%d\" discard=\"%d\" animate=\"%d\" hist=\"%d\" enableclass=\"%d\" class=\"%d\" save=\"%d\" fragat=\"%d\" />\n", m_params.enableFrag, m_params.discardFrags, m_params.animateFrags, m_params.histFrags, m_params.enableFragClass, m_params.fragClass, m_params.outputSaveFrags, m_params.fragmentAt);
#endif //#ifdef WANT_FRAGMENTATION
	fprintf(fp,"</settings>\n");

	fclose(fp);
	return(true);
}

void ControlsPanel::loadDefaults()
{		
	MultiCube::loadDefaults(m_params);

	mParams[0] = mParams[1] = mParams[2] = mParams[3] = 0;
}

bool ControlsPanel::loadConfig()
{
	wxXmlDocument doc;
	bool result = (wxFileName("Samurai.xml").Exists() && doc.Load("Samurai.xml"));
	if (result)
	{	
		wxXmlNode* parent = doc.GetRoot();
		std::string parentName = parent->GetName().ToStdString();
		if (parentName == "settings")
		{	
			wxXmlNode* child = parent->GetChildren();
			while (child)
			{
				std::string childName = child->GetName().ToStdString();
				if (childName == "app")
				{
					wxXmlAttribute* attr = child->GetAttributes();
					while(attr)
					{	std::string name = attr->GetName().ToStdString();
						std::string value = attr->GetValue().ToStdString();
						if (name == "w")
						{	
							m_params.xsize = std::atol(value.c_str());
						}
						else if (name == "h")
						{	
							m_params.ysize = std::atol(value.c_str());
						}
						if (name == "x")
						{
							m_params.xpos = std::atol(value.c_str());
						}
						else if (name == "y")
						{
							m_params.ypos = std::atol(value.c_str());
						}
						else if (name == "sashpos")
						{
							m_params.sashpos = std::atol(value.c_str());
						}
						attr = attr->GetNext();
					}
				}
				else if (childName == "object")
				{
					wxXmlAttribute* attr = child->GetAttributes();
					while(attr)
					{	std::string name = attr->GetName().ToStdString();
						std::string value = attr->GetValue().ToStdString();
						if (name == "isCuboid")
						{	
							m_params.cuboid = std::atol(value.c_str());
						}
						attr = attr->GetNext();
					}
				}
				else if (childName == "dimensions")
				{
					wxXmlAttribute* attr = child->GetAttributes();
					while(attr)
					{	std::string name = attr->GetName().ToStdString();
						std::string value = attr->GetValue().ToStdString();
						if (name == "x")
						{	
							m_params.xdim = std::atol(value.c_str());
						}
						else if (name == "y")
						{	
							m_params.ydim = std::atol(value.c_str());
						}
						else if (name == "z")
						{	
							m_params.zdim = std::atol(value.c_str());
						}
						attr = attr->GetNext();
					}
				}
				else if (childName == "porectrl")
				{
					wxXmlAttribute* attr = child->GetAttributes();
					while(attr)
					{	std::string name = attr->GetName().ToStdString();
						std::string value = attr->GetValue().ToStdString();
						if (name == "porosity")
						{	
							m_params.porosity = std::atof(value.c_str());
						}
						else if (name == "size")
						{	
							m_params.poreSize = std::atol(value.c_str());
						}
						else if (name == "fixed")
						{	
							m_params.poreIsFixed = std::atol(value.c_str());
						}
						else if (name == "isCuboid")
						{	
							m_params.poreIsCuboid = std::atol(value.c_str());
						}
						else if (name == "replace")
						{
							m_params.withReplacement = std::atol(value.c_str());
						}
#ifdef RANDOM_REMOVAL
						else if (name == "naive")
						{
							m_params.naiveRemoval = std::atol(value.c_str());
						}
#endif //#ifdef RANDOM_REMOVAL
						attr = attr->GetNext();
					}
				}
				else if (childName == "aggctrl")
				{
					wxXmlAttribute* attr = child->GetAttributes();
					while (attr)
					{
						std::string name = attr->GetName().ToStdString();
						std::string value = attr->GetValue().ToStdString();
						if (name == "enable")
						{
							m_params.aggregateEnable = std::atol(value.c_str());
						}
						else if (name == "size")
						{
							m_params.particleSize = std::atol(value.c_str());
						}
						else if (name == "replace")
						{
							m_params.replaceEnable = std::atol(value.c_str());
						}
						attr = attr->GetNext();
					}
				}
				else if (childName == "outputctrl")
				{
					wxXmlAttribute* attr = child->GetAttributes();
					while(attr)
					{	std::string name = attr->GetName().ToStdString();
						std::string value = attr->GetValue().ToStdString();
						if (name == "inc")
						{	
							m_params.outputInc = std::atof(value.c_str());
							if (m_params.outputInc <= 0)
								m_params.outputInc = 0.1;	// default
						}
						else if (name == "end")
						{	
							m_params.outputEnd = std::atof(value.c_str());
							if (m_params.outputEnd > 1.0)
								m_params.outputEnd = 1.0;	// default
						}
						else if (name == "runs")
						{
							m_params.nRuns = std::atol(value.c_str());
							if (m_params.nRuns <= 0)
								m_params.nRuns = 1;			// default
						}
						else if (name == "subsamp")
						{
							m_params.outputSubsamp = std::atol(value.c_str());
							if (m_params.outputSubsamp == 0)
								m_params.outputSubsamp = 1;
						}
						else if (name == "nsamps")
						{
							m_params.outputNSamps = std::atol(value.c_str());
						}
						else if (name == "dir")
						{	
							m_params.outputDir = value;
						}
						else if (name == "save")
						{	
							m_params.outputSave = std::atol(value.c_str());
						}
						else if (name == "grid")
						{
							m_params.outputSaveGrid = std::atol(value.c_str());
						}
						else if (name == "info")
						{
							m_params.outputSaveInfo = std::atol(value.c_str());
						}
						attr = attr->GetNext();
					}
				}
#ifdef WANT_FRAGMENTATION
				else if (childName == "fragmentctrl")
				{
					wxXmlAttribute* attr = child->GetAttributes();
					while(attr)
					{	std::string name = attr->GetName().ToStdString();
						std::string value = attr->GetValue().ToStdString();
						if (name == "enable")
						{	
							m_params.enableFrag = std::atol(value.c_str());
						}
						else if (name == "discard")
						{	
							m_params.discardFrags = std::atol(value.c_str());
						}
						else if (name == "animate")
						{
							m_params.animateFrags = std::atol(value.c_str());
						}
						else if (name == "hist")
						{
							m_params.histFrags = std::atol(value.c_str());
						}
						else if (name == "enableclass")
						{
							m_params.enableFragClass = std::atol(value.c_str());
						}
						else if (name == "class")
						{
							m_params.fragClass = std::atol(value.c_str());
						}
						else if (name == "save")
						{
							m_params.outputSaveFrags = std::atol(value.c_str());
						}
						else if (name == "fragat")
						{
							m_params.fragmentAt = std::atol(value.c_str());
						}
						attr = attr->GetNext();
					}
				}
#endif //#ifdef WANT_FRAGMENTATION
#ifdef WANT_INPUT_CONTROL
				else if (childName == "inputctrl")
				{
					wxXmlAttribute* attr = child->GetAttributes();
					while(attr)
					{	std::string name = attr->GetName().ToStdString();
						std::string value = attr->GetValue().ToStdString();
						if (name == "file")
						{	
							m_params.inputFile = value;
						}
						attr = attr->GetNext();
					}
				}
#endif //#ifdef WANT_INPUT_CONTROL
				else if (childName == "displayopts")
				{
					wxXmlAttribute* attr = child->GetAttributes();
					while(attr)
					{	std::string name = attr->GetName().ToStdString();
						std::string value = attr->GetValue().ToStdString();
						if (name == "enable")
						{	
							m_params.displayEnable = std::atol(value.c_str());
						}
						else if (name == "cmindex")
						{
							m_params.colormapIndex = std::atol(value.c_str());
						}
						else if (name == "faces")
						{	
							m_params.displayFaces = std::atol(value.c_str());
						}
						else if (name == "outline")
						{
							m_params.showOutlines = std::atol(value.c_str());
						}
						else if (name == "axes")
						{
							m_params.showAxes = std::atol(value.c_str());
						}
						else if (name == "pause")
						{
							m_params.pauseOnInc = std::atol(value.c_str());
						}
						else if (name == "save")
						{
							m_params.saveGif = std::atol(value.c_str());
						}
						else if (name == "frames")
						{
							m_params.saveFrames = std::atol(value.c_str());
						}
						else if (name == "cubeview")
						{
							m_params.cubeView = std::atol(value.c_str());
						}
						attr = attr->GetNext();
					}
				}
				else if (childName == "displayctrl")
				{
					wxXmlAttribute* attr = child->GetAttributes();
					while(attr)
					{	std::string name = attr->GetName().ToStdString();
						std::string value = attr->GetValue().ToStdString();
						if (name == "fps")
						{
							m_params.fps = std::atol(value.c_str());
						}
						else if (name == "zoom")
						{	
							mParams[3] = std::atof(value.c_str());
						}
						else if (name == "rotangle")
						{
							m_params.rotationAngle = std::atof(value.c_str());
						}
						else if (name == "xangle")
						{	
							mParams[0] = std::atof(value.c_str());
						}
						else if (name == "yangle")
						{	
							mParams[1] = std::atof(value.c_str());
						}
						else if (name == "zangle")
						{	
							mParams[2] = std::atof(value.c_str());
						}
						attr = attr->GetNext();
					}
				}

				child = child->GetNext();
			}
		}
	}

	return(result);
}

void ControlsPanel::generateAggregate()
{
	std::string message = format("Generating Particles\n");
	sendMessage(message);

	startProgress();
	double threshhold = m_params.displayEnable ? POROSITY_PROCESSING_INC : 1.0;
	int progress = 0;
	while (m_grid->produceParticles(threshhold, &progress))
	{
		message = format("Generated %d particles\n", m_grid->getParticleCount());
		updateProgress(message, progress);

		refreshDisplay();	// Do display update

		threshhold += POROSITY_PROCESSING_INC;
	}

	message = format("Generated %d particles\n", m_grid->getParticleCount());
	updateProgress(message, progress);

	doneProgress();
}

void ControlsPanel::generatePores()
{
	if (m_grid == NULL)
		return;

	std::string message = format("Setting Omega to %lf\n", m_params.porosity);
	sendMessage(message);

	Dim_t surfaceArea;
	Dim_t volume = m_grid->getVolume(&surfaceArea);

	message = format("Volume %d Surface Area %d\n", volume, surfaceArea);
	sendMessage(message);

	Dim_t cubesToRemove = (Dim_t)(volume * m_params.porosity);
	message = format("Removing %d cubes\n", cubesToRemove);
	sendMessage(message);

	startProgress();
	double threshhold = m_params.displayEnable ? POROSITY_PROCESSING_INC : 1.0;
	int progress = 0;
	while (m_grid->producePores(threshhold, &progress))
	{
		message = format("Removed %d cubes\n", m_grid->getRemovedCount());
		updateProgress(message, progress);

		refreshDisplay();	// Do display update

		threshhold += POROSITY_PROCESSING_INC;
	}

	m_grid->finishPores();

	message = format("Removed %d cubes\n", m_grid->getRemovedCount());
	updateProgress(message, progress);

	doneProgress();

	refreshDisplay();	// Do display update
}

void ControlsPanel::destroyGrid()
{
	// Destroying the grid		
	if (mGLCanvas)
		mGLCanvas->SetGrid(NULL);
	
	MultiCube* grid = m_grid;
	m_grid = NULL;
	delete grid;
}

bool ControlsPanel::prerun(bool is_prerun)
{
	std::string message = format("-------------- Beginning Preprocessing (%d,%d,%d) %s ---------------\n", m_params.xdim, m_params.ydim, m_params.zdim, m_params.cuboid ? "Cuboid" : "Ellipsoid");
	sendMessage(message);

	// Start timing now
	std::chrono::system_clock::time_point before = std::chrono::system_clock::now();

	if (m_grid)			// Destroy any previously active grid
		destroyGrid();

#ifdef WANT_INPUT_CONTROL
	char* fname = m_params.inputFile.empty() ? NULL : (char*)m_params.inputFile.c_str();
#else
	char* fname = NULL;
#endif //#ifdef WANT_INPUT_CONTROL
	m_grid = new MultiCube(m_params, &m_done, fname);
	updateSampling();
	if (m_plot)
		m_plot->setData(&m_grid->x_cubesRemoved, &m_grid->y_surfaceArea);

#ifdef WANT_FRAGMENTATION
	if (m_params.histFrags)
		m_histWin->setData(&m_grid->dhist);
#endif// #ifdef WANT_FRAGMENTATION

	// Prepare the grid with a porosity level if requested
	if ((m_params.porosity > 0.0) && !m_params.aggregateEnable)
	{
		m_runButton->SetLabel("pause");
		generatePores();
		updateSampling();
	}

	if (is_prerun)
	{
		m_runButton->SetLabel("run");
		if (m_done)	// User requested a Stop
		{
			std::string message = format("\n*** Preprocessing Terminated ***\n");
			sendMessage(message);
			return(false);
		}
		m_done = true;
	}
		// Report elapsed time
	std::chrono::duration<double> duration = std::chrono::system_clock::now() - before;
	message = format("Preprocessing Elapsed Time: %.2lf(s)\n", duration.count());
	sendMessage(message);

	refreshDisplay();	// Do display update

	return(true);
}

void ControlsPanel::run(bool is_prerun)
{
	m_done = false;
	m_paused = false;

	getCurrentParams();		// Read the current control panel settings

	if (is_prerun || m_need_prerun)
	{	
			// Handle any preprocessing
		m_need_prerun = !prerun(is_prerun);
		if (is_prerun || m_need_prerun)
			return;	// Either a pre-processing run or the run was terminated by the user before completion
	}

	std::string message = format("-------------- Beginning Run %d - (%d,%d,%d) %s ---------------\n", m_run_count, m_params.xdim, m_params.ydim, m_params.zdim, m_params.cuboid ? "Cuboid" : "Ellipsoid");
	sendMessage(message);

	char filename[256];	// Used for output files
	std::string outputDir = ".";

	if (m_params.outputSave || m_params.outputSaveGrid || m_params.outputSaveInfo || 
#ifdef WANT_FRAGMENTATION
		m_params.outputSaveFrags ||
#endif //#ifdef WANT_FRAGMENTATION
		m_params.saveFrames || m_params.saveGif)
	{
		if (!m_params.outputDir.empty())
			outputDir = m_params.outputDir;
		// Create the output directory if necessary
		if (!wxFileName::DirExists(outputDir))
			wxFileName::Mkdir(outputDir);
		outputDir.append(format("\\Run%03d", m_run_count));
		if (!wxFileName::DirExists(outputDir))
			wxFileName::Mkdir(outputDir);
	}

	if (m_params.outputSave)
	{
		if (m_params.aggregateEnable)
			sprintf(filename, "%s\\%sSA%dx%dx%dp%ds%d.txt", outputDir.c_str(), m_params.cuboid ? "Cuboid" : "Ellipsoid", m_params.xdim, m_params.ydim, m_params.zdim, (int)(m_params.porosity*100+.5), m_params.particleSize);
		else
			sprintf(filename, "%s\\%sSA%dx%dx%dp%ds.txt", outputDir.c_str(), m_params.cuboid ? "Cuboid" : "Ellipsoid", m_params.xdim, m_params.ydim, m_params.zdim, (int)(m_params.porosity * 100 + .5));
		m_grid->openSAData(filename);		// Open volume vs surface area data file
	}
	if (m_params.outputSaveGrid)
	{
		if (m_params.aggregateEnable)
			sprintf(filename, "%s\\%sGrid%dx%dx%dp%ds%d_0.txt", outputDir.c_str(), m_params.cuboid ? "Cuboid" : "Ellipsoid", m_params.xdim, m_params.ydim, m_params.zdim, (int)(m_params.porosity * 100 + .5), m_params.particleSize);
		else
			sprintf(filename, "%s\\%sGrid%dx%dx%dp%ds_0.txt", outputDir.c_str(), m_params.cuboid ? "Cuboid" : "Ellipsoid", m_params.xdim, m_params.ydim, m_params.zdim, (int)(m_params.porosity * 100 + .5));
		m_grid->outputGrid(filename);	// Dump x,y,z data for doing 3D cube plots
	}
	if (m_params.outputSaveInfo)
	{
		if (m_params.aggregateEnable)
			sprintf(filename, "%s\\%sInfo%dx%dx%dp%ds%d.txt", outputDir.c_str(), m_params.cuboid ? "Cuboid" : "Ellipsoid", m_params.xdim, m_params.ydim, m_params.zdim, (int)(m_params.porosity * 100 + .5), m_params.particleSize);
		else
			sprintf(filename, "%s\\%sInfo%dx%dx%dp%ds.txt", outputDir.c_str(), m_params.cuboid ? "Cuboid" : "Ellipsoid", m_params.xdim, m_params.ydim, m_params.zdim, (int)(m_params.porosity * 100 + .5));
		m_grid->outputInfo(filename, m_run_count);	// Save run information bookkeeping
	}

	m_consuming = true;		// Used for frame capture
	refreshDisplay();		// Do display update
	message = format("Consuming...\n");
	sendMessage(message);

	double Threshhold = m_params.outputInc;
	startProgress();
	int progress = 0, last_progress = -1;
	int fragments = 0;

	// Start timing now
	std::chrono::system_clock::time_point before = std::chrono::system_clock::now();

	while(!m_done)
	{		// Because the thread pause is triggered by an event,
			// there can be a delay between the issuing of the pause
			// and the actual thread suspension. During that 
			// interval we want to prevent work being done.
		if (m_paused)
		{
			Sleep(0);
			continue;
		}
			// Consume some cubes
		if (!m_grid->consume(Threshhold, &progress))
			break;

#ifdef WANT_FRAGMENTATION
		if (m_params.enableFrag)	// If enabled do fragment detection
		{
			fragments = m_grid->detectFragments();

			if (m_params.outputSaveFrags)
			{	sprintf(filename, "%s\\%sFrags%dx%dx%d_%d.txt", outputDir.c_str(), m_params.cuboid ? "Cuboid" : "Ellipsoid", m_params.xdim, m_params.ydim, m_params.zdim, (int)(Threshhold*100+.5));
				m_grid->outputFragments(filename);
			}

			if (m_params.discardFrags)	// Remove them from the consuming process
				m_grid->discardFragments();
		}
#endif// #ifdef WANT_FRAGMENTATION

		if (m_params.outputSaveGrid)
		{
			if (m_params.aggregateEnable)
				sprintf(filename, "%s\\%sGrid%dx%dx%dp%ds%d_%d.txt", outputDir.c_str(), m_params.cuboid ? "Cuboid" : "Ellipsoid", m_params.xdim, m_params.ydim, m_params.zdim, (int)(m_params.porosity * 100 + .5), m_params.particleSize, (int)(Threshhold * 100 + .5));
			else
				sprintf(filename, "%s\\%sGrid%dx%dx%dp%ds_%d.txt", outputDir.c_str(), m_params.cuboid ? "Cuboid" : "Ellipsoid", m_params.xdim, m_params.ydim, m_params.zdim, (int)(m_params.porosity * 100 + .5), (int)(Threshhold * 100 + .5));

			m_grid->outputGrid(filename);	// Dump info for doing 3D cube plots
		}

		if (m_params.outputSave)
		{
			m_grid->outputSAData();	// Dump volume vs surface area data
		}

		if (progress != last_progress)
		{
			std::chrono::duration<double> duration = std::chrono::system_clock::now() - before;
			float percentComplete = (float)m_grid->getRemovedCount() / (float)m_grid->getInitialVolume();
			float estimatedTime = (1.0 / percentComplete)*duration.count();
			std::string message;
#ifdef WANT_FRAGMENTATION
			if (m_params.enableFrag)
				message = format("Estimated: %.2lf(s) - Elapsed: %.2lf(s) - Remaining: %.2lf(s) - Fragments: %d\n", estimatedTime, duration.count(), estimatedTime - duration.count(), fragments);
			else
#endif// #ifdef WANT_FRAGMENTATION
				message = format("Estimated: %.2lf(s) - Elapsed: %.2lf(s) - Remaining: %.2lf(s)\n", estimatedTime, duration.count(), estimatedTime - duration.count());

			updateProgress(message, progress);
		
			last_progress = progress;
		}
		refreshDisplay();	// Do display update

		Threshhold += m_params.outputInc;		// Increment by output increment after each dump
		if (Threshhold > (m_params.outputEnd + m_params.outputInc))	// Reached end of processing 
		{	
			break;
		}

		if (m_params.pauseOnInc)
		{
			wxCommandEvent* event = new wxCommandEvent(wxEVT_THREAD_STATE_EVENT);
			wxQueueEvent(this, event);	// Send thread pause request event to the main GUI frame
			m_paused = true;	// Keep this loop from doing any work
		}
	}
		// Stop timing now
	std::chrono::duration<double> duration = std::chrono::system_clock::now() - before;
	message = format("Consuming Elapsed Time: %.3lf(s)\n", duration.count());
	sendMessage(message);

	if (m_terminating)	// Program is closing, don't bother doing anything else.
	{
		return;
	}

	doneProgress();
	if (GlobalStatusBar)
		GlobalStatusBar->setStatusText("Done");

#ifdef WANT_FRAGMENTATION
	if (m_params.enableFrag)
	{
		m_grid->detectFragments();
		
		if (m_params.outputSaveFrags)
		{
			sprintf(filename, "%s\\%sFrags%dx%dx%d_%d.txt", outputDir.c_str(), m_params.cuboid ? "Cuboid" : "Ellipsoid", m_params.xdim, m_params.ydim, m_params.zdim, (int)(Threshhold * 100 + .5));
			m_grid->outputFragments(filename);
		}
	}
#endif// #ifdef WANT_FRAGMENTATION
	if (m_params.outputSaveGrid)
	{
		if (m_params.aggregateEnable)
			sprintf(filename, "%s\\%sGrid%dx%dx%dp%ds%d_%d.txt", outputDir.c_str(), m_params.cuboid ? "Cuboid" : "Ellipsoid", m_params.xdim, m_params.ydim, m_params.zdim, (int)(m_params.porosity * 100 + .5), m_params.particleSize, (int)(Threshhold * 100 + .5));
		else
			sprintf(filename, "%s\\%sGrid%dx%dx%dp%ds_%d.txt", outputDir.c_str(), m_params.cuboid ? "Cuboid" : "Ellipsoid", m_params.xdim, m_params.ydim, m_params.zdim, (int)(m_params.porosity * 100 + .5), (int)(Threshhold * 100 + .5));

		m_grid->outputGrid(filename);	// Dump info for doing 3D cube plots
	}

	if (m_params.outputSave)
	{	
		m_grid->closeSAData();	// Finish write and close volume vs surface area data file
	}

	refreshDisplay();		// Do display update

	if (m_params.displayEnable)
	{	if (m_plot)
			m_plot->savePlot();		// Push previous 2D line plot to temp storage for subsequent display
	}
	m_consuming = false;	// Finished consuming (Disables frame capture) 

	if (m_done)	// User requested a Stop
	{	message = format("\n*** Run Terminated ***\n");
		sendMessage(message);
		m_need_prerun = true;
		return;
	}

	message = format("-------------- Run Complete (%d,%d,%d) ---------------\n", m_params.xdim, m_params.ydim, m_params.zdim);
	sendMessage(message);

	m_need_prerun = true;
	m_done = true;
}

