#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import copy
import os
import platform
import shutil
import sys
import tempfile
from distutils.errors import CompileError

from setuptools import setup
from setuptools.extension import Extension
from setuptools.command.build_ext import build_ext

# This fallback is only for jinja, which is used by conda to analyze this setup.py before any build environment
# is set up.
try:
    from Cython.Build import cythonize
except ImportError:
    cythonize = None

# ISA-l does not compile on 32-bit becaue it contains statements such as [bits 64].
# It simply is not supported and I also don't see a reason. 32-bit should be long dead exactly
# like almost all (96% according to Steam) PCs have AVX support.
withIsal = shutil.which("nasm") is not None and platform.machine().endswith('64')

zlib_sources = ['inflate.c', 'crc32.c', 'adler32.c', 'inftrees.c', 'inffast.c', 'zutil.c']
zlib_sources = ['external/zlib/' + source for source in zlib_sources]

isal_sources = [
    #"include/igzip_lib.h",
    #"include/unaligned.h",
    "include/reg_sizes.asm",
    "include/multibinary.asm",
    "igzip/igzip_inflate.c",
    "igzip/igzip.c",
    "igzip/hufftables_c.c",
    #"igzip/igzip_checksums.h",
    "igzip/igzip_inflate_multibinary.asm",
    "igzip/igzip_decode_block_stateless_01.asm",
    "igzip/igzip_decode_block_stateless_04.asm",
    "igzip/rfc1951_lookup.asm",
    #"igzip/igzip_wrapper.h",
    #"igzip/static_inflate.h",
    "igzip/stdmac.asm",
]
isal_sources = ['external/isa-l/' + source for source in isal_sources] if withIsal else []

include_dirs = ['.', 'core', 'rapidgzip', 'rapidgzip/huffman', 'external/zlib', 'external/cxxopts/include',
                'external/rpmalloc/rpmalloc']
isal_includes = ['external/isa-l/include', 'external/isa-l/igzip']
if withIsal:
    include_dirs += isal_includes

extensions = [
    Extension(
        # fmt: off
        name         = 'rapidgzip',
        sources      = ['rapidgzip.pyx'] + zlib_sources + isal_sources + ['external/rpmalloc/rpmalloc/rpmalloc.c'],
        include_dirs = include_dirs,
        language     = 'c++',
        # fmt: on
    ),
]

if cythonize:
    extensions = cythonize(extensions, compiler_directives={'language_level': '3'})


def supportsFlag(compiler, flag):
    with tempfile.NamedTemporaryFile('w', suffix='.cpp') as file:
        file.write('int main() { return 0; }')
        try:
            compiler.compile([file.name], extra_postargs=[flag])
        except CompileError:
            print("[Info] Compiling with argument failed. Will try another one. The above error can be ignored!")
            return False
    return True


def hasInclude(compiler, systemInclude):
    with tempfile.NamedTemporaryFile('w', suffix='.cpp') as file:
        file.write(f'#include <{systemInclude}>\n' + 'int main() { return 0; }')
        try:
            compiler.compile([file.name])
        except CompileError:
            print(
                f"[Info] Check for {systemInclude} system header failed. Will try without out it. "
                "The above error can be ignored!"
            )
            return False
    return True


