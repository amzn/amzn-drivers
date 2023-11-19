#!/usr/bin/env bash

usage() {
    echo "$0 -- apply ENA PMD backports for the given DPDK repository"
    echo
    echo "usage: $0 [-h] [-d dpdk_src] [-p patches_dir]"
    echo
    echo "  -h print this message."
    echo "  -d path to the DPDK source code. If not provided, the RTE_SDK environmental"
    echo "     variable will be used instead."
    echo "  -p path to the directory with ENA patches. If not provided, the directory"
    echo "     will be choosen automatically."
    echo
}

parse_args() {
    while getopts :hd:p: opt; do
        case $opt in
            h)
                usage
                exit 0
                ;;
            d) DPDK_SRC_PATH="${OPTARG}" ;;
            p) ENA_BACKPORTS_PATH=$(realpath ${OPTARG}) ;;
            ?)
            echo "error: Wrong parameters provided."
            echo
            usage
            exit 1
            ;;
        esac
    done

    if [ -z "${DPDK_SRC_PATH}" ]; then
        echo "error: path to the DPDK source directory wasn't provided"
        echo
        usage
        exit 1
    fi
}

get_dpdk_version() {
    local version=$(cat "${DPDK_SRC_PATH}/VERSION" 2> /dev/null)
    if [ -z "${version}" ]; then
        # VERSION file was added in the later DPDK releases, so as a fallback
        # behavior let's check for the version in the pkg/dpdk.spec file.
        version=$(grep 'Version' "${DPDK_SRC_PATH}/pkg/dpdk.spec" | cut -f 2 -d ' ')
        if [ -z "${version}" ]; then
            echo "error: invalid DPDK source repository - cannot determine DPDK version."
            exit 1
        fi
    fi

    echo "${version}"
}

DPDK_SRC_PATH="${RTE_SDK}"

parse_args $@

if [ -z "${ENA_BACKPORTS_PATH}" ]; then
    VERSION=$(get_dpdk_version)
    ENA_BACKPORTS_PATH="$(dirname "${0}")/v${VERSION}"
    echo $ENA_BACKPORTS_PATH
    if ! test -d "${ENA_BACKPORTS_PATH}"; then
        echo "error: unsupported DPDK version: ${VERSION}"
	echo
        echo "  Please use supported DPDK version or specify backport folder directly"
	echo "  using '-s patch_folder' parameter."
        exit 1
    fi
fi

git -C "${DPDK_SRC_PATH}" am $(realpath "${ENA_BACKPORTS_PATH}")/*.patch
