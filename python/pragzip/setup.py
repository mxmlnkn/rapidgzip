#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import platform
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


zlib_sources = [ 'inflate.c', 'crc32.c', 'adler32.c', 'inftrees.c', 'inffast.c', 'zutil.c' ]
zlib_sources = [ 'external/zlib/' + source for source in zlib_sources ]

extensions = [
    Extension(
        name         = 'pragzip',
        sources      = [ 'pragzip.pyx' ] + zlib_sources,
        include_dirs = [ '.', 'core', 'pragzip', 'pragzip/huffman', 'external/zlib', 'external/cxxopts/include' ],
        language     = 'c++',
    ),
]

if cythonize:
    extensions = cythonize( extensions, compiler_directives = { 'language_level' : '3' } )


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
            print(f"[Info] Check for {systemInclude} system header failed. Will try without out it. "
                  "The above error can be ignored!")
            return False
    return True


# https://github.com/cython/cython/blob/master/docs/src/tutorial/appendix.rst#python-38
class Build(build_ext):
    def build_extensions(self):
        # This is as hacky as it gets just in order to have different compile arguments for the zlib C-code as
        # opposed to the C++ code but I don't see another way with this subpar "build system" if you can call
        # it even that.
        oldCompile = self.compiler.compile
        def newCompile(sources, *args, **kwargs):
            cSources = [source for source in sources if source.endswith('.c')]
            cppSources = [source for source in sources if not source.endswith('.c')]

            objects = oldCompile(cppSources, *args, **kwargs)
            cppCompileArgs = ['-fconstexpr-ops-limit=99000100', '-fconstexpr-steps=99000100',
                              '-std=c++17', '/std:c++17']
            if 'extra_postargs' in kwargs:
                kwargs['extra_postargs'] = [x for x in kwargs['extra_postargs'] if x not in cppCompileArgs]
            cppObjects = oldCompile(cSources, *args, **kwargs)
            objects.extend(cppObjects)
            return objects

        self.compiler.compile = newCompile

        for ext in self.extensions:
            ext.extra_compile_args = [ '-std=c++17', '-O3', '-DNDEBUG', '-DWITH_PYTHON_SUPPORT',
                                       '-D_LARGEFILE64_SOURCE=1' ]

            # https://github.com/cython/cython/issues/2670#issuecomment-432212671
            # https://github.com/cython/cython/issues/3405#issuecomment-596975159
            # https://bugs.python.org/issue35037
            # https://bugs.python.org/issue4709
            if platform.system() == 'Windows' and platform.machine().endswith( '64' ):
                ext.extra_compile_args += [ '-DMS_WIN64' ]

            if self.compiler.compiler_type == 'mingw32':
                ext.extra_link_args = [
                    '-static-libgcc',
                    '-static-libstdc++',
                    '-Wl,-Bstatic,--whole-archive',
                    '-lwinpthread',
                    '-Wl,--no-whole-archive'
                ]

            elif self.compiler.compiler_type == 'msvc':
                ext.extra_compile_args = [ '/std:c++17', '/O2', '/DNDEBUG', '/DWITH_PYTHON_SUPPORT',
                                           '/constexpr:steps99000100' ]
            else:
                # The default limit is ~33 M (1<<25) and 99 M seem to be enough to compile currently on GCC 11.
                if supportsFlag(self.compiler, '-fconstexpr-ops-limit=99000100'):
                    ext.extra_compile_args += [ '-fconstexpr-ops-limit=99000100' ]
                elif supportsFlag(self.compiler, '-fconstexpr-steps=99000100'):
                    ext.extra_compile_args += [ '-fconstexpr-steps=99000100' ]

            if hasInclude(self.compiler, 'unistd.h'):
                ext.extra_compile_args += ['-DZ_HAVE_UNISTD_H']

        super(Build, self).build_extensions()


setup(
    ext_modules = extensions,
    cmdclass    = { 'build_ext': Build },
)
