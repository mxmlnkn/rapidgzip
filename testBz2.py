#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import bz2
import collections
import concurrent.futures
import hashlib
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

def checkDecompressionBytewise( rawFile, bz2File, bufferSize ):
    # Very slow for some reason! Only use this check if the checksum check fails
    rawFile.seek( 0 )
    bz2File.seek( 0 )

    decFile = IndexedBzip2File( bz2File.fileno() )

    while True:
        oldPos1 = rawFile.tell()
        oldPos2 = decFile.tell()

        data1 = rawFile.read( bufferSize )
        data2 = decFile.read( bufferSize )

        if data1 != data2:
            print( "Data at pos {} ({}) mismatches! After read at pos {} ({}).\nData:\n  {}\n  {}"
                   .format( oldPos1, oldPos2, rawFile.tell(), decFile.tell(), data1.hex(), data2.hex() ) )
            print( "Block offsets:" )
            pprint.pprint( decFile.blockOffsets() )

            bz2File.seek( 0 )
            file = open( "bugged-random.bz2", 'wb' )
            file.write( bz2File.read() )
            file.close()

            raise Exception( "Data mismatches!" )

def checkDecompression( rawFile, bz2File, bufferSize ):
    rawFile.seek( 0 )
    bz2File.seek( 0 )

    file = IndexedBzip2File( bz2File.fileno() )
    sha1 = sha1_160( file, bufferSize )
    sha2 = sha1_160( rawFile )

    if sha1 != sha2:
        print( "SHA1 mismatches:", sha1.hex(), sha2.hex() )
        print( "Checking bytewise ..." )
        checkDecompressionBytewise( rawFile, bz2File, bufferSize )
        assert False, "SHA1 mismatch"

def checkSeek( rawFile, bz2File, seekPos ):
    bz2File.seek( seekPos )
    c1 = bz2File.read( 1 )

    rawFile.seek( seekPos )
    c2 = rawFile.read( 1 )

    if c1 != c2:
        print( "Char at pos", seekPos, "from sbzip2:", c1.hex(), "=?=", c2.hex(), "from raw file" )

    assert c1 == c2

def writeBz2File( data, compresslevel = 9, encoder = 'pybz2' ):
    rawFile = tempfile.TemporaryFile()
    rawFile.write( data )
    rawFile.seek( 0 );

    bz2File = tempfile.TemporaryFile()
    if encoder == 'pybz2':
        bz2File.write( bz2.compress( data, compresslevel ) )
    else:
        bz2File.write( subprocess.check_output( [ encoder, '-{}'.format( compresslevel ) ], input = data ) )
    bz2File.seek( 0 )

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
            rawFile.seek( 0 )
            file.write( rawFile.read() )

    if bz2File:
        with open( name + ".bz2", 'wb' ) as file:
            bz2File.seek( 0 )
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
            storeFiles( rawFile, bz2File )
            raise e

    if parameters.size > 0:
        sbzip2 = IndexedBzip2File( bz2File.fileno() )
        for seekPos in np.append( np.random.randint( 0, parameters.size ), [ 0, parameters.size - 1 ] ):
            try:
                checkSeek( rawFile, sbzip2, seekPos )
            except Exception as e:
                print( "Test for", parameters, "failed when seeking to", seekPos )
                sb = IndexedBzip2File( bz2File )
                sb.read( seekPos )
                print( "Char when doing naive seek:", sb.read( 1 ).hex() )

                storeFiles( rawFile, bz2File )
                raise e

    return True

if __name__ == '__main__':
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
