# How to build GCC for cross-compile, on Mac OS X 

# my version: 10.9 DP7, Xcode5 DP (x86_64-apple-darwin13.0.0)
# Here `cross-compile` means we are buiding a GCC that runs on
# x86_64, in Mach-O format, but produces ELF outputs for i386.

# use LLVM C Compiler that came with OS X 
export "CC=clang"
# or use an real GCC
export "CC=x86_64-apple-darwin13.0.0-gcc"

# stop clang from turning warnings into erros, or use -Wno-unused-value
export "CFLAGS=-g -O2 -Wno-error"

# build binutils FIRST
cd binutils-2.23.2/
./configure --target=i386-jos-elf --disable-nls --prefix=/usr/local/OS
make -j4
sudo make install
# now we have i386-jos-elf-(as|ld|ar|objdump...) in /usr/local/OS/bin
export "PATH=$PATH:/usr/local/OS/bin"

# IMPORTANT: get out of gcc root directory before building.
# and, use gcc-core-4.6.4.tar.bz2 for time and space.
cd gcc-4.6.4/..
mkdir gcc-build
cd gcc-build/
# Our strategy is to disable as much features as we can. Remeber we are
# building a very simple OS.
../gcc-4.6.4/configure --target=i386-jos-elf --prefix=/usr/local/OS \
	--disable-nls --without-headers --with-newlib --disable-threads \
	--disable-shared --without-mpfr --disable-libmudflap --disable-libssp \
    --without-gmp --without-mpc --disable-libquadmath --enable-languages=c

make -j4
sudo make install

# And GDB for cross-debug
mkdir gdb-build
cd gdb-build/
../gdb-6.8/configure --target=i386-jos-elf --prefix=/usr/local/OS \
	--program-prefix=i386-jos-elf- --disable-werror --without-mpfr \
	--disable-libssp --without-gmp --without-mpc
make -j4
sudo make install

# install dependencies before qemu

# zlib, glib(which requires xz, libffi)
# using homebrew is a nice option, but there is a stupid bug in brew's
# formula for libffi - it downloads, compiles, then get stuck forever.
# so install libffi manually, inside brew's Cellar directory (this
# is required, otherwise brew does not recognize).

# Next, download QEMU from MIT (already patched for better debugging)
git clone http://pdos.csail.mit.edu/6.828/qemu.git -b 6.828-0.15
cd qemu
# add -F option pointing to your OSX SDK framworks, after clang
export "CC=clang -F/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX10.7.sdk/System/Library/Frameworks/"
# it is annoying that qemu's Makefile can't find required frameworks.
export "PATH=$PATH:/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX10.7.sdk/System/Library/Frameworks/"
./configure --prefix=/usr/local/OS --target-list="i386-softmmu" \
	--disable-kvm --disable-sdl
make -j4
# Hopefully you will see a nasty compiler error saying something about 
#`NSObject` being defined twice. Once in /usr/objc/include/NSObject.h, 
# another time in Foundation.framework/Headers
# You will need to modify one of the two NSObject.h, so there is only one
# definition of `@interface NSObject ...' then continue with you build.
# Amazing, I'm modifying OS X SDK! .....
make -j4
# Hopefully you will see clang refusing to make a register variable global
# in qemu/target-i386/exec.h `register struct CPUX86State *env asm(AREG0);'
# I don't want to dig deeper into global register variables, not the right way
# of doing things.

# So, now it's time to switch to the humble GCC that accepts many unexplained behaviours.
export "CC=x86_64-apple-darwin13.0.0-gcc \
-F/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX10.7.sdk/System/Library/Frameworks/"
# Follow my instructions: configure again, without `make distclean', 
# so objects compiled by clang are kept inplace. (GCC is not good at locating
# OS X frameworks)
./configure --prefix=/usr/local/OS --target-list="i386-softmmu" \
	--disable-kvm --disable-sdl
make -j4
make install

# Wow, finally
# get into jos directory
cd jos
make qemu-gdb
# then start gdb in the same directory (new terminal tab Cmd+T)
# it will attach to the running qemu
i386-jos-elf-gdb
# This time, hopefully you can start you journey.


# you might want Q app which is a OS X port of QEMU from
# http://www.kju-app.org/
# but I can't attach a debugger. Nice GUI though.



