"""
Cython wrapper for the GzipReader and ParallelGzipReader C++ classes.
"""

from libc.stdlib cimport malloc, free
from libc.stdio cimport SEEK_SET
from libcpp.string cimport string
from libcpp.map cimport map
from libcpp cimport bool
from cpython.buffer cimport PyObject_GetBuffer, PyBuffer_Release, PyBUF_ANY_CONTIGUOUS, PyBUF_SIMPLE
from cpython.ref cimport PyObject

import builtins
import io
import os
import sys

ctypedef (unsigned long long int) size_t
ctypedef (long long int) lli


cdef extern from "ParallelGzipReader.hpp":
    cppclass ParallelGzipReader:
        ParallelGzipReader(string, size_t) except +
        ParallelGzipReader(int, size_t) except +
        ParallelGzipReader(PyObject*, size_t) except +

        bool eof() except +
        int fileno() except +
        bool seekable() except +
        void close() except +
        bool closed() except +
        size_t seek(lli, int) except +
        size_t tell() except +
        size_t size() except +

        size_t tellCompressed() except +
        int read(int, char*, size_t) except +
        bool blockOffsetsComplete() except +
        # TODO Add API for the back-reference windows not just the gzip offset!
        map[size_t, size_t] blockOffsets() except +
        map[size_t, size_t] availableBlockOffsets() except +
        void setBlockOffsets(PyObject*) except +
        void joinThreads() except +

def _isFileObject(file):
    return (
        hasattr(file, 'read') and callable(getattr(file, 'read'))
        and hasattr(file, 'seek') and callable(getattr(file, 'seek'))
        and hasattr(file, 'tell') and callable(getattr(file, 'tell'))
        and hasattr(file, 'seekable') and callable(getattr(file, 'seekable'))
    )

def _hasValidFileno(file):
    if not hasattr(file, 'fileno'):
        return False

    try:
        fileno = file.fileno()
        return isinstance(fileno, int) and fileno >= 0
    except Exception:
        return False

cdef class _PragzipFile():
    cdef ParallelGzipReader* gzipReader

    def __cinit__(self, file, parallelization):
        """
        file : can be a file path, a file descriptor, or a file object
               with suitable read, seekable, seek, tell methods.
        """

        if isinstance(file, int):
            self.gzipReader = new ParallelGzipReader(<int>file, <int>parallelization)
        elif _hasValidFileno(file):
            self.gzipReader = new ParallelGzipReader(<int>file.fileno(), <int>parallelization)
        elif _isFileObject(file):
            self.gzipReader = new ParallelGzipReader(<PyObject*>file, <int>parallelization)
        elif isinstance(file, basestring) and hasattr(file, 'encode'):
            # Note that BytesIO also is an instance of basestring but fortunately has no encode method!
            self.gzipReader = new ParallelGzipReader(<string>file.encode(), <int>parallelization)
        else:
            raise Exception("Expected file name string, file descriptor integer, "
                            "or file-like object for ParallelGzipReader!")


    def __init__(self, *args, **kwargs):
        pass

    def __dealloc__(self):
        self.close()
        del self.gzipReader

    def close(self):
        if not self.gzipReader.closed():
            self.gzipReader.close()

    def closed(self):
        return self.gzipReader.closed()

    def fileno(self):
        return self.gzipReader.fileno()

    def seekable(self):
        return self.gzipReader.seekable()

    def readinto(self, bytes_like):
        bytes_count = 0

        cdef Py_buffer buffer
        PyObject_GetBuffer(bytes_like, &buffer, PyBUF_SIMPLE | PyBUF_ANY_CONTIGUOUS)
        try:
            bytes_count = self.gzipReader.read(-1, <char*>buffer.buf, len(bytes_like))
        finally:
            PyBuffer_Release(&buffer)

        return bytes_count

    def seek(self, offset, whence):
        return self.gzipReader.seek(offset, whence)

    def tell(self):
        return self.gzipReader.tell()

    def size(self):
        return self.gzipReader.size()

    def tell_compressed(self):
        return self.gzipReader.tellCompressed()

    def block_offsets_complete(self):
        return self.gzipReader.blockOffsetsComplete()

    def block_offsets(self):
        return <dict>self.gzipReader.blockOffsets()

    def available_block_offsets(self):
        return <dict>self.gzipReader.availableBlockOffsets()

    def set_block_offsets(self, offsetsFileObject):
        if isinstance(offsetsFileObject, str):
            with builtins.open(offsetsFileObject, "rb") as file:
                return self.gzipReader.setBlockOffsets(<PyObject*>file)
        return self.gzipReader.setBlockOffsets(<PyObject*>offsetsFileObject)

    def join_threads(self):
        return self.gzipReader.joinThreads()

# Extra class because cdefs are not visible from outside but cdef class can't inherit from io.BufferedIOBase
# ParallelGzipReader has its own internal buffer. Using io.BufferedReader is not necessary and might even
# worsen performance for access from parallel processes because it will always try to rebuffer the new seemingly
# random access while the internal cache is able to keep multiple buffers for different offsets.
# Using io.BufferedReader degraded performance by almost 2x for the test case of calculating the CRC32 four times
# using four parallel find | xarg crc32 instances for a file containing 10k files with each 1 MiB of base64 data.
class PragzipFile(io.RawIOBase):
    def __init__(self, filename, parallelization = 0):
        self.gzipReader = _PragzipFile(filename, parallelization)
        self.name = filename
        self.mode = 'rb'

        self.readinto = self.gzipReader.readinto
        self.seek     = self.gzipReader.seek
        self.tell     = self.gzipReader.tell
        self.seekable = self.gzipReader.seekable

        self.tell_compressed         = self.gzipReader.tell_compressed
        self.block_offsets           = self.gzipReader.block_offsets
        self.set_block_offsets       = self.gzipReader.set_block_offsets
        self.block_offsets_complete  = self.gzipReader.block_offsets_complete
        self.available_block_offsets = self.gzipReader.available_block_offsets
        self.size                    = self.gzipReader.size

        if hasattr(self.gzipReader, 'join_threads'):
            self.join_threads = self.gzipReader.join_threads

        # IOBase provides sane default implementations for read, readline, readlines, readall, ...

    def fileno(self):
        try:
            return self.gzipReader.fileno()
        except Exception as exception:
            raise io.UnsupportedOperation() from exception

    def close(self):
        if self.closed:
            return
        super().close()
        self.gzipReader.close()

    def readable(self):
        return True


def open(filename, parallelization = 0):
    """
    filename: can be a file path, a file descriptor, or a file object
              with suitable read, seekable, seek, and tell methods.
    """
    return PragzipFile(filename, parallelization)


__version__ = '0.2.0'
