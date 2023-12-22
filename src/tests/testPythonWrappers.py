#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import bz2
import collections
import concurrent.futures
import gzip
import hashlib
import io
import multiprocessing
import os
import pprint
import shutil
import subprocess
import sys
import tempfile
import time

if __name__ == '__main__' and __package__ is None:
    sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), '..')))

import numpy as np


def sha1_160(fileObject, bufferSize=1024 * 1024):
    hasher = hashlib.sha1()
    for data in iter(lambda: fileObject.read(bufferSize), b''):
        hasher.update(data)
    return hasher.digest()


def checkedSeek(fileobj, offset, whence=io.SEEK_SET):
    new_offset = fileobj.seek(offset)
    if whence == io.SEEK_SET:
        assert new_offset == offset, "Returned offset is something different than the given offset!"
        assert fileobj.tell() == offset, "Offset returned by tell is different from the one given to seek!"


def checkDecompressionBytewise(rawFile, decompressedFile, bufferSize):
    # Very slow for some reason! Only use this check if the checksum check fails
    checkedSeek(rawFile, 0)

    decFile = IndexedBzip2File(decompressedFile.name)

    while True:
        oldPos1 = rawFile.tell()
        oldPos2 = decFile.tell()

        data1 = rawFile.read(bufferSize)
        data2 = decFile.read(bufferSize)

        assert (
            rawFile.tell() >= oldPos1 and rawFile.tell() <= oldPos1 + bufferSize
        ), "Read should move the file position!"
        assert (
            decFile.tell() >= oldPos2 and decFile.tell() <= oldPos2 + bufferSize
        ), "Read should move the file position!"

        if data1 != data2:
            print(
                "Data at pos {} ({}) mismatches! After read at pos {} ({}).\nData:\n  {}\n  {}".format(
                    oldPos1, oldPos2, rawFile.tell(), decFile.tell(), data1.hex(), data2.hex()
                )
            )
            print("Block offsets:")
            pprint.pprint(decFile.block_offsets())

            shutil.copyfile(bz2file.name, "bugged-random.bz2")

            raise Exception("Data mismatches!")


def checkDecompression(rawFile, compressedFile, decompressedFile, bufferSize):
    checkedSeek(rawFile, 0)

    sha1 = sha1_160(decompressedFile, bufferSize)
    sha2 = sha1_160(rawFile)

    if sha1 != sha2:
        print("SHA1 mismatches:", sha1.hex(), sha2.hex())
        print("Checking bytewise ...")
        checkDecompressionBytewise(rawFile, compressedFile, bufferSize)
        assert False, "SHA1 mismatch"


def checkSeek(rawFile, decompressedFile, seekPos):
    # Try to read some bytes and compare them. We can without problem specify than 1 bytes even if we are at the end
    # of the file because then it is counted as the maximum bytes to read and the result will be shorter.
    checkedSeek(decompressedFile, seekPos)
    c1 = decompressedFile.read(256)
    assert (
        decompressedFile.tell() >= seekPos and decompressedFile.tell() <= seekPos + 256
    ), "Read should move the file position!"

    checkedSeek(rawFile, seekPos)
    c2 = rawFile.read(256)
    assert rawFile.tell() >= seekPos and rawFile.tell() <= seekPos + 256, "Read should move the file position!"

    if c1 != c2:
        print("Char at pos", seekPos, "from sbzip2:", c1.hex(), "=?=", c2.hex(), "from raw file")

    assert c1 == c2


def writeCompressedFile(data, compressionLevel, encoder):
    rawFile = tempfile.NamedTemporaryFile()
    rawFile.write(data)
    checkedSeek(rawFile, 0)

    # https://docs.python.org/3/library/tempfile.html#tempfile.NamedTemporaryFile
    # > Whether the name can be used to open the file a second time,
    # > while the named temporary file is still open, varies across platforms
    # > (it can be so used on Unix; it cannot on Windows NT or later).
    # The error on Windows will be:
    # > PermissionError: [WinError 32] The process cannot access the file because it is being used by another process

    compressedFile = tempfile.NamedTemporaryFile(delete=False)
    if encoder == 'pybz2':
        compressedFile.write(bz2.compress(data, compressionLevel))
    elif encoder == 'pygzip':
        compressedFile.write(gzip.compress(data, compressionLevel))
    else:
        compressedFile.write(subprocess.check_output([encoder, '-{}'.format(compressionLevel)], input=data))
    checkedSeek(compressedFile, 0)

    compressedFile.close()

    return rawFile, compressedFile


