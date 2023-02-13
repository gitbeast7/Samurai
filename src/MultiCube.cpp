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

#include "cc3d.hpp"		// Connected components labelling library

#include <stdio.h>
#include <stdarg.h>
#include <windows.h>
#include <time.h>

#define _USE_MATH_DEFINES
#include <math.h>

#include "MultiCube.h"

extern void sendMessage(std::string& message);

uint64_t xorState;
uint64_t xorshift64()
{
	xorState ^= xorState << 13;
	xorState ^= xorState >> 7;
	xorState ^= xorState << 17;
	return xorState;
}

void xsrand(uint64_t seed = 0xABADFEEDDEADBEEFULL)
{
	xorState = seed;
}

double xrand()
{
	return((double)xorshift64() / (double)ULLONG_MAX);
}

// Constructor for MultiCube class
// Creates a simple 3D data structure as a collection of unit cubes of width*height*depth
// Each cube face maintains its state based on the presense of an adjacent cube (set if exposed cleared if hidden by an adjacent cube).
MultiCube::MultiCube(CubeParams& params, bool* doneFlag, char* fname/*=NULL*/) :	m_params(params), m_doneFlag(doneFlag)
{
	// Initialize member variables
	m_saData_fp			= NULL;		// Info output file pointer
	m_lastRemoved		= -1;		// Used for output data flush
	m_initialVolume		= 0;
	m_initialRemoved	= 0;
	m_cubesRemoved		= 0;
	m_maxSurfaceArea	= 0;
	m_particlesGenerated= 0;
	m_Cubes				= NULL;
	m_in_labels			= NULL;
	m_out_labels		= NULL;

#ifdef WANT_FRAGMENTATION
	m_lastFragmentId	= -1;
	m_lastIndex			= -1;
	memset(&m_lastFragInfo, 0, sizeof(m_lastFragInfo));
	dhist.assign(6, 0);
#endif //#ifdef WANT_FRAGMENTATION

//#define VERIFY_SOURCE	// Uncomment if you wish the PRNG seed to always be the same. (Useful for verifing model after code changes.)
#ifdef VERIFY_SOURCE
	xsrand();		// Seed the pseudo-random number generator with fixed value
#else
	LARGE_INTEGER seed;
	QueryPerformanceCounter(&seed);
	xsrand(seed.QuadPart);		// Seed the pseudo-random number generator with the micro-second clock
#endif // VERIFY_SOURCE

	initialize();		// Allocate and initialize the grid
	preprocess(fname);	// Create requested shape (cuboid or ellipsoid) and load exposed faces structures
}

// MultiCube class destructor.
MultiCube::~MultiCube()
{
	cleanup();
}

// Free up all allocated memory.
void MultiCube::cleanup()
{
	if (m_in_labels)
		delete[] m_in_labels;

	if (m_out_labels)
		delete[] m_out_labels;

	if (m_Cubes)
		delete[] m_Cubes;
}

// Load configuration parameters with default values.
// Executed if the configuration .xml file is not present (e.g. the first time this app is run).
void MultiCube::loadDefaults(CubeParams& params)
{
	params.cuboid		= true;
	params.xdim			= 50;
	params.ydim			= 50;
	params.zdim			= 50;
	params.porosity		= 0.0;
	params.poreSize		= 3;
	params.poreIsFixed	= true;
	params.poreIsCuboid	= true;
	params.withReplacement = true;
#ifdef RANDOM_REMOVAL
	params.naiveRemoval = false;
#endif //#ifdef RANDOM_REMOVAL
	params.aggregateEnable = true;
	params.particleSize = 20;
	params.replaceEnable= true;
	params.outputInc	= 0.05;
	params.outputSubsamp= 1;
	params.outputNSamps = 0;
	params.nRuns		= 1;
	params.outputEnd	= 1.0;
	params.outputDir	= "OutputDir";
	params.outputSave	= false;
	params.outputSaveGrid = false;
#ifdef WANT_FRAGMENTATION
	params.outputSaveFrags = false;
	params.enableFrag	= false;
	params.discardFrags = false;
	params.animateFrags = false;
	params.histFrags	= false;
	params.enableFragClass = false;
	params.fragClass	= 0;
#endif //#ifdef WANT_FRAGMENTATION
#ifdef WANT_INPUT_CONTROL
	params.inputFile	= "";
#endif //#ifdef WANT_INPUT_CONTROL

#ifdef HAS_WXWIDGETS
	params.xsize		= 880;
	params.ysize		= 820;
	params.xpos			= 0;
	params.ypos			= 0;
	params.sashpos		= params.ysize / 2;
	params.displayEnable= true;
	params.displayFaces = true;
	params.showOutlines = false;
	params.showAxes		= false;
	params.pauseOnInc	= false;
	params.saveGif		= false;
	params.saveFrames	= false;
	params.fps			= 30;
	params.cubeView		= true;
#endif
}

void MultiCube::initialize()
{
	m_rowSize	= m_params.xdim;				// One row's worth of cubes
	m_layerSize = m_rowSize * m_params.ydim;	// One layer's worth of cubes	
	m_gridSize	= m_layerSize * m_params.zdim;	// Total number of cubes in the 3D grid

	// 3d arrays represented as 1d arrays
	m_Cubes = new Cube[m_gridSize]();			// Allocate the 3D grid

	if ((m_params.porosity > 0.0)
#ifdef WANT_FRAGMENTATION
		|| m_params.enableFrag
#endif //#ifdef WANT_FRAGMENTATION
		)
	{
		m_in_labels = new bool[m_gridSize]();			// Allocate the Fragmentation input labelling space
		m_out_labels = new uint32_t[m_gridSize]();		// Allocate the Fragmentation output labelling space
	}
}

int NCollisions = 0;

// Perform any preprocessing before the consume process.
void MultiCube::preprocess(char* fname)
{
	if (m_params.aggregateEnable)
	{
		NCollisions = 0;

		int x0 = m_params.xdim / 2;
		int y0 = m_params.ydim / 2;
		int z0 = m_params.zdim / 2;
		Dim_t containerVolume = generateEllipsoid(x0, y0, z0, m_params.xdim, m_params.ydim, m_params.zdim, false, true);
		std::string message = format("Container Volume %lld\n", containerVolume);
		sendMessage(message);

		double xdim = m_params.xdim;
		double ydim = m_params.ydim;
		double zdim = m_params.zdim;
		double pdim = m_params.particleSize;

		Aggregate aggregate(m_params.cuboid, xdim, ydim, zdim, pdim);
		pointVect points;
		point3d cOffset;
		cOffset.x = 0;
		cOffset.y = 0;
		cOffset.z = 0;
		if (false)
		{
			double pSize = pdim;
			aggregate.fractalGeneration(points, cOffset, xdim, ydim, zdim, pdim, pSize);
			pdim = pSize;
		}
		else
		{
			aggregate.generateParticles();
			points = aggregate.getParticles();
		}
		pointVect::iterator it = points.begin();
		while (it != points.end())
		{
			point3d& p = *it++;
			int x0 = (int)(p.x);
			int y0 = (int)(p.y);
			int z0 = (int)(p.z);
			generateEllipsoid(x0, y0, z0, (int)pdim, (int)pdim, (int)pdim);
			//break;
		}
		//detectFragments(true);
		m_particlesGenerated = points.size();
		double porosity = 1.0 - ((double)m_initialVolume / (double)containerVolume);
		message = format("Porosity %2.2lf\n", porosity);
		sendMessage(message);
	}
	else
	{
#ifdef WANT_INPUT_CONTROL
		if (fname)	// If an import file was provided load a designer object
		{
			importObject(fname);
		}
		else
#endif //#ifdef WANT_INPUT_CONTROL
		{
			if (m_params.cuboid)	// rectangular solid
				generateCuboid();
			else					// ellipsoid
			{
				int x0 = (int)round((double)m_params.xdim / 2.0);
				int y0 = (int)round((double)m_params.ydim / 2.0);
				int z0 = (int)round((double)m_params.zdim / 2.0);
				generateEllipsoid(x0, y0, z0, m_params.xdim, m_params.ydim, m_params.zdim);	// Ellipsoidal solid
			}
		}
	}

	initExposedFaceMap();	// Put all exposed faces on the exposed face map

	if (m_params.aggregateEnable && m_params.replaceEnable)
	{
		// Print initial volume and surface area of the remaining cubes in the grid
		std::string message = format("Initial Volume %lld Total Cubes : Surface Area %lld\n", m_initialVolume, m_maxSurfaceArea);
		sendMessage(message);

		replaceCubes(NCollisions, false);
		m_maxSurfaceArea = (int)exposedList.size();
	}

	// Print initial volume and surface area of the remaining cubes in the grid
	std::string message = format("Initial Volume %lld Total Cubes : Surface Area %lld\n", m_initialVolume, m_maxSurfaceArea);
	sendMessage(message);

	if ((m_params.porosity > 0.0) 
#ifdef RANDOM_REMOVAL
		|| m_params.naiveRemoval
#endif //#ifdef RANDOM_REMOVAL
#ifdef WANT_FRAGMENTATION
		|| m_params.enableFrag
#endif //#ifdef WANT_FRAGMENTATION
	)
	{
		setupCubeList();
	}
}

// Reserve space for the cube list and index
// and initialize them with all currently visible cubes
void MultiCube::setupCubeList()
{
	cubeList.reserve(m_gridSize);
#ifndef USE_CUBE_MAP
	cubeListIndex.resize(m_gridSize);
#endif //#ifndef USE_CUBE_MAP
	updateCubeList();
}

#ifdef USE_CUBE_MAP
void MultiCube::addToCubeList(Cube* cube)
{
	cubeMap[cube] = (Dim_t)cubeList.size();	// Store the cubeList vector index into the cubeMap lookup
	cubeList.push_back(cube);				// Add the cube to the vector
}

