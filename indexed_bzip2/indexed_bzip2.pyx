"""
Pyrex/C extension supporting `bx.misc.seekbzip2` (wrapping the low level
functions in `micro-bunzip.c`).
"""

from libc.stdlib cimport malloc, free
from libc.stdio cimport SEEK_SET
from libcpp.string cimport string
from libcpp.map cimport map
from libcpp cimport bool
from cpython.buffer cimport PyObject_GetBuffer, PyBuffer_Release, PyBUF_ANY_CONTIGUOUS, PyBUF_SIMPLE

import io
import os
import sys

ctypedef (unsigned long long int) size_t
ctypedef (long long int) lli


cdef extern from "Python.h":
    char * PyString_AsString(object)
    object PyString_FromStringAndSize(char*, int)

cdef extern from "BZ2Reader.hpp":
    cppclass BZ2Reader:
        BZ2Reader(string) except +
        BZ2Reader(int) except +
        bool eof() except +
        int fileno() except +
        bool seekable() except +
        void close() except +
        bool closed() except +
        size_t seek(lli, int) except +
        size_t tell() except +
        size_t tellCompressed() except +
        size_t size() except +
        int read(int, char*, size_t) except +
        bool blockOffsetsComplete() except +
        map[size_t, size_t] blockOffsets() except +
        map[size_t, size_t] availableBlockOffsets() except +
        void setBlockOffsets(map[size_t, size_t]) except +

cdef class _IndexedBzip2File():
    cdef BZ2Reader* bz2reader

    def __cinit__(self, fileNameOrDescriptor):
        if isinstance(fileNameOrDescriptor, basestring):
            self.bz2reader = new BZ2Reader(<string>fileNameOrDescriptor.encode())
        else:
            self.bz2reader = new BZ2Reader(<int>fileNameOrDescriptor)

    def __dealloc__(self):
        del self.bz2reader

    def close(self):
        self.bz2reader.close()

    def closed(self):
        return self.bz2reader.closed()

    def fileno(self):
        return self.bz2reader.fileno()

    def seekable(self):
        return self.bz2reader.seekable()

    def readinto(self, bytes_like):
        bytes_count = 0

        cdef Py_buffer buffer
        PyObject_GetBuffer(bytes_like, &buffer, PyBUF_SIMPLE | PyBUF_ANY_CONTIGUOUS)
        try:
            bytes_count = self.bz2reader.read(-1, <char*>buffer.buf, len(bytes_like))
        finally:
            PyBuffer_Release(&buffer)

        return bytes_count

    def seek(self, offset, whence):
        return self.bz2reader.seek(offset, whence)

    def tell(self):
        return self.bz2reader.tell()

    def size(self):
        return self.bz2reader.size()

    def tell_compressed(self):
        return self.bz2reader.tellCompressed()

    def block_offsets_complete(self):
        return self.bz2reader.blockOffsetsComplete()

    def block_offsets(self):
        return <dict>self.bz2reader.blockOffsets()

    def available_block_offsets(self):
        return <dict>self.bz2reader.availableBlockOffsets()

    def set_block_offsets(self, offsets):
        return self.bz2reader.setBlockOffsets(offsets)


# Extra class because cdefs are not visible from outside but cdef class can't inherit from io.BufferedIOBase

class IndexedBzip2FileRaw(io.RawIOBase):
    def __init__(self, filename):
        self.bz2reader = _IndexedBzip2File(filename)
        self.name = filename
        self.mode = 'rb'

        self.readinto = self.bz2reader.readinto
        self.seek     = self.bz2reader.seek
        self.tell     = self.bz2reader.tell
        self.fileno   = self.bz2reader.fileno
        self.seekable = self.bz2reader.seekable

        # IOBase provides sane default implementations for read, readline, readlines, readall, ...

    def close(self):
        if self.closed:
            return
        super().close()
        self.bz2reader.close()

    def readable(self):
        return True

class IndexedBzip2File(io.BufferedReader):
    def __init__(self, filename):
        fobj = IndexedBzip2FileRaw(filename)
        self.bz2reader = fobj.bz2reader

        self.tell_compressed         = self.bz2reader.tell_compressed
        self.block_offsets           = self.bz2reader.block_offsets
        self.set_block_offsets       = self.bz2reader.set_block_offsets
        self.block_offsets_complete  = self.bz2reader.block_offsets_complete
        self.available_block_offsets = self.bz2reader.available_block_offsets
        self.size                    = self.bz2reader.size

        # Most of the calls like close, seekable, name, mode ... are forwarded to the given raw object
        # by BufferedReader or more specifically _BufferedIOMixin
        super().__init__(fobj, buffer_size=1024**2)

__version__ = '1.2.0'
