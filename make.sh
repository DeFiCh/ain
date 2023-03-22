#!/usr/bin/env bash

# Copyright (c) DeFi Blockchain Developers
# Maker script

# shellcheck disable=SC2155

export LC_ALL=C
set -Eeuo pipefail

setup_vars() {
    IMAGE_PREFIX=${IMAGE_PREFIX:-"defichain"}
    IMAGE_VERSION=${IMAGE_VERSION:-"latest"}

    DOCKER_ROOT_CONTEXT=${DOCKER_ROOT_CONTEXT:-"."}
    DOCKERFILE=${DOCKERFILE:-""}
    DOCKERFILES_DIR=${DOCKERFILES_DIR:-"./contrib/dockerfiles"}
    RELEASE_DIR=${RELEASE_DIR:-"./build"}

    local default_target="x86_64-pc-linux-gnu"
    if [[ "${OSTYPE}" == "darwin"* ]]; then
        default_target="x86_64-apple-darwin18"
    elif [[ "${OSTYPE}" == "msys" ]]; then
        default_target="x86_64-w64-mingw32"
    fi

    # shellcheck disable=SC2206
    # This intentionally word-splits the array as env arg can only be strings.
    # Other options available: x86_64-w64-mingw32 x86_64-apple-darwin11
    TARGET=${TARGET:-"${default_target}"}

    local default_compiler_flags=""
    if [[ "${TARGET}" == "x86_64-pc-linux-gnu" || \
        "${TARGET}" == "x86_64-apple-darwin11" ]]; then
        default_compiler_flags="CC=clang-16 CXX=clang++-16"
    fi

    if [[ "${OSTYPE}" == "darwin"* ]]; then
        default_jobs=$(sysctl -n hw.logicalcpu)
    else
        default_jobs=$(nproc)
    fi

    MAKE_JOBS=${MAKE_JOBS:-"${default_jobs}"}
    MAKE_COMPILER=${MAKE_COMPILER:-"${default_compiler_flags}"}
    MAKE_CONF_ARGS="${MAKE_COMPILER} ${MAKE_CONF_ARGS:-}"
    MAKE_ARGS=${MAKE_ARGS:-}
    MAKE_DEPS_ARGS=${MAKE_DEPS_ARGS:-}
    MAKE_DEBUG=${MAKE_DEBUG:-"0"}
    if [[ "${MAKE_DEBUG}" == "1" ]]; then
      MAKE_CONF_ARGS="${MAKE_CONF_ARGS} --enable-debug";
    fi
}

main() {
    _ensure_script_dir
    trap _cleanup 0 1 2 3 6 15 ERR
    cd "$_SCRIPT_DIR"
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
    printf "\nCommands:\n"
    printf "\t%s\n" "${COMMANDS[@]//_/-}"
    printf "\nNote: All non-docker commands assume that it's run on an environment \n"
    printf "with correct arch and the pre-requisites properly configured. \n"
}

# ----------- Direct builds ---------------

build_deps() {
    local target=${1:-${TARGET}}
    local make_deps_args=${MAKE_DEPS_ARGS:-}
    local make_jobs=${MAKE_JOBS}

    echo "> build-deps: target: ${target} / deps_args: ${make_deps_args} / jobs: ${make_jobs}"
    pushd ./depends >/dev/null
    # XREF: #make-deps
    # shellcheck disable=SC2086
    make HOST="${target}" -j${make_jobs} ${make_deps_args}
    popd >/dev/null
}

build_conf() {
    local target=${1:-${TARGET}}
    local make_conf_opts=${MAKE_CONF_ARGS:-}
    local make_jobs=${MAKE_JOBS}

    echo "> build-conf: target: ${target} / conf_args: ${make_conf_opts} / jobs: ${make_jobs}"

    ./autogen.sh
    # XREF: #make-configure
    # ./configure --prefix="$(pwd)/depends/x86_64-pc-linux-gnu" ${make_conf_opts}
    # shellcheck disable=SC2086
    CONFIG_SITE="$(pwd)/depends/${target}/share/config.site" ./configure --prefix="$(pwd)/depends/${target}" ${make_conf_opts}
}

