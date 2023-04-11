#!/usr/bin/env bash

# Copyright (c) DeFi Blockchain Developers
# Maker script

export LC_ALL=C.UTF-8
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
}

main() {
    _ensure_script_dir
    trap _cleanup 0 1 2 3 6 15 ERR
    cd "$_SCRIPT_DIR"
    _platform_init
    setup_vars

    # Get all functions declared in this file except ones starting with
    # '_' or the ones in the list
    # shellcheck disable=SC2207
    COMMANDS=($(declare -F | cut -d" " -f3 | grep -v -E "^_.*$|main|setup_vars")) || true

    # Commands use `-` instead of `_` for getopts consistency. Flip this.
    local cmd=${1:-} && cmd="${cmd//-/_}"

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
    printf "\n\`%s build\` or \`%s docker-build\` are your friends :) \n" $0 $0
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
    local release_depends_dir=${DEPENDS_DIR}

    echo "> build-deps: target: ${target} / deps_args: ${make_deps_args} / jobs: ${make_jobs}"
    
    _ensure_enter_dir "$release_depends_dir"
    if [[ "$target" =~ .*darwin.* ]]; then
        pkg_local_ensure_osx_sysroot
    fi
    
    _fold_start "build-deps"

    # shellcheck disable=SC2086
    make -C "${src_depends_dir}" DESTDIR="${release_depends_dir}" \
        HOST="${target}" -j${make_jobs} ${make_deps_args}

    _fold_end
    _exit_dir
}

build_conf() {
    local target=${1:-${TARGET}}
    local make_conf_opts=${MAKE_CONF_ARGS:-}
    local make_jobs=${MAKE_JOBS}
    local root_dir=${ROOT_DIR}
    local release_target_dir=${RELEASE_TARGET_DIR}
    local release_depends_dir=${DEPENDS_DIR}

    echo "> build-conf: target: ${target} / conf_args: ${make_conf_opts} / jobs: ${make_jobs}"

    _ensure_enter_dir "${release_target_dir}"
    _fold_start "build-conf::autogen"
    "$root_dir/autogen.sh"
    _fold_end

    _fold_start "build-conf::configure"
    # shellcheck disable=SC2086
    CONFIG_SITE="$release_depends_dir/${target}/share/config.site" \
        $root_dir/configure --prefix="$release_depends_dir/${target}" \
        ${make_conf_opts}
    _fold_end
    _exit_dir
}

build_make() {
    local target=${1:-${TARGET}}
    local make_args=${MAKE_ARGS:-}
    local make_jobs=${MAKE_JOBS}
    local release_target_dir=${RELEASE_TARGET_DIR}
    local release_out=${release_target_dir}/bin

    echo "> build: target: ${target} / args: ${make_args} / jobs: ${make_jobs}"

    _ensure_enter_dir "${release_target_dir}"
    _fold_start "build_make"

    # shellcheck disable=SC2086
    make DESTDIR="${release_target_dir}" -j${make_jobs} ${make_args}

    mkdir -p "${release_out}"
    local bins=(defid defid.exe defi-cli defi-cli.exe defi-tx defi-tx.exe)
    for x in "${bins[@]}"; do
        { cp -f "${release_target_dir}/src/${x}" "${release_out}/" || true; } 2> /dev/null
    done

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
    local release_dir="${RELEASE_DIR}"
    local release_target_dir="${RELEASE_TARGET_DIR}"

    local versioned_name="${img_prefix}-${img_version}"
    local versioned_release_path
    versioned_release_path="$(_canonicalize "${release_dir}/${versioned_name}")"

    echo "> deploy into: ${release_dir} from ${versioned_release_path}"

    _safe_rm_rf "${versioned_release_path}" && mkdir -p "${versioned_release_path}"
    
    make -C "${release_target_dir}" prefix=/ DESTDIR="${versioned_release_path}" \
        install && cp README.md "${versioned_release_path}/"

    echo "> deployed: ${versioned_release_path}"
}

