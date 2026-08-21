Chromium Embedded Framework (CEF) Standard Binary Distribution for Windows
-------------------------------------------------------------------------------

Date:             August 15, 2026

CEF Version:      151.3.18+gbeff58d+chromium-151.0.7922.138
CEF URL:          https://github.com/chromiumembedded/cef.git
                  @beff58dbc4d0fd12b3eafea8f5314ce22e649078

Chromium Version: 151.0.7922.138
Chromium URL:     https://chromium.googlesource.com/chromium/src.git
                  @41fa82442390a4d4456c78f2d69a832d5720cb27

This distribution contains all components necessary to build and distribute an
application using CEF on the Windows platform. Please see the LICENSING
section of this document for licensing terms and conditions.


CONTENTS
--------

bazel       Contains Bazel configuration files shared by all targets.

cmake       Contains CMake configuration files shared by all targets.

Debug       Contains libcef.dll, libcef.lib and other components required to
            build and run the debug version of CEF-based applications. By
            default these files should be placed in the same directory as the
            executable and will be copied there as part of the build process.

include     Contains all required CEF header files.

libcef_dll  Contains the source code for the libcef_dll_wrapper static library
            that all applications using the CEF C++ API must link against.

Release     Contains libcef.dll, libcef.lib and other components required to
            build and run the release version of CEF-based applications. By
            default these files should be placed in the same directory as the
            executable and will be copied there as part of the build process.

Resources   Contains resources required by libcef.dll. By default these files
            should be placed in the same directory as libcef.dll and will be
            copied there as part of the build process.

tests/      Directory of tests that demonstrate CEF usage.

  cefclient Contains the cefclient sample application configured to build
            using the files in this distribution. This application demonstrates
            a wide range of CEF functionalities.

  cefsimple Contains the cefsimple sample application configured to build
            using the files in this distribution. This application demonstrates
            the minimal functionality required to create a browser window.

  ceftests  Contains unit tests that exercise the CEF APIs.

  gmock     Contains the Google C++ Mocking Framework used by the ceftests
            target.

  gtest     Contains the Google C++ Testing Framework used by the ceftests
            target.

  shared    Contains source code shared by the cefclient and ceftests targets.


USAGE
-----

Building using CMake:
  To configure and build the normal cefclient sample with Visual Studio 2022:

     $ cmake -S . -B build -G "Visual Studio 17 2022" -A x64
     $ cmake --build build --config Debug --target cefclient

  For a 32-bit or ARM64 binary distribution, replace x64 with Win32 or arm64,
  respectively. See the usage instructions at the top of CMakeLists.txt for
  other generators and build options.

  Installer-managed cefclient:
  To build cefclient so it can locate or install compatible CEF on first run,
  use a separate build directory, add -DUSE_INSTALLER=On to the configure
  command above, and build the cefclient target in Release.

  USE_INSTALLER=On is Windows-only, requires USE_SANDBOX=On, and supports
  Release builds only. You only need to request cefclient; CMake builds its
  required libcef_dll_wrapper dependency and may create additional generator
  utility targets.

  The Release output contains:
  - cefclient.exe, a byte-for-byte copy of Release\bootstrap.exe
  - cefclient.dll, with the managed installer configuration embedded
  - chrome_elf.dll
  - normal compiler and linker artifacts, such as .lib, .exp, and .pdb files

  A compatible signed CEF distribution will be installed or located on first
  run.

  Before shipping:
  1. Open tests\cefclient\win\installer_config_managed.json and replace the
     sample appid with your application's permanent UUID.
  2. Sign cefclient.exe, cefclient.dll, and chrome_elf.dll. chrome_elf.dll must
     use the same signing certificate as cefclient.exe.
  3. Review the production identity, signing, publication, and installer
     requirements in the CEF Installer documentation:
     https://chromiumembedded.github.io/cef/installer.html

  This example supports normal first-run CEF resolution. It does not modify
  bootstrap.exe resources, so standalone installer mode and explicit commands
  such as /cef-update and /cef-uninstall are not enabled.

  Compatibility details: The embedded configuration accepts CEF releases from
  its selected API version through minor version 99 of the same API major. It
  uses explicit launch-health reporting, which cefclient handles automatically.

