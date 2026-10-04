#!/bin/bash

skip_install=false
if [[ "$1" == "--skip-install" ]]; then
  echo "1: $1"
  skip_install=true
fi

if [[ $skip_install == false ]]; then
  sudo apt update -yqq
  sudo apt install -yqq mesa-common-dev libwayland-dev libxkbcommon-dev wayland-protocols extra-cmake-modules xorg-dev
fi

sudo update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-15 10
sudo update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-15 10

sudo update-alternatives --remove-all clang
sudo update-alternatives --remove-all clang++
sudo update-alternatives --remove-all clang-format
sudo update-alternatives --install /usr/bin/clang clang /usr/bin/clang-22 10
sudo update-alternatives --install /usr/bin/clang++ clang++ /usr/bin/clang++-22 10
sudo update-alternatives --install /usr/bin/clang-format clang-format /usr/bin/clang-format-22 10
