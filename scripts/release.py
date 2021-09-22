#!/bin/env python3
import os
import sys
import shutil

if len(sys.argv) != 2:
    print("Usage: {} VERSION".format(sys.argv[0]))
    print("Version should be similar to \"1.2+3\" (+3 is optional)")
    sys.exit(1)

# Different color outputs (to distinguish errors from standard output)
def prGreen(skk): print("\033[36m-->\033[92m {}\033[00m" .format(skk))
def prRed(skk): print("\033[36m-->\033[91m {}\033[00m" .format(skk))
def prYellow(skk): print("\033[36m-->\033[93m {}\033[00m" .format(skk))

# Defining the different ways the version is represented
version = str(sys.argv[1])
versionNoPatch = version[0:3]
versionvcpkg = version.replace("+", ".")

# Defining the paths used
sourcePath = os.path.join(os.path.dirname(__file__), "..", "include", "crow")
releasePath = os.path.join(os.path.dirname(__file__), "..", "build_release")
releaseCodePath = os.path.join(os.path.dirname(__file__), "..", "build_release", "_CPack_Packages", "Linux")
releaseAURPath = os.path.join(os.path.dirname(__file__), "..", "build_release", "_CPack_Packages", "Linux", "AUR")

# Delete any existing release
if os.path.exists(releasePath):
    prYellow("Detected existing release directory, overriding...")
    shutil.rmtree(releasePath)

# Create the dir
prGreen("Creating new release directory...")
os.mkdir(releasePath)
os.chdir(sourcePath)

# Changing "master" version to <version_number>
prGreen("Applying new version to Crow server name...")
os.system("sed -i 's/char VERSION\\[\\] = \".*\";/char VERSION\\[\\] = \"{}\";/g' version.h".format(versionNoPatch))
prGreen("Applying new version to vcpkg...")
os.system("sed -i 's/\"version-string\": \".*\",/\"version\": \"{}\",/g' ../../vcpkg.json".format(versionvcpkg))
prGreen("Applying new version to PKGBUILD...")
os.system("sed -i 's/^pkgver=.*/pkgver={}/' ../../scripts/PKGBUILD".format(version))
os.system("sed -i 's/^pkgrel=.*/pkgrel=1/' ../../scripts/PKGBUILD".format(version))

# Running CMake to compile crow_all and the deb package
os.chdir(releasePath)
prGreen("Compiling Release...")
os.system("cmake -DCROW_BUILD_EXAMPLES=OFF -DCROW_BUILD_TESTS=OFF -DCROW_AMALGAMATE=ON -DCPACK_PACKAGE_VERSION={0} -DCPACK_PACKAGE_FILE_NAME=\"crow-{0}\" .. && make -j4".format(version))
prGreen("Compiling DEB package...")
os.system("cpack -R {}".format(version))

# Create standalone package from DEB data
prGreen("Compiling Standalone tar package...")
os.chdir(releaseCodePath)
os.system("tar -czf crow-v{0}.tar.gz -C DEB/crow-{0}/usr ./".format(version))
os.system("cp crow-v{0}.tar.gz {1}/crow-v{0}.tar.gz".format(version, releasePath))

# Getting the AUR package data
prGreen("Compiling AUR package...")
os.system("git clone ssh://aur@aur.archlinux.org/crow.git AUR")

# Updating the PKGBUILD in build dir, and adding the tarball for checksum calculations
prGreen("Updating AUR package...")
os.system("cp crow-v{0}.tar.gz AUR/crow-v{0}.tar.gz".format(version))
os.system("cp ../../../scripts/PKGBUILD AUR/PKGBUILD")

# Updating Checksums
os.chdir(releaseAURPath)
prGreen("Updating AUR package Checksums...")
md5 = os.popen("md5sum crow-v{}.tar.gz | cut -c -32".format(version)).read()[:-1]
sha256 = os.popen("sha256sum crow-v{}.tar.gz | cut -c -64".format(version)).read()[:-1]
os.system("sed -i \"s/^md5sums=(\'.*\')/md5sums=(\'{}\')/\" PKGBUILD".format(md5))
os.system("sed -i \"s/^sha256sums=(\'.*\')/sha256sums=(\'{}\')/\" PKGBUILD".format(sha256))

# Updating SRCINFO file
prGreen("Updating AUR SRCINFO...")
if(os.system("which makepkg &>/dev/null") == 0):
    os.system("makepkg --printsrcinfo > .SRCINFO")
else:
    prRed("makepkg not found, AUR SRCINFO cannot be updated.")

doneStr = "Release for Crow-{0} was made successfully. To publish the release please do the following:\n" \
"1. Commit the code changes to a separate branch (Recommended name is \"{1}\").\n" \
"2. Create a tag (Github release) from the branch.\n" \
"3. Upload the \"crow-{0}.deb\", \"crow-v{0}.tar.gz\" and \"crow_all.h\" files to the release.\n" \
"4. Update the changelog in \"{2}\".\n" \
"5. push the changes to AUR (using git and only if AUR update ran without errors)." \
"6. Open issues to update the pakcages in VCPKG and ConanCenter".format(version, versionNoPatch, releaseAURPath)
prYellow(doneStr)