def createRandomCompressedFile(sizeInBytes, compressionLevel, encoder):
    return writeCompressedFile(os.urandom(sizeInBytes), compressionLevel=compressionLevel, encoder=encoder)


def createStripedCompressedFile(sizeInBytes, compressionLevel, encoder, sequenceLength):
    data = b''
    while len(data) < sizeInBytes:
        for char in [b'A', b'B']:
            data += char * min(sequenceLength if sequenceLength else sizeInBytes, sizeInBytes - len(data))

    return writeCompressedFile(data, compressionLevel=compressionLevel, encoder=encoder)


def storeFiles(rawFile, compressedFile, name, compressedExtension):
    if rawFile:
        shutil.copyfile(rawFile.name, name)

    if compressedFile:
        shutil.copyfile(compressedFile.name, f"{name}.{compressedExtension}")

    print(f"Created files {name} and {name}.{compressedExtension} with the failed test")


TestParameters = collections.namedtuple(
    "TestParameters",
    "size encoder compressionLevel pattern patternSize bufferSizes parallelization extension CompressedFile",
)


def testDecompression(parameters):
    if parameters.compressionLevel == 9:  # reduce output
        print("Testing", parameters)

    if parameters.pattern == 'random':
        rawFile, compressedFile = createRandomCompressedFile(
            parameters.size, parameters.compressionLevel, parameters.encoder
        )

    if parameters.pattern == 'sequences':
        rawFile, compressedFile = createStripedCompressedFile(
            parameters.size, parameters.compressionLevel, parameters.encoder, parameters.patternSize
        )

    CompressedFileReader = parameters.CompressedFile

    t0 = time.time()
    for bufferSize in parameters.bufferSizes:
        t1 = time.time()
        if t1 - t0 > 10:
            print("Testing", parameters, "and buffer size", bufferSize)

        try:
            checkDecompression(rawFile, compressedFile, CompressedFileReader(compressedFile.name), bufferSize)
        except Exception as e:
            print("Test for", parameters, "and buffer size", bufferSize, "failed")
            storeFiles(rawFile, compressedFile, str(parameters), parameters.extension)
            raise e

    if parameters.size > 0:
        decompressedFile = CompressedFileReader(compressedFile.name, parameters.parallelization)
        for seekPos in np.append(np.random.randint(0, parameters.size), [0, parameters.size - 1]):
            try:
                checkSeek(rawFile, decompressedFile, seekPos)
            except Exception as e:
                print("Test for", parameters, "failed when seeking to", seekPos)
                sb = CompressedFileReader(compressedFile.name)
                sb.read(seekPos)
                print("    Char when doing naive seek:", sb.read(1).hex())
                print("    index.size:", index.seek(0, io.SEEK_END))

                storeFiles(rawFile, compressedFile, str(parameters), parameters.extension)
                raise e

        index = io.BytesIO()
        offsets = []
        if hasattr(decompressedFile, 'export_index'):
            decompressedFile.export_index(index)
        elif hasattr(decompressedFile, 'block_offsets'):
            offsets = decompressedFile.block_offsets()

        # Check seeking after loading offsets
        for seekPos in np.append(np.random.randint(0, parameters.size), [0, parameters.size - 1]):
            try:
                decompressedFile = CompressedFileReader(compressedFile.name, parallelization=parameters.parallelization)
                if hasattr(decompressedFile, 'import_index'):
                    decompressedFile.import_index(index)
                elif hasattr(decompressedFile, 'set_block_offsets'):
                    decompressedFile.set_block_offsets(offsets)
                checkSeek(rawFile, decompressedFile, seekPos)
            except Exception as e:
                print("Test for", parameters, "failed when seeking to", seekPos, "after loading block offsets")
                sb = CompressedFileReader(compressedFile.name)
                sb.read(seekPos)
                print("    Char when doing naive seek:", sb.read(1).hex())
                print("    index.size:", index.seek(0, io.SEEK_END))

                storeFiles(rawFile, compressedFile, str(parameters), parameters.extension)
                raise e

        decompressedFile.close()
        assert decompressedFile.closed

    os.remove(compressedFile.name)

    return True


