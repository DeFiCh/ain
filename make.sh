#!/usr/bin/env bash

# Copyright (c) DeFi Blockchain Developers
# Maker script

# Note: Ideal to use POSIX C.UTF-8, however Apple systems don't have
# this locale and throws a fit, so en-US.UTF-8 is reasonable middle ground.
export LC_ALL=en_US.UTF-8
set -Eeuo pipefail

setup_vars() {
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

    TARGET=${TARGET:-"$(_get_default_target)"}
    DOCKERFILE=${DOCKERFILE:-"$(_get_default_docker_file)"}

    BUILD_DIR=${BUILD_DIR:-"./build"}
    BUILD_DIR="$(_canonicalize "$BUILD_DIR")"
    # Was previously ${BUILD_DIR}/$TARGET for host specific
    # But simplifying this since autotools conf ends up in reconf and
    # rebuilds anyway, might as well just point manually if needed
    BUILD_TARGET_DIR="${BUILD_DIR}"
    BUILD_DEPENDS_DIR=${BUILD_DEPENDS_DIR:-"${BUILD_DIR}/depends"}
    BUILD_DEPENDS_DIR="$(_canonicalize "$BUILD_DEPENDS_DIR")"

    CLANG_DEFAULT_VERSION=${CLANG_DEFAULT_VERSION:-"15"}
    RUST_DEFAULT_VERSION=${RUST_DEFAULT_VERSION:-"1.70"}
    
    MAKE_DEBUG=${MAKE_DEBUG:-"1"}

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
}

main() {
    _ensure_script_dir
    trap _cleanup 0 1 2 3 6 15 ERR
    cd "$_SCRIPT_DIR"
    _platform_init
    setup_vars
    git_add_hooks

    # Get all functions declared in this file except ones starting with
    # '_' or the ones in the list
    # shellcheck disable=SC2207
    COMMANDS=($(declare -F | cut -d" " -f3 | grep -v -E "^_.*$|main|setup_vars")) || true

    # Commands use `-` instead of `_` for getopts consistency. Flip this.
    local cmd=${1:-} && cmd="${cmd//-/_}"

    local x
    for x in "${COMMANDS[@]}"; do
        if [[ "$x" == "$cmd" ]]; then
            shift
            ${cmd} "$@"
            return 0
        fi
    done

    help
    return 1
}

_ensure_script_dir() {
    _WORKING_DIR="$(pwd)"
    local dir
    dir="$(dirname "${BASH_SOURCE[0]}")"
    _SCRIPT_DIR="$(cd "${dir}/" && pwd)"
}

_cleanup() {
    cd "$_WORKING_DIR"
}

help() {
    echo "Usage: $0 <commands>"
    printf "\n\`%s build\` or \`%s docker-build\` are your friends :) \n" "$0" "$0"
    printf "\nCommands:\n"
    printf "\t%s\n" "${COMMANDS[@]//_/-}"
    printf "\nNote: All commands without docker-* prefix assume that it's run in\n" 
    printf "an environment with correct arch and pre-requisites configured. \n"
    printf "(most pre-requisites can be installed with pkg-* commands). \n"
}

# ----------- Direct builds ---------------

build_deps() {
    local target=${1:-${TARGET}}
    local make_deps_args=${MAKE_DEPS_ARGS:-}
    local make_jobs=${MAKE_JOBS}
    local src_depends_dir=${ROOT_DIR}/depends
    local build_depends_dir=${BUILD_DEPENDS_DIR}

    echo "> build-deps: target: ${target} / deps_args: ${make_deps_args} / jobs: ${make_jobs}"
    
    _ensure_enter_dir "$build_depends_dir"
    if [[ "$target" =~ .*darwin.* ]]; then
        pkg_local_ensure_osx_sysroot
    fi
    
    _fold_start "build-deps"

    # shellcheck disable=SC2086
    make -C "${src_depends_dir}" DESTDIR="${build_depends_dir}" \
        HOST="${target}" -j${make_jobs} ${make_deps_args}

    _fold_end
    _exit_dir
}

