#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import bz2
import collections
import concurrent.futures
import hashlib
import io
import os
import pprint
import subprocess
import tempfile
import time

from indexed_bzip2 import IndexedBzip2File

import numpy as np


def sha1_160( fileObject, bufferSize = 1024 * 1024 ):
    hasher = hashlib.sha1()
    for data in iter( lambda : fileObject.read( bufferSize ), b'' ):
        hasher.update( data )
    return hasher.digest()

def checkedSeek( fileobj, offset, whence = io.SEEK_SET ):
    new_offset = fileobj.seek( offset )
    if whence == io.SEEK_SET:
        assert new_offset == offset, "Returned offset is something different than the given offset!"
        assert fileobj.tell() == offset, "Offset returned by tell is different from the one given to seek!"


def checkDecompressionBytewise( rawFile, bz2File, bufferSize ):
    # Very slow for some reason! Only use this check if the checksum check fails
    checkedSeek( rawFile, 0 )
    checkedSeek( bz2File, 0 )

    decFile = IndexedBzip2File( bz2File.fileno() )

    while True:
        oldPos1 = rawFile.tell()
        oldPos2 = decFile.tell()

        data1 = rawFile.read( bufferSize )
        data2 = decFile.read( bufferSize )

        assert rawFile.tell() >= oldPos1 and rawFile.tell() <= oldPos1 + bufferSize, "Read should move the file position!"
        assert decFile.tell() >= oldPos2 and decFile.tell() <= oldPos2 + bufferSize, "Read should move the file position!"

        if data1 != data2:
            print( "Data at pos {} ({}) mismatches! After read at pos {} ({}).\nData:\n  {}\n  {}"
                   .format( oldPos1, oldPos2, rawFile.tell(), decFile.tell(), data1.hex(), data2.hex() ) )
            print( "Block offsets:" )
            pprint.pprint( decFile.block_offsets() )

            checkedSeek( bz2File, 0 )
            file = open( "bugged-random.bz2", 'wb' )
            file.write( bz2File.read() )
            file.close()

            raise Exception( "Data mismatches!" )

def checkDecompression( rawFile, bz2File, bufferSize ):
    checkedSeek( rawFile, 0 )
    checkedSeek( bz2File, 0 )

    file = IndexedBzip2File( bz2File.fileno() )
    sha1 = sha1_160( file, bufferSize )
    sha2 = sha1_160( rawFile )

    if sha1 != sha2:
        print( "SHA1 mismatches:", sha1.hex(), sha2.hex() )
        print( "Checking bytewise ..." )
        checkDecompressionBytewise( rawFile, bz2File, bufferSize )
        assert False, "SHA1 mismatch"

def checkSeek( rawFile, bz2File, seekPos ):
    # Try to read some bytes and compare them. We can without problem specify than 1 bytes even if we are at the end
    # of the file because then it is counted as the maximum bytes to read and the result will be shorter.
    checkedSeek( bz2File,  seekPos )
    c1 = bz2File.read( 256 )
    assert bz2File.tell() >= seekPos and bz2File.tell() <= seekPos + 256, "Read should move the file position!"

    checkedSeek( rawFile,  seekPos )
    c2 = rawFile.read( 256 )
    assert rawFile.tell() >= seekPos and rawFile.tell() <= seekPos + 256, "Read should move the file position!"

    if c1 != c2:
        print( "Char at pos", seekPos, "from sbzip2:", c1.hex(), "=?=", c2.hex(), "from raw file" )

    assert c1 == c2

def writeBz2File( data, compresslevel = 9, encoder = 'pybz2' ):
    rawFile = tempfile.TemporaryFile()
    rawFile.write( data )
    checkedSeek( rawFile, 0 );

    bz2File = tempfile.TemporaryFile()
    if encoder == 'pybz2':
        bz2File.write( bz2.compress( data, compresslevel ) )
    else:
        bz2File.write( subprocess.check_output( [ encoder, '-{}'.format( compresslevel ) ], input = data ) )
    checkedSeek( bz2File, 0 )

    return rawFile, bz2File

def createRandomBz2( sizeInBytes, compresslevel = 9, encoder = 'pybz2' ):
    return writeBz2File( os.urandom( sizeInBytes ) )

def createStripedBz2( sizeInBytes, compresslevel = 9, encoder = 'pybz2', sequenceLength = None ):
    data = b''
    while len( data ) < sizeInBytes:
        for char in [ b'A', b'B' ]:
            data += char * min( sequenceLength if sequenceLength else sizeInBytes, sizeInBytes - len( data ) )

    return writeBz2File( data )

