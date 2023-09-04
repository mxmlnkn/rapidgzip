"""
Cython wrapper for the GzipReader and ParallelGzipReader C++ classes.
"""

from libc.stdlib cimport malloc, free
from libc.stdio cimport SEEK_SET
from libcpp.string cimport string
from libcpp.map cimport map
from libcpp.vector cimport vector
from libcpp cimport bool
from cpython.buffer cimport PyObject_GetBuffer, PyBuffer_Release, PyBUF_ANY_CONTIGUOUS, PyBUF_SIMPLE
from cpython.ref cimport PyObject

import builtins
import io
import os
import sys

ctypedef (unsigned long long int) size_t
ctypedef (long long int) lli

cdef extern from "tools/rapidgzip.cpp":
    int rapidgzipCLI(int, char**) except +

cdef extern from "rapidgzip/ParallelGzipReader.hpp" namespace "rapidgzip":
    cppclass ChunkData

    cppclass ParallelGzipReader[ChunkData, ENABLE_STATISTICS=*]:
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
        map[size_t, size_t] blockOffsets() except +
        map[size_t, size_t] availableBlockOffsets() except +
        void importIndex(PyObject*) except +
        void exportIndex(PyObject*) except +
        void joinThreads() except +

    # To be used as template argument for ParallelGzipReader to avoid triggering the Cython
    # error message about template arguments being of an unknown type (because it is a value).
    # Must be inside an "extern" section or else Cython will try to generate a struct named
    # "true", which does not compile.
    cdef cppclass TrueValue "true":
        pass
    cdef cppclass RapidgzipChunkData "rapidgzip::ChunkData":
        pass

ctypedef ParallelGzipReader[RapidgzipChunkData, TrueValue] ParallelGzipReaderVerbose
ctypedef ParallelGzipReader[RapidgzipChunkData] ParallelGzipReaderNonVerbose


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

cdef class _RapidgzipFile():
    cdef ParallelGzipReaderNonVerbose* gzipReader
    cdef ParallelGzipReaderVerbose* gzipReaderVerbose

    def __cinit__(self, file, parallelization, verbose = False):
        """
        file : can be a file path, a file descriptor, or a file object
               with suitable read, seekable, seek, tell methods.
        """
        # This should be done before any error handling because we cannot initialize members in-place in Cython!
        # nullptr exists but does not work: https://github.com/cython/cython/issues/3314
        self.gzipReader = NULL
        self.gzipReaderVerbose = NULL

        if not isinstance(parallelization, int):
            raise TypeError(f"Parallelization argument must be an integer not '{parallelization}'!")

        if verbose:
            if isinstance(file, int):
                self.gzipReaderVerbose = new ParallelGzipReaderVerbose(<int>file, <int>parallelization)
            elif _hasValidFileno(file):
                self.gzipReaderVerbose = new ParallelGzipReaderVerbose(<int>file.fileno(), <int>parallelization)
            elif _isFileObject(file):
                self.gzipReaderVerbose = new ParallelGzipReaderVerbose(<PyObject*>file, <int>parallelization)
            elif isinstance(file, basestring) and hasattr(file, 'encode'):
                # Note that BytesIO also is an instance of basestring but fortunately has no encode method!
                self.gzipReaderVerbose = new ParallelGzipReaderVerbose(<string>file.encode(), <int>parallelization)
        else:
            if isinstance(file, int):
                self.gzipReader = new ParallelGzipReaderNonVerbose(<int>file, <int>parallelization)
            elif _hasValidFileno(file):
                self.gzipReader = new ParallelGzipReaderNonVerbose(<int>file.fileno(), <int>parallelization)
            elif _isFileObject(file):
                self.gzipReader = new ParallelGzipReaderNonVerbose(<PyObject*>file, <int>parallelization)
            elif isinstance(file, basestring) and hasattr(file, 'encode'):
                # Note that BytesIO also is an instance of basestring but fortunately has no encode method!
                self.gzipReader = new ParallelGzipReaderNonVerbose(<string>file.encode(), <int>parallelization)

        if self.gzipReader == NULL and self.gzipReaderVerbose == NULL:
            raise Exception("Expected file name string, file descriptor integer, "
                            "or file-like object for ParallelGzipReader!")


    def __init__(self, *args, **kwargs):
        pass

    def __dealloc__(self):
        self.close()

        if self.gzipReader:
            del self.gzipReader
            self.gzipReader = NULL

        if self.gzipReaderVerbose:
            del self.gzipReaderVerbose
            self.gzipReaderVerbose = NULL

    def close(self):
        if self.gzipReaderVerbose != NULL and not self.gzipReaderVerbose.closed():
            self.gzipReaderVerbose.close()
        if self.gzipReader != NULL and not self.gzipReader.closed():
            self.gzipReader.close()

    def closed(self):
        return (
            ( self.gzipReader == NULL or self.gzipReader.closed() )
            and ( self.gzipReaderVerbose == NULL or self.gzipReaderVerbose.closed() )
        )

    def fileno(self):
        if self.gzipReader:
            return self.gzipReader.fileno()
        if self.gzipReaderVerbose:
            return self.gzipReaderVerbose.fileno()
        raise Exception("Invalid file object!")

    def seekable(self):
        return (
            ( self.gzipReader != NULL and self.gzipReader.seekable() )
            or ( self.gzipReaderVerbose != NULL and self.gzipReaderVerbose.seekable() )
        )

    def readinto(self, bytes_like):
        if not self.gzipReader and not self.gzipReaderVerbose:
            raise Exception("Invalid file object!")

        bytes_count = 0

        cdef Py_buffer buffer
        PyObject_GetBuffer(bytes_like, &buffer, PyBUF_SIMPLE | PyBUF_ANY_CONTIGUOUS)
        try:
            if self.gzipReader:
                bytes_count = self.gzipReader.read(-1, <char*>buffer.buf, len(bytes_like))
            else:
                bytes_count = self.gzipReaderVerbose.read(-1, <char*>buffer.buf, len(bytes_like))
        finally:
            PyBuffer_Release(&buffer)

        return bytes_count

    def seek(self, offset, whence = io.SEEK_SET):
        if self.gzipReader:
            return self.gzipReader.seek(offset, whence)
        if self.gzipReaderVerbose:
            return self.gzipReaderVerbose.seek(offset, whence)
        raise Exception("Invalid file object!")

    def tell(self):
        if self.gzipReader:
            return self.gzipReader.tell()
        if self.gzipReaderVerbose:
            return self.gzipReaderVerbose.tell()
        raise Exception("Invalid file object!")

    def size(self):
        if self.gzipReader:
            return self.gzipReader.size()
        if self.gzipReaderVerbose:
            return self.gzipReaderVerbose.size()
        raise Exception("Invalid file object!")

    def tell_compressed(self):
        if self.gzipReader:
            return self.gzipReader.tellCompressed()
        if self.gzipReaderVerbose:
            return self.gzipReaderVerbose.tellCompressed()
        raise Exception("Invalid file object!")

    def block_offsets_complete(self):
        if self.gzipReader:
            return self.gzipReader.blockOffsetsComplete()
        if self.gzipReaderVerbose:
            return self.gzipReaderVerbose.blockOffsetsComplete()
        raise Exception("Invalid file object!")

    def block_offsets(self):
        if self.gzipReader:
            return self.gzipReader.blockOffsets()
        if self.gzipReaderVerbose:
            return self.gzipReaderVerbose.blockOffsets()
        raise Exception("Invalid file object!")

    def available_block_offsets(self):
        if self.gzipReader:
            return self.gzipReader.availableBlockOffsets()
        if self.gzipReaderVerbose:
            return self.gzipReaderVerbose.availableBlockOffsets()
        raise Exception("Invalid file object!")

    def import_index(self, file):
        if self.gzipReader:
            if isinstance(file, str):
                with builtins.open(file, "rb") as fileObject:
                    return self.gzipReader.importIndex(<PyObject*>fileObject)
            return self.gzipReader.importIndex(<PyObject*>file)

        if self.gzipReaderVerbose:
            if isinstance(file, str):
                with builtins.open(file, "rb") as fileObject:
                    return self.gzipReaderVerbose.importIndex(<PyObject*>fileObject)
            return self.gzipReaderVerbose.importIndex(<PyObject*>file)

        raise Exception("Invalid file object!")

    def export_index(self, file):
        if self.gzipReader:
            if isinstance(file, str):
                with builtins.open(file, "wb") as fileObject:
                    return self.gzipReader.exportIndex(<PyObject*>fileObject)
            return self.gzipReader.exportIndex(<PyObject*>file)

        if self.gzipReaderVerbose:
            if isinstance(file, str):
                with builtins.open(file, "wb") as fileObject:
                    return self.gzipReaderVerbose.exportIndex(<PyObject*>fileObject)
            return self.gzipReaderVerbose.exportIndex(<PyObject*>file)

        raise Exception("Invalid file object!")

    def join_threads(self):
        if self.gzipReader:
            return self.gzipReader.joinThreads()
        if self.gzipReaderVerbose:
            return self.gzipReaderVerbose.joinThreads()