def testPythonInterface(openIndexedFileFromName, compressionLevel, encoder, closeUnderlyingFile=None):
    contents = b"Hello\nWorld!\n"
    rawFile, compressedFile = writeCompressedFile(contents, compressionLevel=compressionLevel, encoder=encoder)
    file = openIndexedFileFromName(compressedFile.name)

    # Based on the Python spec, peek might return more or less bytes than requested
    # but I think in this case it definitely should not return less!
    if hasattr(file, 'peek'):
        assert file.peek(1)[0] == contents[0]
        assert file.peek(1)[0] == contents[0], "The previous peek should not change the internal position"

    assert file.tell() == 0
    assert file.read(2) == contents[:2]
    assert file.tell() == 2

    assert file.seek(0) == 0
    assert file.tell() == 0

    assert not file.closed
    assert not file.writable()
    assert file.readable()
    assert file.seekable()

    b = bytearray(5)
    file.readinto(b)
    assert b == contents[: len(b)]

    assert file.seek(1) == 1
    assert file.readline() == b"ello\n"

    assert file.seek(0) == 0
    assert file.readlines() == [x + b'\n' for x in contents.split(b'\n') if x]

    file.close()
    assert file.closed

    # Dirty hack to make os.remove work on Windows as everything must be closed correctly!
    if closeUnderlyingFile:
        closeUnderlyingFile()

    os.remove(compressedFile.name)


