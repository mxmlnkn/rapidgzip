#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import os
import sys
from setuptools import setup
from setuptools.extension import Extension

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
        extra_compile_args = [ '-std=c++17', '-O3', '-DNDEBUG' ],
    ),
]

if buildCython:
    from Cython.Build import cythonize
    extensions = cythonize( extensions, compiler_directives = { 'language_level' : '3' } )
    del sys.argv[sys.argv.index( '--cython' )]

scriptPath = os.path.abspath( os.path.dirname( __file__ ) )
with open( os.path.join( scriptPath, 'README.md' ), encoding = 'utf-8' ) as file:
    readmeContents = file.read()

setup(
    name             = 'indexed_bzip2',
    version          = '1.2.0',

    description      = 'Fast random access to bzip2 files',
    url              = 'https://github.com/mxmlnkn/indexed_bzip2',
    author           = 'Maximilian Knespel',
    author_email     = 'mxmlnkn@github.de',
    license          = 'MIT',
    classifiers      = [ 'License :: OSI Approved :: MIT License',
                         'Development Status :: 3 - Alpha',
                         'Operating System :: POSIX',
                         'Operating System :: Unix',
                         'Programming Language :: Python :: 3',
                         'Topic :: System :: Archiving' ],

    long_description = readmeContents,
    long_description_content_type = 'text/markdown',

    py_modules       = [ 'indexed_bzip2' ],
    ext_modules      = extensions
)
