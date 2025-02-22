<!--- SPDX-License-Identifier: Apache-2.0 -->

# Installation of ONNX-MLIR on Windows

Building onnx-mlir on Windows requires building some additional prerequisites that are not available by default.

Note that the instructions in this file assume you are using [Visual Studio  2019 Community Edition](https://visualstudio.microsoft.com/downloads/) with ninja. 
It is recommended that you have the **Desktop development with C++** and **Linux development with C++** workloads installed. 
This ensures you have all toolchains and libraries needed to compile this project and its dependencies on Windows.

Run all the commands from a shell started from **"Developer Command Prompt for VS 2019"**.

## Protobuf
Build protobuf as a static library.

[same-as-file]: <> (utils/install-protobuf.cmd)
```shell
git clone --recurse-submodules https://github.com/protocolbuffers/protobuf.git
REM Check out a specific branch that is known to work with ONNX-MLIR.
REM This corresponds to the v3.11.4 tag
cd protobuf && git checkout d0bfd5221182da1a7cc280f3337b5e41a89539cf && cd ..

set root_dir=%cd%
md protobuf_build
cd protobuf_build
call cmake %root_dir%\protobuf\cmake -G "Ninja" ^
   -DCMAKE_INSTALL_PREFIX="%root_dir%\protobuf_install" ^
   -DCMAKE_BUILD_TYPE=Release ^
   -Dprotobuf_BUILD_EXAMPLES=OFF ^
   -Dprotobuf_BUILD_SHARED_LIBS=OFF ^
   -Dprotobuf_BUILD_TESTS=OFF ^
   -Dprotobuf_MSVC_STATIC_RUNTIME=OFF ^
   -Dprotobuf_WITH_ZLIB=OFF

call cmake --build . --config Release
call cmake --build . --config Release --target install
```

Before running CMake for onnx-mlir, ensure that the bin directory to this protobuf is before any others in your PATH:
```shell
set PATH=%root_dir%\protobuf_install\bin;%PATH%
```

#### MLIR
Install MLIR (as a part of LLVM-Project):

[same-as-file]: <> (utils/clone-mlir.sh)
```shell
git clone https://github.com/llvm/llvm-project.git
# Check out a specific branch that is known to work with ONNX-MLIR.
cd llvm-project && git checkout 700997aef8c1f2f08c9ac5fca61650b57a01e8b1 && cd ..
```

[same-as-file]: <> (utils/build-mlir.cmd)
```shell
set root_dir=%cd%
md llvm-project\build
cd llvm-project\build
call cmake %root_dir%\llvm-project\llvm -G "Ninja" ^
   -DCMAKE_INSTALL_PREFIX="%root_dir%\llvm-project\build\install" ^
   -DLLVM_ENABLE_PROJECTS=mlir ^
   -DLLVM_TARGETS_TO_BUILD="host" ^
   -DCMAKE_BUILD_TYPE=Release ^
   -DLLVM_ENABLE_ASSERTIONS=ON ^
   -DLLVM_ENABLE_RTTI=ON ^
   -DLLVM_ENABLE_ZLIB=OFF ^
   -DLLVM_INSTALL_UTILS=ON

call cmake --build . --config Release
call cmake --build . --config Release --target install
call cmake --build . --config Release --target check-mlir
```

## ONNX-MLIR (this project)

### Build
The following environment variables can be set before building onnx-mlir (or alternatively, they need to be passed as CMake variables):
- MLIR_DIR should point to the mlir cmake module inside an llvm-project build or install directory (e.g., c:/repos/llvm-project/build/lib/cmake/mlir).

This project uses lit ([LLVM's Integrated Tester](http://llvm.org/docs/CommandGuide/lit.html)) for unit tests. When running CMake, we can specify the path to the lit tool from LLVM using the LLVM_EXTERNAL_LIT define, as in the example below. If MLIR_DIR points to an install directory of llvm-project, LLVM_EXTERNAL_LIT is required and %lit_path% should point to a valid lit. It is not required if MLIR_DIR points to a build directory of llvm-project, which will contain lit.

To build ONNX-MLIR, use the following commands:

[same-as-file]: <> ({"ref": "utils/build-onnx-mlir.cmd", "skip-doc": 2})
```shell
git clone --recursive https://github.com/onnx/onnx-mlir.git

set root_dir=%cd%

md onnx-mlir\build
cd onnx-mlir\build
call cmake %root_dir%\onnx-mlir -G "Ninja" ^
   -DCMAKE_BUILD_TYPE=Release ^
   -DCMAKE_PREFIX_PATH=%root_dir%\protobuf_install ^
   -DLLVM_EXTERNAL_LIT=%lit_path% ^
   -DLLVM_LIT_ARGS=-v ^
   -DMLIR_DIR=%root_dir%\llvm-project\build\lib\cmake\mlir

call cmake --build . --config Release --target onnx-mlir
```
After the above commands succeed, an `onnx-mlir` executable should appear in the `Debug/bin` or `Release/bin` directory.