void MultiCube::removeFromCubeList(Cube* cube)
{
	// Retrieve the cube's cubeList index from the cubeMap lookup
	CubePtrsMap::iterator cit = cubeMap.find(cube);	
	if (cit == cubeMap.end())
		return;
	Dim_t index = cit->second;
	cubeMap.erase(cit);		// Remove it from the map
	// Instead of deleting the item from the cubeList vector directly
	// we simply copy the last item in the list to this item's index, overwriting the to-be-removed item
	// and then pop the last item off the list. This is MUCH more efficient!
	Cube* lastCube = cubeList.back();
	if (cube != lastCube)	// If it's already the last item on the list we simply pop it.
	{	// Replace the removed item with the last item
		cubeList[index] = lastCube;
		// Update the cubeMap's index for the just moved item
		cit = cubeMap.find(lastCube);	
		cit->second = index;
	}
	cubeList.pop_back();
}
#else
void MultiCube::addToCubeList(Cube* cube)
{
	uint64_t offset = cube - m_Cubes;
	cubeListIndex[offset] = (Dim_t)cubeList.size();		// Add the cube to the index vector
	cubeList.push_back(cube);				// Add the cube to the vector
}

void MultiCube::removeFromCubeList(Cube* cube)
{
	uint64_t offset = cube - m_Cubes;
	if (cubeListIndex[offset] == REMOVED)
		return;

	Dim_t index = cubeListIndex[offset];
	cubeListIndex[offset] = REMOVED;
	Cube* lastCube = cubeList.back();
	if (cube != lastCube)	// If it's already the last item on the list we simply pop it.
	{	// Replace the removed item with the last item
		cubeList[index] = lastCube;
		offset = lastCube - m_Cubes;
		cubeListIndex[offset] = index;
	}
	cubeList.pop_back();
}
#endif //#ifdef USE_CUBE_MAP

void MultiCube::updateCubeList()
{
	int64_t totalCubes = getSize();

	Cube* cubePtr = m_Cubes;
	while (totalCubes--)
	{
		if (visible(cubePtr->info))
		{
			addToCubeList(cubePtr);
		}
		++cubePtr;
	}
}

/****************************************************/
/* Cube removal/bookkeeping/manipulation routines	*/
/****************************************************/
void MultiCube::generateCuboid()
{
		// Attach all the pointers (and back pointers)
	for (int z = 0; z < (int)m_params.zdim; z++)
	{
		for (int y = 0; y < (int)m_params.ydim; y++)
		{
			for (int x = 0; x < (int)m_params.xdim; x++)
			{
				insertCube(x, y, z);
			}
		}
	}
}

Dim_t MultiCube::generateEllipsoid(int x0, int y0, int z0, int width, int height, int depth, bool remove/*=false*/, bool countOnly/* = false*/)
{
	m_insertionCount = 0;
	bool rpt = !(depth % 2);
	if (rpt) --depth;

	double zradius = (double)depth / 2.0;
	int zpos = (int)floor(z0 - zradius);
	int zA = (int)ceil(zradius - 1);
	int zB = (int)floor(-zradius);
	for (int z = zA; z > zB; z--)
	{
		if (!remove || (remove && ((zpos >= 0) && (zpos < (int)m_params.zdim))))
		{
			double zcomp = sqrt(1.0 - ((double)z / zradius)*((double)z / zradius));
			generateEllipse(x0, y0, zpos, zcomp, width, height, remove, countOnly);
		}
		++zpos;
		if (rpt && (z == 0))
		{
			++z;
			rpt = false;
		}
	}

	return(m_insertionCount);
}

void MultiCube::generateEllipse(int x0, int y0, int zpos, double zcomp, int width, int height, bool remove, bool countOnly)
{
	bool yrpt = !(height % 2);
	if (yrpt) --height;

	double yradius = ((double)height / 2.0)*zcomp;

	bool xrpt = !(width % 2);
	bool xrpt_org = xrpt;
	if (xrpt) --width;

	double xradius = ((double)width / 2.0)*zcomp;

	int ypos = (int)floor(y0 - yradius);
	int yA = (int)ceil(yradius - 1);
	int yB = (int)floor(-yradius);
	for (int y = yA; y > yB; y--)
	{
		if (!remove || (remove && ((ypos >= 0) && (ypos < (int)m_params.ydim))))
		{
			xrpt = xrpt_org;
			double yr = (double)y / yradius;
			double xcomp = sqrt(1.0 - yr * yr) * xradius;		// for ellipses
			//double xcomp_circ = sqrt(yradius*yradius-y*y);	// for circles
			int xpos = (int)floor(x0 - xcomp);
			int xA = (int)ceil(xcomp - 1);
			int xB = (int)floor(-xcomp);
			for (int x = xA; x > xB; x--)
			{
				if (remove)
				{
					if ((xpos >= 0) && (xpos < (int)m_params.xdim))
						removeCube(xpos, ypos, zpos);	// remove used by pore creation
				}
				else
				{
					++m_insertionCount;
					if (!countOnly)
						insertCube(xpos, ypos, zpos);
				}
				++xpos;
				if (xrpt && (x == 0))
				{
					++x;
					xrpt = false;
				}
			}
		}
		++ypos;
		if (yrpt && (y == 0))
		{
			++y;
			yrpt = false;
		}
	}
}

// Initialize the exposed face map to all the initially exposed faces.
// These are simply the faces on outer surface of the cubes that make up the 3D grid object
void MultiCube::initExposedFaceMap()
{
	Cube* cube = m_Cubes;
	Dim_t totalCubes = getSize();
	while (totalCubes--)
	{
		if (visible(cube->info))
		{
			int face = NUMFACES;
			while (face--)
			{
				if (isExposed(cube->info, face))
				{
					addFace(cube, face);
				}
			}
		}
		++cube;
	}
	surfaceMap = exposedMap;	// Copy the exposed map for later use replacing cubes (see replaceCubes())

	m_maxSurfaceArea = (int)surfaceMap.size();
}

// Returns the maximum size of the 3D grid
Dim_t MultiCube::getSize()
{
	return(m_gridSize);
}

// The cube count of the 3D object before processing
Dim_t MultiCube::getInitialVolume()
{ 
	return(m_initialVolume); 
}

Dim_t MultiCube::getRemovedCount()
{
	return(m_cubesRemoved);
}

void MultiCube::id2pos(Cube* cube, int& x, int& y, int& z)
{
	Dim_t offset = getOffset(cube);
	lldiv_t zresult = std::lldiv(offset, m_layerSize);
	z = (int)zresult.quot;
	lldiv_t yresult = std::lldiv(zresult.rem, m_rowSize);
	y = (int)yresult.quot;
	x = (int)yresult.rem;
}

// Returns a cube pointer by its 3D coordinates
Cube* MultiCube::getCube(int x, int y, int z)
{
	Dim_t offset = z * m_layerSize + y * m_rowSize + x;
	return(m_Cubes + offset);
}

Dim_t MultiCube::getOffset(uint32_t x, uint32_t y, uint32_t z)
{
	return(z * m_layerSize + y * m_rowSize + x);
}

// Returns a cube pointer by its cube id
Cube* MultiCube::getCube(Key_t key)
{
	return(m_Cubes + getPosition(key));
}

Dim_t MultiCube::getOffset(Cube* cube)
{
	return((Dim_t)(cube - m_Cubes));
}

Key_t MultiCube::genKey(Cube* cube, int face)
{
	return((getOffset(cube) << POSITION_SHIFT) | face);
}

Cube* MultiCube::getAdjacentCube(Cube* cube, int face)
{
	switch (face) {
	case 0:
		cube -= m_layerSize; // Down
		break;
	case 1:
		--cube; // Back
		break;
	case 2:
		cube -= m_rowSize; // Right
		break;
	case 3:
		cube += m_rowSize; // Left
		break;
	case 4:
		++cube; // Front
		break;
	case 5:
		cube += m_layerSize; // Up
		break;
	}

	return(cube);
}

// Add a face to the exposed face map and list
void MultiCube::addFace(Cube* cube, int face)
{
	// Set the cube's face bit flag (indicates the face is now exposed)
	setFaceBit(cube->info, face);

	// Generate the key for quick lookup from the exposed faces map and list
	Key_t key = genKey(cube, face);

	// Add the index of the newly exposed face to the exposed face map
	exposedMap[key] = (Key_t)exposedList.size();

	// Add to the exposed faces list as well
#ifdef HAS_WXWIDGETS
	if (m_params.displayEnable)	// Only need display thread protection when the display is enabled
	{
#ifdef NEED_THREAD_PROTECTION
		PortCriticalSection::AutoLock autolock(m_listProtect);	// Protect the cube list from display thread
#endif
		exposedList.push_back(key);
	}
	else
#endif //#ifdef HAS_WXWIDGETS
		exposedList.push_back(key);
}

// Remove a face from the exposed face map and list
void MultiCube::removeFace(Key_t key)
{
	// Remove the to-be-removed cube's face from the exposed faces map			
	// Remove the face from the exposed face list as well.
	// 1) Find the index in the exposed list using the exposed map's index
	// 2) Replace the to be erased item with the one from the end of the list
	// 3) Reset the index for the reassigned cube
	// 4) Pop off the end of the vector
	// This is much quicker then removing the item from the middle of the vector. Avoids big copies.
	CubeMap::iterator it = exposedMap.find(key);
	if (it == exposedMap.end())
		return;	// Not on the map. Nothing to do.
	Key_t lastKey = exposedList.back();
	if (lastKey != key)
	{
		CubeMap::iterator nit = exposedMap.find(lastKey);
		nit->second = it->second;
		exposedList[it->second] = lastKey;
	}
	exposedMap.erase(it);

#ifdef HAS_WXWIDGETS
	if (m_params.displayEnable)	// Only need display thread protection when the display is enabled
	{
#ifdef NEED_THREAD_PROTECTION
		PortCriticalSection::AutoLock autolock(m_listProtect);	// Protect the cube list from display thread
#endif
		exposedList.pop_back();
	}
	else
#endif //#ifdef HAS_WXWIDGETS
		exposedList.pop_back();
}

