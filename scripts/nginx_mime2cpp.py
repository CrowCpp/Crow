#!/usr/bin/env python3
"""
Copyright (c) 2020-2021, CrowCpp
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

* Redistributions of source code must retain the above copyright notice, this
  list of conditions and the following disclaimer.

* Redistributions in binary form must reproduce the above copyright notice,
  this list of conditions and the following disclaimer in the documentation
  and/or other materials provided with the distribution.

* Neither the name of the author nor the names of its
  contributors may be used to endorse or promote products derived from
  this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
"""

#get mime.types file from the nginx repository at nginx/conf/mime.types
#typical output filename: mime_types.h
import sys

if len(sys.argv) != 3:
    print("Usage: {} <NGINX_MIME_TYPE_FILE_PATH> <CROW_OUTPUT_HEADER_PATH>".format(sys.argv[0]))
    sys.exit(1)

file_path = sys.argv[1]
output_path = sys.argv[2]

tabspace = "    "
def main():
    outLines = []
    outLines.append("//This file is generated from nginx/conf/mime.types using nginx_mime2cpp.py")
    outLines.extend([
        "#include <unordered_map>",
        "#include <string>",
        "",
        "namespace crow {",
        "",
        "#ifdef CROW_MAIN"
        tabspace + "std::unordered_map<std::string, std::string> mime_types {"])

    with open(file_path, "r") as mtfile:
        incomplete = ""
        incompleteExists = False
        for line in mtfile:
            if ('{' not in line and '}' not in line):
                splitLine = line.split()
                if (';' not in line):
                    incomplete += line
                    incompleteExists = True
                    continue
                elif (incompleteExists):
                    splitLine = incomplete.split()
                    splitLine.extend(line.split())
                    incomplete = ""
                    incompleteExists = False
                outLines.extend(mime_line_to_cpp(splitLine))

    outLines[-1] = outLines[-1][:-1]
    outLines.extend([
		tabspace + "};",
		"#else",
		"extern std::unordered_map<std::string, std::string> mime_types;",
		"#endif",
		"}"
		])

    with open(output_path, "w") as mtcppfile:
        mtcppfile.writelines(x + '\n' for x in outLines)

def mime_line_to_cpp(mlist):
    #print("recieved: " + str(mlist))
    stringReturn = []
    for i in range (len(mlist)):
        if (mlist[i].endswith(";")):
            mlist[i] = mlist[i][:-1]
    for i in range (len(mlist)-1, 0, -1):
        stringReturn.append(tabspace*2 + "{\"" + mlist[i] + "\", \"" + mlist[0] + "\"},")
    #print("created: " + stringReturn)
    return stringReturn

main()