def commandExists(name):
    try:
        subprocess.call([name, "-h"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    except FileNotFoundError:
        return False

    return True


def openFileAsBytesIO(name):
    with open(name, 'rb') as file:
        return io.BytesIO(file.read())


openedFileForInterfaceTest = None


def openThroughGlobalFile(name, module):
    global openedFileForInterfaceTest
    openedFileForInterfaceTest = open(name, 'rb')
    return module.open(openedFileForInterfaceTest, parallelization=parallelization)


import indexed_bzip2
from indexed_bzip2 import IndexedBzip2File

import rapidgzip
from rapidgzip import RapidgzipFile


class FileWrapper:
    def __init__(self, fd):
        self.fd = fd

    def seek(self, offset, whence=0) -> int:
        return self.fd.seek(offset, whence)

    def read(self, amt=-1) -> bytes:
        return self.fd.read(amt)

    def tell(self) -> int:
        return self.fd.tell()

    def seekable(self):
        return self.fd.seekable()

    def readable(self):
        return self.fd.readable()

    def writable(self):
        return self.fd.writable()

    def write(self, data):
        raise NotImplemented()

    def close(self):
        self.fd.close()


def triggerDeadlock(filePath):
    print("Open test file...")
    with open(filePath, 'rb') as fp:
        wrapper = FileWrapper(fp)  # deadlock only seems to happen with a Python wrapper
        with RapidgzipFile(wrapper, verbose=True, parallelization = 4) as fd:
            for i in range(100):
                print(f"{i}: seek to 0")
                fd.seek(0)
                print(f"{i}: seek to 1000")
                fd.seek(1000)


def testTriggerDeadlock(filePath):
    for i in range(20):
        triggerDeadlock(filePath)


def testDeadlock(encoder):
    print("Create test file...")
    # We need at least something larger than the chunk size.
    rawFile, compressedFile = createRandomCompressedFile(100 * 1024 * 1024, 6, 'pygzip')

    task = multiprocessing.Process(target=testTriggerDeadlock, args = (compressedFile.name,))
    task.start()
    print("Started test process")
    task.join(10)
    if task.is_alive():
        task.terminate()
        task.join()
        sys.exit(1)


if __name__ == '__main__':
    print("indexed_bzip2 version:", indexed_bzip2.__version__)
    print("rapidgzip version:", rapidgzip.__version__)
    print("Cores:", os.cpu_count())

    testDeadlock('pygzip')

    def test(openIndexedFileFromName, closeUnderlyingFile=None):
        testPythonInterface(
            openIndexedFileFromName, compressionLevel=9, encoder="pybz2", closeUnderlyingFile=closeUnderlyingFile
        )

    for parallelization in [1, 2, 3, 8]:
        print("Test Python Interface of IndexedBzip2File for parallelization = ", parallelization)

        print("  Test opening with IndexedBzip2File")
        test(lambda name: IndexedBzip2File(name, parallelization=parallelization))

        print("  Test opening with indexed_bzip2.open")
        test(lambda name: indexed_bzip2.open(name, parallelization=parallelization))

        print("  Test opening with indexed_bzip2.open and file object with fileno")
        test(lambda name: openThroughGlobalFile(name, indexed_bzip2), lambda: openedFileForInterfaceTest.close())

        print("  Test opening with indexed_bzip2.open and file object without fileno")
        test(lambda name: indexed_bzip2.open(openFileAsBytesIO(name), parallelization=parallelization))

    def test(openIndexedFileFromName, closeUnderlyingFile=None):
        testPythonInterface(
            openIndexedFileFromName, compressionLevel=9, encoder="pygzip", closeUnderlyingFile=closeUnderlyingFile
        )

    for parallelization in [1, 2, 3, 8]:
        print("Test Python Interface of RapidgzipFile for parallelization = ", parallelization)

        print("  Test opening with RapidgzipFile")
        test(lambda name: RapidgzipFile(name, parallelization=parallelization))

        print("  Test opening with rapidgzip.open")
        test(lambda name: rapidgzip.open(name, parallelization=parallelization))

        print("  Test opening with rapidgzip.open and file object with fileno")
        test(lambda name: openThroughGlobalFile(name, rapidgzip), lambda: openedFileForInterfaceTest.close())

        print("  Test opening with rapidgzip.open and file object without fileno")
        test(lambda name: rapidgzip.open(openFileAsBytesIO(name), parallelization=parallelization))

    encoders = ['pybz2']
    if commandExists('bzip2'):
        encoders += ['bzip2']
    if commandExists('pbzip2'):
        encoders += ['pbzip2']

    bufferSizes = [-1, 128, 333, 500, 1024, 1024 * 1024, 64 * 1024 * 1024]
    parameters = [
        TestParameters(size, encoder, compressionLevel, pattern, patternSize, bufferSizes, 1, 'bz2', IndexedBzip2File)
        for size in [1, 2, 3, 4, 5, 10, 20, 30, 100, 1000, 10000, 100000, 200000, 0]
        for encoder in encoders
        for compressionLevel in range(1, 9 + 1)
        for pattern in ['random', 'sequences']
        for patternSize in ([None] if pattern == 'random' else [1, 2, 8, 123, 257, 2048, 100000])
    ]
    parameters += [
        TestParameters(size, 'pygzip', compressionLevel, pattern, patternSize, bufferSizes, 1, 'gz', RapidgzipFile)
        for size in [1, 2, 3, 4, 5, 10, 20, 30, 100, 1000, 10000, 100000, 200000, 0]
        for compressionLevel in range(1, 9 + 1)
        for pattern in ['random', 'sequences']
        for patternSize in ([None] if pattern == 'random' else [1, 2, 8, 123, 257, 2048, 100000])
    ]

    for parallelization in [1, 2, 3, 8]:
        print(f"Will test { parallelization }-parallelization with { len( parameters ) } different bzip2 files")
        parameters = [x._replace(parallelization=parallelization) for x in parameters]

        # The files are too small so that dividing by parallelization is not necessary to avoid oversubscribing
        # the CPU. Even with cpu_count() // 2, the total CPU usage does not exceed 60%
        # max_workers = max(1, (os.cpu_count() - 2) // parallelization)
        #   python3        real 1m11.116s, user  9m11.438s, sys 0m41.820s
        #   python3 -X dev real 9m42.763s, user 51m30.527s, sys 33m37.464s
        # max_workers = 1
        #   python3        real  3m36.056s, user  3m15.755s, sys 0m19.152s
        #   python3 -X dev real 19m59.795s, user 11m58.012s, sys 8m1.521s
        # max_workers = 2
        #   python3        real  1m47.980s, user  3m16.397s, sys 0m18.248s
        #   python3 -X dev real 11m58.835s, user 14m37.603s, sys 9m19.235s
        # max_workers = max(1, os.cpu_count() // 4)  # 6
        #   python3        real 0m43.877s, user  3m55.304s, sys  0m20.435s
        #   python3 -X dev real 7m47.180s, user 28m11.068s, sys 18m29.631s
        # max_workers = max(1, os.cpu_count() // 2)  # 12
        #   python3        real 0m44.114s, user 7m45.942s, sys 0m37.996s
        #   python3 -X dev real 8m2.940s, user 61m11.703s, sys 34m51.367s
        max_workers = os.cpu_count() if 'CI' in os.environ else max(1, os.cpu_count() // 4)
        with concurrent.futures.ProcessPoolExecutor(max_workers=max_workers) as executor:
            for input, output in zip(parameters, executor.map(testDecompression, parameters)):
                assert output == True