build_make() {
    local target=${1:-${TARGET}}
    local make_args=${MAKE_ARGS:-}
    local make_jobs=${MAKE_JOBS}

    echo "> build: target: ${target} / args: ${make_args} / jobs: ${make_jobs}"
    # shellcheck disable=SC2086
    make -j${make_jobs} ${make_args}
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

    mkdir -p "${release_dir}"

    # XREF: #pkg-name
    local versioned_name="${img_prefix}-${img_version}"
    local versioned_release_path
    versioned_release_path="$(readlink -m "${release_dir}/${versioned_name}")"

    echo "> deploy into: ${release_dir} from ${versioned_release_path}"

    pushd "${release_dir}" >/dev/null
    rm -rf "./${versioned_name}" && mkdir "${versioned_name}"
    popd >/dev/null

    make prefix=/ DESTDIR="${versioned_release_path}" install && cp README.md "${versioned_release_path}/"

    echo "> deployed: ${versioned_release_path}"
}

package() {
    local target=${1:-${TARGET}}
    local img_prefix="${IMAGE_PREFIX}"
    local img_version="${IMAGE_VERSION}"
    local release_dir="${RELEASE_DIR}"

    deploy "${target}"

    # XREF: #pkg-name
    local pkg_name="${img_prefix}-${img_version}-${target}"
    local pkg_tar_file_name="${pkg_name}.tar.gz"

    local pkg_path
    pkg_path="$(readlink -m "${release_dir}/${pkg_tar_file_name}")"

    local versioned_name="${img_prefix}-${img_version}"
    local versioned_release_dir="${release_dir}/${versioned_name}"

    echo "> packaging: ${pkg_name} from ${versioned_release_dir}"

    pushd "${versioned_release_dir}" >/dev/null
    tar --transform "s,^./,${versioned_name}/," -cvzf "${pkg_path}" ./*
    popd >/dev/null

    echo "> package: ${pkg_path}"
}

release() {
    local target=${1:-${TARGET}}

    build "${target}"
    package "${target}"
    sign "${target}"
}

# -------------- Docker ---------------

docker_build() {
    local target=${1:-${TARGET}}
    local img_prefix="${IMAGE_PREFIX}"
    local img_version="${IMAGE_VERSION}"
    local docker_context="${DOCKER_ROOT_CONTEXT}"
    local docker_file="${DOCKERFILES_DIR}/${DOCKERFILE:-"${target}"}.dockerfile"

    echo "> docker-build";

    if [[ "$target" == "x86_64-apple-darwin"* ]]; then
        pkg_ensure_mac_sdk
    fi
    local img="${img_prefix}-${target}:${img_version}"
    echo "> building: ${img}"
    echo "> docker build: ${img}"
    docker build -f "${docker_file}" -t "${img}" "${docker_context}"
}

docker_package() {
    local target=${1:-${TARGET}}
    local img_prefix="${IMAGE_PREFIX}"
    local img_version="${IMAGE_VERSION}"
    local release_dir="${RELEASE_DIR}"

    echo "> docker-package";

    local img="${img_prefix}-${target}:${img_version}"
    echo "> packaging: ${img}"

    # XREF: #pkg-name
    local pkg_name="${img_prefix}-${img_version}-${target}"
    local pkg_tar_file_name="${pkg_name}.tar.gz"
    local pkg_rel_path="${release_dir}/${pkg_tar_file_name}"
    local versioned_name="${img_prefix}-${img_version}"

    mkdir -p "${release_dir}"

    docker run --rm "${img}" bash -c \
        "tar --transform 's,^./,${versioned_name}/,' -czf - ./*" >"${pkg_rel_path}"

    echo "> package: ${pkg_rel_path}"
}

docker_deploy() {
    local target=${1:-${TARGET}}
    local img_prefix="${IMAGE_PREFIX}"
    local img_version="${IMAGE_VERSION}"
    local release_dir="${RELEASE_DIR}"

    echo "> docker-deploy";

    local img="${img_prefix}-${target}:${img_version}"
    echo "> deploy from: ${img}"

    # XREF: #pkg-name
    local pkg_name="${img_prefix}-${img_version}-${target}"
    local versioned_name="${img_prefix}-${img_version}"
    local versioned_release_dir="${release_dir}/${versioned_name}"

    rm -rf "${versioned_release_dir}" && mkdir -p "${versioned_release_dir}"

    local cid
    cid=$(docker create "${img}")
    local e=0

    { docker cp "${cid}:/app/." "${versioned_release_dir}" 2>/dev/null && e=1; } || true
    docker rm "${cid}"

    if [[ "$e" == "1" ]]; then
        echo "> deployed into: ${versioned_release_dir}"
    else
        echo "> failed: please sure package is built first"
    fi
}

docker_release() {
    docker_build "$@"
    docker_package "$@"
    sign "$@"
}

docker_package_git() {
    git_version
    docker_package "$@"
}

docker_release_git() {
    git_version
    docker_release "$@"
}

docker_build_deploy_git() {
    git_version
    docker_build "$@"
    docker_deploy "$@"
}

docker_clean() {
    rm -rf "${RELEASE_DIR}"
    docker_clean_images
}

docker_clean_images() {
    echo "> clean: defichain images"

    local imgs
    imgs="$(docker images -f label=org.defichain.name=defichain -q)"
    if [[ -n "${imgs}" ]]; then
        # shellcheck disable=SC2086
        docker rmi ${imgs} --force
        _docker_clean_builder_base
    fi
}

_docker_clean_builder_base() {
    echo "> clean: defichain-builder-base images"

    # shellcheck disable=SC2046
    docker rmi $(docker images -f label=org.defichain.name=defichain-builder-base -q) \
        2>/dev/null || true
}

docker_purge() {
    echo "> clean: defichain* images"

    # shellcheck disable=SC2046
    docker rmi $(docker images -f label=org.defichain.name -q) \
        2>/dev/null || true
}

# -------------- Misc -----------------

sign() {
    # TODO: generate sha sums and sign
    :
}

git_version() {
    # If we have a tagged version (for proper releases), then just
    # release it with the tag, otherwise we use the commit hash
    local current_tag
    local current_commit
    local current_branch

    git fetch --tags
    current_tag=$(git tag --points-at HEAD | head -1)
    current_commit=$(git rev-parse --short HEAD)
    current_branch=$(git rev-parse --abbrev-ref HEAD)

    if [[ -z $current_tag || "${current_branch}" == "hotfix" ]]; then
        # Replace `/` in branch names with `-` as / is trouble
        IMAGE_VERSION="${current_branch//\//-}-${current_commit}"
        if [[ "${current_branch}" == "hotfix" ]]; then
            # If the current branch is hotfix branch,
            # prefix it with the last available tag.
            local last_tag
            last_tag="$(git describe --tags "$(git rev-list --tags --max-count=1)")"
            echo "> last tag: ${last_tag}"
            if [[ -n "${last_tag}" ]]; then
                IMAGE_VERSION="${last_tag}-${IMAGE_VERSION}"
            fi
        fi
    else
        IMAGE_VERSION="${current_tag}"
        # strip the 'v' infront of version tags
        if [[ "$IMAGE_VERSION" =~ ^v[0-9]\.[0-9] ]]; then
            IMAGE_VERSION="${IMAGE_VERSION##v}"
        fi
    fi

    echo "> git branch: ${current_branch}"
    echo "> version: ${IMAGE_VERSION}"

    if [[ -n "${GITHUB_ACTIONS-}" ]]; then
        # GitHub Actions
        echo "BUILD_VERSION=${IMAGE_VERSION}" >> "$GITHUB_ENV"
    fi
}

pkg_install_base() {
  apt update
  apt install -y apt-transport-https
  apt dist-upgrade -y
}

pkg_install_deps_x86_64() {
    apt install -y \
        software-properties-common build-essential git libtool autotools-dev automake \
        pkg-config bsdmainutils python3 python3-pip libssl-dev libevent-dev libboost-system-dev \
        libboost-filesystem-dev libboost-chrono-dev libboost-test-dev libboost-thread-dev \
        libminiupnpc-dev libzmq3-dev libqrencode-dev wget \
        libdb-dev libdb++-dev libdb5.3 libdb5.3-dev libdb5.3++ libdb5.3++-dev \
        curl cmake
}

pkg_install_mac_sdk_deps() {
  apt install -y \
        python3-dev python3-pip libcap-dev libbz2-dev libz-dev fonts-tuffy librsvg2-bin libtiff-tools imagemagick libtinfo5
}

pkg_install_deps_mingw_x86_64() {
  apt install -y \
        g++-mingw-w64-x86-64 mingw-w64-x86-64-dev nsis
}

pkg_install_llvm() {
    wget -O - "https://apt.llvm.org/llvm.sh" | bash -s 16
}

pkg_ensure_mac_sdk() {
    local sdk_name="Xcode-11.3.1-11C505-extracted-SDK-with-libcxx-headers"
    local pkg="${sdk_name}.tar.gz"

    echo "> ensuring mac sdk"

    mkdir -p ./depends/SDKs
    pushd ./depends/SDKs >/dev/null
    if [[ ! -d "$sdk_name" ]]; then
        if [[ ! -f "${pkg}" ]]; then
            wget https://bitcoincore.org/depends-sources/sdks/${pkg}
        fi
        tar -zxvf "${pkg}"
    fi
    rm "${pkg}" 2>/dev/null || true
    popd >/dev/null
}

clean_mac_sdk() {
    rm -rf ./depends/SDKs
}

purge() {
    clean
    clean_depends
    # shellcheck disable=SC2119
    docker_purge
}

clean_depends() {
    pushd ./depends >/dev/null
    make clean-all || true
    clean_mac_sdk
    rm -rf built \
        work \
        sources \
        x86_64* \
        i686* \
        mips* \
        arm* \
        aarch64* \
        riscv32* \
        riscv64*
    popd >/dev/null
}

clean() {
    make clean || true
    make distclean || true
    rm -rf "${RELEASE_DIR}"

    # All untracked git files that's left over after clean
    find . -type d -name ".deps" -exec rm -rf {} + || true

    rm -rf src/secp256k1/Makefile.in \
        src/secp256k1/aclocal.m4 \
        src/secp256k1/autom4te.cache/ \
        src/secp256k1/build-aux/compile \
        src/secp256k1/build-aux/config.guess \
        src/secp256k1/build-aux/config.sub \
        src/secp256k1/build-aux/depcomp \
        src/secp256k1/build-aux/install-sh \
        src/secp256k1/build-aux/ltmain.sh \
        src/secp256k1/build-aux/m4/libtool.m4 \
        src/secp256k1/build-aux/m4/ltoptions.m4 \
        src/secp256k1/build-aux/m4/ltsugar.m4 \
        src/secp256k1/build-aux/m4/ltversion.m4 \
        src/secp256k1/build-aux/m4/lt~obsolete.m4 \
        src/secp256k1/build-aux/missing \
        src/secp256k1/build-aux/test-driver \
        src/secp256k1/configure \
        src/secp256k1/src/libsecp256k1-config.h.in \
        src/secp256k1/src/libsecp256k1-config.h.in~ \
        src/univalue/Makefile.in \
        src/univalue/aclocal.m4 \
        src/univalue/autom4te.cache/ \
        src/univalue/build-aux/compile \
        src/univalue/build-aux/config.guess \
        src/univalue/build-aux/config.sub \
        src/univalue/build-aux/depcomp \
        src/univalue/build-aux/install-sh \
        src/univalue/build-aux/ltmain.sh \
        src/univalue/build-aux/m4/libtool.m4 \
        src/univalue/build-aux/m4/ltoptions.m4 \
        src/univalue/build-aux/m4/ltsugar.m4 \
        src/univalue/build-aux/m4/ltversion.m4 \
        src/univalue/build-aux/m4/lt~obsolete.m4 \
        src/univalue/build-aux/missing \
        src/univalue/build-aux/test-driver \
        src/univalue/configure \
        src/univalue/univalue-config.h.in \
        src/univalue/univalue-config.h.in~

    rm -rf ./autom4te.cache \
        Makefile.in \
        aclocal.m4 \
        build-aux/compile \
        build-aux/config.guess \
        build-aux/config.sub \
        build-aux/depcomp \
        build-aux/install-sh \
        build-aux/ltmain.sh \
        build-aux/m4/libtool.m4 \
        build-aux/m4/ltoptions.m4 \
        build-aux/m4/ltsugar.m4 \
        build-aux/m4/ltversion.m4 \
        build-aux/m4/lt~obsolete.m4 \
        build-aux/missing \
        build-aux/test-driver \
        configure \
        doc/man/Makefile.in \
        src/Makefile.in \
        src/config/defi-config.h.in \
        src/config/defi-config.h.in~
}

main "$@"
