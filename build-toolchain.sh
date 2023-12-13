# running this downloads and extracts binutils and gcc,
# builds them and sets according environment variables

# Dependencies:
# wget (apt install wget / pacman -S wget)
# gcc for building gcc

mkdir -p toolchain/src
cd toolchain/src

wget https://ftp.gnu.org/gnu/binutils/binutils-latest.tar.gz
tar -xf binutils-latest.tar.gz

wget https://ftp.gnu.org/gnu/gcc/gcc-latest.tar.gz
tar -xf gcc-latest.tar.gz
