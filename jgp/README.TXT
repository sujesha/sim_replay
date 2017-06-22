	minimalistic README for

JGP JAVA GNUplot GUI Ver. 0.1.2 pre alpha

		author

	Maximilian H. Fabricius
===============================================================================
Changelog:
* 0.1.2
	- Added support for 3D plots.
	- Decoupled shell window.
	- Improved exception handeling.
	- Improved behavious of dataset table during moving.
	- Implemeten undo/redo feature.
	- Improved menu.
===============================================================================

0) License

	 JGNUplot is a GUI for gnuplot (http://www.gnuplot.info/)
	 The GUI is build on JAVA wrappers for gnuplot also provided in this package.
	 
	 Copyright (C) 2006  Maximilian H. Fabricius 
	 
	 This program is free software; you can redistribute it and/or
	 modify it under the terms of the GNU General Public License
	 as published by the Free Software Foundation; either version 2
	 of the License, or (at your option) any later version.
	 
	 This program is distributed in the hope that it will be useful,
	 but WITHOUT ANY WARRANTY; without even the implied warranty of
	 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	 GNU General Public License for more details.
	 
	 You should have received a copy of the GNU General Public License
	 along with this program; if not, write to the Free Software
	 Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.

	 (for detailed gpl see gpl.txt)
 
1) prerequisites

You need to have gnuplot installed. See http://www.gnuplot.info/.
It is recommended to install version 4.1 to get all features.

JGP was compiled for Java 5.0. You will need a proper runtime or jdk.
See http://java.sun.com/.
JGP does make use of JGP features, don't try to compile it with an older 
JAVA version.

2) installation

Unzip the zip archive into a directory of you choice.
Make the file jgp executeable 
	# chmod +x jgp
and execute it.
Do not background the process. JGP was found to crash if backgrounded.
You may also start JGP with
	# java -cp ./classes jgp.gui.JGP
	
3) compiling

The source is provided along the JAVA binaries. To compile JGP
make the file makeJGNUplot executeable 
	# chmod +x makeJGNUplot
and execute it or run
	# javac  -d classes  ./src/jgp/*.java ./src/jgp/gui/*.java