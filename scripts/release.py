#!/bin/env python3
import os
import sys
import shutil

if len(sys.argv) != 2:
    print("Usage: {} VERSION".format(sys.argv[0]))
    sys.exit(1)

version = str(sys.argv[1])

releasePath = os.path.join(os.path.dirname(__file__), "..", "build_release")

if os.path.exists(releasePath):
    shutil.rmtree(releasePath)

os.mkdir(releasePath)
os.chdir(releasePath)
os.system("cmake -DBUILD_EXAMPLES=OFF -DBUILD_TESTING=OFF -DCPACK_PACKAGE_FILE_NAME=\"crow-{}\" .. && make -j5".format(version))
os.system("sed -i 's/char VERSION\\[\\] = \"master\";/char VERSION\\[\\] = \"{}\";/g' crow_all.h".format(version))
os.system("cpack -R {}".format(version))