// Deletes a cube from the active cube list and set its removed flag to true
void MultiCube::deleteCube(Cube* cube)
{
	hide(cube->info);	// No longer "visible"
	if (!cubeList.empty())
		removeFromCubeList(cube);
}

// Removes a cube from the 3D grid.
void MultiCube::removeCube(Cube* cube)
{
	if (cube == NULL)
	{
		// Retrieve the to-be-removed cube's id using the randomly selected index into the exposed face list
		Dim_t index = (Dim_t)(xrand() * exposedList.size());
		cube = getCube(exposedList[index]);
	}

		// Go through the faces of the cube and "expose" the faces of any adjacent cubes
		// This is done by setting the adjacent cube's face state (face bit = 1).
		// The face is then added to the exposed face map.
		// Finally the to-be-removed cubes faces are removed from the map and the cube is flagged as "removed"
		// Note: A list of the to-be-removed cubes is created to avoid recursive calls
		//		 which can blow the stack if the pores are too large
	CubePtrs tbrCubes;	// To be removed cubes
	tbrCubes.push_back(cube);	// Always at least one cube to be removed
	int cubeIndex = 0;
	while (cubeIndex < tbrCubes.size())
	{
		Cube* cube = tbrCubes[cubeIndex];
		int face = NUMFACES;
		while(face--)
		{
			Cube* adjCube = isExposed(cube->info, face) ? NULL : getAdjacentCube(cube, face);
			if (adjCube)	// Face is hidden (i.e. has adjacent cube)
			{
				if (!visible(adjCube->info))
				{		// If the adjacent cube was already removed we need to handle
						// the possibility that the newly exposed face is due to the cube's removal
						// Note: this only happens when porosity is enabled
					setFaceBit(adjCube->info, opFace(face));	// Detach adjacent cube's face from the cube's face
					show(adjCube->info);						// Prevent reprocessing of the to-be-removed cube
					tbrCubes.push_back(adjCube);
				}
				else	// Simply add the newly exposed face of the adjacent cube to the list
					addFace(adjCube, opFace(face));
			}
			else	// No adjacent cube, simply remove the cube's face from the list
				removeFace(genKey(cube, face));
		}

		deleteCube(cube);	// Remove cube from active cube list
		++cubeIndex;
	}
}

// Removes a cube from the 3D grid.
void MultiCube::removeCube()
{
	// Retrieve the to-be-removed cube using the randomly selected index into the exposed face list
	Dim_t index = (Dim_t)(xrand() * exposedList.size());
	Cube* cube = getCube(exposedList[index]);
	int face = NUMFACES;
	while (face--)
	{
		if (isExposed(cube->info, face))	// No adjacent cube, simply remove the cube's face from the list
			removeFace(genKey(cube, face));
		else	// Face is hidden (i.e. has adjacent cube) Add adjacent cube's newly exposed face
			addFace(getAdjacentCube(cube, face), opFace(face));
	}

	deleteCube(cube);	// Mark as removed (no longer "visible") and remove cube from active cube list if used
}

// Attaches the faces of two adjacent cubes and if doUpdate is true,
// inserts the face onto the exposed face map and list
void MultiCube::insertFace(Cube* cube, int face, Cube* adjCube, bool doUpdate)
{
	if (adjCube && visible(adjCube->info))
	{
		int adjFace = opFace(face);
			// Attach the faces (by hiding opposing faces)
		clearFaceBit(cube->info, face);
		clearFaceBit(adjCube->info, adjFace);
		if (doUpdate)
		{	// Remove the exposed face
			removeFace(genKey(adjCube, adjFace));
		}
		return;
	}

	if (doUpdate)
	{	// Add the newly exposed face
		addFace(cube, face);
		return;
	}
	setFaceBit(cube->info, face);	// Indicate exposed face
}

// Inserts a cube into the 3D grid at (x,y,z)
// If doUpdate is true, any adjacent cubes that would
// be affected by the insert are adjusted.
void MultiCube::insertCube(int x, int y, int z, bool doUpdate/*=false*/)
{
//	if ((x < 0) || (y < 0) || (z < 0))
//		return;
//	if ((x >= m_params.xdim) || (y >= m_params.ydim) || (z >= m_params.zdim))
//		return;

	Cube* cube = getCube(x, y, z);

	if (visible(cube->info))
	{
		NCollisions++;
		return;	// Already visible in the grid - nothing to do
	}

	show(cube->info);	// Make cube visible in the grid
	Cube* adjCube = NULL;
	
	if (x > 0)
		adjCube = getAdjacentCube(cube, 1);
	else
		adjCube = NULL;
	insertFace(cube, 1, adjCube, doUpdate);

	if (y > 0)
		adjCube = getAdjacentCube(cube, 2);
	else
		adjCube = NULL;
	insertFace(cube, 2, adjCube, doUpdate);

	if (z > 0)
		adjCube = getAdjacentCube(cube, 0);
	else
		adjCube = NULL;
	insertFace(cube, 0, adjCube, doUpdate);

	if (x < (int)m_params.xdim - 1)
		adjCube = getAdjacentCube(cube, 4);
	else
		adjCube = NULL;
	insertFace(cube, 4, adjCube, doUpdate);

	if (y < (int)m_params.ydim - 1)
		adjCube = getAdjacentCube(cube, 3);
	else
		adjCube = NULL;
	insertFace(cube, 3, adjCube, doUpdate);

	if (z < (int)m_params.zdim - 1)
		adjCube = getAdjacentCube(cube, 5);
	else
		adjCube = NULL;
	insertFace(cube, 5, adjCube, doUpdate);

	++m_initialVolume;
}

// Make sure a "pore" doesn't exceed the bounds of the 3D grid
void MultiCube::getBounds(int pos, int poreSz, int& start, int& end, int boundry)
{
	int offset = poreSz / 2;
	--poreSz;
	start = pos - offset;
	if (start < 0)
		start = 0;
	end = start + poreSz;
	if (end >= boundry)
	{	end = boundry - 1;
		start = end - poreSz;
	}
}

// Remove a cube from the 3D grid
// This simulates consuming 1 element
void MultiCube::removeCube(int x, int y, int z)
{
	Cube* cube = getCube(x, y, z);
	if (!visible(cube->info))
		return;	// Cube outside grid or already removed. Nothing to do

	if (hasExposed(cube->info))	// Faces exposed during cube removal. Need to remove it properly.
		removeCube(cube);
	else
		deleteCube(cube);		// Simply remove from active cube list
}

// Remove a pore sized collection of cubes from the 3D grid
// The cube parameter is at the centre of the pore
void MultiCube::removePore(Cube* cube, int poreSize)
{
	if (poreSize == 1)	// Simple single cube removal
	{
		if (!visible(cube->info))
			return;	// Cube already removed. Nothing to do

		if (!hasExposed(cube->info))
			deleteCube(cube);	// Not on exposed list; do a simple delete
		else
			removeCube(cube);	// Remove the cube

		return;
	}

	int xpos, ypos, zpos;
	id2pos(cube, xpos, ypos, zpos);

	if (!m_params.poreIsCuboid)	// Check to see if we want spherical pores
	{
		if (poreSize > 2)
		{
			generateEllipsoid(xpos, ypos, zpos, poreSize, poreSize, poreSize, true);
			return;
		}
	}

	CubePtrs cubes;
	cubes.reserve(poreSize * poreSize * poreSize);
	int xStart, xEnd, yStart, yEnd, zStart, zEnd;
	getBounds(xpos, poreSize, xStart, xEnd, m_params.xdim);
	getBounds(ypos, poreSize, yStart, yEnd, m_params.ydim);
	getBounds(zpos, poreSize, zStart, zEnd, m_params.zdim);
	for (int z = zStart; z <= zEnd; z++)
	{
		for (int y = yStart; y <= yEnd; y++)
		{
			for (int x = xStart; x <= xEnd; x++)
			{
				Cube* cube = getCube(x, y, z);
				if (!visible(cube->info))
					continue;	// Cube already removed. Nothing to do

				if (!hasExposed(cube->info))
					deleteCube(cube);		// Not on exposed list; do a simple delete
				else
					cubes.push_back(cube);	// Only push cubes that need to be removed
			}
		}
	}
	// Do the cube removals all at once
	CubePtrs::iterator it = cubes.begin();
	while (it != cubes.end())
		removeCube(*it++);
}

void MultiCube::resetExpectedVolume(Dim_t expectedVolume)
{
	std::string message;

	Dim_t surfaceArea;
	Dim_t volume = getVolume(&surfaceArea);

	// Replace any missing cubes caused by porosity and fragmentation
	int missingCubes = (int)(expectedVolume - volume);
	if (missingCubes)
	{
		replaceCubes(missingCubes);
		
		//detectFragments(true);	// Fragmentation Verification (Must return 1 - If not something went wrong)

		volume = getVolume(&surfaceArea);
		message = format("After Replacing %d Cubes - %lld Total Cubes - %lld exposed faces\n", missingCubes, volume, surfaceArea);
		sendMessage(message);
	}

#ifdef WANT_FRAGMENTATION
	if (!m_params.enableFrag)
	{
		delete [] m_in_labels;	// No longer needed
		m_in_labels = NULL;
		delete[] m_out_labels;	// No longer needed
		m_out_labels = NULL;
	}
#endif //#ifdef WANT_FRAGMENTATION
}

