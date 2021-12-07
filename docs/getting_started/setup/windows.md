Here's how you can install Crow on your Windows machine.
## Getting and Compiling Crow
### Using A package manager
#### VCPKG
Crow can be simply installed through VCPKG using the command `vcpkg install crow`

### Manually (source or release)
#### Microsoft Visual Studio and VCPKG
The following guide will use `example_with_all.cpp` as the Crow application for demonstration purposes. VCPKG will be used only to install Crow's dependencies.

1. Generate `crow_all.h` by navigating to the `scripts` folder and running `python3 merge_all.py ..\include crow_all.h`.
2. `git clone https://github.com/microsoft/vcpkg.git`
3. `.\vcpkg\bootstrap-vcpkg.bat`
4. `.\vcpkg\vcpkg integrate install`
5. Create empty Visual Studio project.
6. In solution explorer, right click the name of your project then click `Open Folder in File Explorer`.
7. Copy `crow_all.h`, `example_with_all.cpp`, `vcpkg.json` to opened folder.
8. Add `crow_all.h` to `Header Files` and `example_with_all.cpp` to `Source Files`.
9. In solution explorer, right click the name of your project then click `Properties`.
10. Under `vcpkg`, set `Use Vcpkg Manifest` to `Yes` and `Additional Options` to `--feature-flags="versions"`.
11. Set `Debug/Release` and `x64/x86`. 
12. Run.
