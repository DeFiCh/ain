#!/bin/bash

# Maker script

set -Eeuo pipefail

setup_vars() {
    IMAGE_PREFIX=${IMAGE_PREFIX:-"defichain"}
    IMAGE_VERSION=${IMAGE_VERSION:-"latest"}

    DOCKER_ROOT_CONTEXT=${DOCKER_ROOT_CONTEXT:-"."}
    DOCKERFILES_DIR=${DOCKERFILES_DIR:-"./contrib/dockerfiles"}
    DOCKER_DEV_VOLUME_SUFFIX=${DOCKER_DEV_VOLUME_SUFFIX:-"dev-data"}
    RELEASE_DIR=${RELEASE_DIR:-"./build"}

    # Other options available: x86_64-w64-mingw32
    TARGETS=(${TARGETS:-"x86_64-pc-linux-gnu"})

    ## AWS config for S3 upload
    AWS_ACCESS_KEY_ID=${AWS_ACCESS_KEY_ID:-}         # ci
    AWS_SECRET_ACCESS_KEY=${AWS_SECRET_ACCESS_KEY:-} # ci
    AWS_DEFAULT_REGION=${AWS_DEFAULT_REGION:-"ap-southeast-1"}

    # use defichain/release for tagged releases from ci
    AWS_S3_BUCKET=${AWS_S3_BUCKET:-"defichain/dev"}
    AWS_S3_STORAGE_TYPE=${AWS_S3_STORAGE_TYPE:-"STANDARD"}

    # packaging specifics
    PKG_VERSION_PREFIX=
}

