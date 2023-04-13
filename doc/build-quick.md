# Quick build notes

> NOTE: This section is a work in progress for DeFi Blockchain.

DeFi Blockchain is built with the same process as Bitcoin, but provides certain convenience steps to 
build it easily with the `./make.sh` file in the root directory.

```
$ ./make.sh 
Usage: ./make.sh <commands>

`./make.sh build` or `./make.sh docker-build` are your friends :) 

Commands:
        build
        build-conf
        build-deps
        build-make
        ci-export-vars
        clean
        clean-conf
        clean-depends
        clean-pkg-local-osx-sysroot
        deploy
        docker-build
        docker-clean-all
        docker-clean-builds
        docker-deploy
        docker-release
        exec
        git-version
        help
        package
        pkg-install-deps
        pkg-install-deps-arm64
        pkg-install-deps-armhf
        pkg-install-deps-mingw-x86-64
        pkg-install-deps-osx-tools
        pkg-install-llvm
        pkg-install-rust
        pkg-local-ensure-osx-sysroot
        pkg-update-base
        purge
        release
        test

Note: All commands without docker-* prefix assume that it's run in
an environment with correct arch and pre-requisites configured. 
(most pre-requisites can be installed with pkg-* commands).
```

## `TARGET` values

### Supported

- x86_64-pc-linux-gnu
- x86_64-w64-mingw32
- x86_64-apple-darwin

### Best effort

- aarch64-linux-gnu
- arm-linux-gnueabihf
- arm-apple-darwin

## Defined `env` variables

```
    IMAGE_PREFIX=${IMAGE_PREFIX:-"defichain"}
    GIT_VERSION=${GIT_VERSION:-0}
    if [[ "$GIT_VERSION" == 1 ]]; then
        IMAGE_VERSION=${IMAGE_VERSION:-"$(git_version 0)"}
    else 
        IMAGE_VERSION=${IMAGE_VERSION:-"latest"}
    fi

    DOCKER_ROOT_CONTEXT=${DOCKER_ROOT_CONTEXT:-"."}
    DOCKERFILE=${DOCKERFILE:-""}
    DOCKERFILES_DIR=${DOCKERFILES_DIR:-"./contrib/dockerfiles"}

    ROOT_DIR="$(_canonicalize "${_SCRIPT_DIR}")"

    TARGET=${TARGET:-"$(_get_default_target)"}

    RELEASE_DIR=${RELEASE_DIR:-"./build"}
    RELEASE_DIR="$(_canonicalize "$RELEASE_DIR")"
    RELEASE_TARGET_DIR="${RELEASE_DIR}/${TARGET}"
    DEPENDS_DIR=${DEPENDS_DIR:-"${RELEASE_DIR}/depends"}
    DEPENDS_DIR="$(_canonicalize "$DEPENDS_DIR")"

    CLANG_DEFAULT_VERSION=${CLANG_DEFAULT_VERSION:-"15"}
    MAKE_DEBUG=${MAKE_DEBUG:-"0"}

    local default_compiler_flags=""
    if [[ "${TARGET}" == "x86_64-pc-linux-gnu" ]]; then
        local clang_ver="${CLANG_DEFAULT_VERSION}"
        default_compiler_flags="CC=clang-${clang_ver} CXX=clang++-${clang_ver}"
    fi

    MAKE_JOBS=${MAKE_JOBS:-"$(_nproc)"}

    MAKE_CONF_ARGS="$(_get_default_conf_args) ${MAKE_CONF_ARGS:-}"
    MAKE_CONF_ARGS="${default_compiler_flags} ${MAKE_CONF_ARGS:-}"
    if [[ "${MAKE_DEBUG}" == "1" ]]; then
      MAKE_CONF_ARGS="${MAKE_CONF_ARGS} --enable-debug";
    fi

    MAKE_CONF_ARGS_OVERRIDE="${MAKE_CONF_ARGS_OVERRIDE:-}"
    if [[ "${MAKE_CONF_ARGS_OVERRIDE}" ]]; then
        MAKE_CONF_ARGS="${MAKE_CONF_ARGS_OVERRIDE}"
    fi

    MAKE_ARGS=${MAKE_ARGS:-}
    MAKE_DEPS_ARGS=${MAKE_DEPS_ARGS:-}
```

Please read the `./make.sh` file for more details on the build helpers.
[UNIX build process](./build-unix.md) should also have more info though using
`./make.sh` is recommended as it builds out-of-tree by default and supports 
multiple targets.
