Required:
CMake
vcpkg (optional, but recommended)

Clone vcpkg into ~/vcpkg and run the bootstrap script to build it.

Regarding CMake, you can either use the CLI to build the project, or you can use an IDE that supports CMake, such as Visual Studio or CLion.

Ninja is our C++ build system of choice, but you can use any generator that CMake supports.

Blue Emu has these external dependencies:
imgui
SDL2
libzip

imgui is included as code. The other two can either be installed on your system, or you can use vcpkg via CMake to install them.

The easiest way to compile the project is to use Visual Studio.