// Reinserts cubes that were removed due to fragmentation or porosity
// This is necessary when consistent/reproducable starting volumes are important
// Note: We prevent the replacement of cubes on original surface faces. 
// i.e. the shape will never exceed its original boundaries.
void MultiCube::replaceCubes(int nCubes, bool excludeSurface/*=true*/)
{
	std::string message = format("Replacing %d cubes\n", nCubes);
	sendMessage(message);

	int origExposedFaces = (int)exposedList.size();		// Number of exposed faces originally available.
	while (nCubes--)
	{
		int exposedFaces = (int)exposedList.size();		// Number of exposed faces currently available.
		Key_t key;
		// We don't replace cubes adjacent to surface faces.
		// This would exceed the object's boundary
		int watchdog_count = 0;
		while (watchdog_count++ < exposedFaces)
		{
			// Pick a face at random
			Dim_t index = (Dim_t)(xrand() * exposedFaces);
			key = exposedList[index];

			if (excludeSurface)
			{	// Make sure the face isn't a surface face
				if (surfaceMap.find(key) == surfaceMap.end())
				{
					surfaceMap[key] = (int)surfaceMap.size();
					break;		// Not on surface map, we're done.
				}
			}
			else
				break;	// We accept any face (including surface faces)
		}
		if (watchdog_count >= exposedFaces)
		{	
			message = format("Unable to replace all cubes - %d left\n", nCubes);
			sendMessage(message);
			return;	// Unable to fullfill replacement request
		}

		int xpos, ypos, zpos;
		Cube* cube = getCube(key);
		id2pos(cube, xpos, ypos, zpos);
		// Select the adjacent cube using the face
		// NOTE: Boundary checks are no longer necessary due to the non-surface face criteria imposed above.
		switch (getFace(key)) {
		case 0:
			--zpos;
			break;
		case 5:
			++zpos;
			break;
		case 1:
			--xpos;
			break;
		case 4:
			++xpos;
			break;
		case 2:
			--ypos;
			break;
		case 3:
			++ypos;
			break;
		}

		insertCube(xpos, ypos, zpos, true);
		cube = getCube(xpos, ypos, zpos);
		if (excludeSurface)
			addToCubeList(cube);
	}

	surfaceMap.clear();		// Free up the surface map - no longer necessary

	int newlyExposed = (int)exposedList.size() - origExposedFaces;
	message = format("Replaced cubes - %d newly exposed faces\n", newlyExposed);
	sendMessage(message);
}

// Retrieve the pore's dimensions
int MultiCube::getPoreSize(Dim_t cubesToRemove)
{
	// If not "poreIsFixed" create a "pore" of a random size between 1 and maxPoreSize
	// Else it's just "poreSize"
	uint32_t poreSize = m_params.poreIsFixed ? m_params.poreSize : (uint32_t)(xrand()*m_params.poreSize) + 1;
	// Detect the case where the pore is bigger than what we want removed
	// If so, we set the poreSize to the largest possible for the remaining cubes
	uint32_t maxPoreSize = poreSize * poreSize * poreSize;	// a cube of poreSize dimensions
	if (maxPoreSize > cubesToRemove)
		poreSize = (int)std::cbrt(cubesToRemove);

	return(poreSize);
}

#ifdef RANDOM_REMOVAL
void MultiCube::naiveRemoveCube()
{
	// Select a random location for the pore to be removed
	Dim_t index = (Dim_t)(xrand()*cubeList.size());
	Cube* cube = cubeList[index];
	if (!visible(cube->info))
		return;	// Cube already removed. Nothing to do

	if (!hasExposed(cube->info))
		deleteCube(cube);	// Not on exposed list; do a simple delete
	else
		removeCube(cube);	// Remove the cube
}
#endif //#ifdef RANDOM_REMOVAL

// Randomly removes collections of cubes (aka pores) from the 3D grid cubes.
//	porosity - how many total cubes are removed. (e.g. 30% porosity = 30% of total cubes are removed)
//	maxPoreSize - largest pore generated (pores are cubes of maxPoreSize x maxPoreSize x maxPoreSize)
//	randPoreSize - (en/dis)ables random pore size generation (1 - maxPoreSize)
bool MultiCube::producePores(double& threshhold, int* progress)
{
	Dim_t cubesToRemove = (Dim_t)round(m_initialVolume * m_params.porosity);
	int poreSize = getPoreSize(cubesToRemove);	// Get initial pore dimensions
	m_cubesRemoved = m_initialVolume - (Dim_t)cubeList.size();
	while ((m_cubesRemoved < cubesToRemove) && !testDone())
	{
		// Select a random location for the pore to be removed
		Dim_t index = (Dim_t)(xrand()*cubeList.size());
		// Get pore dimensions if poresize can vary
		if (!m_params.poreIsFixed)
			poreSize = getPoreSize(cubesToRemove);
		// Remove the pore
		removePore(cubeList[index], poreSize);
		m_cubesRemoved = m_initialVolume - (Dim_t)cubeList.size();
		double ratio_removed = (double)m_cubesRemoved / (double)cubesToRemove;
		if (ratio_removed >= threshhold)
		{
			// In cases where more cubes are removed than what would occur
			// during the next threshhold we adjust the threshhold to the 
			// next increment.
			if (ratio_removed > threshhold)
			{
				while (ratio_removed > threshhold)
					threshhold += POROSITY_PROCESSING_INC;
				threshhold -= POROSITY_PROCESSING_INC;
			}
			if (progress)
				*progress = (int)round(ratio_removed * 100.0);

			return(true);
		}
	}

	if (progress)
		*progress = (int)round(((double)m_cubesRemoved / (double)cubesToRemove)*100.0);

	Dim_t surfaceArea;
	Dim_t volume = getVolume(&surfaceArea);
	std::string message = format("After Removal %lld Total Cubes - %lld exposed faces\n", volume, surfaceArea);
	sendMessage(message);

	// Remove any fragments that might have been created during the porosity phase
	detectFragments(true);
	int fragmentCubesRemoved = discardFragments();
	if (fragmentCubesRemoved)
	{
		volume = getVolume(&surfaceArea);
		message = format("After fragment removal: Discarded %d Cubes - %lld Total Cubes - %lld exposed faces\n", fragmentCubesRemoved, volume, surfaceArea);
		sendMessage(message);
	}

	if (m_params.withReplacement)
	{	// If we removed more cubes than requested due to
		// pore size or fragmentation, replace them now.
		if (m_cubesRemoved != cubesToRemove)
		{
			Dim_t expectedVolume = m_initialVolume - cubesToRemove;

			message = format("Expected volume %lld cubes\n", expectedVolume);
			sendMessage(message);

			resetExpectedVolume(expectedVolume);
		}
	}

	// Reset initial volume of the remaining cubes in the grid
	m_initialVolume = getVolume(&surfaceArea);
	m_maxSurfaceArea = surfaceArea;

	m_initialRemoved = 0;				// Don't bias the plot by the porosity.
	//m_initialRemoved = m_cubesRemoved;// Bias the plot by the porosity.

	return(false);	// All pores have been "produced"
}

// Free up pore storage if we're done with it.
void MultiCube::finishPores()
{
#ifdef WANT_FRAGMENTATION
	if (!m_params.enableFrag)
#endif //#ifdef WANT_FRAGMENTATION
	{
		if (!(m_params.porosity > 0.0)
#ifdef RANDOM_REMOVAL
			&& !m_params.naiveRemoval
#endif //#ifdef RANDOM_REMOVAL
		)
		{
			cubeList.clear();
#ifdef USE_CUBE_MAP
			cubeMap.clear();
#else
			cubeListIndex.clear();
#endif //#ifdef USE_CUBE_MAP
		}
		fragments.clear();
		if (m_in_labels)
			delete[] m_in_labels;
		m_in_labels = NULL;
		if (m_out_labels)
			delete[] m_out_labels;
		m_out_labels = NULL;
	}
	m_cubesRemoved = 0;	// Reset for normal consuming
}

// This routine initializes the fragmentation detection routines (the labelling library)
// Each cube present in the current shape is set to "true" in the label space.
void MultiCube::initLabels()
{
	if (m_in_labels == NULL)
		m_in_labels = new bool[m_gridSize]();
	else
		memset((void*)m_in_labels, 0, m_gridSize * sizeof(bool));

	if (m_out_labels == NULL)
		m_out_labels = new uint32_t[m_gridSize]();
	else
		memset((void*)m_out_labels, 0, m_gridSize * sizeof(uint32_t));

	CubePtrs::iterator it = cubeList.begin();
	while (it != cubeList.end())
	{
		Cube* cube = *it++;
		m_in_labels[getOffset(cube)] = true;
	}
}

void MultiCube::assignFragmentIds(bool init)
{
	// Initialize the label state for all cubes on the Cube List
	initLabels();

	// Run the labeller (all cubes in the label space are checked for connectivity)
	// The labeller assigns an id number to each item in the label space indicating which component/fragment it belongs to.
	size_t n_labels = 0;
	int64_t connectivity = 6;	// Default is faces
#ifdef WANT_FRAGMENTATION
	switch (m_params.fragmentAt) {
		case 0 : connectivity = 6; break;	// Faces	
		case 1 : connectivity = 18; break;	// Edges	
		case 2 : connectivity = 26; break;	// Verts
	}
#endif //#ifdef WANT_FRAGMENTATION
	cc3d::connected_components3d<bool>(m_in_labels, m_params.xdim, m_params.ydim, m_params.zdim, MAXFRAGS, connectivity, m_out_labels, n_labels);

	fragmentSizes.assign(n_labels + 1, 0);
	fragments.clear();	// Clear any old fragments

	if (n_labels == 1)
		return;			// No fragmentation detected (i.e. everything is part of one object)

	// Once the labelling is complete, collect all cubes
	// with the same id and store them in a vector.
	// Each vector will then contain all the cubes of a fragment
	CubePtrs::iterator it = cubeList.begin();
	while (it != cubeList.end())
	{
		Cube* cube = *it++;
		Dim_t offset = getOffset(cube);
#ifdef WANT_FRAGMENTATION
		if (m_params.animateFrags)
			cube->fragId = getFragmentId(cube->info);	// Previous fragment Id
#endif //#ifdef WANT_FRAGMENTATION
		uint32_t fragment = m_out_labels[offset];
		++fragmentSizes[fragment];
		cube->info = ((Info_t)fragment << FRAGMENT_ID_SHIFT) | (cube->info & ~FRAGMENTMASK);

#ifdef WANT_FRAGMENTATION
		if (init || m_params.outputSaveFrags || m_params.discardFrags || m_params.animateFrags || m_params.histFrags)
#endif //#ifdef WANT_FRAGMENTATION
		{
			VectorMap::iterator fit = fragments.find(fragment);
			if (fit == fragments.end())
			{		// Create a new fragment vector
				CubePtrs cubeVect;
				cubeVect.push_back(cube);
				fragments[fragment] = cubeVect;
			}
			else
			{		// Add cube to existing fragment vector
				CubePtrs& cubeVect = fit->second;
				cubeVect.push_back(cube);
			}
		}
	}
}