Building using Bazel:
  Bazel can be used to build CEF-based applications. CEF support for Bazel is
  considered experimental. For current development status see
  https://github.com/chromiumembedded/cef/issues/3757.

  To build the bundled cefclient sample application using Bazel:

  1. Install Bazelisk [https://github.com/bazelbuild/bazelisk/blob/master/README.md]
  2. Build using Bazel:
     $ bazel build //tests/cefclient
  3. Run using Bazel:
     $ bazel run //tests/cefclient/win:cefclient.exe

  Other sample applications (cefsimple, ceftests) can be built in the same way.

  Additional notes:
  - To generate a Debug build add `-c dbg` (both `build` and `run`
    command-line).
  - To pass arguments using the `run` command add `-- [...]` at the end.
  - Windows x86 and ARM64 builds using Bazel may be broken, see
    https://github.com/bazelbuild/bazel/issues/22164.

Please visit the CEF Website for additional usage information.

https://github.com/chromiumembedded/cef/


REDISTRIBUTION
--------------

This binary distribution contains the below components.

Required components:

The following components are required. CEF will not function without them.

* CEF core library.
  * libcef.dll

* Crash reporting library.
  * chrome_elf.dll

* Unicode support data.
  * icudtl.dat

* V8 snapshot data.
  * v8_context_snapshot.bin

Optional components:

The following components are optional. If they are missing CEF will continue to
run but any related functionality may become broken or disabled.

* Localized resources.
  Locale file loading can be disabled completely using
  CefSettings.pack_loading_disabled. The locales directory path can be
  customized using CefSettings.locales_dir_path. 
 
  * locales/
    Directory containing localized resources used by CEF, Chromium and Blink. A
    .pak file is loaded from this directory based on the CefSettings.locale
    value. Only configured locales need to be distributed. If no locale is
    configured the default locale of "en-US" will be used. Without these files
    arbitrary Web components may display incorrectly.

* Other resources.
  Pack file loading can be disabled completely using
  CefSettings.pack_loading_disabled. The resources directory path can be
  customized using CefSettings.resources_dir_path.

  * chrome_100_percent.pak
  * chrome_200_percent.pak
  * resources.pak
    These files contain non-localized resources used by CEF, Chromium and Blink.
    Without these files arbitrary Web components may display incorrectly.

* Direct3D support.
  * d3dcompiler_47.dll
  Support for GPU accelerated rendering of HTML5 content like 2D canvas, 3D CSS
  and WebGL. Without this file the aforementioned capabilities may fail when GPU
  acceleration is enabled (default in most cases). Use of this bundled version
  is recommended instead of relying on the possibly old and untested system
  installed version.

* DirectX compiler support (x64 only).
  * dxil.dll
  * dxcompiler.dll
  Support for DirectX rendering of WebGPU. Without these files the
  aforementioned capabilities may fail.

* ANGLE support.
  * libEGL.dll
  * libGLESv2.dll
  Support for rendering of HTML5 content like 2D canvas, 3D CSS and WebGL.
  Without these files the aforementioned capabilities may fail.

* SwANGLE support.
  * vk_swiftshader.dll
  * vk_swiftshader_icd.json
  * vulkan-1.dll
  Support for software rendering of HTML5 content like 2D canvas, 3D CSS and
  WebGL using SwiftShader's Vulkan library as ANGLE's Vulkan backend. Without
  these files the aforementioned capabilities may fail when GPU acceleration is
  disabled or unavailable.


LICENSING
---------

The CEF project is BSD licensed. Please read the LICENSE.txt file included with
this binary distribution for licensing terms and conditions. Other software
included in this distribution is provided under other licenses. Please see the
CREDITS.html file or visit "about:credits" in a CEF-based application for
complete Chromium and third-party licensing information.
