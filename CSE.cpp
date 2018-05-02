// CSE (CoDeSys symbol export)
// Copyright(C) 2018 Martial Demolins AKA Folco
//
// This program is free software : you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.If not, see <https://www.gnu.org/licenses/>.

#include "stdafx.h"
#include "Files.hpp"
#include "Parsing.hpp"
#include "POU.hpp"
#include "Variable.hpp"
#include <sys/stat.h>
#include <iostream>
#include <fstream>
#include <string>
#include <vector>

using namespace std;

// Wait for ENTER to be presssed, then exit with an error code
void WaitAndExit(int retval)
{
//#ifdef _DEBUG
    cout << endl << "*** Press ENTER to close the window ***" << endl;
    getchar();
//#endif
    exit(retval);
}


// Open a file, throw a message if it fails. Return true if opening suceeded
bool Open(fstream& stream, string& filename, ios_base::openmode mode)
{
    stream.open(filename, mode);
    if (stream.fail()) {
        cout << "ERROR: Unable to open file " << filename.c_str() << endl;
        return false;
    }
    return true;
}


// Entry point
int main(int argc, char** argv)
{
    // Preamble
    cout << "CSE v2.0, (c)2018 Martial Demolins AKA Folco" << endl;
    cout << "This program strips the symbol files generated by CoDeSys 2.3.x" << endl;
    cout << "keeping only symbols tagged with \"" << EXPORT_TAG << "\" in the source files" << endl;
    cout << "Bug reports and suggestions: mdemolins <at> gmail <dot> com";
    cout << endl;
    cout << "License: GPL 3" << endl;
    cout << "This program comes with ABSOLUTELY NO WARRANTY; for details see:" << endl;
    cout << "https://www.gnu.org/licenses/gpl.txt" << endl;
    cout << "This is free software, and you are welcome to redistribute it under certain conditions" << endl;
    cout << endl;

    // Support only one arg, the symbol file name
    if (argc != 2) {
        cout << "ERROR: one argument required" << endl;
        cout << "Usage: CSE <symbol file name>" << endl;
        WaitAndExit(-1);
    }

    // Some computations around the file names
    string SrcFilename = argv[1];
    auto SrcFilenameLength = SrcFilename.length();
    auto ExtensionLength = string(SYM_FILE_EXT).length();

    string SrcFilenameBase = SrcFilename.substr(0, SrcFilenameLength - ExtensionLength);
    string SrcFilenameEnd = SrcFilename.substr(SrcFilenameLength - ExtensionLength);

    string DestFilename = SrcFilenameBase + CSE_FILENAME_ADDITION + SYM_FILE_EXT;
    string ProjectFilename = SrcFilenameBase + PRO_FILE_EXT;

    // Check the source file extension
    if (!StrCmpI(SYM_FILE_EXT, SrcFilenameEnd.c_str())) {
        cout << "ERROR: The symbol file name must have the " << SYM_FILE_EXT << " extension" << endl;
        WaitAndExit(-1);
    }

    // Create and open streams
    fstream Src, Dest, Project;
    if (!Open(Src, SrcFilename, fstream::in) ||
        !Open(Dest, DestFilename, fstream::out) ||
        !Open(Project, ProjectFilename, fstream::in | fstream::binary)
        ) {
        WaitAndExit(-1);
    }

    // Create a buffer to read the project file
    struct _stat64 StatResult;
    _stat64(ProjectFilename.c_str(), &StatResult);
    const unsigned int ProjectSize = (unsigned int)StatResult.st_size;
    char* Buffer = new char[ProjectSize];

    // Read the project file and abort if it failed
    Project.read(Buffer, StatResult.st_size);
    if (Project.fail()) {
        cout << "ERROR: Unable to read the project file: " << ProjectFilename.c_str() << endl;
        WaitAndExit(-1);
    }

    // Check the signature of the project file
    if (strncmp(CODESYS_PROJECT_SIGNATURE, Buffer, sizeof(CODESYS_PROJECT_SIGNATURE) - 1)) {
        cout << "ERROR: The project file is not a valid CoDeSys file" << endl;
        WaitAndExit(-1);
    }

    // Parse the project file to index POUs, and output the list
    cout << ">>> Indexing POUs...";
    vector<POU> POUindex;
    IndexExportedVariables(POUindex, Buffer, ProjectSize);    
    cout << " found " << POUindex.size() << endl;

    for (unsigned int i = 0; i < POUindex.size(); i++) {
        POUindex.at(i).POUname.empty()
            ? cout << "<Global variables>" << endl
            : cout << POUindex.at(i).POUname.c_str() << endl;
    }
    cout << ">>> End of indexed POUs" << endl << endl;

    // Copy the initial part of the src symbol file to the destination one
    cout << ">>> Exported tags:" << endl;
    string Line;
    do {
        getline(Src, Line);
        Dest << Line << endl;
    } while (Line.find(SYM_BLOCK_START) == string::npos);

    // Copy only the tagged symbols
    do {
        getline(Src, Line);
        // Remove the XML part, keeping only the symbol name
        auto FirstCharOffset = Line.find('>') + 1;
        auto LastCharOffset = Line.find('<', FirstCharOffset);
        string FullSymbol = Line.substr(FirstCharOffset, LastCharOffset - FirstCharOffset);

        // Get the POU name and the symbol name
        auto DotOffset = FullSymbol.find('.');
        string POUname = FullSymbol.substr(0, DotOffset);
        string SymbolName = FullSymbol.substr(DotOffset + 1);

        // If the symbol is exported, copy it in the new file
        for (unsigned int i = 0; i < POUindex.size(); i++) {
            if (POUindex[i].POUname == POUname) {
                for (unsigned int j = 0; j < POUindex[i].Variables.size(); j++) {
                    if (POUindex[i].Variables[j].SymbolName == SymbolName) {
                        Dest << Line << endl;
                        POUindex[i].Variables[j].Used = true;
                        cout << "+++ " << FullSymbol << endl;
                        break;
                    }
                }
            }
        }
    } while (Line.find(SYM_BLOCK_END) == string::npos);
    cout << endl << ">>> End of exported tags..." << endl;

    // Copy the end of the symbol file
	do {
		Dest << Line << endl;
		getline(Src, Line);
	} while (!Src.eof());

	// Copy the last line
	Dest << Line << endl;

/*    while (!Src.eof()) {
        getline(Src, Line);
        Dest << Line << endl;
    }
	*/
    // Check that all symbols tagged for export have really been exported
    for (unsigned int i = 0; i < POUindex.size(); i++) {
        for (unsigned int j = 0; j < POUindex[i].Variables.size(); j++) {
            if (!POUindex[i].Variables[j].Used) {
                cout << "WARNING !" << endl
                    << "The symbol " << POUindex[i].POUname << "." << POUindex[i].Variables[j].SymbolName
                    << " is tagged for export, but was not found in the .SYM_XML file." << endl
                    << "You should probably perform a full rebuild of your project" << endl;
            }
        }
    }

    // Return success;
    cout << ">>> File \"" << DestFilename.c_str() << "\" created sucessfully" << endl;
    delete[] Buffer;
    WaitAndExit(0);
}
