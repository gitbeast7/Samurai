/*-----------------------------------------------------------------------------------
	MIT License

	Copyright 2021 Robert L Eastwood

	Permission is hereby granted, free of charge, to any person obtaining a copy of this
	software and associated documentation files (the "Software"), to deal in the Software
	without restriction, including without limitation the rights to use, copy, modify,
	merge, publish, distribute, sublicense, and/or sell copies of the Software, and to
	permit persons to whom the Software is furnished to do so, subject to the following
	conditions:

	The above copyright notice and this permission notice shall be included in all copies
	or substantial portions of the Software.

	THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
	INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
	PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
	FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
	OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
	DEALINGS IN THE SOFTWARE.
------------------------------------------------------------------------------------*/

#pragma once
#include <vector>
#include <string>
#include <map>

#include "robin_hood.h"	// Fast and memory efficient hash table

#ifdef HAS_WXWIDGETS			// Uses wxWidgets GUI framework
#define NEED_THREAD_PROTECTION	// GUI version is multi-threaded
class GLDisplayContext3D;		// OpenGL context and canvas for model visualization
class GLDisplayCanvas;
#endif 

// Conditional Compilation
//#define WANT_FRAGMENTATION	// Uncomment for fragmentation detection
//#define WANT_INPUT_CONTROL	// Uncomment for importing designer objects (experimental)
//#define RANDOM_REMOVAL		// Uncomment for naive removal support (can remove regardless of surface exposure)
//#define USE_CUBE_MAP			// Uncomment for cube map use (instead of fixed array)

#ifdef NEED_THREAD_PROTECTION	// Needs critical sections due to multi-threading
#include "PortCriticalSection.h"
#endif 

// typedefs
struct Cube;	// Forward declaration

typedef unsigned long long Dim_t;		// Dimension type (grid, layer, row, surface area, volume, etc.)
typedef unsigned long long Key_t;		// Key type (cube lookup)
typedef unsigned long Info_t;			// Information type (cube state bits)
typedef std::vector<Key_t> CubeList;
typedef std::vector<Cube*> CubePtrs;
typedef robin_hood::unordered_flat_map<Key_t, Dim_t> CubeMap;
typedef robin_hood::unordered_flat_map<Cube*, Dim_t> CubePtrsMap;
typedef robin_hood::unordered_flat_map<uint32_t, CubePtrs> VectorMap;

#ifdef WANT_FRAGMENTATION
struct Fragment;	// Forward declaration
typedef robin_hood::unordered_flat_map<int, Fragment> FragmentMap;
#endif //#define WANT_FRAGMENTATION

// non-class member prototypes
extern std::string format(const char *fmt, ...);
extern void sendMessage(std::string& message);

// Configuration Parameters
struct CubeParams
{
		// Shape/Dimensions
	bool cuboid;
	unsigned long xdim, ydim, zdim;
		// Porosity Control
	bool	poreIsFixed;
	double	porosity;
	unsigned long poreSize;
	bool	poreIsCuboid;
	bool	withReplacement;
#ifdef RANDOM_REMOVAL
	bool	naiveRemoval;
#endif //#ifdef RANDOM_REMOVAL

	bool	aggregateEnable;
	unsigned long particleSize;
	bool	replaceEnable;

		// Data Output Control
	double	outputInc;
	double	outputEnd;
	unsigned long nRuns;
	unsigned long outputSubsamp;
	unsigned long outputNSamps;
	std::string outputDir;
	bool	outputSave;
	bool	outputSaveGrid;
	bool	outputSaveInfo;
#ifdef WANT_INPUT_CONTROL
	std::string inputFile;
#endif //#ifdef WANT_INPUT_CONTROL
#ifdef HAS_WXWIDGETS	// Used only by GUI Application
		// Application Window Parameters
	unsigned long xsize, ysize;
	unsigned long xpos, ypos;
	unsigned long sashpos;
		// Display Control
	bool	displayEnable;
	bool	displayFaces;
	bool	pauseOnInc;
	bool	showOutlines;
	bool	showAxes;
	bool	saveGif;
	bool	saveFrames;
	unsigned long fps;
	bool	cubeView;
	double	rotationAngle;
#endif

#ifdef WANT_FRAGMENTATION
	// Fragment Control
	bool	enableFrag;
	bool	discardFrags;
	bool	animateFrags;
	bool	histFrags;
	bool	enableFragClass;
	unsigned long fragClass;
	bool	outputSaveFrags;
	unsigned long fragmentAt;
#endif //#ifdef WANT_FRAGMENTATION
};

