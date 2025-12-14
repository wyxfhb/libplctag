# Building libplctag Wrappers with CMake

The libplctag wrapper generation is fully integrated into CMake and works on 
macOS, Linux, and Windows.

## Quick Start

### Generate Wrappers with CMake

```bash
# From the project root
cd libplctag
mkdir build
cd build

# Configure with wrapper generation enabled
cmake -DBUILD_WRAPPERS=ON ..

# Build wrappers (generates C#, Python, and optionally Java)
cmake --build . --target swig_csharp swig_python
```

Generated files will be in:
- build/src/wrappers/generated/csharp/ - C# wrapper files
- build/src/wrappers/generated/python/ - Python wrapper files
- build/src/wrappers/generated/java/ - Java wrapper files (if Java is available)

## Platform-Specific Setup

### macOS

Prerequisites:
```bash
# Install SWIG
brew install swig

# Install Java (optional, for Java wrappers)
brew install openjdk
```

Build:
```bash
cd libplctag
mkdir build && cd build
cmake -DBUILD_WRAPPERS=ON ..
cmake --build . --target swig_csharp swig_python swig_java
```

### Linux (Ubuntu/Debian)

Prerequisites:
```bash
# Install build tools and SWIG
sudo apt update
sudo apt install cmake build-essential swig

# Install Java (optional, for Java wrappers)
sudo apt install default-jdk
```

Build:
```bash
cd libplctag
mkdir build && cd build
cmake -DBUILD_WRAPPERS=ON ..
cmake --build . --target swig_csharp swig_python swig_java
```

### Linux (RedHat/CentOS/Fedora)

Prerequisites:
```bash
# Install build tools and SWIG
sudo yum install cmake gcc swig

# Install Java (optional)
sudo yum install java-latest-openjdk-devel
```

Build:
```bash
cd libplctag
mkdir build && cd build
cmake -DBUILD_WRAPPERS=ON ..
cmake --build . --target swig_csharp swig_python swig_java
```

### Windows

Prerequisites (using vcpkg):
```powershell
# Install vcpkg if not already installed
# https://github.com/Microsoft/vcpkg

vcpkg install swig:x64-windows

# Install Java (optional, from java.com)
```

Build with Visual Studio:
```powershell
cd libplctag
mkdir build
cd build

# Configure (vcpkg will handle SWIG finding)
cmake -DCMAKE_TOOLCHAIN_FILE=C:\path\to\vcpkg\scripts\buildsystems\vcpkg.cmake -DBUILD_WRAPPERS=ON ..

# Build
cmake --build . --config Release --target swig_csharp swig_python
```

Build with MinGW:
```bash
cd libplctag
mkdir build
cd build
cmake -DBUILD_WRAPPERS=ON -G "MinGW Makefiles" ..
cmake --build .
```

## CMake Configuration Options

```bash
# Enable/disable wrapper generation (default: ON)
cmake -DBUILD_WRAPPERS=ON ..

# Other useful options
cmake -DBUILD_EXAMPLES=OFF -DBUILD_TESTS=OFF -DBUILD_WRAPPERS=ON ..
```

## Using Generated Wrappers

### Java

Compile the native wrapper and package into JAR:

```bash
cd build/src/wrappers/generated/java

# Compile Java classes
javac *.java

# Create JAR
jar cvf libplctag.jar com/libplctag/*.class

# Compile JNI wrapper (requires libplctag development headers)
g++ -fPIC -shared -I${JAVA_HOME}/include -I${JAVA_HOME}/include/linux \
    -I../../../../../../src/libplctag/lib \
    -o liblibplctag.so libplctag_wrap.cxx \
    -L../../../../../../build/bin_dist -lplctag

# Test
export LD_LIBRARY_PATH=.:$LD_LIBRARY_PATH
java -cp libplctag.jar:. YourJavaApp
```

### C#

Compile managed assembly:

```bash
cd build/src/wrappers/generated/csharp

# Compile C# files
csc /target:library /out:libplctag.dll *.cs

# Reference in your project
# csc /reference:libplctag.dll YourApp.cs
```

Ensure libplctag shared library is in the system library path or same directory 
as executable.

### Python

The generated libplctag.py can be used directly with ctypes:

```bash
cd build/src/wrappers/generated/python

# Copy to your project or install in site-packages
cp libplctag.py /path/to/your/project/

# Or install system-wide
sudo cp libplctag.py /usr/local/lib/python3.x/site-packages/
```

Use in Python:
```python
import sys
sys.path.insert(0, '/path/to/generated/python')
from libplctag import libplctag

tag_id = libplctag.create(...)
```

## Regenerating After libplctag.h Changes

When the C library header (src/libplctag/lib/libplctag.h) is modified:

```bash
cd build

# Reconfigure to pick up changes
cmake ..

# Regenerate wrappers
cmake --build . --target swig_csharp swig_python swig_java
```

Or manually run SWIG:

```bash
cd src/wrappers

# Generate all wrappers
swig -java -package com.libplctag -outdir generated/java \
    -o generated/java/libplctag_wrap.cxx \
    -I../libplctag/lib libplctag.i

swig -csharp -dllimport libplctag -outdir generated/csharp \
    -o generated/csharp/libplctag_wrap.cxx \
    -I../libplctag/lib libplctag.i

swig -python -outdir generated/python \
    -o generated/python/libplctag_wrap.cxx \
    -I../libplctag/lib libplctag.i
```

## Troubleshooting

### SWIG not found

Error: Could not find SWIG

Solution:
- macOS: brew install swig
- Linux: apt install swig or yum install swig
- Windows: Install with vcpkg or download from http://www.swig.org

### Java not found

Error: Could NOT find Java

The Java wrappers are optional. Install JDK if you need them:
- macOS: brew install openjdk
- Linux: apt install default-jdk
- Windows: Download from java.com

### Python not found

Python development headers are needed:
- macOS: Usually included with system Python
- Linux: apt install python3-dev
- Windows: Select "Install development headers" during Python installation

### Compiler errors in generated code

Ensure you have:
- A C/C++ compiler installed
- libplctag development headers (src/libplctag/lib/libplctag.h)
- Platform-specific build tools (build-essential on Linux, Xcode on macOS)

## Files and Directories

```
src/wrappers/
|-- libplctag.i              # SWIG interface file (defines wrapper API)
|-- CMakeLists.txt           # CMake configuration for SWIG
|-- generated/               # Output directory (created by CMake)
|   |-- java/                # Java JNI wrappers
|   |-- csharp/              # C# P/Invoke wrappers
|   +-- python/              # Python ctypes wrappers
+-- README_AUTOGENERATED.md  # Generated wrapper documentation
```

## See Also

- SWIG Documentation: http://www.swig.org
- libplctag GitHub: https://github.com/libplctag/libplctag
- CMake FindPython: https://cmake.org/cmake/help/latest/module/FindPython.html
- CMake FindJava: https://cmake.org/cmake/help/latest/module/FindJava.html
- CMake FindSWIG: https://cmake.org/cmake/help/latest/module/FindSWIG.html