// Wraps the Connected Component 3d library (cc3d.hpp).
// 1) load cubes in cube list (all active cubes) into cc3d label array
// 2) run the cc3d labeller - labels each fragment detected with a fragment label
// 3) scan the newly labelled fragments to set the cube fragment id
// 4) compute the size of each fragment for display
// 5) collect cubes of each distinct fragment into their own vector (for culling and output)
int MultiCube::detectFragments(bool init/*= false*/)
{
#ifdef WANT_FRAGMENTATION
	m_lastFragmentId= -1;
	m_lastIndex		= -1;
	memset(&m_lastFragInfo, 0, sizeof(m_lastFragInfo));
#endif //#ifdef WANT_FRAGMENTATION
	
		// Assign the label to the cube's fragment ID
	assignFragmentIds(init);

#ifdef WANT_FRAGMENTATION
	if (!init)
	{
		dhist.assign(6, 0);
		getHistogram();

		// Run flow simulator on fragments
		if (m_params.animateFrags)
			flow_simulator();
	}
#endif //#ifdef WANT_FRAGMENTATION

/*
	// Send some status messages
	if (fragmentSizes.size() > 1)
	{
		std::string message = format("%d Fragment Sizes.\n", (int)fragmentSizes.size());
		sendMessage(message);
	}
	if (fragments.size() > 1)
	{
		std::string message = format("%d Fragments detected.\n", (int)fragments.size());
		sendMessage(message);
	}
	else if (fragments.size() == 1)
	{
		std::string message = format("Found Single Fragment.\n");
		sendMessage(message);
	}
*/
	return(fragmentSizes.empty() ? (int)fragments.size() : (int)fragmentSizes.size());
}

// Used by sort routine to order the list of fragments from largest to smallest
bool fragmentCompare(CubePtrs* frag0, CubePtrs* frag1)
{
	return (frag0->size() > frag1->size());
}

// Sort the list of fragments from largest to smallest
void MultiCube::sortFragments(std::vector<CubePtrs*>& fragmentList)
{
	VectorMap::iterator it = fragments.begin();
	while (it != fragments.end())
	{
		fragmentList.push_back(&(it->second));
		++it;
	}

	std::sort(fragmentList.begin(), fragmentList.end(), fragmentCompare);
}

// Remove all but largest fragment
int MultiCube::discardFragments()
{
	if (fragments.size() <= 1)
		return(0);	// Nothing to do (all cubes are part of one fragment)

	int totalCubesRemoved = 0;
	//	std::string message = format("%d Fragments discarded\n", (int)fragments.size()-1);
	//	sendMessage(message);

	std::vector<CubePtrs*> fragmentList;

	sortFragments(fragmentList);

	std::vector<CubePtrs*>::iterator it = fragmentList.begin();
	if (it != fragmentList.end())
		++it;	// Skip over primary fragment (it's the first after the sort)

		// Remove all the other fragment's cubes
	while (it != fragmentList.end())
	{
		CubePtrs* cubeVec = *it++;
		CubePtrs::iterator cit = cubeVec->begin();
		totalCubesRemoved += (int)cubeVec->size();
		while (cit != cubeVec->end())
		{
			Cube* cube = *cit++;
			if (hasExposed(cube->info))		// This cube's faces got exposed during removal. Need to remove it properly.
				removeCube(cube);
			else
				deleteCube(cube);
		}
	}

	fragmentList.clear();

	m_cubesRemoved += totalCubesRemoved;

	return(totalCubesRemoved);
}

#ifdef WANT_FRAGMENTATION
bool AABBtoAABB(Fragment& tBox1, Fragment& tBox2)
{
	//Check if Box1's max is greater than Box2's min and Box1's min is less than Box2's max
	return(
		tBox1.maxZ > tBox2.minZ &&
		tBox1.minZ < tBox2.maxZ &&
		tBox1.maxX > tBox2.minX &&
		tBox1.minX < tBox2.maxX &&
		tBox1.maxY > tBox2.minY &&
		tBox1.minY < tBox2.maxY
);

	//If not, it will return false
}

Fragment MultiCube::getBoundingBox(CubePtrs& cubeVec, Fragment* prevFrag)
{
	CubePtrs::iterator cit = cubeVec.begin();
	Cube* cube = *cit++;
	int minX, minY, minZ;
	id2pos(cube, minX, minY, minZ);
	int maxX = minX;
	int maxY = minY;
	int maxZ = minZ;
	while (cit != cubeVec.end())
	{
		Cube* cube = *cit++;
		int x, y, z;
		id2pos(cube, x, y, z);
		if (x < minX)
			minX = x;
		else if (x > maxX)
			maxX = x;
		if (y < minY)
			minY = y;
		else if (y > maxY)
			maxY = y;
		if (z < minZ)
			minZ = z;
		else if (z > maxZ)
			maxZ = z;
	}
	
	Fragment fragInfo;

		// Set Centroid
	fragInfo.cx = minX + (maxX - minX) / 2.0;
	fragInfo.cy = minY + (maxY - minY) / 2.0;
	fragInfo.cz = minZ + (maxZ - minZ) / 2.0;

		// Set Position
	fragInfo.x = fragInfo.cx;
	fragInfo.y = fragInfo.cy;
	fragInfo.z = fragInfo.cz;

		// Set Bounding Box
	fragInfo.minX = minX; fragInfo.maxX = maxX;
	fragInfo.minY = minY; fragInfo.maxY = maxY;
	fragInfo.minZ = minZ; fragInfo.maxZ = maxZ;

		// Adjust the position and bounding box to previous location
	if (prevFrag != NULL) 
	{
		double xdiff = (fragInfo.x - prevFrag->x);
		double cxdiff = (fragInfo.cx - prevFrag->cx);
		if ((xdiff != 0) || (cxdiff != 0))
		{
			fragInfo.minX += xdiff + cxdiff; fragInfo.maxX += xdiff + cxdiff;
			fragInfo.x = prevFrag->x + cxdiff;
		}
		double ydiff = (fragInfo.y - prevFrag->y);
		double cydiff = (fragInfo.cy - prevFrag->cy);
		if ((ydiff != 0) || (cydiff != 0))
		{
			fragInfo.minY += ydiff + cydiff; fragInfo.maxY += ydiff + cydiff;
			fragInfo.y = prevFrag->y + cydiff;
		}
		double zdiff = (fragInfo.z - prevFrag->z);
		double czdiff = (fragInfo.cz - prevFrag->cz);
		if ((zdiff != 0) || (czdiff != 0))
		{
			fragInfo.minZ += zdiff + czdiff; fragInfo.maxZ += zdiff + cxdiff;
			fragInfo.z = prevFrag->z + czdiff;
		}
	}

	return(fragInfo);
}

void MultiCube::flow_simulator()
{
	if (fragments.size() > 0)
	{
		int nlevels = (int)round(log10((double)getVolume())) - 1;
		int minsize = (int)pow(10, nlevels);
		if (minsize < 10)
			minsize = 10;
		bool doCollisionDetect = false;

		FragmentMap newFragMap;

			// Get the biggest fragment (it won't move)
		VectorMap::iterator it = fragments.begin();
		int maxFragSize = 0;
		Fragment biggestFrag;
		int biggestFragId = 0;
		while (it != fragments.end())
		{
			CubePtrs& cubeVec = it->second;
			++it;

			int fragSize = (int)cubeVec.size();
			if (fragSize > maxFragSize)
			{
				Cube* cube = cubeVec[0];
				Fragment* prevFrag = NULL;
				if (cube->fragId != 0)
				{
					FragmentMap::iterator fit = fragMap.find(cube->fragId);
					if (fit != fragMap.end())
						prevFrag = &fit->second;
				}
				biggestFragId = getFragmentId(cube->info);
				biggestFrag = getBoundingBox(cubeVec, prevFrag);
				maxFragSize = fragSize;
			}
		}
		if (maxFragSize > minsize)
		{
			newFragMap[biggestFragId] = biggestFrag;
			doCollisionDetect = true;
		}

			// Map the small fragments
		it = fragments.begin();
		while (it != fragments.end())
		{
			CubePtrs& cubeVec = it->second;
			++it;

			int fragSize = (int)cubeVec.size();
			if ((fragSize >= maxFragSize) && doCollisionDetect)
				continue;

			Cube* cube = cubeVec[0];
			uint32_t fragmentId = getFragmentId(cube->info);
			Fragment* prevFrag = NULL;
			if (cube->fragId != 0)
			{
				FragmentMap::iterator fit = fragMap.find(cube->fragId);
				if (fit != fragMap.end())
					prevFrag = &fit->second;
			}

			Fragment fragInfo = getBoundingBox(cubeVec, prevFrag);

			double index = log10((double)fragSize);
			if (index > MAX_COLOR_INDEX)
				index = MAX_COLOR_INDEX;
			index = MAX_COLOR_INDEX - index;	// flip the index

			double dim = (m_params.zdim > m_params.ydim) ? m_params.zdim : m_params.ydim;
			dim = (dim > m_params.xdim) ? dim : m_params.xdim;

			double xoff = m_params.xdim / 2.0;
			double yoff = m_params.ydim / 2.0;
			double xscale = (xoff - fragInfo.cx) / xoff;
			double yscale = (yoff - fragInfo.cy) / yoff;
			double zscale = sqrt(0.005 * dim);
			double xadjust = 0.02 * xscale;
			double yadjust = 0.02 * yscale;
			double zadjust = (index / 5.0) * zscale;

			bool collision = false;
			if (doCollisionDetect)
				collision = AABBtoAABB(fragInfo, biggestFrag);

			fragInfo.x += xadjust;
			fragInfo.y += yadjust;
			if (!collision)
			{
				fragInfo.z += zadjust;
			}
			else
			{	fragInfo.x += xadjust*1;
				fragInfo.y += yadjust*1;
				fragInfo.z += zadjust*0.2;
			}

			newFragMap[fragmentId] = fragInfo;
		}
#ifdef NEED_THREAD_PROTECTION
		PortCriticalSection::AutoLock autolock(m_fragProtect);	// Protect the fragment map from display thread
#endif
		fragMap = newFragMap;
	}
}

