#!/bin/bash

##### Different color outputs (to distinguish errors from standard output) #####
prGreen() { 
echo -e "\e[36m-->\e[92m $1\e[00m" 
}
prRed() { 
echo -e "\e[36m-->\e[91m $1\e[00m" 
}
prYellow() { 
echo -e "\e[36m-->\e[93m $1\e[00m" 
}

##### Check whether the script is called properly #####
if [ $# != 1 ]
then
    echo "Usage: $0 <VERSION>"
    echo "<VERSION> should be similar to \"1.2+3\" (+3 is optional)"
    exit 1;
fi

##### Defining the different ways the version is represented #####
VERSION=$1
VERSION_NO_PATCH=${VERSION/+*/}
VERSION_VCPKG=$(echo $VERSION | tr + .)

##### Defining the paths used #####
SCRIPT_PATH=$(dirname $(realpath $BASH_SOURCE))
SOURCE_PATH=$SCRIPT_PATH/../include/crow
RELEASE_PATH=$SCRIPT_PATH/../build_release
RELEASE_CODE_PATH=$SCRIPT_PATH/../build_release/_CPack_Packages/Linux
RELEASE_AUR_PATH=$SCRIPT_PATH/../build_release/_CPack_Packages/Linux/AUR

if [ -d $RELEASE_PATH ]
then
    prYellow "Detected existing release directory, overriding..."
    rm -rf $RELEASE_PATH
fi

##### Create the dir #####
prGreen "Creating new release directory..."
mkdir $RELEASE_PATH

##### Changing "master" version to <version_number> #####
cd $SOURCE_PATH
prGreen "Applying new version to Crow server name..."
sed -i "s/char VERSION\\[\\] = \".*\";/char VERSION\\[\\] = \"$VERSION_NO_PATCH\";/g" version.h
prGreen "Applying new version to vcpkg..."
sed -i "s/\"version-string\": \".*\",/\"version\": \"$VERSION_VCPKG\",/g" ../../vcpkg.json
prGreen "Applying new version to PKGBUILD..."
sed -i "s/^pkgver=.*/pkgver=$VERSION/" $SCRIPT_PATH/PKGBUILD
sed -i "s/^pkgrel=.*/pkgrel=1/" $SCRIPT_PATH/PKGBUILD

##### Running CMake to compile crow_all and the deb package #####
cd $RELEASE_PATH
prGreen "Compiling Release..."
cmake -DCROW_BUILD_EXAMPLES=OFF -DCROW_BUILD_TESTS=OFF -DCROW_AMALGAMATE=ON -DCPACK_PACKAGE_VERSION=$VERSION -DCPACK_PACKAGE_FILE_NAME="crow-$VERSION" .. && make -j4
prGreen "Compiling DEB package..."
cpack -R $VERSION

##### Create standalone package from DEB data #####
prGreen "Compiling Standalone tar package..."
cd $RELEASE_CODE_PATH
tar -czf crow-v$VERSION.tar.gz -C DEB/crow-$VERSION/usr ./
cp crow-v$VERSION.tar.gz $RELEASE_PATH/crow-v$VERSION.tar.gz

##### Getting the AUR package data #####
prGreen "Cloning AUR package..."
git clone ssh://aur@aur.archlinux.org/crow.git AUR

##### Updating the PKGBUILD in build dir, and adding the tarball for checksum calculations #####
prGreen "Updating AUR package..."
cp crow-v$VERSION.tar.gz AUR/crow-v$VERSION.tar.gz
cp $SCRIPT_PATH/PKGBUILD AUR/PKGBUILD

##### Updating Checksums #####
cd $RELEASE_AUR_PATH
prGreen "Updating AUR package Checksums..."
MD5=$(md5sum crow-v$VERSION.tar.gz | cut -c -32)
SHA256=$(sha256sum crow-v$VERSION.tar.gz | cut -c -64)
sed -i "s/^md5sums=.*/md5sums=(\'$MD5\')/" PKGBUILD
sed -i "s/^sha256sums=.*/sha256sums=(\'$SHA256\')/" PKGBUILD

##### Updating SRCINFO file #####
prGreen "Updating AUR SRCINFO..."
which makepkg &>/dev/null
if [ $? -eq 0 ]
then
    makepkg --printsrcinfo > .SRCINFO
else
    prRed "makepkg not found, AUR SRCINFO cannot be updated."
fi

##### Give instructions on how to upload the finished release #####
prYellow "Release for Crow-$VERSION was made successfully. To publish the release please do the following:
1. Commit the code changes to a separate branch (Recommended name is \"$VERSION_NO_PATCH\").
2. Create a tag (Github release) from the branch.
3. Upload the \"crow-$VERSION.deb\", \"crow-v$VERSION.tar.gz\" and \"crow_all.h\" files to the release.
4. Update the changelog in \"$RELEASE_AUR_PATH\".
5. push the changes to AUR (using git and only if AUR update ran without errors).
6. Open issues to update the packages in VCPKG and ConanCenter."

exit 0