package() {
    local target=${1:-${TARGET}}
    local img_prefix="${IMAGE_PREFIX}"
    local img_version="${IMAGE_VERSION}"
    local release_dir="${RELEASE_DIR}"

    local pkg_name="${img_prefix}-${img_version}-${target}"
    local pkg_tar_file_name="${pkg_name}.tar.gz"

    local pkg_path
    pkg_path="$(_canonicalize "${release_dir}/${pkg_tar_file_name}")"

    local versioned_name="${img_prefix}-${img_version}"
    local versioned_release_dir="${release_dir}/${versioned_name}"

    if [[ ! -d "$versioned_release_dir" ]]; then
        echo "> error: deployment required to package"
        echo "> tip: try \`$0 deploy\` or \`$0 docker-deploy\` first"
        exit 1
    fi

    echo "> packaging: ${pkg_name} from ${versioned_release_dir}"

    _ensure_enter_dir "${versioned_release_dir}"
    tar --transform "s,^./,${versioned_name}/," -czf "${pkg_path}" ./*
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
    local docker_file="${DOCKERFILES_DIR}/${DOCKERFILE:-"${target}"}.dockerfile"

    echo "> docker-build";

    local img="${img_prefix}-${target}:${img_version}"
    echo "> building: ${img}"
    echo "> docker build: ${img}"
    docker build -f "${docker_file}" -t "${img}" "${docker_context}"
}

docker_deploy() {
    local target=${1:-${TARGET}}
    local img_prefix="${IMAGE_PREFIX}"
    local img_version="${IMAGE_VERSION}"
    local release_dir="${RELEASE_DIR}"

    echo "> docker-deploy";

    local img="${img_prefix}-${target}:${img_version}"
    echo "> deploy from: ${img}"

    local pkg_name="${img_prefix}-${img_version}-${target}"
    local versioned_name="${img_prefix}-${img_version}"
    local versioned_release_dir="${release_dir}/${versioned_name}"

    _safe_rm_rf "${versioned_release_dir}" && mkdir -p "${versioned_release_dir}"

    local cid
    cid=$(docker create "${img}")
    local e=0

    { docker cp "${cid}:/app/." "${versioned_release_dir}" 2>/dev/null && e=1; } || true
    docker rm "${cid}"

    if [[ "$e" == "1" ]]; then
        echo "> deployed into: ${versioned_release_dir}"
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

test() {
    local make_jobs=${MAKE_JOBS}
    local make_args=${MAKE_ARGS:-}
    local release_target_dir=${RELEASE_TARGET_DIR}

    _ensure_enter_dir "${release_target_dir}"

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

exec() {
    local make_jobs=${MAKE_JOBS}
    local make_args=${MAKE_ARGS:-}
    local release_target_dir=${RELEASE_TARGET_DIR}

    _ensure_enter_dir "${release_target_dir}"
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

pkg_update_base() {
    _fold_start "pkg-update-base"

    apt update
    apt install -y apt-transport-https
    apt dist-upgrade -y
    
    _fold_end
}

pkg_install_deps() {
    _fold_start "pkg-install-deps"

    apt install -y \
        software-properties-common build-essential git libtool autotools-dev automake \
        pkg-config bsdmainutils python3 python3-pip libssl-dev libevent-dev libboost-system-dev \
        libboost-filesystem-dev libboost-chrono-dev libboost-test-dev libboost-thread-dev \
        libminiupnpc-dev libzmq3-dev libqrencode-dev wget \
        libdb-dev libdb++-dev libdb5.3 libdb5.3-dev libdb5.3++ libdb5.3++-dev \
        curl cmake

    _fold_end
}

pkg_install_deps_mingw_x86_64() {
    _fold_start "pkg-install-deps-mingw-x86_64"
    
    apt install -y \
        g++-mingw-w64-x86-64 mingw-w64-x86-64-dev

    _fold_end
}

pkg_install_deps_armhf() {
    _fold_start "pkg-install-deps-armhf"

    apt install -y \
        g++-arm-linux-gnueabihf binutils-arm-linux-gnueabihf

    _fold_end
}

pkg_install_deps_arm64() {
    _fold_start "pkg-install-deps-arm64"

    apt install -y \
        g++-aarch64-linux-gnu binutils-aarch64-linux-gnu

    _fold_end
}

pkg_install_deps_osx_tools() {
    _fold_start "pkg-install-deps-mac-tools"

    apt install -y \
        python3-dev libcap-dev libbz2-dev libz-dev fonts-tuffy librsvg2-bin libtiff-tools imagemagick libtinfo5

    _fold_end
}

pkg_local_ensure_osx_sysroot() {
    local sdk_name="Xcode-12.2-12B45b-extracted-SDK-with-libcxx-headers"
    local pkg="${sdk_name}.tar.gz"
    local release_depends_dir=${DEPENDS_DIR}

    _ensure_enter_dir "$release_depends_dir/SDKs"
    if [[ -d "${sdk_name}" ]]; then return; fi

    _fold_start "pkg-local-mac-sdk"

    if [[ ! -f "${pkg}" ]]; then 
        wget https://bitcoincore.org/depends-sources/sdks/${pkg}
    fi
    tar -zxf "${pkg}"
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
    curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh
}

clean_pkg_local_osx_sysroot() {
    local release_depends_dir=${DEPENDS_DIR}
    _safe_rm_rf "$release_depends_dir/SDKs"
}

purge() {
    local release_dir="${RELEASE_DIR}"
    local release_depends_dir=${DEPENDS_DIR}

    clean_depends
    _safe_rm_rf "$release_depends_dir"
    clean_conf
    docker_clean_all
    _safe_rm_rf "$release_dir"
}

clean_conf() {
    local top_left_overs=(\
        Makefile.in aclocal.m4 autom4te.cache configure configure~ )

    local build_aux_left_overs=(\
        ar-lib compile config.guess config.sub depcomp install-sh ltmain.sh
        missing test-driver)

    local build_aux_m4_left_overs=(\
        libtool.m4 lt~obsolete.m4 ltoptions.m4 ltsugar.m4 ltversion.m4)

    local left_overs=("${top_left_overs[@]}" \
        "${build_aux_left_overs[@]/#/build-aux/}" \
        "${build_aux_m4_left_overs[@]/#/build-aux/m4/}")

    for x in "${left_overs[@]} "; do
        _safe_rm_rf "$x"
        _safe_rm_rf "src/secp256k1/$x"
        _safe_rm_rf "src/univalue/$x"
    done

    _safe_rm_rf \
        src/Makefile.in \
        src/defi-config.h.{in,in~} \
        src/univalue/src/univalue-config.h.{in,in~} \
        src/secp256k1/src/libsecp256k1-config.h.{in,in~}
}

clean_depends() {
    local root_dir="$ROOT_DIR"
    local release_dir="${RELEASE_DIR}"
    local release_depends_dir=${DEPENDS_DIR}

    make -C "$root_dir/depends" DESTDIR="${release_depends_dir}" clean-all || true
    _ensure_enter_dir "$release_depends_dir"
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
    local release_dir="${RELEASE_TARGET_DIR}"
    _ensure_enter_dir "${release_dir}"
    make clean || true
    _exit_dir
}

# ========
# Internal Support methods
# ========

# Defaults
# ---

_get_default_target() {
    local default_target=""
    if [[ "${OSTYPE}" == "darwin"* ]]; then
        default_target="x86_64-apple-darwin"
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

# Platform specifics
# ---

_platform_init() {
    # Lazy init functions
    if [[ $(readlink -m . 2> /dev/null) != "${_SCRIPT_DIR}" ]]; then
        if [[ $(greadlink -m . 2> /dev/null) != "${_SCRIPT_DIR}" ]]; then 
            echo "error: readlink or greadlink with \`-m\` support is required"
            echo "tip: debian/ubuntu: apt install coreutils"
            echo "tip: osx: brew install coreutils"
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
    fi
}

_sign() {
    # TODO: generate sha sums and sign
    :
}

_safe_rm_rf() {
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
