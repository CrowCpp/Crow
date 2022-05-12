#!/bin/sh
################################################################################
# Title         : generateDocumentationAndDeploy.sh
# Date created  : 2016/02/22
# Notes         : This script was modified to suit Crow and work with mkdocs (multiple versions) and Drone.io CI.
__AUTHOR__="Jeroen de Bruijn"
# Preconditions:
# - Packages doxygen doxygen-doc doxygen-latex doxygen-gui graphviz
#   must be installed.
# - Doxygen configuration file must have the destination directory empty and
#   source code directory with a $(TRAVIS_BUILD_DIR) prefix.
# - A gh-pages branch should already exist. See below for more info on how to
#   create a gh-pages branch.
#
# Required global variables:
# - ~TRAVIS_BUILD_NUMBER~ DRONE_BUILD_NUMBER : The number of the current build.
# - ~TRAVIS_COMMIT~ DRONE_COMMIT             : The commit that the current build is testing.
# - DOXYFILE                                 : The Doxygen configuration file.
# - GH_REPO_NAME                             : The name of the repository.
# - GH_REPO_REF                              : The GitHub reference to the repository.
# - GH_REPO_TOKEN                            : Secure token to the github repository.
# - DOCS_VERSION                             : Used for custom version documentation (defaults to master).
#
# For information on how to encrypt variables for Travis CI please go to
# https://docs.travis-ci.com/user/environment-variables/#Encrypted-Variables
# or https://gist.github.com/vidavidorra/7ed6166a46c537d3cbd2
# For information on how to create a clean gh-pages branch from the master
# branch, please go to https://gist.github.com/vidavidorra/846a2fc7dd51f4fe56a0
#
# This script will generate Doxygen documentation and push the documentation to
# the gh-pages branch of a repository specified by GH_REPO_REF.
# Before this script is used there should already be a gh-pages branch in the
# repository.
#
# The branch should be empty except for an empty '.nojekyll' file and an
# 'index.html' file that redirects to master/index.html. Optionally you would
# also need a 'CNAME' file containing your custom domain (without http or www).
# 
################################################################################
################################################################################


################################################################################
#####        Setup this script and get the current gh-pages branch.        #####

echo 'Setting up the script...'
# Exit with nonzero exit code if anything fails
set -e

DOCS_VERSION=${DOCS_VERSION:-"master"}
echo "Using $DOCS_VERSION as version."

# Create a clean working directory for this script.
mkdir code_docs
cd code_docs

# Get the current gh-pages branch
git clone -b gh-pages https://git@$GH_REPO_REF
git clone https://$THEME_REPO_REF
cd $GH_REPO_NAME

##### Configure git.
# Set the push default to simple i.e. push only the current branch.
git config --global push.default simple
# Pretend to be an user called Drone CI.
git config user.name "Drone CI"
git config user.email "drone@drone.io"

# Create a new directory for this version of the docs if one doesn't exist.
# Then navigate to that directory.
mkdir -p $DOCS_VERSION
cd $DOCS_VERSION

################################################################################
#####      Generate the MkDocs documentation to replace the old docs.      #####

echo 'Removing old documentation...'
# Remove everything currently in the version directory.
# GitHub is smart enough to know which files have changed and which files have
# stayed the same and will only update the changed files. So the gh-pages branch
# can be safely cleaned, and it is sure that everything pushed later is the new
# documentation.
rm -rf *

echo 'Generating MkDocs documentation...'
# Copy the mkdocs documentation to the work directory and generate the mkdocs' 
# 'site' directory
cp ../../../mkdocs.yml .
cp -r ../../../docs .
mkdocs build -v

# Need to create a .nojekyll file to allow filenames starting with an underscore
# to be seen on the gh-pages site. Therefore creating an empty .nojekyll file.
# Presumably this is only needed when the SHORT_NAMES option in Doxygen is set
# to NO, which it is by default. So creating the file just in case.
#echo "" > .nojekyll

################################################################################
#####     Generate the Doxygen code documentation and log the output.      #####

echo 'Generating Doxygen code documentation...'
# Redirect both stderr and stdout to the log file AND the console.
doxygen $DOXYFILE 2>&1 | tee doxygen.log

# Rename mkdocs' output folder to 'html' to retain compatibility with the 
# existing index.html and the rest of this code.
# Also remove any remaining documentation files.
mv  site/* .
rm -r site
rm mkdocs.yml
rm -r docs
mv versions.json ../

################################################################################
#####  Upload the documentation to the gh-pages branch of the repository.  #####
# Only upload if Doxygen successfully created the documentation.
# Check this by verifying that the reference directory (for doxygen) and 
# the file index.html (for mkdocs) both exist. 
# This is a good indication that Doxygen and Mkdocs did their work.
if [ -d "reference" ] && [ -f "index.html" ]; then

    echo 'Uploading documentation to the gh-pages branch...'
    # Add everything in this directory (the Doxygen code documentation) to the
    # gh-pages branch.
    # GitHub is smart enough to know which files have changed and which files have
    # stayed the same and will only update the changed files.
    git add --all

    # Commit the added files with a title and description containing the Travis CI
    # build number and the GitHub commit reference that issued this build.
    git commit -m "Deploy code docs to GitHub Pages Drone build: ${DRONE_BUILD_NUMBER}" -m "Commit: ${DRONE_COMMIT}"

    # Force push to the remote gh-pages branch.
    # The ouput is redirected to /dev/null to hide any sensitive credential data
    # that might otherwise be exposed.
    git push --force "https://${GH_REPO_TOKEN}@${GH_REPO_REF}" > /dev/null 2>&1
else
    echo '' >&2
    echo 'Warning: No documentation (html) files have been found!' >&2
    echo 'Warning: Not going to push the documentation to GitHub!' >&2
    exit 1
fi
