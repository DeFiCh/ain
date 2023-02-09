#! /bin/bash

export LC_ALL=C

TARGET=$1

case ${TARGET} in

  "x86_64-pc-linux-gnu")
    echo "x86_64-unknown-linux-gnu"
    ;;

  "arm-linux-gnueabihf")
    echo "arm-unknown-linux-gnueabihf"
    ;;

  "x86_64-w64-mingw32")
    echo "x86_64-pc-windows-gnu"
    ;;

  "x86_64-apple-darwin18")
    arch=$(uname -m)
    if [[ "${arch}" == "arm64" ]]; then
      echo "aarch64-apple-darwin"
    else
      echo "x86_64-apple-darwin18"
    fi
    ;;
  *)
    echo "${TARGET}"
    ;;

esac
