#!/bin/sh
# Run on a native Ubuntu amd64/arm64 runner with musl-tools and ncurses-bin.
set -eu
root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
curl -fsSL --proto '=https' https://ftp.gnu.org/gnu/ncurses/ncurses-6.5.tar.gz -o "$work/ncurses.tar.gz"
printf '%s  %s\n' 136d91bc269a9a5785e5f9e980bc76ab57428f604ce3e5a5a90cebc767971cc6 "$work/ncurses.tar.gz" | sha256sum -c -
tar -xzf "$work/ncurses.tar.gz" -C "$work"
cd "$work/ncurses-6.5"
CC=musl-gcc ./configure --prefix="$work/local" --enable-widec --without-shared \
    --without-debug --without-progs --without-tests --without-manpages \
    --without-cxx --without-cxx-binding --without-ada --disable-db-install \
    --with-fallbacks=xterm,xterm-256color,screen,screen-256color,tmux,tmux-256color,linux,vt100,ansi \
    --with-tic-path=/usr/bin/tic --with-infocmp-path=/usr/bin/infocmp
make -j2
make install
cd "$root"
make clean
make CC=musl-gcc CPPFLAGS="-I$work/local/include/ncursesw" \
    CFLAGS='-O2 -std=c11 -Wall -Wextra -Wpedantic -Werror' \
    LDFLAGS="-static -L$work/local/lib" LDLIBS='-lncursesw -lpthread -lm'
mkdir -p dist-notices
cp "$work/ncurses-6.5/COPYING" dist-notices/ncurses-LICENSE
cp /usr/share/doc/musl/copyright dist-notices/musl-COPYRIGHT
file viewbernetes
file viewbernetes | grep 'statically linked'