// Build a histogram of fragment sizes. Bin width = log10(size) 
void MultiCube::getHistogram()
{
	int hist[6] = { 0,0,0,0,0,0 };
	VectorMap::iterator it = fragments.begin();
	while (it != fragments.end())
	{
		CubePtrs& cubeVec = it->second;
		++it;

		int fragSize = (int)cubeVec.size();
		int	index = (fragSize > 0.0) ? (int)round(log10(fragSize)) : MAX_COLOR_INDEX;
		if (index > MAX_COLOR_INDEX)
			index = MAX_COLOR_INDEX;
		++hist[index];
	}
	for (int i = 0; i < 6; i++)
		dhist[i] = (hist[i] > 0) ? log10((double)hist[i]) : 0;
}

#endif //#ifdef WANT_FRAGMENTION

// Utility routine to return the number of exposed faces of a Cube
int MultiCube::exposedFaceCount(Cube* cube)
{
	int exposed = 0;
	int face = NUMFACES;
	while (face--)
	{
		if (isExposed(cube->info, face))
		{
			++exposed;
		}
	}
	return(exposed);
}

int MultiCube::getExposedFaces(Cube* cube)
{
	int exposed = 0;
	int x, y, z;
	id2pos(cube, x, y, z);

	if (x > 0)
	{
		Cube* adjCube = getAdjacentCube(cube, 1);
		if (adjCube && !visible(adjCube->info))
			++exposed;
	}
	else
		++exposed;	// Surface cube; always exposed

	if (y > 0)
	{
		Cube* adjCube = getAdjacentCube(cube, 2);
		if (adjCube && !visible(adjCube->info))
			++exposed;
	}
	else
		++exposed;	// Surface cube; always exposed

	if (z > 0)
	{
		Cube* adjCube = getAdjacentCube(cube, 0);
		if (adjCube && !visible(adjCube->info))
			++exposed;
	}
	else
		++exposed;	// Surface cube; always exposed

	if (x < (int)m_params.xdim - 1)
	{
		Cube* adjCube = getAdjacentCube(cube, 4);
		if (adjCube && !visible(adjCube->info))
			++exposed;
	}
	else
		++exposed;	// Surface cube; always exposed

	if (y < (int)m_params.ydim - 1)
	{
		Cube* adjCube = getAdjacentCube(cube, 3);
		if (adjCube && !visible(adjCube->info))
			++exposed;
	}
	else
		++exposed;	// Surface cube; always exposed

	if (z < (int)m_params.zdim - 1)
	{
		Cube* adjCube = getAdjacentCube(cube, 5);
		if (adjCube && !visible(adjCube->info))
			++exposed;
	}
	else
		++exposed;	// Surface cube; always exposed

	return(exposed);
}

uint64_t MultiCube::exposedFaceCount()
{
	uint64_t exposed = 0;
	uint64_t testexposed = 0;
	CubePtrs::iterator it = cubeList.begin();
	while (it != cubeList.end())
	{
		testexposed += exposedFaceCount(*it);
		exposed += getExposedFaces(*it++);
	}
	return(exposed);
}

#ifdef HAS_WXWIDGETS
// Loads a vector of cube pointers only with cubes that have at 
// least one exposed surface. 
bool MultiCube::loadCubeListFromExposedList(CubePtrs& activeCubes)
{
#ifdef NEED_THREAD_PROTECTION
	if (!m_listProtect.TryEnter())
		return(false);
#endif

	activeCubes.clear();

	// A cube may have more than one exposed face.
	// We only want to add it to the list once.
	// Once a cube has been added we mark it as "visited"
	// to prevent further consideration
	// For efficiency the # of faces is also extracted. 
	// This is simply the # of times the cube shows up on the list
	CubeList::iterator it = exposedList.begin();
	while (it != exposedList.end())
	{
		Cube* cube = getCube(*it++);
		clearFaceCount(cube->info);
	}

	it = exposedList.begin();
	while (it != exposedList.end())
	{
		Cube* cube = getCube(*it++);
		if (getFaceCount(cube->info))
		{	// Found another face of the cube on the list, increment the face count
			++cube->info;
			continue;
		}
		activeCubes.push_back(cube);
		cube->info |= 1;	// Found the first face of this cube, set the face count to 1
	}
#ifdef NEED_THREAD_PROTECTION
	m_listProtect.Leave();
#endif
	return(!activeCubes.empty());
}

int MultiCube::getColorIndexAndPosition(Cube* cube, int& x, int& y, int& z)
{
	id2pos(cube, x, y, z);
 
	int index = 0;
#ifdef WANT_FRAGMENTATION
	index = m_lastIndex;

	if (!m_params.displayFaces || m_params.animateFrags)
	{
		uint32_t fragmentId = getFragmentId(cube->info);
		Fragment fragInfo = m_lastFragInfo;

		if (fragmentId != m_lastFragmentId)
		{
			if (fragmentId == UNINITIALIZED)
				index = 0;
			else
			{
				double fragSize = 0;
				if (fragmentSizes.size() > fragmentId)
					fragSize = fragmentSizes[fragmentId];
				index = (fragSize > 0.0) ? (int)round(log10(fragSize)) : MAX_COLOR_INDEX;
				if (index > MAX_COLOR_INDEX)
					index = MAX_COLOR_INDEX;

				if (m_params.enableFragClass && (index != m_params.fragClass))
					index = -1;	// ignore this cube (i.e. don't draw it)
				else
					index = MAX_COLOR_INDEX - index;	// flip the index
			}

			if (m_params.animateFrags)
			{
#ifdef NEED_THREAD_PROTECTION
				PortCriticalSection::AutoLock autolock(m_fragProtect);	// Protect the fragment map from display thread
#endif
				FragmentMap::iterator fit = fragMap.find(fragmentId);
				if (fit != fragMap.end())
					fragInfo = fit->second;
				else
					memset(&fragInfo, 0, sizeof(fragInfo));
			}
			else
				memset(&fragInfo, 0, sizeof(fragInfo));

			// Update state
			m_lastFragInfo = fragInfo;
			m_lastFragmentId = fragmentId;
			m_lastIndex = index;
		}
		x -= (int)round(fragInfo.x - fragInfo.cx); y -= (int)round(fragInfo.y - fragInfo.cy); z -= (int)round(fragInfo.z - fragInfo.cz);
	}
#else
	if (!m_params.displayFaces)
	{
		Dim_t cubesLeft = getInitialVolume() - getRemovedCount();
		index = (cubesLeft > 0) ? (int)round(log10(cubesLeft)) : MAX_COLOR_INDEX;
		if (index > MAX_COLOR_INDEX)
			index = MAX_COLOR_INDEX;

		index = MAX_COLOR_INDEX - index;	// flip the index
	}
#endif //#ifdef WANT_FRAGMENTATION

	if (m_params.displayFaces)
		index = getFaceCount(cube->info) - 1;
	
	return(index);
}
#endif //#ifdef HAS_WXWIDGETS

// Returns the number of cubes in the 3D grid that haven't been removed
// The surface area (exposed faces) of those remaing cubes is also returned
Dim_t MultiCube::getVolume(Dim_t* surfaceArea/*=NULL*/)
{
	if (surfaceArea)
		*surfaceArea = (Dim_t)exposedList.size();
	return((Dim_t)cubeList.size());
}

#ifdef WANT_FRAGMENTATION
// Output the Fragment Vectors
void MultiCube::outputFragments(char* filename)
{
	FILE* fp = fopen(filename, "w+");
	if (fp == NULL)
		return;

	std::vector<CubePtrs*> fragmentList;

	sortFragments(fragmentList);

	std::vector<CubePtrs*>::iterator it = fragmentList.begin();
	if (it != fragmentList.end())
		++it;	// Skip over 1st fragment (it's the largest after the sort)

		// Remove all the other fragment's cubes
	while (it != fragmentList.end())
	{
		CubePtrs* cubeVec = *it++;
		int exposedFaces = 0;
		int exposedCubes = 0;
		CubePtrs::iterator cit = cubeVec->begin();
		while (cit != cubeVec->end())
		{
			Cube* cube = *cit++;
			int nFaces = exposedFaceCount(cube);
			if (nFaces)
			{
				exposedFaces += nFaces;
				++exposedCubes;
			}
		}
		int numCubes = (int)cubeVec->size();
		fprintf(fp, "%d,%d,%d\n", numCubes, exposedCubes, exposedFaces);
	}

	fclose(fp);
}
#endif //#ifdef WANT_FRAGMENTATION

