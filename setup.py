#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import os
import platform
import sys

from setuptools import setup
from setuptools.extension import Extension
from setuptools.command.build_ext import build_ext


buildCython = '--cython' in sys.argv


extensions = [
    Extension(
        name               = 'indexed_bzip2',
        sources            = [ 'indexed_bzip2/indexed_bzip2.pyx' if buildCython
                               else 'indexed_bzip2/indexed_bzip2.cpp' ],
        depends            = [] if buildCython else \
                             [ 'indexed_bzip2/BitReader.hpp',
                               'indexed_bzip2/BitStringFinder.hpp',
                               'indexed_bzip2/BZ2Reader.hpp',
                               'indexed_bzip2/BZ2ReaderInterface.hpp',
                               'indexed_bzip2/bzip2.hpp',
                               'indexed_bzip2/Cache.hpp',
                               'indexed_bzip2/common.hpp',
                               'indexed_bzip2/FileReader.hpp',
                               'indexed_bzip2/JoiningThread.hpp',
                               'indexed_bzip2/ParallelBitStringFinder.hpp',
                               'indexed_bzip2/ParallelBZ2Reader.hpp',
                               'indexed_bzip2/Prefetcher.hpp',
                               'indexed_bzip2/ThreadPool.hpp'
                             ],
        include_dirs       = [ '.' ],
        language           = 'c++',
    ),
]


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
                ext.extra_compile_args = [ '/std:c++17', '/O2', '/DNDEBUG', '/DWITH_PYTHON_SUPPORT' ]

        super(Build, self).build_extensions()


if buildCython:
    from Cython.Build import cythonize
    extensions = cythonize( extensions, compiler_directives = { 'language_level' : '3' } )
    del sys.argv[sys.argv.index( '--cython' )]


scriptPath = os.path.abspath( os.path.dirname( __file__ ) )
with open( os.path.join( scriptPath, 'README.md' ), encoding = 'utf-8' ) as file:
    readmeContents = file.read()


setup(
    name             = 'indexed_bzip2',
    version          = '1.3.1',

    description      = 'Fast random access to bzip2 files',
    url              = 'https://github.com/mxmlnkn/indexed_bzip2',
    author           = 'Maximilian Knespel',
    author_email     = 'mxmlnkn@github.de',
    license          = 'MIT',
    classifiers      = [ 'License :: OSI Approved :: MIT License',
                         'Development Status :: 4 - Beta',
                         'Intended Audience :: Developers',
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
                         'Programming Language :: C++',
                         'Topic :: Software Development :: Libraries',
                         'Topic :: Software Development :: Libraries :: Python Modules',
                         'Topic :: System :: Archiving' ],

    long_description = readmeContents,
    long_description_content_type = 'text/markdown',

    py_modules       = [ 'indexed_bzip2' ],
    ext_modules      = extensions,
    cmdclass         = { 'build_ext': Build },
)
