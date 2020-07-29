#!/usr/bin/env bash

export LC_ALL=C
TOPDIR=${TOPDIR:-$(git rev-parse --show-toplevel)}
BUILDDIR=${BUILDDIR:-$TOPDIR}

BINDIR=${BINDIR:-$BUILDDIR/src}
MANDIR=${MANDIR:-$TOPDIR/doc/man}

DEFID=${DEFID:-$BINDIR/defid}
DEFICLI=${DEFICLI:-$BINDIR/defi-cli}
DEFITX=${DEFITX:-$BINDIR/defi-tx}
WALLET_TOOL=${WALLET_TOOL:-$BINDIR/defi-wallet}
DEFIQT=${DEFIQT:-$BINDIR/qt/defi-qt}

[ ! -x $DEFID ] && echo "$DEFID not found or not executable." && exit 1

# The autodetected version git tag can screw up manpage output a little bit
read -r -a BTCVER <<< "$($DEFICLI --version | head -n1 | awk -F'[ -]' '{ print $6, $7 }')"

# Create a footer file with copyright content.
# This gets autodetected fine for defid if --version-string is not set,
# but has different outcomes for defi-qt and defi-cli.
echo "[COPYRIGHT]" > footer.h2m
$DEFID --version | sed -n '1!p' >> footer.h2m

for cmd in $DEFID $DEFICLI $DEFITX $WALLET_TOOL $DEFIQT; do
  cmdname="${cmd##*/}"
  help2man -N --version-string=${BTCVER[0]} --include=footer.h2m -o ${MANDIR}/${cmdname}.1 ${cmd}
  sed -i "s/\\\-${BTCVER[1]}//g" ${MANDIR}/${cmdname}.1
done

rm -f footer.h2m
