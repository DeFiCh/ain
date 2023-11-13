# Quick build notes

DeFiChain is built with the same process as Bitcoin, but provides certain convenience steps to 
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
        check
        check-cpp
        check-enter-build-rs-dir
        check-git-dirty
        check-lints
        check-py
        check-rs
        check-sh
        ci-export-vars
        ci-setup-deps
        ci-setup-deps-target
        ci-setup-user-deps
        clean
        clean-artifacts
        clean-conf
        clean-depends
        clean-pkg-local-osx-sysroot
        clean-pkg-local-py-deps
        compiledb
        debug-env
        deploy
        docker-build
        docker-clean-all
        docker-clean-builds
        docker-deploy
        docker-release
        exec
        fmt
        fmt-cpp
        fmt-lib
        fmt-py
        fmt-rs
        get-default-ci-group-logs
        get-default-conf-args
        get-default-docker-file
        get-default-jobs
        get-default-target
        get-default-use-clang
        get-rust-triplet
        git-add-hooks
        git-version
        help
        lib
        package
        pkg-install-deps
        pkg-install-deps-arm64
        pkg-install-deps-armhf
        pkg-install-deps-mingw-x86-64
        pkg-install-deps-osx-tools
        pkg-install-llvm
        pkg-local-ensure-osx-sysroot
        pkg-local-ensure-py-deps
        pkg-local-install-py-deps
        pkg-setup-locale
        pkg-setup-mingw-x86-64
        pkg-update-base
        pkg-user-install-rust
        pkg-user-setup-rust
        purge
        py-ensure-env-active
        py-env-activate
        py-env-deactivate
        release
        rust-analyzer-check
        test
        test-cpp
        test-py
        test-rs
        test-unit

Note: All commands without docker-* prefix assume that it's run in
an environment with correct arch and pre-requisites configured.
(most pre-requisites can be installed with pkg-* commands).
```


## `TARGET` values

### Tier 1

- x86_64-pc-linux-gnu
- aarch64-linux-gnu

Usage: Core team + critical infrastructure depends on these. Well tested.

### Tier 2

- x86_64-apple-darwin
- x86_64-w64-mingw32
- aarch64-apple-darwin

Usage: Core teams rely on these for development, ecosystem products built on it or
key for end user UX, but lower priority than critical infrastructure.

### Tier 3

- arm-linux-gnueabihf

Usage: Best effort. Compilation may be broken at times, expected to work mostly,
but receives little to no testing.

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
    DOCKERFILES_DIR=${DOCKERFILES_DIR:-"./contrib/dockerfiles"}

    ROOT_DIR="$(_canonicalize "${_SCRIPT_DIR}")"

    TARGET=${TARGET:-"$(get_default_target)"}
    DOCKERFILE=${DOCKERFILE:-"$(get_default_docker_file)"}

    BUILD_DIR=${BUILD_DIR:-"./build"}
    BUILD_DIR="$(_canonicalize "$BUILD_DIR")"
    # Was previously ${BUILD_DIR}/$TARGET for host specific
    # But simplifying this since autotools conf ends up in reconf and
    # rebuilds anyway, might as well just point manually if needed
    BUILD_TARGET_DIR="${BUILD_DIR}"
    BUILD_DEPENDS_DIR=${BUILD_DEPENDS_DIR:-"${BUILD_DIR}/depends"}
    BUILD_DEPENDS_DIR="$(_canonicalize "$BUILD_DEPENDS_DIR")"
    PYTHON_VENV_DIR=${PYTHON_VENV_DIR:-"${BUILD_DIR}/pyenv"}

    CLANG_DEFAULT_VERSION=${CLANG_DEFAULT_VERSION:-"15"}
    RUST_DEFAULT_VERSION=${RUST_DEFAULT_VERSION:-"1.72"}

    MAKE_DEBUG=${MAKE_DEBUG:-"1"}
    MAKE_USE_CLANG=${MAKE_USE_CLANG:-"$(get_default_use_clang)"}

    if [[ "${MAKE_USE_CLANG}" == "1" ]]; then
        local clang_ver="${CLANG_DEFAULT_VERSION}"
        export CC=clang-${clang_ver}
        export CXX=clang++-${clang_ver}
    fi

    MAKE_JOBS=${MAKE_JOBS:-"$(get_default_jobs)"}

    MAKE_CONF_ARGS="$(get_default_conf_args) ${MAKE_CONF_ARGS:-}"
    if [[ "${MAKE_DEBUG}" == "1" ]]; then
      MAKE_CONF_ARGS="${MAKE_CONF_ARGS} --enable-debug";
    fi

    MAKE_CONF_ARGS_OVERRIDE="${MAKE_CONF_ARGS_OVERRIDE:-}"
    if [[ "${MAKE_CONF_ARGS_OVERRIDE}" ]]; then
        MAKE_CONF_ARGS="${MAKE_CONF_ARGS_OVERRIDE}"
    fi

    MAKE_ARGS=${MAKE_ARGS:-}
    MAKE_DEPS_ARGS=${MAKE_DEPS_ARGS:-}
    TESTS_FAILFAST=${TESTS_FAILFAST:-"0"}
    TESTS_COMBINED_LOGS=${TESTS_COMBINED_LOGS:-"0"}
    CI_GROUP_LOGS=${CI_GROUP_LOGS:-"$(get_default_ci_group_logs)"}
```

Please read the `./make.sh` file for more details on the build helpers.
[UNIX build process](./build-unix.md) should also have more info though using
`./make.sh` is recommended as it builds out-of-tree by default and supports 
multiple targets.