def storeFiles( rawFile, bz2File, name ):
    if rawFile:
        with open( name, 'wb' ) as file:
            checkedSeek( rawFile, 0 )
            file.write( rawFile.read() )

    if bz2File:
        with open( name + ".bz2", 'wb' ) as file:
            checkedSeek( bz2File, 0 )
            file.write( bz2File.read() )

    print( "Created files {} and {}.bz2 with the failed test".format( name, name ) )


Bzip2TestParameters = collections.namedtuple( "Bzip2TestParameters",
                                              "size encoder compressionlevel pattern patternsize buffersizes" )

def testBz2( parameters ):
    if parameters.compressionlevel == 9: # reduce output
        print( "Testing", parameters )

    if parameters.pattern == 'random':
        rawFile, bz2File = createRandomBz2( parameters.size, parameters.compressionlevel, parameters.encoder )

    if parameters.pattern == 'sequences':
        rawFile, bz2File = createStripedBz2( parameters.size,
                                             parameters.compressionlevel,
                                             parameters.encoder,
                                             parameters.patternsize )

    t0 = time.time()
    for bufferSize in parameters.buffersizes:
        t1 = time.time()
        if t1 - t0 > 10:
            print( "Testing", parameters, "and buffer size", bufferSize )

        try:
            checkDecompression( rawFile, bz2File, bufferSize )
        except Exception as e:
            print( "Test for", parameters, "and buffer size", bufferSize, "failed" )
            storeFiles( rawFile, bz2File, str( parameters ) )
            raise e

    if parameters.size > 0:
        sbzip2 = IndexedBzip2File( bz2File.fileno() )
        for seekPos in np.append( np.random.randint( 0, parameters.size ), [ 0, parameters.size - 1 ] ):
            try:
                checkSeek( rawFile, sbzip2, seekPos )
            except Exception as e:
                print( "Test for", parameters, "failed when seeking to", seekPos )
                sb = IndexedBzip2File( bz2File.fileno() )
                sb.read( seekPos )
                print( "Char when doing naive seek:", sb.read( 1 ).hex() )

                storeFiles( rawFile, bz2File, str( parameters ) )
                raise e

        offsets = sbzip2.block_offsets()

        # Check seeking after loading offsets
        for seekPos in np.append( np.random.randint( 0, parameters.size ), [ 0, parameters.size - 1 ] ):
            try:
                sbzip2 = IndexedBzip2File( bz2File.fileno() )
                sbzip2.set_block_offsets( offsets )
                checkSeek( rawFile, sbzip2, seekPos )
            except Exception as e:
                print( "Test for", parameters, "failed when seeking to", seekPos )
                sb = IndexedBzip2File( bz2File.fileno() )
                sb.read( seekPos )
                print( "Char when doing naive seek:", sb.read( 1 ).hex() )

                storeFiles( rawFile, bz2File, str( parameters ) )
                raise e

    return True

def testPythonInterface():
    contents = b"Hello\nWorld!\n"
    rawFile, bz2File = writeBz2File( contents )
    file = IndexedBzip2File( bz2File.name )

    # Based on the Python spec, peek might return more or less bytes than requested
    # but I think in this case it definitely should not return less!
    assert file.peek( 1 )[0] == contents[0]
    assert file.peek( 1 )[0] == contents[0], "The previous peek should not change the internal position"

    assert file.tell() == 0
    assert file.read( 2 ) == contents[:2]
    assert file.tell() == 2

    assert file.seek( 0 ) == 0
    assert file.tell() == 0

    assert not file.closed
    assert not file.writable()
    assert file.readable()
    assert file.seekable()

    b = bytearray( 5 )
    file.readinto(  b )
    assert b == contents[:len(b)]

    assert file.seek( 1 ) == 1
    assert file.readline() == b"ello\n"

    assert file.seek( 0 ) == 0
    assert file.readlines() == [ x + b'\n' for x in contents.split( b'\n' ) if x ]

    file.close()
    assert file.closed

if __name__ == '__main__':
    testPythonInterface()

    buffersizes = [ -1, 128, 333, 500, 1024, 1024*1024, 64*1024*1024 ]
    parameters = [
        Bzip2TestParameters( size, encoder, compressionlevel, pattern, patternsize, buffersizes )
        for size in [ 1, 2, 3, 4, 5, 10, 20, 30, 100, 1000, 10000, 100000, 200000, 0 ]
        for encoder in [ 'pbzip2', 'bzip2', 'pybz2' ]
        for compressionlevel in range( 1, 9 + 1 )
        for pattern in [ 'random', 'sequences' ]
        for patternsize in ( [ None ] if pattern == 'random' else [ 1, 2, 8, 123, 257, 2048, 100000 ] )
    ]

    print( "Will test with", len( parameters ), "different bzip2 files" )
    with concurrent.futures.ProcessPoolExecutor() as executor:
        for input, output in zip( parameters, executor.map( testBz2, parameters ) ):
            assert output == True