// Stores surface area (nExposedFaces) after each cube is consumed (nCubesRemoved).
struct SAData
{
	Dim_t nCubesRemoved;		// Cubes consumed
	Dim_t nExposedFaces;		// Surface area
#ifdef RANDOM_REMOVAL
	Dim_t nTotalExposedFaces;	// Every currently exposed face (even hidden ones)
#endif //#ifdef RANDOM_REMOVAL
};

// Internal Cube parameters 
struct Cube
{
	// 32 bits = ffffffffffffffffffffffreeeeeeccc 
	//	f = frag 22 bits, r = removed 1 bit, e = exposed 6 bits, c = count 3 bits
	Info_t	info;	// Fragment Id + 
					// removed flag + 
					// bit set indicating presence of adjacent cube (0 if no adjacent cube) + 
					// exposed face count

#ifdef WANT_FRAGMENTATION
	int fragId;		// Previous fragment this cube was part of. (Used for fragment animation)
#endif //#ifdef WANT_FRAGMENTATION
};

//#ifdef WANT_INPUT_CONTROL

struct point3d {
	double x, y, z;
	int nNeighbors;
};

typedef std::vector<point3d> pointVect;

//#endif //#ifdef WANT_INPUT_CONTROL

#ifdef WANT_FRAGMENTATION
struct Fragment
{
	double cx, cy, cz;	// The centroid of the fragment
	double x, y, z;		// The position of the fragment
	double minX, maxX;
	double minY, maxY;
	double minZ, maxZ;
};
#endif //#ifdef WANT_FRAGMENTATION

// Map Key  
#define POSITION_SHIFT		(3)				// 64 bits total: 61 bits of cubes, 3 for face
#define FACE_MASK			(0x07ULL)
// State Info
#define FACE_COUNT_MASK		(0x00000007UL)	// Face count bits 
#define BITMASK_OFFSET		(0x00000008UL)
#define EXPOSED_MASK		(0x000001F8UL)	// 6 face state bits (exposed or hidden)
#define INFOMASK			(0x000003FFUL)	// Everything but the fragment id
#define SETVISIBLE			(0x00000200UL)	// 1 inserted state bit
#define CLRVISIBLE			(~SETVISIBLE)
// Fragment Id
#define FRAGMENTMASK		(~INFOMASK)		// Everything but the info bits
#define MAXFRAGS			(0x3FFFFF)
#define FRAGMENT_ID_SHIFT	(14)
// Misc.
#define MAX_COLOR_INDEX		(5)				// 0 - MAX_COLOR_INDEX colors available
#define POROSITY_PROCESSING_INC	(0.2)		// Show intermediate progress during porosity phase every POROSITY_PROCESSING_INC cubes removed
#define REMOVED				(0xFFFFFFFFFFFFFFFFULL)
// Macros
#define getPosition(x)		(x >> POSITION_SHIFT)
#define clearFaceBit(x, f)	(x &= ~(BITMASK_OFFSET << f))
#define setFaceBit(x, f)	(x |= (BITMASK_OFFSET << f))
#define visible(x)			(x & SETVISIBLE)
#define show(x)				(x |= SETVISIBLE)
#define hide(x)				(x &= CLRVISIBLE)

