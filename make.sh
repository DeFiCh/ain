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
    DOCKERFILES_DIR=${DOCKERFILES_DIR:-"./contrib/dockerfiles"}
    RELEASE_DIR=${RELEASE_DIR:-"./build"}

    EXTRA_CONF_ARGS=${EXTRA_CONF_ARGS:-}
    EXTRA_MAKE_ARGS=${EXTRA_MAKE_ARGS:-}
    EXTRA_MAKE_DEPENDS_ARGS=${EXTRA_MAKE_DEPENDS_ARGS:-}

    # shellcheck disable=SC2206
    # This intentionally word-splits the array as env arg can only be strings.
    # Other options available: x86_64-w64-mingw32 x86_64-apple-darwin11
    TARGETS=(${TARGETS:-"x86_64-pc-linux-gnu"})
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


build() {
    local target=${1:-"x86_64-pc-linux-gnu"}
    local extra_conf_opts=${EXTRA_CONF_ARGS:-}
    local extra_make_args=${EXTRA_MAKE_ARGS:--j $(nproc)}
    local extra_make_depends_args=${EXTRA_MAKE_DEPENDS_ARGS:--j $(nproc)}

    echo "> build: ${target}"
    pushd ./depends >/dev/null
    # XREF: #depends-make
    make NO_QT=1 ${extra_make_depends_args}
    popd >/dev/null
    ./autogen.sh
    # XREF: #make-configure
    ./configure CC=clang-11 CXX=clang++-11 --prefix="$(pwd)/depends/${target}" ${extra_conf_opts}
    make ${extra_make_args}
}

deploy() {
    local target=${1:-"x86_64-pc-linux-gnu"}
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
    rm -rf ./${versioned_name} && mkdir "${versioned_name}"
    popd >/dev/null

    make prefix=/ DESTDIR="${versioned_release_path}" install && cp README.md "${versioned_release_path}/"

    echo "> deployed: ${versioned_release_path}"
}

package() {
    local target=${1:-"x86_64-pc-linux-gnu"}
    local img_prefix="${IMAGE_PREFIX}"
    local img_version="${IMAGE_VERSION}"
    local release_dir="${RELEASE_DIR}"

    deploy "${target}"

    # XREF: #pkg-name
    local pkg_name="${img_prefix}-${img_version}-${target}"
    local pkg_tar_file_name="${pkg_name}.tar.gz"

    local pkg_path
    pkg_path="$(readlink -m ${release_dir}/${pkg_tar_file_name})"

    local versioned_name="${img_prefix}-${img_version}"
    local versioned_release_dir="${release_dir}/${versioned_name}"

    echo "> packaging: ${pkg_name} from ${versioned_release_dir}"

    pushd "${versioned_release_dir}" >/dev/null
    tar --transform "s,^./,${versioned_name}/," -cvzf "${pkg_path}" ./*
    popd >/dev/null

    echo "> package: ${pkg_path}"
}

release() {
    local target=${1:-"x86_64-pc-linux-gnu"}

    build "${target}"
    package "${target}"
    sign
}

# -------------- Docker ---------------

docker_build() {
    local targets=("${TARGETS[@]}")
    local img_prefix="${IMAGE_PREFIX}"
    local img_version="${IMAGE_VERSION}"
    local dockerfiles_dir="${DOCKERFILES_DIR}"
    local docker_context="${DOCKER_ROOT_CONTEXT}"

    echo "> docker-build";

    for target in "${targets[@]}"; do
        if [[ "$target" == "x86_64-apple-darwin11" ]]; then
            pkg_ensure_mac_sdk
        fi
        local img="${img_prefix}-${target}:${img_version}"
        echo "> building: ${img}"
        local docker_file="${dockerfiles_dir}/${target}.dockerfile"
        echo "> docker build: ${img}"
        docker build -f "${docker_file}" -t "${img}" "${docker_context}"
    done
}

docker_package() {
    local targets=("${TARGETS[@]}")
    local img_prefix="${IMAGE_PREFIX}"
    local img_version="${IMAGE_VERSION}"
    local release_dir="${RELEASE_DIR}"

    echo "> docker-package";

    for target in "${targets[@]}"; do
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
    done
}

docker_deploy() {
    local targets=("${TARGETS[@]}")
    local img_prefix="${IMAGE_PREFIX}"
    local img_version="${IMAGE_VERSION}"
    local release_dir="${RELEASE_DIR}"

    echo "> docker-deploy";

    for target in "${targets[@]}"; do
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
    done
}

docker_release() {
    docker_build
    docker_package
    sign
}

docker_package_git() {
    git_version
    docker_package
}

docker_release_git() {
    git_version
    docker_release
}

docker_build_deploy_git() {
    git_version
    docker_build
    docker_deploy
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

    current_tag=$(git tag --points-at HEAD | head -1)
    current_commit=$(git rev-parse --short HEAD)
    current_branch=$(git rev-parse --abbrev-ref HEAD)

    if [[ -z $current_tag ]]; then
        # Replace `/` in branch names with `-` as / is trouble
        IMAGE_VERSION="${current_branch//\//-}-${current_commit}"
    else
        IMAGE_VERSION="${current_tag}"
        # strip the 'v' infront of version tags
        if [[ "$IMAGE_VERSION" =~ ^v[0-9]\.[0-9] ]]; then
            IMAGE_VERSION="${IMAGE_VERSION##v}"
        fi
    fi

    echo "> version: ${IMAGE_VERSION}"
    echo "BUILD_VERSION=${IMAGE_VERSION}" >> $GITHUB_ENV # GitHub Actions
}

pkg_install_deps() {
    sudo apt update && sudo apt dist-upgrade -y
    sudo apt install -y software-properties-common build-essential libtool autotools-dev automake \
        pkg-config bsdmainutils python3 libssl-dev libevent-dev libboost-system-dev \
        libboost-filesystem-dev libboost-chrono-dev libboost-test-dev libboost-thread-dev \
        libminiupnpc-dev libzmq3-dev libqrencode-dev wget \
        curl cmake
    wget https://apt.llvm.org/llvm.sh
    chmod +x llvm.sh
    sudo ./llvm.sh 11
}

pkg_ensure_mac_sdk() {
    local sdk_name="MacOSX10.11.sdk"
    local pkg="${sdk_name}.tar.xz"

    echo "> ensuring mac sdk"

    mkdir -p ./depends/SDKs
    pushd ./depends/SDKs >/dev/null
    if [[ ! -d "$sdk_name" ]]; then
        if [[ ! -f "${pkg}" ]]; then
            wget https://github.com/phracker/MacOSX-SDKs/releases/download/10.15/MacOSX10.11.sdk.tar.xz
        fi
        tar -xvf "${pkg}"
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