main() {
    _ensure_script_dir
    trap _cleanup 0 1 2 3 6 15 ERR
    cd "$_SCRIPT_DIR"
    setup_vars

    # Get all functions declared in this file except ones starting with
    # '_' or the ones in the list
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
    # NOTE: intentionally unquoted to leave out empty ones
    local cmds=(${COMMANDS[@]//dev_*/})
    printf "\t%s\n" "${cmds[@]//_/-}"
    printf "\nDeveloper Commands: \n"
    local cmds=($(printf "%s\n" "${COMMANDS[@]}" | grep "^dev_")) || true
    printf "\t%s\n" "${cmds[@]//_/-}"
    printf "\nNote: The developer commands assume that it's run on an environment \n"
    printf "with correct arch and the pre-requisites properly configured. \n"
}

build() {
    local targets=${TARGETS[@]}
    local img_prefix=${IMAGE_PREFIX}
    local img_version=${IMAGE_VERSION}
    local dockerfiles_dir=${DOCKERFILES_DIR}
    local docker_context=${DOCKER_ROOT_CONTEXT}

    for target in ${targets[@]}; do
        local img="${img_prefix}-${target}:${img_version}"
        echo "> building: ${img}"

        local builder_docker_file="${dockerfiles_dir}/builder-${target}.dockerfile"
        local docker_file="${dockerfiles_dir}/${target}.dockerfile"
        
        echo "> docker build: ${img_prefix}-builder-${target}"
        docker build -t "${img_prefix}-builder-${target}" - <${builder_docker_file}
        echo "> docker build: ${img}"
        docker build -f ${docker_file} -t "${img}" "${docker_context}"
    done
}

package() {
    local targets=${TARGETS[@]}
    local img_prefix=${IMAGE_PREFIX}
    local img_version=${IMAGE_VERSION}
    local release_dir=${RELEASE_DIR}
    local pkg_ver_prefix=${PKG_VERSION_PREFIX//\//-}

    for target in ${targets[@]}; do
        local img="${img_prefix}-${target}:${img_version}"
        echo "> packaging: ${img}"

        # XREF: #pkg-name
        local pkg_name="${img_prefix}-${target}-${pkg_ver_prefix}${img_version}.tar.gz"
        local pkg_path=$(readlink -m "${release_dir}/${pkg_name}")
        mkdir -p $(dirname ${pkg_path})

        docker run --rm "${img}" bash -c "tar -czf - *" >"${pkg_path}"
        echo "> package: ${pkg_path}"
    done
}

sign() {
    # TODO: generate sha sums and sign
    :
}

release() {
    build
    package
    sign
}

release_git() {
    # If we have a tagged version (for proper releases), then just
    # release it with the tag, otherwise we use the commit hash
    local current_tag=$(git tag --points-at HEAD | head -1)
    local current_commit=$(git rev-parse --short HEAD)

    if [[ -z $current_tag ]]; then
        IMAGE_VERSION="${current_commit}"
    else
        IMAGE_VERSION="${current_tag}"
    fi

    echo "> version: ${IMAGE_VERSION}"

    release
}

dev_pkg_install_deps() {
    sudo apt update && sudo apt dist-upgrade -y
    sudo apt install -y software-properties-common build-essential libtool autotools-dev automake \
        pkg-config bsdmainutils python3 libssl-dev libevent-dev libboost-system-dev \
        libboost-filesystem-dev libboost-chrono-dev libboost-test-dev libboost-thread-dev \
        libminiupnpc-dev libzmq3-dev libqrencode-dev \
        curl cmake
}

dev_build() {
    local target=${1:-"x86_64-pc-linux-gnu"}

    echo "> dev build: ${target}"
    pushd ./depends >/dev/null
    # XREF: #depends-make
    make NO_QT=1
    popd >/dev/null
    ./autogen.sh
    # XREF: #make-configure
    ./configure --prefix=$(pwd)/depends/${target} --without-gui --disable-tests
    make
}

dev_package() {
    local target=${1:-"x86_64-pc-linux-gnu"}
    local img_prefix=${IMAGE_PREFIX}
    local img_version=${IMAGE_VERSION}
    local release_dir=${RELEASE_DIR}
    local pkg_ver_prefix=${PKG_VERSION_PREFIX//\//-}

    # XREF: #pkg-name
    local pkg_name="${img_prefix}-${target}-${pkg_ver_prefix}${img_version}.tar.gz"
    local pkg_path=$(readlink -m "${release_dir}/${pkg_name}")
    mkdir -p $(dirname ${pkg_path})

    echo "> packaging: ${pkg_name}"

    pushd ./src/ >/dev/null
    # XREF: #defi-package-bins
    tar -cvzf ${pkg_path} ./defid ./defi-cli ./defi-wallet ./defi-tx
    popd >/dev/null
    echo "> package: ${pkg_path}"
}

dev_release() {
    local target=${1:-"x86_64-pc-linux-gnu"}
    dev_build "${target}"
    dev_package "${target}"
}

dev_build_on_docker() {
    local target=${1:-"x86_64-pc-linux-gnu"}
    local img_prefix=${IMAGE_PREFIX}
    local img_version=${IMAGE_VERSION}
    local dockerfiles_dir=${DOCKERFILES_DIR}
    local docker_context=${DOCKER_ROOT_CONTEXT}
    local docker_dev_volume_suffix=${DOCKER_DEV_VOLUME_SUFFIX}

    local img="${img_prefix}-${target}-dev:${img_version}"
    echo "> building: ${img}"

    local builder_docker_file="${dockerfiles_dir}/builder-${target}.dockerfile"
    local docker_file="${dockerfiles_dir}/${target}-dev.dockerfile"

    docker build -t "${img_prefix}-builder-${target}" - <${builder_docker_file}
    docker build -f ${docker_file} -t "${img}" "${docker_context}"
    docker run -v ${img_prefix}-${target}-${docker_dev_volume_suffix}:/data ${img}
}

dev_package_on_docker() {
    local target=${1:-"x86_64-pc-linux-gnu"}

    # TODO: package from volume
    echo "> todo: package from volume"
}

dev_release_on_docker() {
    local target=${1:-"x86_64-pc-linux-gnu"}

    dev_build_on_docker "${target}"
    dev_package_on_docker "${target}"
}

publish() {
    for file in ${RELEASE_DIR}/*; do
        _aws_put_s3 ${file}
    done
}

_aws_put_s3() {
    local pkg_file="${1?-package file required}"
    local s3_bucket=${AWS_S3_BUCKET?-AWS_S3_BUCKET bucket env required}

    local pkg_name="$(basename ${pkg_file})"

    if [[ -z $(command -v aws) ]]; then
        echo "> AWS CLI required for deployment to S3"
        exit 1
    fi

    aws s3 cp ${pkg_file} s3://${s3_bucket}/${pkg_name}
}

dev_clean() {
    make clean || true
    make distclean || true
    rm -rf ${RELEASE_DIR}

    # All untracked git files that's left over after clean

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

dev_purge() {
    dev_clean
    pushd ./depends >/dev/null
    make clean-all || true
    popd >/dev/null
}

main "$@"