#define hasExposed(x)		(x & EXPOSED_MASK)
#define isExposed(x, f)		(x & (BITMASK_OFFSET << f))
#define getFace(x)			(x & FACE_MASK)
#define getFaceCount(x)		(x & FACE_COUNT_MASK)
#define clearFaceCount(x)	(x &= ~FACE_COUNT_MASK)
#define getFragmentId(x)	(x >> FRAGMENT_ID_SHIFT)
#define opFace(x)			(5 - x)		// Faces are numbered like a die (0 opposite 5, 1 opposite 4, etc.)

class MultiCube
{
public:
	MultiCube(CubeParams& params, bool* doneFlag, char* fname=NULL);
	~MultiCube();

	static void loadDefaults(CubeParams& params);

	// Consumer simulation
	bool consume(double& threshhold, int* progress=NULL);
	bool producePores(double& threshhold, int* progress);
	void finishPores();	// Free up pore data

	bool produceParticles(double& threshhold, int* progress);
	int getParticleCount() { return((int)m_aggPoints.size()); }

	// I/O
	bool openSAData(char* filename);
	void closeSAData();
	void outputSAData();
	void outputGrid(char* filename);
	void outputInfo(char* filename, int runCount);

	// Fragment handling
	void outputFragments(char* filename);

	int detectFragments(bool init= false);
	int discardFragments();

#ifdef WANT_FRAGMENTATION
	std::vector<double>		dhist;
#endif //#ifdef WANT_FRAGMENTATION

	// Misc
	Dim_t getVolume(Dim_t* surfaceArea = NULL);	// The current cube count of the object (+ exposed surface count if requested)
	Dim_t getInitialVolume();					// The cube count of the 3D object before processing
	Dim_t getRemovedCount();

	CubeParams	m_params;	// Configuration parameter interface to MultiCube class.

	std::vector<double>		x_cubesRemoved;
	std::vector<double>		y_surfaceArea;

#ifdef HAS_WXWIDGETS	// Used only by GUI Application
	friend GLDisplayContext3D;
	friend GLDisplayCanvas;

protected: 
	// Display helpers
	CubeParams& getParams() { return(m_params); }
	bool loadCubeListFromExposedList(CubePtrs& activeCubes);
	int getColorIndexAndPosition(Cube* cube, int& x, int& y, int& z);
#endif	// #ifdef HAS_WXWIDGETS

private:
	bool testDone() { return(m_doneFlag ? *m_doneFlag : false); }

	enum { UNINITIALIZED = 0, NUMFACES = 6 };

	void generateCuboid();
	Dim_t generateEllipsoid(int x0, int y0, int z0, int width, int height, int depth, bool remove=false, bool countOnly=false);
	void generateEllipse(int x0, int y0, int zpos, double zcomp, int width, int height, bool remove, bool countOnly);
#ifdef WANT_INPUT_CONTROL
	void importObject(char* fname);
#endif //#ifdef WANT_INPUT_CONTROL

	void initialize();
	void cleanup();

	// Consumer simulation
	void preprocess(char* fname);
	void resetExpectedVolume(Dim_t expectedVolume);
	void removePore(Cube* cube, int poreSize);
	void removeCube();
	void removeCube(Cube* cube);
	void removeCube(int x, int y, int z);
	void insertCube(int x, int y, int z, bool doUpdate=false);
	void deleteCube(Cube* cube);
#ifdef RANDOM_REMOVAL
	void naiveRemoveCube();
#endif //#ifdef RANDOM_REMOVAL
	void getBounds(int pos, int poreSz, int& start, int& end, int boundry);
	int getPoreSize(Dim_t cubesToRemove);

	// Exposed surface routines
	void initExposedFaceMap();
	void addFace(Cube* cube, int face);
	void removeFace(Key_t key);
	void insertFace(Cube* cube, int face, Cube* adjCube, bool doUpdate);
	static int exposedFaceCount(Cube* cube);
	uint64_t exposedFaceCount();
	int getExposedFaces(Cube* cube);

	void replaceCubes(int nCubes, bool excludeSurface=true);
	void addToCubeList(Cube* cube);
	void removeFromCubeList(Cube* cube);
	void setupCubeList();
	void updateCubeList();