# https://github.com/cython/cython/blob/master/docs/src/tutorial/appendix.rst#python-38
class Build(build_ext):
    def build_extensions(self):
        # This is as hacky as it gets just in order to have different compile arguments for the zlib C-code as
        # opposed to the C++ code but I don't see another way with this subpar "build system" if you can call
        # it even that.
        oldCompile = self.compiler.compile

        if not withIsal:
            nasmCompiler = None
        elif sys.platform == "win32":
            from nasm_extension.winnasmcompiler import WinNasmCompiler
            nasmCompiler = WinNasmCompiler(verbose=True)
        else:
            from nasm_extension.nasmcompiler import NasmCompiler
            nasmCompiler = NasmCompiler(verbose=True)

        def newCompile(sources, *args, **kwargs):
            cSources = [source for source in sources if source.endswith('.c')]
            asmSources = [source for source in sources if source.endswith('.asm')]
            cppSources = [source for source in sources if not source.endswith('.c') and not source.endswith('.asm')]

            objects = []
            if asmSources and nasmCompiler:
                nasm_kwargs = copy.deepcopy(kwargs)
                nasm_kwargs['extra_postargs'] = []
                nasm_kwargs['include_dirs'] = isal_includes

                # One of the crudest hacks ever. But for some reason, I get:
                # fatal: unable to open include file `reg_sizes.asm'
                # But this only happens with the manylinux2014 image. Building with cibuildwheel and manylinux2_28
                # works perfectly fine without this hack. Maybe it is a problem with 2.10.07-7.el7 (manylinux2014),
                # which has been fixed in 2.15.03-3.el8 (manylinux_2_28).
                # @see https://www.nasm.us/xdoc/2.16.01/html/nasmdocc.html#section-C.1.32
                # The only mention of -I, which I could find, is in 2.14:
                # > Changed -I option semantics by adding a trailing path separator unconditionally.
                # It even fails to include .asm files in the same directory as the .asm file to be compiled.
                #   nasm -I. -Iexternal/isa-l/include -Iexternal/isa-l/igzip -f elf64 \
                #       external/isa-l/igzip/igzip_decode_block_stateless_01.asm -o \
                #       build/temp.linux-x86_64-3.6/external/isa-l/igzip/igzip_decode_block_stateless_01.obj
                # external/isa-l/igzip/igzip_decode_block_stateless_01.asm:3: fatal: unable to open include file
                #   `igzip_decode_block_stateless.asm'
                # error: command 'nasm' failed with exit status 1
                # I even tried chaning -I<dir> to -I <dir> by overwriting nasmcompiler._setup_compile but to no avail.
                for path in isal_includes:
                    for fileName in os.listdir(path):
                        if fileName.endswith('.asm'):
                            shutil.copy(os.path.join(path, fileName), ".")
                objects.extend(nasmCompiler.compile(asmSources, *args, **nasm_kwargs))

            if cppSources:
                objects.extend(oldCompile(cppSources, *args, **kwargs))

            if cSources:
                cppCompileArgs = [
                    '-fconstexpr-ops-limit=99000100',
                    '-fconstexpr-steps=99000100',
                    '-std=c++17',
                    '/std:c++17',
                ]
                if 'extra_postargs' in kwargs:
                    kwargs['extra_postargs'] = [x for x in kwargs['extra_postargs'] if x not in cppCompileArgs]
                objects.extend(oldCompile(cSources, *args, **kwargs))

            return objects

        self.compiler.compile = newCompile

        for ext in self.extensions:
            ext.extra_compile_args = [
                '-std=c++17',
                '-O3',
                '-DNDEBUG',
                '-DWITH_PYTHON_SUPPORT',
                '-DWITH_RPMALLOC',
                '-D_LARGEFILE64_SOURCE=1',
            ]
            if nasmCompiler:
                ext.extra_compile_args.append('-DWITH_ISAL')

            # https://github.com/cython/cython/issues/2670#issuecomment-432212671
            # https://github.com/cython/cython/issues/3405#issuecomment-596975159
            # https://bugs.python.org/issue35037
            # https://bugs.python.org/issue4709
            if platform.system() == 'Windows' and platform.machine().endswith('64'):
                ext.extra_compile_args += ['-DMS_WIN64']

            if self.compiler.compiler_type == 'mingw32':
                ext.extra_link_args = [
                    '-static-libgcc',
                    '-static-libstdc++',
                    '-Wl,-Bstatic,--whole-archive',
                    '-lwinpthread',
                    '-Wl,--no-whole-archive',
                ]

            elif self.compiler.compiler_type == 'msvc':
                ext.extra_compile_args = [
                    '/std:c++17',
                    '/O2',
                    '/DNDEBUG',
                    '/DWITH_PYTHON_SUPPORT',
                    '/DWITH_RPMALLOC',
                    '/constexpr:steps99000100',
                ]
                if nasmCompiler:
                    ext.extra_compile_args.append('/DWITH_ISAL')
                # This list is from rpmalloc/build/ninja/msvc.py
                ext.libraries = ['kernel32', 'user32', 'shell32', 'advapi32']
            else:
                # The default limit is ~33 M (1<<25) and 99 M seem to be enough to compile currently on GCC 11.
                if supportsFlag(self.compiler, '-fconstexpr-ops-limit=99000100'):
                    ext.extra_compile_args += ['-fconstexpr-ops-limit=99000100']
                elif supportsFlag(self.compiler, '-fconstexpr-steps=99000100'):
                    ext.extra_compile_args += ['-fconstexpr-steps=99000100']

                if sys.platform.startswith('darwin') and supportsFlag(self.compiler, '-mmacosx-version-min=10.14'):
                    ext.extra_compile_args += ['-mmacosx-version-min=10.14']
                    ext.extra_link_args += ['-mmacosx-version-min=10.14']

            if hasInclude(self.compiler, 'unistd.h'):
                ext.extra_compile_args += ['-DZ_HAVE_UNISTD_H']

        super(Build, self).build_extensions()


setup(
    # fmt: off
    ext_modules = extensions,
    cmdclass    = {'build_ext': Build},
    # fmt: on
)