build_conf() {
    local target=${1:-${TARGET}}
    local make_conf_opts=${MAKE_CONF_ARGS:-}
    local make_jobs=${MAKE_JOBS}
    local root_dir=${ROOT_DIR}
    local build_target_dir=${BUILD_TARGET_DIR}
    local build_depends_dir=${BUILD_DEPENDS_DIR}

    echo "> build-conf: target: ${target} / conf_args: ${make_conf_opts} / jobs: ${make_jobs}"

    _ensure_enter_dir "${build_target_dir}"
    _fold_start "build-conf::autogen"
    "$root_dir/autogen.sh"
    _fold_end

    _fold_start "build-conf::configure"
    # shellcheck disable=SC2086
    CONFIG_SITE="$build_depends_dir/${target}/share/config.site" \
        $root_dir/configure --prefix="$build_depends_dir/${target}" \
        ${make_conf_opts}
    _fold_end
    _exit_dir
}

build_make() {
    local target=${1:-${TARGET}}
    local make_args=${MAKE_ARGS:-}
    local make_jobs=${MAKE_JOBS}
    local build_target_dir=${BUILD_TARGET_DIR}

    echo "> build: target: ${target} / args: ${make_args} / jobs: ${make_jobs}"

    _ensure_enter_dir "${build_target_dir}"
    _fold_start "build_make"

    # shellcheck disable=SC2086
    make DESTDIR="${build_target_dir}" -j${make_jobs} ${make_args}


    _fold_end
    _exit_dir
}

build() {
    build_deps "$@"
    build_conf "$@"
    build_make "$@"
}

deploy() {
    local target=${1:-${TARGET}}
    local img_prefix="${IMAGE_PREFIX}"
    local img_version="${IMAGE_VERSION}"
    local build_dir="${BUILD_DIR}"
    local build_target_dir="${BUILD_TARGET_DIR}"

    local versioned_name="${img_prefix}-${img_version}"
    local versioned_build_path
    versioned_build_path="$(_canonicalize "${build_dir}/${versioned_name}")"

    echo "> deploy into: ${build_dir} from ${versioned_build_path}"

    _safe_rm_rf "${versioned_build_path}" && mkdir -p "${versioned_build_path}"
    
    make -C "${build_target_dir}" prefix=/ DESTDIR="${versioned_build_path}" \
        install && cp README.md "${versioned_build_path}/"

    echo "> deployed: ${versioned_build_path}"
}

