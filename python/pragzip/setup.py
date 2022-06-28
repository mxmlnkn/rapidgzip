#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import os
import platform
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


extensions = [
    Extension(
        name         = 'pragzip',
        sources      = [ 'pragzip.pyx' ],
        include_dirs = [ 'core', 'pragzip', 'pragzip/huffman' ],
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


# https://github.com/cython/cython/blob/master/docs/src/tutorial/appendix.rst#python-38
class Build(build_ext):
    def build_extensions(self):
        for ext in self.extensions:
            ext.extra_compile_args = [ '-std=c++17', '-O3', '-DNDEBUG', '-DWITH_PYTHON_SUPPORT' ]

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

        super(Build, self).build_extensions()


scriptPath = os.path.abspath( os.path.dirname( __file__ ) )
with open( os.path.join( scriptPath, 'README.md' ), encoding = 'utf-8' ) as file:
    readmeContents = file.read()


setup(
    name             = 'pragzip',
    version          = '0.1.0',

    description      = 'Parallel random access to gzip files',
    url              = 'https://github.com/mxmlnkn/indexed_bzip2',
    author           = 'Maximilian Knespel',
    author_email     = 'mxmlnkn@github.de',
    license          = 'MIT',
    classifiers      = [ 'License :: OSI Approved :: MIT License',
                         'Development Status :: 3 - Alpha',
                         'Natural Language :: English',
                         'Operating System :: MacOS',
                         'Operating System :: POSIX',
                         'Operating System :: Unix',
                         'Operating System :: Microsoft :: Windows',
                         'Programming Language :: Python :: 3',
                         'Programming Language :: Python :: 3.6',
                         'Programming Language :: Python :: 3.7',
                         'Programming Language :: Python :: 3.8',
                         'Programming Language :: Python :: 3.9',
                         'Programming Language :: Python :: 3.10',
                         'Programming Language :: C++',
                         'Topic :: Software Development :: Libraries',
                         'Topic :: Software Development :: Libraries :: Python Modules',
                         'Topic :: System :: Archiving',
                         'Topic :: System :: Archiving :: Compression'
                       ],

    long_description = readmeContents,
    long_description_content_type = 'text/markdown',

    py_modules       = [ 'pragzip' ],
    ext_modules      = extensions,
    cmdclass         = { 'build_ext': Build },
)
