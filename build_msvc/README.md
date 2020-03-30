Building DeFi Blockchain with Visual Studio
========================================

Introduction
---------------------
Solution and project files to build DeFi Blockchain applications (except Qt dependent ones) with Visual Studio 2017 can be found in the build_msvc directory.

Building with Visual Studio is an alternative to the Linux based [cross-compiler build](../doc/build-windows.md).

Dependencies
---------------------
A number of [open source libraries](../doc/dependencies.md) are required in order to be able to build the DeFi Blockchain.

Options for installing the dependencies in a Visual Studio compatible manner are:

- Use Microsoft's [vcpkg](https://docs.microsoft.com/en-us/cpp/vcpkg) to download the source packages and build locally. This is the recommended approach.
- Download the source code, build each dependency, add the required include paths, link libraries and binary tools to the Visual Studio project files.
- Use [nuget](https://www.nuget.org/) packages with the understanding that any binary files have been compiled by an untrusted third party.

The external dependencies required for the Visual Studio build are (see [dependencies.md](../doc/dependencies.md) for more info):

- Berkeley DB
- OpenSSL
- Boost
- libevent
- ZeroMQ
- RapidCheck

Additional dependencies required from the [DeFi Blockchain](https://github.com/defich/ain) GitHub repository are:
- libsecp256k1
- LevelDB

Building
---------------------
The instructions below use `vcpkg` to install the dependencies.

- Clone `vcpkg` from the [github repository](https://github.com/Microsoft/vcpkg) and install as per the instructions in the main README.md.
- Install the required packages (replace x64 with x86 as required):

```
    PS >.\vcpkg install --triplet x64-windows-static boost-filesystem boost-signals2 boost-test libevent openssl zeromq berkeleydb secp256k1 leveldb rapidcheck
```

- Use Python to generate *.vcxproj from Makefile

```
    PS >py -3 msvc-autogen.py
```

- Build in Visual Studio.