package() {
    local target=${1:-${TARGET}}
    local img_prefix="${IMAGE_PREFIX}"
    local img_version="${IMAGE_VERSION}"
    local build_dir="${BUILD_DIR}"

    local pkg_name="${img_prefix}-${img_version}-${target}"
    local pkg_tar_file_name="${pkg_name}.tar.gz"

    local pkg_path
    pkg_path="$(_canonicalize "${build_dir}/${pkg_tar_file_name}")"

    local versioned_name="${img_prefix}-${img_version}"
    local versioned_build_dir="${build_dir}/${versioned_name}"

    if [[ ! -d "$versioned_build_dir" ]]; then
        echo "> error: deployment required to package"
        echo "> tip: try \`$0 deploy\` or \`$0 docker-deploy\` first"
        exit 1
    fi

    echo "> packaging: ${pkg_name} from ${versioned_build_dir}"

    _ensure_enter_dir "${versioned_build_dir}"
    _tar --transform "s,^./,${versioned_name}/," -czf "${pkg_path}" ./*
    _exit_dir

    echo "> package: ${pkg_path}"
}

release() {
    local target=${1:-${TARGET}}

    build "${target}"
    deploy "${target}"
    package "${target}"
    _sign "${target}"
}

# -------------- Docker ---------------

docker_build() {
    local target=${1:-${TARGET}}
    local img_prefix="${IMAGE_PREFIX}"
    local img_version="${IMAGE_VERSION}"
    local docker_context="${DOCKER_ROOT_CONTEXT}"
    local docker_file="${DOCKERFILE}"

    echo "> docker-build";

    local img="${img_prefix}-${target}:${img_version}"
    echo "> building: ${img}"
    echo "> docker build: ${img}"
    docker build -f "${docker_file}" \
        --build-arg TARGET="${target}" \
        --build-arg MAKE_DEBUG="${MAKE_DEBUG}" \
        -t "${img}" "${docker_context}"
}

docker_deploy() {
    local target=${1:-${TARGET}}
    local img_prefix="${IMAGE_PREFIX}"
    local img_version="${IMAGE_VERSION}"
    local build_dir="${BUILD_DIR}"

    echo "> docker-deploy";

    local img="${img_prefix}-${target}:${img_version}"
    echo "> deploy from: ${img}"

    local pkg_name="${img_prefix}-${img_version}-${target}"
    local versioned_name="${img_prefix}-${img_version}"
    local versioned_build_dir="${build_dir}/${versioned_name}"

    _safe_rm_rf "${versioned_build_dir}" && mkdir -p "${versioned_build_dir}"

    local cid
    cid=$(docker create "${img}")
    local e=0

    { docker cp "${cid}:/app/." "${versioned_build_dir}" 2>/dev/null && e=1; } || true
    docker rm "${cid}"

    if [[ "$e" == "1" ]]; then
        echo "> deployed into: ${versioned_build_dir}"
    else
        echo "> failed: please ensure package is built first"
    fi
}

docker_release() {
    local target=${1:-${TARGET}}

    docker_build "$target"
    docker_deploy "$target"
    package "$target"
    _sign "$target"
}

docker_clean_builds() {
    echo "> clean: defichain build images"
    _docker_clean "org.defichain.name=defichain"
}

docker_clean_all() {
    echo "> clean: defichain* images"
    _docker_clean "org.defichain.name"
}

_docker_clean() {
    local labels_to_clean=${1:?labels required}
    # shellcheck disable=SC2046
    docker rmi $(docker images -f label="${labels_to_clean}" -q) \
        --force 2>/dev/null || true
}

# -------------- Misc -----------------

debug_env() {
    (set -o posix ; set)
    (set -x +e
    uname -a
    gcc -v
    "clang-${CLANG_DEFAULT_VERSION}" -v
    rustup show)
}

test() {
    local make_jobs=${MAKE_JOBS}
    local make_args=${MAKE_ARGS:-}
    local build_target_dir=${BUILD_TARGET_DIR}

    _ensure_enter_dir "${build_target_dir}"

    _fold_start "unit-tests"
    # shellcheck disable=SC2086
    make -j$make_jobs check
    _fold_end

    _fold_start "functional-tests"
    # shellcheck disable=SC2086
    ./test/functional/test_runner.py --ci -j$make_jobs --tmpdirprefix "./test_runner/" --ansi --combinedlogslen=10000
    _fold_end

    _exit_dir
}

test_py() {
    local build_target_dir=${BUILD_TARGET_DIR}
    local first_arg="${1:-}"

    if [[ -f "${first_arg}" ]]; then
      shift
      "${first_arg}" --configfile "${build_target_dir}/test/config.ini" --tmpdirprefix "./test_runner/" --ansi "$@"
      return
    fi

    _ensure_enter_dir "${build_target_dir}"

    # shellcheck disable=SC2086
    ./test/functional/test_runner.py --tmpdirprefix "./test_runner/" --ansi "$@"

    _exit_dir
}

exec() {
    local make_jobs=${MAKE_JOBS}
    local make_args=${MAKE_ARGS:-}
    local build_target_dir=${BUILD_TARGET_DIR}

    _ensure_enter_dir "${build_target_dir}"
    _fold_start "exec:: ${*-(default)}"

    # shellcheck disable=SC2086,SC2068
    make -j$make_jobs $make_args $@

    _fold_end
    _exit_dir
}

git_version() {
    local verbose=${1:-1}
    # If we have a tagged version (for proper releases), then just
    # release it with the tag, otherwise we use the commit hash
    local current_tag
    local current_commit
    local current_branch

    git fetch --tags &> /dev/null
    current_tag=$(git tag --points-at HEAD | head -1)
    current_commit=$(git rev-parse --short HEAD)
    current_branch=$(git rev-parse --abbrev-ref HEAD)

    local ver=""

    if [[ -z $current_tag ]]; then
        # Replace `/` in branch names with `-` as / is trouble
        ver="${current_branch//\//-}-${current_commit}"
    else
        ver="${current_tag}"
        # strip the 'v' infront of version tags
        if [[ "$ver" =~ ^v[0-9]\.[0-9] ]]; then
            ver="${ver##v}"
        fi
    fi

    if [[ "$verbose" == "1" ]]; then 
        echo "> git branch: ${current_branch}"
        echo "> version: ${ver}"
    else
        echo "$ver"
    fi
}

# ------------ pkg --------------------
# Conventions:
# - pkg_*
# - pkg_install_*: for installing packages (system level)
# - pkg_update_*: distro update only
# - pkg_local_*: for pulling packages into the local dir
#   - clean_pkg_local*: Make sure to have the opp. 
# - pkg_setup*: setup of existing (or installed) pkgs // but not installing now


pkg_update_base() {
    _fold_start "pkg-update-base"

    apt-get update
    apt-get install -y apt-transport-https
    apt-get upgrade -y
    
    _fold_end
}

pkg_install_deps() {
    _fold_start "pkg-install-deps"

    # gcc-multilib: for cross compilations
    # locales: for using en-US.UTF-8 (see head of this file).

    apt-get install -y \
        software-properties-common build-essential git libtool autotools-dev automake \
        pkg-config bsdmainutils python3 python3-pip libssl-dev libevent-dev libboost-system-dev \
        libboost-filesystem-dev libboost-chrono-dev libboost-test-dev libboost-thread-dev \
        libminiupnpc-dev libzmq3-dev libqrencode-dev wget \
        libdb-dev libdb++-dev libdb5.3 libdb5.3-dev libdb5.3++ libdb5.3++-dev \
        curl cmake unzip libc6-dev gcc-multilib locales locales-all

    _fold_end
}

pkg_install_deps_mingw_x86_64() {
    _fold_start "pkg-install-deps-mingw-x86_64"
    
    apt-get install -y \
        g++-mingw-w64-x86-64 mingw-w64-x86-64-dev

    _fold_end
}

pkg_setup_mingw_x86_64() {
    update-alternatives --set x86_64-w64-mingw32-gcc /usr/bin/x86_64-w64-mingw32-gcc-posix
    update-alternatives --set x86_64-w64-mingw32-g++ /usr/bin/x86_64-w64-mingw32-g++-posix
}

pkg_install_deps_armhf() {
    _fold_start "pkg-install-deps-armhf"

    apt-get install -y \
        g++-arm-linux-gnueabihf binutils-arm-linux-gnueabihf libc6-dev-armhf-cross

    _fold_end
}

pkg_install_deps_arm64() {
    _fold_start "pkg-install-deps-arm64"

    apt-get install -y \
        g++-aarch64-linux-gnu binutils-aarch64-linux-gnu libc6-dev-arm64-cross

    _fold_end
}

pkg_install_deps_osx_tools() {
    _fold_start "pkg-install-deps-mac-tools"

    apt-get install -y \
        python3-dev libcap-dev libbz2-dev libz-dev fonts-tuffy librsvg2-bin libtiff-tools imagemagick libtinfo5

    _fold_end
}

pkg_local_ensure_osx_sysroot() {
    local sdk_name="Xcode-12.2-12B45b-extracted-SDK-with-libcxx-headers"
    local pkg="${sdk_name}.tar.gz"
    local build_depends_dir="${BUILD_DEPENDS_DIR}"
    local sdk_base_dir="$build_depends_dir/SDKs"

    if [[ -d "${sdk_base_dir}/${sdk_name}" ]]; then 
        return
    fi

    _fold_start "pkg-local-mac-sdk"

    _ensure_enter_dir "${sdk_base_dir}"
    if [[ ! -f "${pkg}" ]]; then 
        wget https://bitcoincore.org/depends-sources/sdks/${pkg}
    fi
    _tar -zxf "${pkg}"
    rm "${pkg}" 2>/dev/null || true
    _exit_dir

    _fold_end
}

pkg_install_llvm() {
    _fold_start "pkg-install-llvm"
    # shellcheck disable=SC2086
    wget -O - "https://apt.llvm.org/llvm.sh" | bash -s ${CLANG_DEFAULT_VERSION}
    _fold_end
}

pkg_install_rust() {
    _fold_start "pkg-install-rust"
    curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh -s -- \
        --default-toolchain="${RUST_DEFAULT_VERSION}" -y
    _fold_end
}

pkg_install_web3_deps() {
    _fold_start "pkg-install-solc"
    python3 -m pip install py-solc-x web3
    python3 -c 'from solcx import install_solc;install_solc("0.8.20")'
    _fold_end
}

pkg_setup_rust() {
    local rust_target
    rust_target=$(get_rust_target)
    rustup target add "${rust_target}"
}

clean_pkg_local_osx_sysroot() {
    local build_depends_dir="${BUILD_DEPENDS_DIR}"
    _safe_rm_rf "$build_depends_dir/SDKs"
}

purge() {
    local build_dir="${BUILD_DIR}"
    local build_depends_dir="${BUILD_DEPENDS_DIR}"

    clean_depends
    _safe_rm_rf "$build_depends_dir"
    clean_conf
    clean_artifacts
    docker_clean_all
    _safe_rm_rf "$build_dir"
}

clean_artifacts() {
    # If build is done out of tree, this is not needed at all. But when done
    # in-tree, or helper tools that end up running configure in-tree, this is 
    # a helpful method to clean up left overs. 
    local items=(\
        .libs .deps obj "*.dirstamp" "*.a" "*.o" "*.Po" "*.lo")
    
    local x
    for x in "${items[@]}"; do
        find src -iname "$x" -exec rm -rf \;
    done
}

clean_conf() {
    local top_left_overs=(\
        Makefile.in aclocal.m4 autom4te.cache configure configure~)

    # If things were built in-tree, help clean this up as well
    local in_tree_conf_left_overs=(\
        Makefile libtool config.log config.status)

    local build_aux_left_overs=(\
        ar-lib compile config.guess config.sub depcomp install-sh ltmain.sh
        missing test-driver)

    local build_aux_m4_left_overs=(\
        libtool.m4 lt~obsolete.m4 ltoptions.m4 ltsugar.m4 ltversion.m4)

    local left_overs=("${top_left_overs[@]}" \
        "${in_tree_conf_left_overs[@]}" \
        "${build_aux_left_overs[@]/#/build-aux/}" \
        "${build_aux_m4_left_overs[@]/#/build-aux/m4/}")

    local individual_files=(./test/config.ini)

    local x
    for x in "${individual_files[@]}"; do
        _safe_rm_rf "$x"
    done

    for x in "${left_overs[@]} "; do
        _safe_rm_rf "$x"
        _safe_rm_rf "src/secp256k1/$x"
        _safe_rm_rf "src/univalue/$x"
    done

    _safe_rm_rf \
        src/Makefile.in \
        src/defi-config.h.{in,in~} \
        src/univalue/univalue-config.h.{in,in~} \
        src/secp256k1/libsecp256k1-config.h.{in,in~}
}

clean_depends() {
    local root_dir="$ROOT_DIR"
    local build_dir="${BUILD_DIR}"
    local build_depends_dir="${BUILD_DEPENDS_DIR}"

    make -C "$root_dir/depends" DESTDIR="${build_depends_dir}" clean-all || true
    _ensure_enter_dir "$build_depends_dir"
    clean_pkg_local_osx_sysroot
    _safe_rm_rf built \
        work \
        sources \
        x86_64* \
        i686* \
        mips* \
        arm* \
        aarch64* \
        riscv32* \
        riscv64*
    _exit_dir
}

clean() {
    local build_dir="${BUILD_TARGET_DIR}"
    _ensure_enter_dir "${build_dir}"
    make clean || true
    _exit_dir
    clean_artifacts
}

# ========
# Internal Support methods
# ========

# Defaults
# ---

_get_default_target() {
    local default_target=""
    if [[ "${OSTYPE}" == "darwin"* ]]; then
        local macos_arch=""
        macos_arch=$(uname -m || true)
        if [[ "$macos_arch" == "x86_64" ]]; then
            default_target="x86_64-apple-darwin"
        else
            default_target="aarch64-apple-darwin"
        fi
    elif [[ "${OSTYPE}" == "msys" ]]; then
        default_target="x86_64-w64-mingw32"
    else
        # Note: make.sh only formally supports auto selection for 
        # windows under msys, mac os and debian derivatives to build on.
        # Also note: Support for auto selection on make.sh does not imply
        # support for the architecture. 
        # Only supported architectures are the ones with release builds
        # enabled on the CI. 
        local dpkg_arch=""
        dpkg_arch=$(dpkg --print-architecture || true)
        if [[ "$dpkg_arch" == "armhf" ]]; then
            default_target="arm-linux-gnueabihf"
        elif [[ "$dpkg_arch" == "aarch64" ]]; then
            default_target="aarch64-linux-gnu"
        else
            # Global default if we can't determine it from the 
            # above, which are our only supported list for auto select
            default_target="x86_64-pc-linux-gnu"
        fi
    fi
    echo "$default_target"
}

_get_default_docker_file() {
    local target="${TARGET}"
    local dockerfiles_dir="${DOCKERFILES_DIR}"

    local try_files=(\
        "${dockerfiles_dir}/${target}.dockerfile" \
        "${dockerfiles_dir}/${target}" \
        "${dockerfiles_dir}/noarch.dockerfile" \
        )

    for file in "${try_files[@]}"; do
        if [[ -f "$file" ]]; then 
            echo "$file"
            return
        fi
    done
    # If none of these were found, assumes empty val
    # Empty will fail if docker cmd requires it, or continue for 
    # non docker cmds
}

_get_default_conf_args() {
    local conf_args=""
    if [[ "$TARGET" =~ .*linux.* ]]; then
        conf_args="${conf_args} --enable-glibc-back-compat";
    fi
    conf_args="${conf_args} --enable-static";
    conf_args="${conf_args} --enable-reduce-exports";
    # Note: https://stackoverflow.com/questions/13636513/linking-libstdc-statically-any-gotchas
    # We don't use dynamic loading at the time being
    conf_args="${conf_args} LDFLAGS=-static-libstdc++";
    # Other potential options: -static-libgcc on gcc, -static on clang
    echo "$conf_args"
}


# Dev tools
# ---

# shellcheck disable=SC2120
git_add_hooks() {
    local force_update=${1:-0}
    local file=".git/hooks/pre-push"
    if [[ -f "$file" && $force_update == "0" ]]; then 
        return;
    fi
    echo "> add pre-push-hook"
    mkdir -p "$(dirname $file)" 2>/dev/null || { true && return; }
    cat <<END > "$file"
#!/bin/bash
set -Eeuo pipefail
dir="\$(dirname "\${BASH_SOURCE[0]}")"
_SCRIPT_DIR="\$(cd "\${dir}/" && pwd)"
cd \$_SCRIPT_DIR/../../
./make.sh check
END
    chmod +x "$file"
}

check() {
    check_git_dirty
    check_rs
}

check_git_dirty() {
    if [[ $(git status -s) ]]; then
        echo "error: Git tree dirty. Please commit or stash first"
        exit 1
    fi
}

check_rs() {
    check_enter_build_rs_dir
    lint_cargo_check
    lint_cargo_clippy
    lint_cargo_fmt
    _exit_dir
}

check_enter_build_rs_dir() {
    local build_dir="${BUILD_DIR}"
    _ensure_enter_dir "$build_dir/lib" || { 
        echo "Please configure first";
        exit 1; }
}

lint_cargo_check() {
    check_enter_build_rs_dir
    make check || { 
        echo "Error: Please resolve compiler checks before commit"; 
        exit 1; }
    _exit_dir
}

lint_cargo_clippy() {
    check_enter_build_rs_dir 
    make clippy || { 
        echo "Error: Please resolve compiler lints before commit"; 
        exit 1; }
    _exit_dir
}

lint_cargo_fmt() {
    check_enter_build_rs_dir
    make fmt-check  || {
        echo "Error: Please format code before commit"; 
        exit 1; }
    _exit_dir
}


# Platform specifics
# ---

_platform_init() {
    # Lazy init functions
    if [[ $(readlink -m . 2> /dev/null) != "${_SCRIPT_DIR}" ]]; then
        if [[ $(greadlink -m . 2> /dev/null) != "${_SCRIPT_DIR}" ]]; then 
            echo "error: readlink or greadlink with \`-m\` support is required"
            _platform_pkg_tip coreutils
            exit 1
        else
        _canonicalize() {
            greadlink -m "$@"
        }
        fi
    else
        _canonicalize() {
            readlink -m "$@"
        }
    fi

    if tar --version 2> /dev/null | grep -q 'GNU tar'; then
        _tar() {
            tar "$@"
        }
    else
        if gtar --version 2> /dev/null | grep -q 'GNU tar'; then
            _tar() {
                gtar "$@"
            }
        else
            echo "error: GNU version of tar is required"
            _platform_pkg_tip tar gnu-tar
            exit 1
        fi
    fi
}

_platform_pkg_tip() {
    local apt_pkg=${1:?pkg required}
    local brew_pkg=${2:-${apt_pkg}}

    echo "tip: debian/ubuntu: apt install ${apt_pkg}"
    echo "tip: osx: brew install ${brew_pkg}"
}

_nproc() {
    if [[ "${OSTYPE}" == "darwin"* ]]; then
        sysctl -n hw.logicalcpu
    else
        nproc
    fi
}

# Misc
# ---

ci_export_vars() {
    if [[ -n "${GITHUB_ACTIONS-}" ]]; then
        # GitHub Actions
        echo "BUILD_VERSION=${IMAGE_VERSION}" >> "$GITHUB_ENV"
        echo "PATH=$HOME/.cargo/bin:$PATH" >> "$GITHUB_ENV"
    fi
}

ci_setup_deps() {
    DEBIAN_FRONTEND=noninteractive pkg_update_base
    DEBIAN_FRONTEND=noninteractive pkg_install_deps
    DEBIAN_FRONTEND=noninteractive pkg_install_llvm
    DEBIAN_FRONTEND=noninteractive pkg_install_rust
}

_ci_setup_deps_target() {
    local target=${TARGET}
    case $target in
        # Nothing to do on host
        x86_64-pc-linux-gnu) ;;
        aarch64-linux-gnu) 
            DEBIAN_FRONTEND=noninteractive pkg_install_deps_arm64;;
        arm-linux-gnueabihf) 
            DEBIAN_FRONTEND=noninteractive pkg_install_deps_armhf;;
        x86_64-apple-darwin|aarch64-apple-darwin) 
            DEBIAN_FRONTEND=noninteractive pkg_install_deps_osx_tools;;
        x86_64-w64-mingw32)
            DEBIAN_FRONTEND=noninteractive pkg_install_deps_mingw_x86_64
            pkg_setup_mingw_x86_64;;
        *)
            echo "error: unsupported target: ${target}"
            exit 1;;
    esac
}

ci_setup_deps_target() {
    _ci_setup_deps_target
    pkg_setup_rust
}

ci_setup_deps_test() {
    pkg_install_web3_deps
}

get_rust_target() {
    local target=${TARGET}
    local rust_target
    case $target in
        x86_64-pc-linux-gnu) rust_target=x86_64-unknown-linux-gnu;;
        aarch64-linux-gnu) rust_target=aarch64-unknown-linux-gnu;;
        arm-linux-gnueabihf) rust_target=armv7-unknown-linux-gnueabihf;;
        x86_64-apple-darwin) rust_target=x86_64-apple-darwin;;
        aarch64-apple-darwin) rust_target=aarch64-apple-darwin;;
        x86_64-w64-mingw32) rust_target=x86_64-pc-windows-gnu;;
        *) echo "error: unsupported target: ${target}"; exit 1;;
    esac
    echo "$rust_target"
}

_sign() {
    # TODO: generate sha sums and sign
    :
}

_safe_rm_rf() {
    local x
    for x in "$@"; do
        if [[ "$x" =~ ^[[:space:]]*$ || "$x" =~ ^/*$ ]]; then 
            # Safe guard against accidental rm -rfs
            echo "error: unsafe delete attempted"
            exit 1
        fi
        rm -rf "$x"
    done
}

_fold_start() {
    echo "::group::${*:-}"
}

_fold_end() {
    echo "::endgroup::"
}

_ensure_enter_dir() {
    local dir="${1?dir required}"
    mkdir -p "${dir}" && pushd "${dir}" > /dev/null
}

_exit_dir() {
    popd > /dev/null
}

main "$@"