void MultiCube::outputInfo(char* filename, int runCount)
{
	FILE* fp = fopen(filename, "w+");
	if (fp == NULL)
		return;

	time_t _tm = time(NULL);
	struct tm * curtime = localtime(&_tm);
	fprintf(fp, "Date:\t%s", asctime(curtime));

	fprintf(fp, "File:\t%s\n", filename);
	fprintf(fp, "Run:\t%d\n", runCount);

	fprintf(fp, "\nVolume %lld : Surface Area %lld\n", m_initialVolume, m_maxSurfaceArea);

	if (m_params.aggregateEnable)
	{
		int x0 = m_params.xdim / 2;
		int y0 = m_params.ydim / 2;
		int z0 = m_params.zdim / 2;
		Dim_t containerVolume = generateEllipsoid(x0, y0, z0, m_params.xdim, m_params.ydim, m_params.zdim, false, true);

		x0 = y0 = z0 = m_params.particleSize / 2;
		Dim_t particleVolume = generateEllipsoid(x0, y0, z0, m_params.particleSize, m_params.particleSize, m_params.particleSize, false, true);
		double porosity = 1.0 - ((double)m_initialVolume / (double)containerVolume);
		fprintf(fp, "Aggregate:\n");
		fprintf(fp, "\tContainer Volume:\t%lld\n", containerVolume);
		fprintf(fp, "\tParticle Volume:\t%lld\n", particleVolume);
		fprintf(fp, "\tParticle Count:\t\t%lld\n", m_particlesGenerated);
		fprintf(fp, "\tPorosity:\t\t%g\n", porosity);
		fprintf(fp, "\tCube Shortfall:\t\t%d ", NCollisions);
		if (m_params.replaceEnable && NCollisions)
			fprintf(fp, "(Replaced %d)\n", NCollisions);
		else
			fprintf(fp, "(No Cubes Replaced)\n");
	}
	if (m_params.porosity > 0.0)
	{
		Dim_t surfaceArea;
		Dim_t volume = getVolume(&surfaceArea);
		fprintf(fp, "Pore Removal: Now %lld Total Cubes: %lld Exposed Faces: Porosity %g\n", volume, surfaceArea, m_params.porosity);
	}

	fclose(fp);
}

// Utility routine for writing out 3d grid.
// Output is cube fragment id, xyz position, # of exposed faces, # of cubes in fragment
void MultiCube::outputGrid(char* filename)
{
	FILE* fp = fopen(filename, "w+");
	if (fp == NULL)
		return;

	Cube* cube = m_Cubes;
	Dim_t totalCubes = getSize();
	while (totalCubes--)
	{
		if (!visible(cube->info))
		{	++cube;
			continue;
		}
		if (hasExposed(cube->info))	// Only bother with cubes with exposed faces (no internal cubes)
		{
			int xpos, ypos, zpos;
			id2pos(cube, xpos, ypos, zpos);

#ifdef WANT_FRAGMENTATION
			uint32_t  fragment = 0;
			int size = 0;
			fragment = getFragmentId(cube->info);

			VectorMap::iterator it = fragments.find(fragment);
			if (it != fragments.end())
			{
				CubePtrs& cubeVect = it->second;
				size = (int)cubeVect.size();
			}

			fprintf(fp, "%d,%d,%d,%d,%d,%d\n", fragment, xpos, ypos, zpos, exposedFaceCount(cube), size);
#else
			fprintf(fp, "%d,%d,%d,%d\n", xpos, ypos, zpos, exposedFaceCount(cube));
#endif //#ifdef WANT_FRAGMENTATION
		}
		++cube;
	}

	fclose(fp);
}

bool MultiCube::openSAData(char* filename)
{
	if (m_saData_fp)
		fclose(m_saData_fp);

	m_saData_fp = fopen(filename, "w+");
	return(m_saData_fp != NULL);
}

void MultiCube::closeSAData()
{
	if (m_saData_fp == NULL)
		return;
	
	outputSAData();	// Last try

	fclose(m_saData_fp);
	m_saData_fp = NULL;
}

// Utility routine for writing out 3d grid during processing.
// Output is the current number of cubes removed and the number of exposed faces
// This routine will be called at each processing interval determined by
// the output increment parameter.
void MultiCube::outputSAData()
{
	if ((m_saData_fp == NULL) || saData.empty())
		return;		// Nothing to output

	std::vector<SAData>::iterator it = saData.begin();
	while(it != saData.end())
	{	SAData& pInfo = *it++;
#ifdef RANDOM_REMOVAL
	fprintf(m_saData_fp, "%lld,%lld,%lld\n", pInfo.nCubesRemoved, pInfo.nExposedFaces, pInfo.nTotalExposedFaces);
#else
	fprintf(m_saData_fp, "%lld,%lld\n", pInfo.nCubesRemoved, pInfo.nExposedFaces);
#endif //#ifdef RANDOM_REMOVAL
	}
	fflush(m_saData_fp);

	saData.clear();	// Done writing this chunk, clear the old data
}

#ifdef WANT_INPUT_CONTROL
// EXPERIMENTAL
// Used to import designer objects.
// File format expected... x, y, z
void MultiCube::importObject(char* fname)
{
	FILE* fp = fopen(fname, "r");
	if (fp == NULL)
		return;

	std::vector<point3d> points;
	double maxX = -DBL_MAX;
	double minX = DBL_MAX;
	double maxY = -DBL_MAX;
	double minY = DBL_MAX;
	double maxZ = -DBL_MAX;
	double minZ = DBL_MAX;
	char delims[] = " ,\n";
	char linebuf[128];
	while (fgets(linebuf, 128, fp))
	{	
		point3d point;
		char* tok = strtok(linebuf, delims);
		if (tok)
		{
			point.x = atof(tok);
			tok = strtok(NULL, delims);
			if (tok)
			{
				point.y = atof(tok);
				tok = strtok(NULL, delims);
				if (tok)
				{
					point.z = atof(tok);
					if (point.x > maxX)
						maxX = point.x;
					if (point.x < minX)
						minX = point.x;
					if (point.y > maxY)
						maxY = point.y;
					if (point.y < minY)
						minY = point.y;
					if (point.z > maxZ)
						maxZ = point.z;
					if (point.z < minZ)
						minZ = point.z;
					points.push_back(point);
				}
			}
		}
	}

	double xscalar = 1;
	double xdiff = maxX - minX;
	double ydiff = maxY - minY;
	double zdiff = maxZ - minZ;

	//if ((maxX > 0) && (maxX <= 1))
		xscalar = (double)(m_params.xdim-1)/(maxX-minX);
	double yscalar = 1;
	//if ((maxY > 0) && (maxY <= 1)) 
		yscalar = (double)(m_params.ydim-1)/(maxY-minY);
	double zscalar = 1;
	//if ((maxZ > 0) && (maxZ <= 1))
		zscalar = (double)(m_params.zdim-1)/(maxZ-minZ);

	std::vector<point3d>::iterator it = points.begin();
	while (it != points.end())
	{
		point3d point = *it++;
		uint32_t x = (uint32_t)round((point.x - minX) * xscalar);
		uint32_t y = (uint32_t)round((point.y - minY) * yscalar);
		uint32_t z = (uint32_t)round((point.z - minZ) * zscalar);
//		if ((x >= m_params.xdim) || (y >= m_params.ydim) || (z >= m_params.zdim))
//			x = x;
		insertCube(x, y, z);
	}
}
#endif //#ifdef WANT_INPUT_CONTROL

// Simulates the consuming of the cubes in the 3D grid
// This routine is callable multiple times as long as 
// there are still cubes to be removed.
// A threshold is used to indicate when to return from this routine if additional
// intermediate processing needs to be done (e.g. fragmentation detection or data output)
bool MultiCube::consume(double& threshhold, int* progress/*=NULL*/)
{
	bool fastRemove = !(m_params.porosity > 0.0);
	Dim_t surfaceArea = (Dim_t)exposedList.size();
	while ((surfaceArea > 0) && !testDone())
	{
		if (m_params.outputSave && (m_lastRemoved != m_cubesRemoved) &&
			((m_params.outputSubsamp == 1) || !(m_cubesRemoved % m_params.outputSubsamp)))
		{	// Add processing information to the vector
			SAData pInfo;
			pInfo.nCubesRemoved = m_cubesRemoved;
			pInfo.nExposedFaces = surfaceArea;
#ifdef RANDOM_REMOVAL
			pInfo.nTotalExposedFaces = (fastRemove && !m_params.naiveRemoval) ? surfaceArea : exposedFaceCount();
#endif #ifdef RANDOM_REMOVAL
			saData.push_back(pInfo);
			m_lastRemoved = m_cubesRemoved;
		}
#ifdef HAS_WXWIDGETS
		if (m_params.displayEnable && ((m_params.outputSubsamp == 1) || !(m_cubesRemoved % m_params.outputSubsamp)))
		{
			x_cubesRemoved.push_back((double)((m_cubesRemoved + m_initialRemoved)) / (double)(m_initialVolume + m_initialRemoved));
			y_surfaceArea.push_back((double)surfaceArea / (double)m_maxSurfaceArea);
		}
#endif //#ifdef HAS_WXWIDGETS

			// Break out when threshhold is reached
		double ratio_removed = (double)m_cubesRemoved / (double)m_initialVolume;
		if (ratio_removed >= threshhold)
		{
				// In cases where more cubes are removed than what would occur
				// during the next threshhold we adjust the threshhold to the 
				// next increment.
			if (ratio_removed > threshhold)
			{		
				while (ratio_removed > threshhold)
					threshhold += m_params.outputInc;
				threshhold -= m_params.outputInc;
			}
			if (progress)
				*progress = (int)round(ratio_removed*100.0);

			return(true);
		}
		// Remove a random cube from the exposed face map
#ifdef RANDOM_REMOVAL
		if (m_params.naiveRemoval)
			naiveRemoveCube();
		else 
#endif //#ifdef RANDOM_REMOVAL
		if (fastRemove)
			removeCube();
		else
			removeCube(NULL);

		++m_cubesRemoved;							// increment # of cubes removed
		surfaceArea = (Dim_t)exposedList.size();	// adjust exposed faces count by net gain/loss due to cube removal
	}
	
	if (m_params.outputSave && (m_lastRemoved != m_cubesRemoved))
	{	// Add processing information to the vector (last one!)
		SAData pInfo;
		pInfo.nCubesRemoved = m_cubesRemoved;
		pInfo.nExposedFaces = surfaceArea;
#ifdef RANDOM_REMOVAL
		pInfo.nTotalExposedFaces = (fastRemove && !m_params.naiveRemoval) ? surfaceArea : exposedFaceCount();
#endif //#ifdef RANDOM_REMOVAL
		saData.push_back(pInfo);
		m_lastRemoved = m_cubesRemoved;
	}
#ifdef HAS_WXWIDGETS
	if (m_params.displayEnable && ((m_params.outputSubsamp == 1) || !(m_cubesRemoved % m_params.outputSubsamp)))
	{
		x_cubesRemoved.push_back((double)((m_cubesRemoved + m_initialRemoved)) / (double)(m_initialVolume + m_initialRemoved));
		y_surfaceArea.push_back((double)surfaceArea / (double)m_maxSurfaceArea);
	}
#endif //#ifdef HAS_WXWIDGETS

	if (progress)
		*progress = (int)round(((double)m_cubesRemoved / (double)m_initialVolume)*100.0);

	return(false);	// All cubes have been "consumed"
}

