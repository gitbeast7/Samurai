
#include <stdio.h>
#include <chrono>
#include <iostream>
#include <windows.h>

#include "cxxopts.hpp"	// Used to parse command line options

#include "MultiCube.h"

void parse(int argc, const char* argv[], CubeParams& params)
{
	try
	{
		cxxopts::Options options(argv[0], " - command line options");
		options
			.positional_help("[optional args]")
			.show_positional_help();

		options
			.set_width(70)
			.set_tab_expansion()
			.allow_unrecognised_options()
			.add_options()
			("e", "Ellipsoidal object shape. Default cuboid.", cxxopts::value<bool>())
			("dim", "Object dimensions x,y,z", cxxopts::value<std::vector<int>>())
			("p", "Porosity. Default 0.0 (no porosity)", cxxopts::value<double>()->default_value("0.3"))
			("z", "Pore size. Default 3x3", cxxopts::value<int>()->default_value("3"))
			("f", "Fixed pore size. Default is randomized pore size [1:Pore size].", cxxopts::value<bool>())
			("s", "Spherical pore shape. Default cube.", cxxopts::value<bool>())
			("R", "Don't replace removed cubes.", cxxopts::value<bool>())
			("o", "Output directory. Default OutputDir.", cxxopts::value<std::string>())
			("i", "Output processing increment. Default .05 (Write every 5%)", cxxopts::value<double>())
			("t", "Processing termination. Default 1.0 (Run until 100% completion)", cxxopts::value<double>())
			("help", "Print usage")
			;
		auto result = options.parse(argc, argv);

		if (result.count("help"))
		{
			std::cout << options.help() << std::endl;
			exit(0);
		}

		if (result.count("e"))
		{
			params.cuboid = false;
		}

		if (result.count("p"))
		{
			params.porosity = result["p"].as<double>();
		}

		if (result.count("z"))
		{
			params.poreSize = result["z"].as<int>();
		}

		if (result.count("f"))
		{
			params.poreIsFixed = true;
		}

		if (result.count("s"))
		{
			params.poreIsCuboid = false;
		}

		if (result.count("R"))
		{
			params.withReplacement = false;
		}

		if (result.count("i"))
		{
			params.outputInc = result["i"].as<double>();
		}

		if (result.count("t"))
		{
			params.outputEnd = result["t"].as<double>();
		}

		if (result.count("o"))
		{
			params.outputDir = result["o"].as<std::string>();
		}

		if (result.count("dim"))
		{
			const auto values = result["dim"].as<std::vector<int>>();
			int nvalues = (int)values.size();
			if (nvalues--)
			{	params.xdim = values[0];
				if (nvalues--)
				{	params.ydim = values[1];
					if (nvalues--)
						params.zdim = values[2];
				}
			}
		}
	}
	catch (const cxxopts::OptionException& e)
	{
		std::cout << "error parsing options: " << e.what() << std::endl;
		exit(1);
	}
}

void run(CubeParams& params)
{
	double Threshhold = params.outputInc;

	char filename[128];	// Used for output files

	MultiCube* grid = new MultiCube(params, NULL);

	// Prepare the grid with a porosity level if requested
	if (params.porosity > 0.0)
	{
		std::chrono::system_clock::time_point before = std::chrono::system_clock::now();

		uint64_t totalCubes = grid->getInitialVolume();
		uint64_t cubesToRemove = (uint64_t)(totalCubes * params.porosity);
		std::string message = format("Removing %lld cubes\n", cubesToRemove);
		sendMessage(message);

		double threshhold = 1.0;	// Complete in one go 
		grid->producePores(threshhold, NULL);
		if (params.withReplacement)	// Call function one last time if cube replacement is desired
			grid->producePores(threshhold, NULL);
		grid->finishPores();

		// Report elapsed time
		std::chrono::duration<double> duration = std::chrono::system_clock::now() - before;
		message = format("Preprocessing Elapsed Time: %.2lf(s)\n", duration.count());
		sendMessage(message);
	}

	if (params.outputSaveGrid)
	{
		sprintf(filename, "%s%dx%dx%d.txt", params.cuboid ? "Cuboid" : "Ellipsoid", params.xdim, params.ydim, params.zdim);
		grid->outputGrid(filename);	// Dump info for doing 3D cube plots
	}
	if (params.outputSave)
	{
		sprintf(filename, "%s\\%sInfo%dx%dx%dp%d.txt", params.outputDir.c_str(), params.cuboid ? "Cuboid" : "Ellipsoid", params.xdim, params.ydim, params.zdim, (int)(params.porosity * 100 + .5));
		grid->openSAData(filename);		// Open volume vs surface area data file
	}

	int progress = 0;
	// Start timing now
	std::chrono::system_clock::time_point before = std::chrono::system_clock::now();
	while (grid->consume(Threshhold, &progress))
	{
		//		const sec duration = clock::now() - before;
		//		printf("%d%% Processed - Elapsed Time: %.2lf(s)\r", progress, duration.count());

#ifdef WANT_FRAGMENTATION
		if (params.enableFrag)	// If enabled do fragment detection
		{
			grid->detectFragments();
			if (params.outputSaveFrags)
			{
				sprintf(filename, "%sFrags%dx%dx%d_%d.txt", params.cuboid ? "Cuboid" : "Ellipsoid", params.xdim, params.ydim, params.zdim, (int)(Threshhold * 100 + .5));
				grid->outputFragments(filename);
			}
		}
#endif //#ifdef WANT_FRAGMENTATION
		if (params.outputSaveGrid)
		{
			sprintf(filename, "%s%dx%dx%d_%d.txt", params.cuboid ? "Cuboid" : "Ellipsoid", params.xdim, params.ydim, params.zdim, (int)(Threshhold * 100 + .5));
			grid->outputGrid(filename);	// Dump info for doing 3D cube plots
		}
		if (params.outputSave)
		{
			grid->outputSAData();	// Dump volume vs surface area data
		}

		Threshhold += params.outputInc;		// Increment by "outputInc" after each dump
		if (Threshhold >= params.outputEnd)	// Reached end of processing 
			break;
	}
	std::chrono::duration<double> duration = std::chrono::system_clock::now() - before;
	printf("Consuming Elapsed Time: %.3lf(s)\n", duration.count());

	if (params.outputSave)
	{
		grid->closeSAData();	// Finish write and close volume vs surface area data file
	}

	delete grid;
}

int main(int argc, const char* argv[])
{
	if (!SetPriorityClass(GetCurrentProcess(), ABOVE_NORMAL_PRIORITY_CLASS))
	{
		fprintf(stderr, "Couldn't set priority %d\n", ABOVE_NORMAL_PRIORITY_CLASS);
	}

	CubeParams params;
	
	MultiCube::loadDefaults(params);	// Set default parameters
	
	parse(argc, argv, params);			// Parse command line parameters

	run(params);						// Run with selected parameters

	return(0);
}

