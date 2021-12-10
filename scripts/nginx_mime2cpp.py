#!/usr/bin/env python3

#get mime.types file from the nginx repository at nginx/conf/mime.types
#typical output filename: mime_types.h
import sys
from datetime import date

if (len(sys.argv) != 3) and (len(sys.argv) != 2):
    print("Usage (local file): {} <NGINX_MIME_TYPE_FILE_PATH> <CROW_OUTPUT_HEADER_PATH>".format(sys.argv[0]))
    print("(downloads file)  : {} <CROW_OUTPUT_HEADER_PATH>".format(sys.argv[0]))
    sys.exit(1)
if len(sys.argv) == 3:
    file_path = sys.argv[1]
    output_path = sys.argv[2]
elif len(sys.argv) == 2:
    import requests
    open("mime.types", "wb").write(requests.get("https://hg.nginx.org/nginx/raw-file/tip/conf/mime.types").content)
    file_path = "mime.types"
    output_path = sys.argv[1]

tabspace = "    "
tabandahalfspace = "      "
def main():
    outLines = []
    outLines.append("// This file is generated from nginx/conf/mime.types using nginx_mime2cpp.py on " + date.today().strftime('%Y-%m-%d') + ".")
    outLines.extend([
        "#include <unordered_map>",
        "#include <string>",
        "",
        "namespace crow",
        "{",
        tabspace + "const std::unordered_map<std::string, std::string> mime_types{"])

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

    outLines[-1] = outLines[-1][:-1] + "};"
    outLines.append("}")

    with open(output_path, "w") as mtcppfile:
        mtcppfile.writelines(x + '\n' for x in outLines)

def mime_line_to_cpp(mlist):
    #print("recieved: " + str(mlist))
    stringReturn = []
    for i in range (len(mlist)):
        if (mlist[i].endswith(";")):
            mlist[i] = mlist[i][:-1]
    for i in range (len(mlist)-1, 0, -1):
        stringReturn.append(tabandahalfspace + "{\"" + mlist[i] + "\", \"" + mlist[0] + "\"},")
    #print("created: " + stringReturn)
    return stringReturn

main()