#ifndef HAS_WXWIDGETS
void sendMessage(std::string& message)
{
	printf("%s", message.c_str());
}
#endif

// Utility to use old-style printf formatting and return a std::string
std::string format(const char *fmt, ...)
{
	std::string retStr("");

	if (fmt)
	{
		va_list marker;

		// initalize variable arguments
		va_start(marker, fmt);

		// Get formatted string length adding one for the NULL
		size_t len = _vscprintf(fmt, marker) + 1;

		// Create a char vector to hold the formatted string.
		std::vector<char> buffer(len);
		if (vsnprintf(&buffer[0], len, fmt, marker) != -1)
		{
			buffer.push_back('\0');	// Stuff in a NULL
			retStr = &buffer[0];
		}

		// Reset variable arguments
		va_end(marker);
	}

	return retStr;
}

bool MultiCube::produceParticles(double& threshhold, int* progress)
{
	Dim_t cubesToRemove = (Dim_t)round(m_initialVolume * m_params.porosity);
	int poreSize = getPoreSize(cubesToRemove);	// Get initial pore dimensions
	m_cubesRemoved = m_initialVolume - (Dim_t)cubeList.size();
	while ((m_cubesRemoved < cubesToRemove) && !testDone())
	{
		m_cubesRemoved = m_initialVolume - (Dim_t)cubeList.size();
		double ratio_removed = (double)m_cubesRemoved / (double)cubesToRemove;
		if (ratio_removed >= threshhold)
		{
			// In cases where more cubes are removed than what would occur
			// during the next threshhold we adjust the threshhold to the 
			// next increment.
			if (ratio_removed > threshhold)
			{
				while (ratio_removed > threshhold)
					threshhold += POROSITY_PROCESSING_INC;
				threshhold -= POROSITY_PROCESSING_INC;
			}
			if (progress)
				*progress = (int)round(ratio_removed * 100.0);

			return(true);
		}
	}

	if (progress)
		*progress = (int)round(((double)m_cubesRemoved / (double)cubesToRemove)*100.0);

	return(false);	// All particles have been "produced"
}

#define MAX_MISSES		(100000)
#define FILL_PERCENT	(0.35)
#define SPHERE_SCALAR	((4.0 / 3.0) * 3.141592635)
#define MAX_NEIGHBORS	(12)

Aggregate::Aggregate(bool isCuboid, double xd, double yd, double zd, double pd) : m_isCuboid(isCuboid), m_xd(xd), m_yd(yd), m_zd(zd), m_pd(pd)
{
	// Get radii for convenience
	m_xr = m_xd / 2;
	m_yr = m_yd / 2;
	m_zr = m_zd / 2;
	m_pr = m_pd / 2;

	// Create an initial point at the center of the container offset by the radius of the particle
	double pAdj = 0;
	int ipd = (int)m_pd;
	if (ipd % 2)
		pAdj = .5;

	point3d p;
	p.x = m_xr + pAdj;
	p.y = m_yr + pAdj;
	p.z = m_zr + pAdj;

	p.nNeighbors = 0;
	m_points.push_back(p);

	if (m_isCuboid)
		m_containerVolume = m_xd * m_yd * m_zd;
	else
		m_containerVolume = SPHERE_SCALAR * (m_xr * m_yr * m_zr);
	m_particleVolume = SPHERE_SCALAR * (m_pr * m_pr * m_pr);
	m_expected = (uint64_t)((m_containerVolume / m_particleVolume) * FILL_PERCENT);
}

void Aggregate::generateParticles(bool verbose/*=true*/)
{
	if (verbose)
	{
		std::string message = format("Generating Aggregate: Container Volume %g Particle Volume %g\n", m_containerVolume, m_particleVolume);
		sendMessage(message);
	}

	double pMag = m_xr - m_pr;

	uint64_t nMisses = 0;
	while ((m_points.size() < m_expected) && (nMisses < MAX_MISSES))
	{
		point3d p = generateParticle();
		p.nNeighbors = 0;
		if (validateParticle(p, pMag))
		{
			int nPoints = (int)m_points.size();
			// Update the neighbor fields of the new particle and its neighbors
			for (int index = 0; index < nPoints; index++)
			{
				// Get a particle center off the list
				// Compute the distance to the new particle center
				point3d& c = m_points[index];
				double xdiff = c.x - p.x;
				double ydiff = c.y - p.y;
				double zdiff = c.z - p.z;
				double mag = sqrt(xdiff * xdiff + ydiff * ydiff + zdiff * zdiff);
				// Count the new particle's neighbors
				if (mag < (m_pd + m_pr))
				{
					++p.nNeighbors;
					++c.nNeighbors;
				}
			}
			m_points.push_back(p);

			nMisses = 0;
		}
		else
			++nMisses;	// debug break point location
	}

	if (verbose)
	{
		std::string message = format("Finished: Expected %d Created %d\n", m_expected, m_points.size());
		sendMessage(message);
	}
}

point3d Aggregate::generateParticle()
{
	// Pick an existing point from the list (the center of a sub-particle)
	int index = (int)(xrand() * m_points.size());
	point3d c = m_points[index];
	while (c.nNeighbors >= MAX_NEIGHBORS)
	{
		index = (int)(xrand() * m_points.size());
		c = m_points[index];
	}

	// Create a random point somewhere in the volume
	point3d p;
	p.x = xrand() * m_xd;
	p.y = xrand() * m_yd;
	p.z = xrand() * m_zd;

	// Create a vector from selected point to random point
	point3d np;
	np.x = (c.x - p.x);
	np.y = (c.y - p.y);
	np.z = (c.z - p.z);
	double mag = sqrt((np.x * np.x) + (np.y * np.y) + (np.z * np.z));

	// Normalize and scale the vector and add the components to the existing point creating the new sub-particles center point
	np.x = ceil(c.x + (m_pd * (np.x / mag)));
	np.y = ceil(c.y + (m_pd * (np.y / mag)));
	np.z = ceil(c.z + (m_pd * (np.z / mag)));

	return(np);
}

bool Aggregate::validateParticle(point3d p, double pMag)
{
	bool condition;
	// Verify that the particle will be within the volume of the container
	if (m_isCuboid)
	{
		condition = (
			((p.x >= m_pr) && (p.x <= (m_xd - m_pr))) &&
			((p.y >= m_pr) && (p.y <= (m_yd - m_pr))) &&
			((p.z >= m_pr) && (p.z <= (m_zd - m_pr))));
	}
	else
	{
		// Create a vector from container center to test point
		point3d np;
		np.x = m_xr - p.x;
		np.y = m_yr - p.y;
		np.z = m_zr - p.z;
		double mag = sqrt((np.x * np.x) + (np.y * np.y) + (np.z * np.z));

		condition = (mag < pMag);	// Verify that the sub-particle will fit inside the container
	}

	if (condition)
	{
		// Verify the new particle doesn't overlap with any existing particles
		pointVect::iterator it = m_points.begin();
		while (it != m_points.end())
		{
			// Get a particle center off the list
			// Compute the distance to the new particle center
			point3d& c = *it++;
			double xdiff = (c.x - p.x);
			double ydiff = (c.y - p.y);
			double zdiff = (c.z - p.z);
			double mag = sqrt(xdiff * xdiff + ydiff * ydiff + zdiff * zdiff);
			if (mag < m_pd)	// The new particle would overlap an existing one (Fail!!)
				return(false);
			mag = mag;
		}
		return(true);	// Success!!
	}

	return(false);
}

void Aggregate::fractalGeneration(pointVect& displayPoints, point3d cOffset, double xd, double yd, double zd, double pd, double& pSize)
{
	Aggregate aggregate(m_isCuboid, xd, yd, zd, pd);
	aggregate.generateParticles(false);
	pointVect& points = aggregate.getParticles();

	double new_r = std::cbrt(aggregate.m_particleVolume / SPHERE_SCALAR);
	double expected = (aggregate.m_containerVolume / aggregate.m_particleVolume);
	double new_pv = aggregate.m_particleVolume / expected;
	double new_pr = std::cbrt(new_pv / SPHERE_SCALAR);
	if (new_pr > 1)
	{
		xd = yd = zd = pd;
		pd = new_pr * 2;

		double xr = xd / 2.0;
		double yr = yd / 2.0;
		double zr = zd / 2.0;
		double pr = pd / 2.0;

		//int nParticles = 0;
		pointVect::iterator it = points.begin();
		while (it != points.end())
		{
			point3d& c = *it++;
			point3d p = cOffset;
			p.x += c.x - xr;
			p.y += c.y - yr;
			p.z += c.z - zr;
			fractalGeneration(displayPoints, p, xd, yd, zd, pd, pSize);
			//if (++nParticles > 1)
			//	break;
		}
	}
	else
	{
		pointVect::iterator it = points.begin();
		while (it != points.end())
		{
			point3d& p = *it++;
			p.x += cOffset.x;
			p.y += cOffset.y;
			p.z += cOffset.z;
			displayPoints.push_back(p);
		}
		pSize = pd;
	}
}