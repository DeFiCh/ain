# Quick build notes

> NOTE: This section is a work in progress for DeFi Blockchain.

DeFi Blockchain is built with the same process as Bitcoin, but provides certain convenience steps to 
build it easily with the `./make.sh` file in the root directory.

```
$ ./make.sh 
Usage: ./make.sh <commands>

Commands:
        build
        clean
        clean-depends
        clean-mac-sdk
        deploy
        docker-build
        docker-build-deploy-git
        docker-clean
        docker-clean-images
        docker-deploy
        docker-package
        docker-package-git
        docker-purge
        docker-release
        docker-release-git
        git-version
        help
        package
        pkg-ensure-mac-sdk
        pkg-install-deps
        purge
        release
        sign

Note: All non-docker commands assume that it's run on an environment 
with correct arch and the pre-requisites properly configured. 
```

More documentation on this will be update shortly. Meanwhile, please take a look at the [UNIX build process](./build-unix.md)