# Extra class because cdefs are not visible from outside but cdef class can't inherit from io.BufferedIOBase
# ParallelGzipReader has its own internal buffer. Using io.BufferedReader is not necessary and might even
# worsen performance for access from parallel processes because it will always try to rebuffer the new seemingly
# random access while the internal cache is able to keep multiple buffers for different offsets.
# Using io.BufferedReader degraded performance by almost 2x for the test case of calculating the CRC32 four times
# using four parallel find | xarg crc32 instances for a file containing 10k files with each 1 MiB of base64 data.
class RapidgzipFile(io.RawIOBase):
    def __init__(self, filename, parallelization = 0, verbose = False):
        self.gzipReader = _RapidgzipFile(filename, parallelization, verbose)
        self.name = filename if isinstance(filename, str) else ""
        self.mode = 'rb'

        self.readinto = self.gzipReader.readinto
        self.seek     = self.gzipReader.seek
        self.tell     = self.gzipReader.tell
        self.seekable = self.gzipReader.seekable

        self.tell_compressed         = self.gzipReader.tell_compressed
        self.block_offsets           = self.gzipReader.block_offsets
        self.export_index            = self.gzipReader.export_index
        self.import_index            = self.gzipReader.import_index
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


def open(filename, parallelization = 0, verbose = False):
    """
    filename: can be a file path, a file descriptor, or a file object
              with suitable read, seekable, seek, and tell methods.
    """
    return RapidgzipFile(filename, parallelization, verbose)


def cli():
    args = sys.argv
    cdef char** cargs = <char**> malloc(len(args) * sizeof(char*))
    cdef vector[Py_buffer] buffers
    buffers.resize(len(args))

    try:
        for i, arg in enumerate(args):
            PyObject_GetBuffer(arg.encode(), &buffers[i], PyBUF_SIMPLE | PyBUF_ANY_CONTIGUOUS)
            cargs[i] = <char*>buffers[i].buf

        return rapidgzipCLI(len(args), cargs)
    finally:
        free(cargs)
        for buffer in buffers:
            PyBuffer_Release(&buffer)


__version__ = '0.10.1'
