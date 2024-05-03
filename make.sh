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
    DOCKER_RELATIVE_BUILD_DIR=${DOCKER_RELATIVE_BUILD_DIR:-"./build"}
    DOCKERFILES_DIR=${DOCKERFILES_DIR:-"./contrib/dockerfiles"}
    DEFI_DOCKERFILE=${DEFI_DOCKERFILE:-"${DOCKERFILES_DIR}/defi.dockerfile"}

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
}

main() {
    _bash_version_check
    _setup_dir_env
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

_setup_dir_env() {
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
    make DESTDIR="${build_target_dir}" -j${make_jobs} JOBS=${make_jobs} ${make_args}

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

    local pkg_path
    if [[ "$target" == "x86_64-w64-mingw32" ]]; then
        pkg_path="$(_canonicalize "${build_dir}/${pkg_name}.zip")"
    else
        pkg_path="$(_canonicalize "${build_dir}/${pkg_name}.tar.gz")"
    fi

    local versioned_name="${img_prefix}-${img_version}"
    local versioned_build_dir="${build_dir}/${versioned_name}"

    if [[ ! -d "$versioned_build_dir" ]]; then
        echo "> error: deployment required to package"
        echo "> tip: try \`$0 deploy\` or \`$0 docker-deploy\` first"
        exit 1
    fi

    echo "> packaging: ${pkg_name} from ${versioned_build_dir}"

    if [[ "$target" == "x86_64-w64-mingw32" ]]; then
        _ensure_enter_dir "${build_dir}"
        zip -r "${pkg_path}" "$(basename "${versioned_build_dir}")"
    else
        _ensure_enter_dir "${versioned_build_dir}"
        _tar --transform "s,^./,${versioned_name}/," -czf "${pkg_path}" ./*
    fi
    sha256sum "${pkg_path}" > "${pkg_path}.SHA256"
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

docker_build_from_binaries() {
    local target=${1:-${TARGET}}
    local img_prefix="${IMAGE_PREFIX}"
    local img_version="${IMAGE_VERSION}"
    local build_dir="${DOCKER_RELATIVE_BUILD_DIR}"

    local docker_context="${DOCKER_ROOT_CONTEXT}"
    local docker_file="${DEFI_DOCKERFILE}"

    echo "> docker-build-from-binaries";

    local img="${img_prefix}-${target}:${img_version}"
    echo "> building: ${img}"
    echo "> docker defi build: ${img}"

    local versioned_name="${img_prefix}-${img_version}"
    local versioned_build_dir="${build_dir}/${versioned_name}"

    docker build -f "${docker_file}" \
        --build-arg BINARY_DIR="${versioned_build_dir}" \
        -t "${img}" "${docker_context}"
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

# Python helpers
# ---

py_ensure_env_active() {
    pkg_local_ensure_py_deps
    py_env_activate
}

py_env_activate() {
  local python_venv="${PYTHON_VENV_DIR}"
  python3 -m venv "${python_venv}"
  # shellcheck disable=SC1091
  source "${python_venv}/bin/activate"
}

py_env_deactivate() {
  deactivate
}

# -------------- Misc -----------------


# check
# ---

check() {
    check_git_dirty
    check_sh
    check_py
    check_rs
    # check_lints
    check_cpp
}

check_git_dirty() {
    if [[ $(git status -s) ]]; then
        echo "error: Git tree dirty. Please commit or stash first"
        exit 1
    fi
}

check_py() {
    py_ensure_env_active
    _exec_black 1
    # TODO Add flake as well
    py_env_deactivate
}

check_rs() {
    lib clippy 1
    lib fmt-check 1
}

check_lints() {
    py_ensure_env_active
    _fold_start "check-doc"
    test/lint/check-doc.py
    _fold_end

    # _fold_start "check-rpc-mappings"
    # test/lint/check-rpc-mappings.py .
    # _fold_end

    test/lint/lint-all.sh
    py_env_deactivate
}

check_sh() {
    py_ensure_env_active

    # TODO: Remove most of the specific ignores after resolving them
    # shellcheck disable=SC2086
    find . -not \( -path ./build -prune \
        -or -path ./autogen.sh \
        -or -path ./test/lint/lint-python.sh \
        -or -path ./test/lint/lint-rpc-help.sh \
        -or -path ./test/lint/lint-shell.sh \
        -or -path ./test/lint/disabled-lint-spelling.sh \
        -or -path ./test/lint/commit-script-check.sh \
        -or -path ./test/lint/lint-includes.sh \
        -or -path ./test/lint/lint-python-dead-code.sh \
        -or -path ./src/univalue -prune \
        -or -path ./src/secp256k1 -prune \
        -or -path ./build\* \)  -name '*.sh' -print0 | xargs -0L1 shellcheck

    py_env_deactivate
}

check_cpp() {
    _run_clang_format 1
}

check_enter_build_rs_dir() {
    local build_dir="${BUILD_DIR}"
    _ensure_enter_dir "$build_dir/lib" || {
        echo "Please configure first";
        exit 1; }
}


# fmt
# ---

fmt() {
    fmt_py
    fmt_lib
    fmt_cpp
}

fmt_py() {
    echo "> fmt: py"
    py_ensure_env_active
    _exec_black
    py_env_deactivate
}

fmt_rs() {
    fmt_lib
}

fmt_cpp() {
    echo "> fmt: cpp"
    _run_clang_format 0
}

_run_clang_format() {
    local check_only=${1:-0}
    local clang_ver=${CLANG_DEFAULT_VERSION}
    local clang_formatters=("clang-format-${clang_ver}" "clang-format")
    local index=-1
    local fmt_args=""

    for ((idx=0; idx<${#clang_formatters[@]}; ++idx)); do
        if "${clang_formatters[$idx]}" --version &> /dev/null; then
            index="$idx"
            break
        fi
    done
    if [[ "$index" == -1 ]]; then
        echo "clang-format(-${clang_ver}) required"
        exit 1
    fi

    if [[ "$check_only" == 1 ]]; then
        fmt_args="--dry-run --Werror"
    fi

    # shellcheck disable=SC2086
    find src/dfi src/ffi \( -iname "*.cpp" -o -iname "*.h" \) -print0 | \
        xargs -0 -I{} "${clang_formatters[$index]}" $fmt_args -i -style=file {}

    local whitelist_files=(src/miner.{cpp,h} src/txmempool.{cpp,h} src/validation.{cpp,h})

    # shellcheck disable=SC2086
    "${clang_formatters[$index]}" $fmt_args -i -style=file "${whitelist_files[@]}"
}

fmt_lib() {
    echo "> fmt: rs"
    check_enter_build_rs_dir
    lib fmt
    _exit_dir
}

# tests
# ---

test() {
    _fold_start "unit-tests"
    # shellcheck disable=SC2086
    test_unit "$@"
    _fold_end

    _fold_start "functional-tests"
    # shellcheck disable=SC2119
    test_py
    _fold_end
}

test_unit() {
    test_cpp "$@"
    test_rs "$@"
}

test_cpp() {
    local make_jobs=${MAKE_JOBS}
    local make_args=${MAKE_ARGS:-}
    local build_target_dir=${BUILD_TARGET_DIR}

    _ensure_enter_dir "${build_target_dir}"
    # shellcheck disable=SC2086
    make -j$make_jobs check "$@"
    _exit_dir
}

test_rs() {
    lib test "$@"
}

# shellcheck disable=SC2120
test_py() {
    local build_target_dir=${BUILD_TARGET_DIR}
    local src_dir=${_SCRIPT_DIR}
    local tests_fail_fast=${TESTS_FAILFAST}
    local tests_combined_logs=${TESTS_COMBINED_LOGS}
    local make_jobs=${MAKE_JOBS}
    local extra_args=""
    local first_arg="${1:-}"

    # If an argument is given as an existing file, we switch that
    # out to the last arg
    if [[ -f "${first_arg}" ]]; then
      shift
    elif [[ -f "${src_dir}/test/functional/${first_arg}" ]]; then
      first_arg="${src_dir}/test/functional/${first_arg}"
      shift
    else
      # We don't shift, this just ends up in the $@ args
      first_arg=""
    fi

    if [[ "$tests_fail_fast" == "1" ]]; then
        extra_args="--failfast"
    fi

    _ensure_enter_dir "${build_target_dir}"
    py_ensure_env_active

    # shellcheck disable=SC2086
    (set -x; python3 ${build_target_dir}/test/functional/test_runner.py \
        --tmpdirprefix="./test_runner/" \
        --ansi \
        --configfile="${build_target_dir}/test/config.ini" \
        --jobs=${make_jobs} \
        --combinedlogslen=${tests_combined_logs} \
        ${extra_args} ${first_arg} "$@")

    py_env_deactivate
    _exit_dir
}

# Others

debug_env() {
    (set -o posix ; set)
    (set -x +e
    uname -a
    gcc -v
    "clang-${CLANG_DEFAULT_VERSION}" -v
    rustup show)
}

exec() {
    local make_jobs=${MAKE_JOBS}
    local make_args=${MAKE_ARGS:-}
    local build_target_dir=${BUILD_TARGET_DIR}

    _ensure_enter_dir "${build_target_dir}"
    _fold_start "exec:: ${*-(default)}"

    # shellcheck disable=SC2086,SC2068
    make -j$make_jobs JOBS=${make_jobs} $make_args $@

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
    # python3-venv for settings up all python deps
    apt-get install -y \
        software-properties-common build-essential git libtool autotools-dev automake \
        pkg-config bsdmainutils python3 python3-pip python3-venv libssl-dev libevent-dev \
        libboost-chrono-dev libboost-test-dev libboost-thread-dev \
        libminiupnpc-dev libzmq3-dev libqrencode-dev wget ccache \
        libdb-dev libdb++-dev libdb5.3 libdb5.3-dev libdb5.3++ libdb5.3++-dev \
        curl cmake zip unzip libc6-dev gcc-multilib locales locales-all

    _fold_end
}

pkg_setup_locale() {
    # Not a hard requirement. We use en_US.UTF-8 to maintain coherence across
    # different platforms. C.UTF-8 is not available on all platforms.
    locale-gen "en_US.UTF-8"
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

pkg_install_gh_cli() {
    _fold_start "pkg-install-gh_cli"
    curl -fsSL https://cli.github.com/packages/githubcli-archive-keyring.gpg | \
        dd of=/usr/share/keyrings/githubcli-archive-keyring.gpg && \
        chmod go+r /usr/share/keyrings/githubcli-archive-keyring.gpg
    echo "deb [arch=$(dpkg --print-architecture) \
        signed-by=/usr/share/keyrings/githubcli-archive-keyring.gpg] \
        https://cli.github.com/packages stable main" | \
        tee /etc/apt/sources.list.d/github-cli.list > /dev/null
    apt-get update
    apt-get install -y gh

}

pkg_install_llvm() {
    _fold_start "pkg-install-llvm"
    # shellcheck disable=SC2086
    wget -O - "https://apt.llvm.org/llvm.sh" | bash -s ${CLANG_DEFAULT_VERSION}
    _fold_end
}

pkg_user_install_rust() {
    _fold_start "pkg-install-rust"
    curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh -s -- \
        --default-toolchain="${RUST_DEFAULT_VERSION}" -y
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

clean_pkg_local_osx_sysroot() {
    local build_depends_dir="${BUILD_DEPENDS_DIR}"
    _safe_rm_rf "$build_depends_dir/SDKs"
}

pkg_local_ensure_py_deps() {
    local python_venv="${PYTHON_VENV_DIR}"
    if [[ -d "${python_venv}" ]]; then
        return
    fi
    pkg_local_install_py_deps
}

pkg_local_install_py_deps() {
    _fold_start "pkg-install-py-deps"
    py_env_activate

    # lints, fmt, checks deps
    python3 -m pip install black shellcheck-py codespell==2.2.4 flake8==6.0.0 vulture==2.7

    # test deps
    python3 -m pip install py-solc-x eth_typing==4.0.0 eth_account==0.11.2 web3
    python3 -c 'from solcx import install_solc;install_solc("0.8.20")'

    py_env_deactivate
    _fold_end
}

clean_pkg_local_py_deps() {
  local python_venv="${PYTHON_VENV_DIR}"
  _safe_rm_rf "${python_venv}"
}

pkg_user_setup_rust() {
    local rust_target
    # shellcheck disable=SC2119
    rust_target=$(get_rust_triplet)
    rustup target add "${rust_target}"
}

# Clean
# ---

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
# Support methods
# ========

# Defaults
# ---

get_default_target() {
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

get_default_docker_file() {
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

get_default_conf_args() {
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

get_default_jobs() {
    local total
    total=$(_nproc)
    # Use a num closer to 80% of the cores by default
    local jobs=$(( total * 4/5 ))
    if (( jobs > 1 )); then
        echo $jobs
    else
        echo 1
    fi
}

get_default_use_clang() {
    local target=${TARGET}
    local cc=${CC:-}
    local cxx=${CXX:-}
    if [[ -z "${cc}" && -z "${cxx}" ]]; then
        if [[ "${target}" == "x86_64-pc-linux-gnu" ]]; then
            echo 1
            return
        fi
    fi
    echo 0
    return
}

get_default_ci_group_logs() {
    if [[ -n "${GITHUB_ACTIONS-}" ]]; then
        echo 1
    else
        echo 0
    fi
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


# Platform helpers
# ---

_bash_version_check() {
    _bash_ver_err_exit() {
        echo "Bash version 5+ required."; exit 1;
    }
    [ -z "$BASH_VERSION" ] && _bash_ver_err_exit
    case $BASH_VERSION in
        5.*) return 0;;
        *) _bash_ver_err_exit;;
    esac
}

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

_platform_init_intercept_build() {
    _INTERCEPT_BUILD_CMD=""
    local intercept_build_cmds=("intercept-build-15" "intercept-build")
    local x
    for x in "${intercept_build_cmds[@]}"; do
        if command -v "$x" >/dev/null ; then
            _INTERCEPT_BUILD_CMD="$x"
            _intercept_build() {
                "$_INTERCEPT_BUILD_CMD" "$@"
            }
            return
        fi
    done

    echo "error: intercept-build-15/intercept-build required"
    _platform_pkg_tip clang-tools
    exit 1
}

_nproc() {
    if [[ "${OSTYPE}" == "darwin"* ]]; then
        sysctl -n hw.logicalcpu
    else
        nproc
    fi
}

# CI
# ---

# shellcheck disable=SC2129
ci_export_vars() {
    local build_dir="${BUILD_DIR}"

    if [[ -n "${GITHUB_ACTIONS-}" ]]; then
        # GitHub Actions
        echo "BUILD_VERSION=${IMAGE_VERSION}" >> "$GITHUB_ENV"
        echo "PATH=$HOME/.cargo/bin:$PATH" >> "$GITHUB_ENV"
        echo "CARGO_INCREMENTAL=0" >> "$GITHUB_ENV"

        if [[ "${MAKE_DEBUG}" == "1" ]]; then
            echo "BUILD_TYPE=debug" >> "$GITHUB_ENV"
        else
            echo "BUILD_TYPE=release" >> "$GITHUB_ENV"
        fi

        if [[ "${TARGET}" == "x86_64-w64-mingw32" ]]; then
            echo "PKG_TYPE=zip" >> "$GITHUB_ENV"
        else
            echo "PKG_TYPE=tar.gz" >> "$GITHUB_ENV"
        fi

        if [[ "${TARGET}" =~ .*darwin.* ]]; then
            echo "MACOSX_DEPLOYMENT_TARGET=${MACOSX_DEPLOYMENT_TARGET:-10.15}" >> "$GITHUB_ENV"
        fi

        echo "RUST_DEFAULT_VERSION=1.76" >> "$GITHUB_ENV"
    fi
}

ci_setup_deps() {
    DEBIAN_FRONTEND=noninteractive pkg_update_base
    DEBIAN_FRONTEND=noninteractive pkg_install_deps
    DEBIAN_FRONTEND=noninteractive pkg_setup_locale
    DEBIAN_FRONTEND=noninteractive pkg_install_llvm
    DEBIAN_FRONTEND=noninteractive pkg_install_gh_cli
    ci_setup_deps_target
}

ci_setup_deps_target() {
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

ci_setup_user_deps() {
    pkg_user_install_rust
    pkg_user_setup_rust
}

# Public helpers
# ---

lib() {
    local cmd="${1-}"
    local exit_on_err="${2:-0}"
    local jobs="$MAKE_JOBS"

    check_enter_build_rs_dir
    # shellcheck disable=SC2086
    make JOBS=${jobs} ${cmd} || { if [[ "${exit_on_err}" == "1" ]]; then
        echo "Error: Please resolve all checks";
        exit 1;
        fi; }
    _exit_dir
}

rust_analyzer_check() {
    lib "check CARGO_EXTRA_ARGS=--all-targets --workspace --message-format=json"
}

compiledb() {
    _platform_init_intercept_build
    clean 2> /dev/null || true
    build_deps
    build_conf
    _intercept_build ./make.sh build_make
}

# shellcheck disable=SC2120
get_rust_triplet() {
    # Note: https://github.com/llvm/llvm-project/blob/master/llvm/lib/TargetParser/Triple.cpp
    # The function is called in 2 places:
    # 1. When setting up Rust, which TARGET is passed from the environment
    # 2. In configure, which sets TARGET with the additional unknown vendor part in the triplet
    # Thus, we normalize across both to source the correct rust target.
    local triplet=${1:-${TARGET}}
    local result
    case $triplet in
        x86_64-pc-linux-gnu) result=x86_64-unknown-linux-gnu;;
        aarch64-linux-gnu|aarch64-unknown-linux-gnu) result=aarch64-unknown-linux-gnu;;
        arm-linux-gnueabihf|arm-unknown-linux-gnueabihf) result=armv7-unknown-linux-gnueabihf;;
        x86_64-apple-darwin*) result=x86_64-apple-darwin;;
        aarch64-apple-darwin*) result=aarch64-apple-darwin;;
        x86_64-w64-mingw32) result=x86_64-pc-windows-gnu;;
        *) echo "error: unsupported triplet: ${triplet}"; exit 1;;
    esac
    echo "$result"
}

_sign() {
    # TODO: generate sha sums and sign
    :
}

# Internal misc helpers
# ---

_exec_black() {
    local is_check=${1:-0}
    local black_args=""
    if [[ "${is_check}" == "1" ]]; then
        black_args="${black_args} --check"
    fi
    # shellcheck disable=SC2086,SC2090
    python3 -m black --extend-exclude 'src/crc32c' ${black_args} .
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
    if [[ "${CI_GROUP_LOGS}" == "1" ]]; then
        echo "::group::${*:-}";
    fi
}

_fold_end() {
    if [[ "${CI_GROUP_LOGS}" == "1" ]]; then
        echo "::endgroup::"
    fi
}

_ensure_enter_dir() {
    local dir="${1?dir required}"
    mkdir -p "${dir}" && pushd "${dir}" > /dev/null
}

_exit_dir() {
    popd > /dev/null
}

main "$@"
