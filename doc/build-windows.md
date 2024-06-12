WINDOWS BUILD NOTES
====================

> NOTE: This section is a work in progress for DeFi Blockchain, and may not be applicable at it's current state.

Below are some notes on how to build DeFiChain for Windows.

The options known to work for building DeFiChain on Windows are:

* On Linux, using the [Mingw-w64](https://mingw-w64.org/doku.php) cross compiler tool chain. Ubuntu Jammy Jellyfish 22.04 is required
and is the platform used to build the DeFiChain Windows release binaries.
* On Windows, using [Windows
Subsystem for Linux (WSL)](https://msdn.microsoft.com/commandline/wsl/about) and the Mingw-w64 cross compiler tool chain.

Other options which may work, but which have not been extensively tested are (please contribute instructions):

* On Windows, using a POSIX compatibility layer application such as [cygwin](http://www.cygwin.com/) or [msys2](http://www.msys2.org/).
* On Windows, using a native compiler tool chain such as [Visual Studio](https://www.visualstudio.com).

Installing Windows Subsystem for Linux
---------------------------------------

Follow the upstream installation instructions, available [here](https://learn.microsoft.com/en-us/windows/wsl/install).

Cross-compilation for Ubuntu and Windows Subsystem for Linux
------------------------------------------------------------

Note that for WSL the DeFiChain source path MUST be somewhere in the default mount file system, for
example /usr/src/ain, AND not under /mnt/d/. If this is not the case the dependency autoconf scripts will fail.
This means you cannot use a directory that is located directly on the host Windows file system to perform the build.

The steps below can be performed on Ubuntu (including in a VM) or WSL.

Acquire the source in the usual way:

    git clone https://github.com/DeFiCh/ain.git
    cd ain

The first step is to install the system wide dependencies.

    sudo TARGET=x86_64-w64-mingw32 ./make.sh ci-setup-deps

Next install the user dependencies.

    TARGET=x86_64-w64-mingw32 ./make.sh ci-setup-user-deps

At this point you MUST restart the VM or close and reopen WSL.

Install the Rust toolchain for Windows.

    rustup target add x86_64-pc-windows-gnu

Build using:

    PATH=$(echo "$PATH" | sed -e 's/:\/mnt.*//g') # strip out problematic Windows %PATH% imported var
    TARGET=x86_64-w64-mingw32 ./make.sh build

## Depends system

For further documentation on the depends system see [README.md](../depends/README.md) in the depends directory.

Footnotes
---------

<a name="footnote1">1</a>: Starting from Ubuntu Xenial 16.04, both the 32 and 64 bit Mingw-w64 packages install two different
compiler options to allow a choice between either posix or win32 threads. The default option is win32 threads which is the more
efficient since it will result in binary code that links directly with the Windows kernel32.lib. Unfortunately, the headers
required to support win32 threads conflict with some of the classes in the C++11 standard library, in particular std::mutex.
It's not possible to build the DeFiChain code using the win32 version of the Mingw-w64 cross compilers (at least not without
modifying headers in the Bitcoin Core source code).
