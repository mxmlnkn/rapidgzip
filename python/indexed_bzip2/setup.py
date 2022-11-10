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


extensions = [
    Extension(
        # fmt: off
        name         = 'indexed_bzip2',
        sources      = ['indexed_bzip2.pyx'],
        include_dirs = ['.', 'core', 'indexed_bzip2', 'external/cxxopts/include'],
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


# https://github.com/cython/cython/blob/master/docs/src/tutorial/appendix.rst#python-38
class Build(build_ext):
    def build_extensions(self):
        for ext in self.extensions:
            ext.extra_compile_args = ['-std=c++17', '-O3', '-DNDEBUG', '-DWITH_PYTHON_SUPPORT']

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
                    '/constexpr:steps99000100',
                ]
            else:
                # The default limit is ~33 M (1<<25) and 99 M seem to be enough to compile currently on GCC 11.
                if supportsFlag(self.compiler, '-fconstexpr-ops-limit=99000100'):
                    ext.extra_compile_args += ['-fconstexpr-ops-limit=99000100']
                elif supportsFlag(self.compiler, '-fconstexpr-steps=99000100'):
                    ext.extra_compile_args += ['-fconstexpr-steps=99000100']

        super(Build, self).build_extensions()


setup(
    # fmt: off
    py_modules  = ['indexed_bzip2'],
    ext_modules = extensions,
    cmdclass    = {'build_ext': Build},
    # fmt: on
)
