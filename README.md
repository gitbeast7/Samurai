# Samurai
SAMURAI (Surface Area Modelling Using Rubik As Inspiration) version 1.0 is a model that
simulates the progressive degradation of individual detritus particles that are represented as a
matrix of aligned sub-units akin to the well-known Rubik cube. The seminal publication is:
Anderson, T.R., Gentleman, W.C., Cael, B.B., Hirschi, J.M. and Mayor, D.J. (2022).
Modelling surface area as a dynamic regulator of particle remineralisation in the ocean.
Journal of Theoretical Biology *** [vol, pp., doi.]
# Installation and Usage
The application is a Windows 64-bit executable.

Note: If you're only using the application you can simply drop the executable wherever you like.
A single .xml file will be created the first time you run the application. This simply stores configuration
parameter values so subsequent launches will be initialized to the last user settings.
If the configuration file is removed or can't be written a default configuration is used on each launch.
# Using the application 

A user manual accompanies this distribution.

# Using the MultiCube class 
There is an example in the SamuraiConsole directory which demonstrates how the MultiCube class can be used
without the GUI.

# Experimental
In the MultiCube.h file there are two conditional complication macros, WANT_FRAGMENTATION and WANT_INPUT_CONTROL.

The first provides functionality for keeping track of object fragmentation. This includes labelling as well as a
size histogram and fragment animation.

The second provides functionality for inputing designer objects. It accepts objects defined by their x,y,z positions.

# External Sources Used 
Connected Components 3D: https://github.com/seung-lab/connected-components-3d

Robinhood Hashing: https://github.com/martinus/robin-hood-hashing

# License
Licensed under the MIT License.
