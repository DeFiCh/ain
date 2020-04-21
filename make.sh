#!/usr/bin/env bash

# Copyright (c) 2020 DeFi Blockchain Developers
# Maker script

# shellcheck disable=SC2155

export LC_ALL=C
set -Eeuo pipefail

setup_vars() {
    IMAGE_PREFIX=${IMAGE_PREFIX:-"defichain"}
    IMAGE_VERSION=${IMAGE_VERSION:-"latest"}

    DOCKER_ROOT_CONTEXT=${DOCKER_ROOT_CONTEXT:-"."}
    DOCKERFILES_DIR=${DOCKERFILES_DIR:-"./contrib/dockerfiles"}
    DOCKER_DEV_VOLUME_SUFFIX=${DOCKER_DEV_VOLUME_SUFFIX:-"dev-data"}
    RELEASE_DIR=${RELEASE_DIR:-"./build"}

    # shellcheck disable=SC2206
    # This intentionally word-splits the array as env arg can only be strings.
    # Other options available: x86_64-w64-mingw32
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
    local dir="$(dirname "${BASH_SOURCE[0]}")"
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

# -------------- Docker ---------------

docker_build() {
    local targets=("${TARGETS[@]}")
    local img_prefix="${IMAGE_PREFIX}"
    local img_version="${IMAGE_VERSION}"
    local dockerfiles_dir="${DOCKERFILES_DIR}"
    local docker_context="${DOCKER_ROOT_CONTEXT}"

    echo "> docker-build";

    for target in "${targets[@]}"; do
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
        local pkg_path="$(realpath "${release_dir}/${pkg_tar_file_name}")"
        local versioned_name="${img_prefix}-${img_version}"

        mkdir -p "${release_dir}"

        docker run --rm "${img}" bash -c \
            "tar --transform 's,^./,${versioned_name}/,' -czf - ./*" >"${pkg_path}"

        echo "> package: ${pkg_path}"
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

        local cid=$(docker create "${img}")
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

# ----------- Direct builds ---------------

build() {
    local target=${1:-"x86_64-pc-linux-gnu"}

    echo "> build: ${target}"
    pushd ./depends >/dev/null
    # XREF: #depends-make
    make NO_QT=1
    popd >/dev/null
    ./autogen.sh
    # XREF: #make-configure
    ./configure --prefix="$(pwd)/depends/${target}" --without-gui --disable-tests
    make
}

deploy() {
    local target=${1:-"x86_64-pc-linux-gnu"}
    local img_prefix="${IMAGE_PREFIX}"
    local img_version="${IMAGE_VERSION}"
    local release_dir="${RELEASE_DIR}"

    # XREF: #pkg-name
    local versioned_name="${img_prefix}-${img_version}"
    local versioned_release_path="$(realpath "${release_dir}/${versioned_name}")"

    echo "> deploy into: ${release_dir}"

    mkdir -p "${release_dir}"

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
    local pkg_path="$(realpath ${release_dir}/${pkg_tar_file_name})"

    local versioned_name="${img_prefix}-${img_version}"
    local versioned_release_dir="${release_dir}/${versioned_name}"

    echo "> packaging: ${pkg_name}"

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

# -------------- Docker dev -------------------

docker_dev_build() {
    local target=${1:-"x86_64-pc-linux-gnu"}

    local img_prefix="${IMAGE_PREFIX}"
    local img_version="${IMAGE_VERSION}"
    local dockerfiles_dir="${DOCKERFILES_DIR}"
    local docker_context="${DOCKER_ROOT_CONTEXT}"
    local docker_dev_volume_suffix="${DOCKER_DEV_VOLUME_SUFFIX}"

    local img="${img_prefix}-dev-${target}:${img_version}"
    local vol="${img_prefix}-${target}-${docker_dev_volume_suffix}"

    echo "> docker-dev-build: ${img}"

    local builders_docker_file="${dockerfiles_dir}/${target}.dockerfile"
    local docker_file="${dockerfiles_dir}/${target}-dev.dockerfile"

    docker build --target "builder-base" \
        -t "${img_prefix}-builder-base-${target}" - <"${builders_docker_file}"
    docker build -f "${docker_file}" -t "${img}" "${docker_context}"
    docker run --rm -v "${vol}:/data" "${img}"

    echo "> built: ${img}"
}

docker_dev_package() {
    local target=${1:-"x86_64-pc-linux-gnu"}

    local img_prefix="${IMAGE_PREFIX}"
    local img_version="${IMAGE_VERSION}"
    local release_dir="${RELEASE_DIR}"
    local docker_dev_volume_suffix="${DOCKER_DEV_VOLUME_SUFFIX}"

    # XREF: #pkg-name
    local pkg_name="${img_prefix}-${img_version}-${target}"
    local pkg_tar_file_name="${pkg_name}.tar.gz"
    local pkg_path="${release_dir}/${pkg_tar_file_name}"
    local vol="${img_prefix}-${target}-${docker_dev_volume_suffix}"

    echo "> docker-dev-package: ${pkg_name}"

    mkdir -p "${release_dir}"

    local cid=$(docker create -v "${vol}:/data" ubuntu:18.04)
    local e=0

    { docker cp "${cid}:/data/${pkg_path}" "${pkg_path}" 2>/dev/null && e=1; } || true
    docker rm "${cid}" >/dev/null

    if [[ "$e" == "1" ]]; then
        echo "> package: ${pkg_path}"
    else
        echo "> failed: please sure package is built first"
    fi
}

docker_dev_release() {
    local target=${1:-"x86_64-pc-linux-gnu"}

    docker_dev_build "${target}"
    docker_dev_package "${target}"
    sign
}

docker_dev_clean() {
    local target=${1:-"x86_64-pc-linux-gnu"}

    docker_dev_clean_images
    docker_dev_clean_volumes "${target}"
}

docker_clean_images() {
    echo "> clean: defichain images"

    local imgs="$(docker images -f label=org.defichain.name=defichain -q)"
    if [[ -n "${imgs}" ]]; then
        # shellcheck disable=SC2086
        docker rmi ${imgs} --force
        _docker_clean_builder_base
    fi
}

docker_dev_clean_images() {
    echo "> clean: defichain-dev images"

    local imgs="$(docker images -f label=org.defichain.name=defichain-dev -q)"
    if [[ -n "${imgs}" ]]; then
        # shellcheck disable=SC2086
        docker rmi ${imgs} --force
        _docker_clean_builder_base
    fi
}

docker_dev_clean_volumes() {
    local target=${1:-"x86_64-pc-linux-gnu"}

    local img_prefix="${IMAGE_PREFIX}"
    local docker_dev_volume_suffix="${DOCKER_DEV_VOLUME_SUFFIX}"
    local vol="${img_prefix}-${target}-${docker_dev_volume_suffix}"

    echo "> clean: docker volume: ${vol}"

    docker volume rm "${vol}" 2>/dev/null || true
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

    docker_dev_clean_volumes
}

# -------------- Misc -----------------

sign() {
    # TODO: generate sha sums and sign
    :
}

git_version() {
    # If we have a tagged version (for proper releases), then just
    # release it with the tag, otherwise we use the commit hash
    local current_tag=$(git tag --points-at HEAD | head -1)
    local current_commit=$(git rev-parse --short HEAD)
    local current_branch=$(git rev-parse --abbrev-ref HEAD)

    if [[ -z $current_tag ]]; then
        IMAGE_VERSION="${current_branch}-${current_commit}"
    else
        IMAGE_VERSION="${current_tag}"
    fi

    echo "> version: ${IMAGE_VERSION}"
}

pkg_install_deps() {
    sudo apt update && sudo apt dist-upgrade -y
    sudo apt install -y software-properties-common build-essential libtool autotools-dev automake \
        pkg-config bsdmainutils python3 libssl-dev libevent-dev libboost-system-dev \
        libboost-filesystem-dev libboost-chrono-dev libboost-test-dev libboost-thread-dev \
        libminiupnpc-dev libzmq3-dev libqrencode-dev \
        curl cmake
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
    rm -rf built
    rm -rf work
    rm -rf sources
    rm -rf x86_64*
    rm -rf i686*
    rm -rf mips*
    rm -rf arm*
    rm -rf aarch64*
    rm -rf riscv32*
    rm -rf riscv64*
    popd >/dev/null
}

clean() {
    make clean || true
    make distclean || true
    rm -rf "${RELEASE_DIR}"

    # All untracked git files that's left over after clean
    find . -type d -name ".deps" -exec rm -rf {} + || true

    rm -rf src/secp256k1/Makefile.in
    rm -rf src/secp256k1/aclocal.m4
    rm -rf src/secp256k1/autom4te.cache/
    rm -rf src/secp256k1/build-aux/compile
    rm -rf src/secp256k1/build-aux/config.guess
    rm -rf src/secp256k1/build-aux/config.sub
    rm -rf src/secp256k1/build-aux/depcomp
    rm -rf src/secp256k1/build-aux/install-sh
    rm -rf src/secp256k1/build-aux/ltmain.sh
    rm -rf src/secp256k1/build-aux/m4/libtool.m4
    rm -rf src/secp256k1/build-aux/m4/ltoptions.m4
    rm -rf src/secp256k1/build-aux/m4/ltsugar.m4
    rm -rf src/secp256k1/build-aux/m4/ltversion.m4
    rm -rf src/secp256k1/build-aux/m4/lt~obsolete.m4
    rm -rf src/secp256k1/build-aux/missing
    rm -rf src/secp256k1/build-aux/test-driver
    rm -rf src/secp256k1/configure
    rm -rf src/secp256k1/src/libsecp256k1-config.h.in
    rm -rf src/secp256k1/src/libsecp256k1-config.h.in~
    rm -rf src/univalue/Makefile.in
    rm -rf src/univalue/aclocal.m4
    rm -rf src/univalue/autom4te.cache/
    rm -rf src/univalue/build-aux/compile
    rm -rf src/univalue/build-aux/config.guess
    rm -rf src/univalue/build-aux/config.sub
    rm -rf src/univalue/build-aux/depcomp
    rm -rf src/univalue/build-aux/install-sh
    rm -rf src/univalue/build-aux/ltmain.sh
    rm -rf src/univalue/build-aux/m4/libtool.m4
    rm -rf src/univalue/build-aux/m4/ltoptions.m4
    rm -rf src/univalue/build-aux/m4/ltsugar.m4
    rm -rf src/univalue/build-aux/m4/ltversion.m4
    rm -rf src/univalue/build-aux/m4/lt~obsolete.m4
    rm -rf src/univalue/build-aux/missing
    rm -rf src/univalue/build-aux/test-driver
    rm -rf src/univalue/configure
    rm -rf src/univalue/univalue-config.h.in
    rm -rf src/univalue/univalue-config.h.in~

    rm -rf ./autom4te.cache
    rm -rf Makefile.in
    rm -rf aclocal.m4
    rm -rf build-aux/compile
    rm -rf build-aux/config.guess
    rm -rf build-aux/config.sub
    rm -rf build-aux/depcomp
    rm -rf build-aux/install-sh
    rm -rf build-aux/ltmain.sh
    rm -rf build-aux/m4/libtool.m4
    rm -rf build-aux/m4/ltoptions.m4
    rm -rf build-aux/m4/ltsugar.m4
    rm -rf build-aux/m4/ltversion.m4
    rm -rf build-aux/m4/lt~obsolete.m4
    rm -rf build-aux/missing
    rm -rf build-aux/test-driver
    rm -rf configure
    rm -rf doc/man/Makefile.in
    rm -rf src/Makefile.in
    rm -rf src/config/defi-config.h.in
    rm -rf src/config/defi-config.h.in~
}

main "$@"
