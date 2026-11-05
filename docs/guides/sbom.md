Crow has the ability to generate a SBOM (Software Bill of Materials) at build using [`cmake-sbom`](https://github.com/DEMCON/cmake-sbom) in SPDX format.

## Prerequisites

The CMake module `cmake-sbom` expects the `spdx-tools`, `reuse`, and `ntia-conformance-checker` Python packages as external prerequisites. 

## Example Steps

Below you'll find the steps to add SBOM generation. 

### Install Python Packages

Create a Python virtual environment first so you're not installing Python packages globally:

1. Create and navigate to the build directory:<br> `mkdir build && cd build`
2. Create the venv:<br> `python3 -m venv .venv`
3. Activate the venv (macOS/Linux):<br> `source .venv/bin/activate`
4. Install the required Python packages:<br> `pip install spdx-tools reuse ntia-conformance-checker`

### Build Crow with cmake-sbom

Steps to generate a `.spdx` file using CMake for your project:

1. If you didn't create the `build` directory, create and navigate to the build directory:<br> `mkdir build && cd build`
2. Enable the build option:<br> `cmake .. -DCROW_GENERATE_SBOM=ON -DPython3_EXECUTABLE=$(which python3)`
3. Then run:<br> `cmake --build .`
4. Run the install:<br> `cmake --install . --prefix /tmp/crow-install`

Check for the generated .spdx file in the `build/` directory. The file name follows this pattern:<br> `crow-<version>-<date>.spdx`

!!! note
    You must pass `-DPython3_EXECUTABLE=$(which python3)` so CMake uses the venv's Python instead of the system Python. Without it,
    the SBOM verification step will fail because the system Python won't have the required packages installed. 

!!! note
    The .spdx file is generated when you run the `cmake --install . --prefix /tmp/crow-install`

!!! note
    You can also combine `-DCROW_GENERATE_SBOM=ON` with conditional dependencies:<br> `cmake .. -DCROW_GENERATE_SBOM=ON -DCROW_ENABLE_SSL=ON -DCROW_ENABLE_COMPRESSION=ON`