	// Cube access
	Cube* getCube(int x, int y, int z);
	Cube* getCube(Key_t key);
	Dim_t getOffset(uint32_t x, uint32_t y, uint32_t z);
	Cube* getCubes() { return(m_Cubes); }

	// Utilities
	Dim_t getOffset(Cube* cube);
	Key_t genKey(Cube* cube, int face);
	Cube* getAdjacentCube(Cube* cube, int face);
	void id2pos(Cube* cube, int& x, int& y, int& z);	// Retrieve xyz position via cube pointer
	Dim_t getSize();				// The total cube count of the 3D grid (x*y*z)

	// Fragment handling
	void sortFragments(std::vector<CubePtrs*>& fragmentList);
	void initLabels();
	void assignFragmentIds(bool init);

#ifdef WANT_FRAGMENTATION
	void getHistogram();
	void flow_simulator();
	Fragment getBoundingBox(CubePtrs& cubeVec, Fragment* prevFrag);
#endif //#ifdef WANT_FRAGMENTATION

	// Member variables
	Cube*	m_Cubes;	// Contiguous cube space (NxNxN)

	Dim_t	m_gridSize;
	Dim_t	m_layerSize;
	Dim_t	m_rowSize;
	Dim_t	m_initialVolume;
	Dim_t	m_initialRemoved;
	Dim_t	m_maxSurfaceArea;
	Dim_t	m_cubesRemoved;
	Dim_t	m_insertionCount;
	Dim_t	m_particlesGenerated;

	double	m_ellipse_scalar;

	bool*	m_doneFlag;		// Used to test for user request to stop processing
	FILE*	m_saData_fp;	// For Surface Area data output
	Dim_t	m_lastRemoved;

#ifdef	NEED_THREAD_PROTECTION
	// Multi-thread access protection
	PortCriticalSection		m_listProtect;
#endif

	CubeMap						exposedMap;		// Exposed faces map
	CubeList					exposedList;	// Exposed faces list (for fast lookup on removal)
	CubePtrs					cubeList;		// Active cube list (for fast access for display)
#ifdef USE_CUBE_MAP
	CubePtrsMap					cubeMap;		// Provides fast access to cube list (avoids slow vector:find())
#else
	std::vector<Dim_t>			cubeListIndex;
#endif //#ifdef USE_CUBE_MAP
	CubeMap						surfaceMap;		// Exposed faces map for original surface of shape (used for block replacement)
	std::vector<SAData>			saData;	// Volume and Surface Area information acquired during consume() phase

	bool*					m_in_labels;
	uint32_t*				m_out_labels;

	VectorMap				fragments;		// Fragment list. (Stores vectors of cubes mapped by fragment id)
	std::vector<uint32_t>	fragmentSizes;

	pointVect				m_aggPoints;

#ifdef WANT_FRAGMENTATION
	FragmentMap			fragMap;		// List of fragment attributes

	int					m_lastFragmentId;
	int					m_lastIndex;
	Fragment			m_lastFragInfo;
#ifdef	NEED_THREAD_PROTECTION
	PortCriticalSection	m_fragProtect;
#endif
#endif //#ifdef WANT_FRAGMENTATION
};

class Aggregate
{
public:
	Aggregate(bool isCuboid, double xd, double yd, double zd, double pd);

	pointVect&	getParticles() { return(m_points); }
	void generateParticles(bool verbose=true);
	void fractalGeneration(pointVect& displayPoints, point3d cOffset, double xd, double yd, double zd, double pd, double& pSize);

	double		m_containerVolume;
	double		m_particleVolume;
	uint64_t	m_expected;

private:
	point3d generateParticle();
	bool validateParticle(point3d p, double pMag);

	bool		m_isCuboid;
	double		m_xd, m_yd, m_zd;
	double		m_xr, m_yr, m_zr;
	double		m_pd, m_pr;
	pointVect	m_points;
};
