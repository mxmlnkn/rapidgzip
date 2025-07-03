#!/usr/bin/env python3

"""
GIL API:

 - Py_BEGIN_ALLOW_THREADS: macro for: PyThreadState *_save; _save = PyEval_SaveThread();
 - Py_END_ALLOW_THREADS: macro for: PyEval_RestoreThread(_save);

 - PyEval_SaveThread: https://docs.python.org/3/c-api/init.html#c.PyEval_SaveThread
   > Release the global interpreter lock (if it has been created) and reset the thread state to NULL,
   > returning the previous thread state (which is not NULL). If the lock has been created,
   > the current thread must have acquired it.
   -> BEWARE that "the current thread must have acquired it"! This shit makes everything much more complicated
      than it could be for recursive calls and so on.
 - PyEval_RestoreThread: https://docs.python.org/3/c-api/init.html#c.PyEval_RestoreThread
   > Acquire the global interpreter lock (if it has been created) and set the thread state to tstate,
   > which must not be NULL. If the lock has been created, the current thread must not have acquired it,
   > otherwise deadlock ensues.
    -> BEWARE the deadlock warning! Why the fuck can't you check for that yourself!? This makes everything complicated.

   > Calling this function from a thread when the runtime is finalizing will terminate the thread,
   > even if the thread was not created by Python. You can use Py_IsFinalizing() or sys.is_finalizing() to check
   > if the interpreter is in process of being finalized before calling this function to avoid unwanted termination.
    -> AND ANOTHER FUCKING GOTCHA! Please just stop!? I am not ranting without reason. I have triggered basically
       every gotcha there is in the documentation at some point :'(.

 - PyGILState_Ensure: https://docs.python.org/3/c-api/init.html#c.PyGILState_Ensure
   > Ensure that the current thread is ready to call the Python C API regardless of the current state of Python,
   > or of the global interpreter lock. This may be called as many times as desired by a thread as long as each call
   > is matched with a call to PyGILState_Release().
   > [...] recursive calls are allowed
   > When the function returns, the current thread will hold the GIL and be able to call arbitrary Python code.
   > Failure is a fatal error.
   > Calling this function from a thread when the runtime is finalizing will terminate the thread,
   > even if the thread was not created by Python. You can use Py_IsFinalizing() or sys.is_finalizing() to checkout
   > if the interpreter is in process of being finalized before calling this function to avoid unwanted termination.
    -> Sounds like a useful abstraction except for the GOTCHA that every Ensure must be matched by a Release
       and the GOTCHA about finalizing.
   https://github.com/python/cpython/blob/5334732f9c8a44722e4b339f4bb837b5b0226991/Python/pystate.c#L2735
    -> It's somewhat similar to my ScopedGILLock! It checks whether locking is necessary or not
       and returns the old lock state so that the operation can be reversed.
    -> It creates a new thread-state if necessary! This is why we need to use this on the spawned threads because
       there seems to be no other API for ensuring the thread-state...
       There is not even a way to CHECK THE FUCKING THREAD_STATE EXISTENCE?! Goddamn it
       https://docs.python.org/3/c-api/init.html#c.PyThreadState_Get
        > When the current thread state is NULL, this issues a fatal error (so that the caller needn’t check for NULL).
        -> Only starting from Python 3.13, there finally is PyThreadState_GetUnchecked, but it is a bit too late.
           Would have been nice to simply check for this thread state to decide when to use Ensure/Release vs.
           SaveThread/RestoreThread.
           -> PyGILState_GetThisThreadState may work for this and even is part of the Stable ABI!
    -> It calls PyEval_RestoreThread only when necessary.
    -> PyGILState_Release only calls PyEval_SaveThread when necessary (the old GIL state was unlocked)
       -> It does not seem to lock when the old state was locked!
          I guess because it assumes that it is called after PyGILState_Ensure, which already locks the GIL.
          But it does not account for the GIL to get unlocked through other means!?

 - PyThreadState_Swap
   > Swap the current thread state with the thread state given by the argument tstate, which may be NULL.
   > The GIL does not need to be held, but will be held upon returning if tstate is non-NULL.
   Is this like SaveThread and RestoreThread in one?

grep -I -r -E '(Py_BEGIN_ALLOW_THREADS|Py_END_ALLOW_THREADS|PyEval_SaveThread|PyEval_RestoreThread|PyGILState_Ensure|
    PyGILState_Release|PyGILState_Check|Py_BEGIN_ALLOW_THREADS|Py_END_ALLOW_THREADS|Py_BLOCK_THREADS|
    Py_UNBLOCK_THREADS)' src/

History (git log --oneline | grep GIL):

    c9d7fbc4 2024-02-21 mxmlnkn [fix] ScopedGILLock: Do not throw a warning when trying to unlock the GIL during Python finalization
    5992d3a7 2023-12-21 mxmlnkn [fix] Avoid deadlock with GIL and SharedFileReader
        https://github.com/mxmlnkn/rapidgzip/issues/27
         -> issue has a reproducer
    989a08fe 2023-12-20 mxmlnkn [fix] Do not throw exceptions from ScopedGIL destructor
    a96aef25 2023-12-22 mxmlnkn [test] Add test that seeks frequently to trigger a GIL+SharedFileReader deadlock
    3cf30452 2023-12-18 mxmlnkn [fix] Do not call PyGILState_Release when we do not own the GIL because Python is finalizing
        https://github.com/mxmlnkn/rapidgzip/issues/26
         -> issue has a reproducer
         -> The fix simple only prints a better error message to properly join threads, e.g., with a context manager.
    edfa0c5c 2023-12-18 mxmlnkn [refactor] Simplify ScopedGIL reference counting
    f2d990d8 2023-11-24 mxmlnkn [fix] Suppress "Python memory allocator called without holding the GIL" errors with PYTHONDEVMODE=1
        https://github.com/mxmlnkn/rapidgzip/issues/24
         -> has a reproducer


Considerations:

 1. The threads spawned via C++ must initialize a thread state and lock the GIL when calling a Python file object!
 2. In order for the background threads to be able to lock the GIL, the GIL must be unlocked on the main thread!
   2.1. Beware that this unlock might also happen from a non-main thread for recursive calls of Python multi-threading.
 3. The main rapidgzip thread should probably unlock the GIL so that Python-side multi-threading works.
   - [ ] Add test that spawns multiple Python threads, which are all decompressing gzip or bzip2 with rapidgzip P=1.
   - This should already be the case for 2. to be correct.
 4. For P=1, the Python main thread may try to call a Python file object and should (relock) the GIL.
 5. For P=1, the Python main thread may try to call a Python file object, which might also be a RapidgzipFile
    at which point the GIL may be locked recursively!
 6. For P>1, the spawned threads may try to call a Python file object, which might also be a RapidgzipFile,
    which would call the main thread! Deadlocks and recursive locking becomes important.
 7. The gotcha with the GIL API during Python finalizing becomes an issue when a RapidgzipFile, or rather its
       background C threads, is not correctly closed before quitting Python.
   7.1. This becomes extra-difficult when Python is quit with Ctrl+C! -> issue 26
"""

import bz2
import gzip
import io
import os
import sys

import pytest
import rapidgzip


def test_issue_24():
    """
    This issue noticed that the background threads of rapidgzip, which may read from a Python file object class,
    do not do any GIL locking at all! In fact, rapidgzip did no GIL call at all prior to this! This is because
    the main thread normally does not need to unlock the GIL because it simply computes something and then returns.
    However, without unlocking the GIL, it disables Python-side multi-threading.

    The recommended usage of non-Python-created threads is described here:
    https://docs.python.org/3/c-api/init.html#non-python-created-threads
    > However, when threads are created from C (for example by a third-party library with its own thread management),
    > they don’t hold the GIL, nor is there a thread state structure for them.
    >
    > If you need to call Python code from these threads (often this will be part of a callback API provided by the
    > aforementioned third-party library), you must first register these threads with the interpreter by creating a
    > thread state data structure, then acquiring the GIL, and finally storing their thread state pointer, before you
    > can start using the Python/C API. When you are done, you should reset the thread state pointer, release the GIL,
    > and finally free the thread state data structure.
    >
    > The PyGILState_Ensure() and PyGILState_Release() functions do all of the above automatically.
    > The typical idiom for calling into Python from a C thread is:
    >
    >    PyGILState_STATE gstate;
    >    gstate = PyGILState_Ensure();
    >
    >    /* Perform Python actions here. */
    >    result = CallSomeFunction();
    >    /* evaluate result or handle exception */
    >
    >    /* Release the thread. No Python API allowed beyond this point. */
    >    PyGILState_Release(gstate);

    Red-green test by checking out older commit or installing an older version:

       git checkout f2d990d8~ && git submodule update && python3 -m build . &&
       python3.12 -m pip install --force-reinstall dist/rapidgzip-0.10.3-cp312-cp312-linux_x86_64.whl
       python3.12 -m pytest ../../src/tests/testGIL.py   # segfault in Python 3.12

       python3.11 -m pip install --force-reinstall rapidgzip==0.10.3
       python3.11 -m pytest ../../src/tests/testGIL.py   # no error in Python 3.11!
       python3.11 -X dev -m pytest ../../src/tests/testGIL.py   # "Aborted" with exit code 134 in Python 3.11
    """
    # Check for dev mode because this issue only caused a segfault for Python 3.12+.
    # https://docs.python.org/3/library/devmode.html
    assert sys.flags.dev_mode

    compressed_file = io.BytesIO()

    with gzip.open(compressed_file, mode='wb') as f:
        f.write(b"Hello\nWorld!\n")

    fobj = rapidgzip.RapidgzipFile(compressed_file)
    fobj.block_offsets()


def test_issue_24_bzip2():
    assert sys.flags.dev_mode

    compressed_file = io.BytesIO()

    with bz2.open(compressed_file, mode='wb') as f:
        f.write(b"Hello\nWorld!\n")

    fobj = rapidgzip.IndexedBzip2File(compressed_file)
    fobj.block_offsets()


def test_issue_26(tmp_path):
    """
    This issue was caused because two locks are required:
     - SharedFileReader lock
     - GIL lock
    Without something like std::scoped_lock, which has deadlock-avoidance algorithms for locking multiple
    mutexes, this can deadlock.

    Fixed by introducing filereader/Shared.hpp:FileLock, which forces a fixed order:
     1. unlock GIL in case it was already locked.
     2. lock C++ mutex
     3. lock GIL
    See commenst in the code for FileLock. The problem is that the GIL may be unlocked by other parts.

    https://github.com/mxmlnkn/rapidgzip/issues/27#issuecomment-1866805141

    Reproduce with:
        python3.12 -m pip install --force-reinstall rapidgzip==0.11.0
        timeout 10 python3.12 -X dev -m pytest -s ../../src/tests/testGIL.py; echo $?
    Output for me is:
        ../../src/tests/testGIL.py .Create random gzip file...
        Seek to 0 the 0-th time.
        Seek to 1000
        Seek to 0 the 1-th time.
        124
    """

    def create_temp_gzip_file(size_in_MiB):
        path = tmp_path / f"random-{size_in_MiB}MiB.gz"
        with path.open('wb') as file:
            with gzip.GzipFile(fileobj=file, mode='wb', compresslevel=1) as gz:
                for _ in range(size_in_MiB):
                    gz.write(os.urandom(1024 * 1024))
        return path

    print('Create random gzip file...')
    # Hang does not happen for smaller sizes (tried with 30 MiB to no avail).
    compressed_file = create_temp_gzip_file(size_in_MiB=50)

    # Hang requires a Python wrapper so that rapidgzip has to use the Python C-API and lock the GIL.
    gzip_fd = io.BytesIO(compressed_file.read_bytes())
    # Hang is less likely to happen smaller parallelization.
    with rapidgzip.open(gzip_fd, parallelization=8, verbose=True) as fd:
        # The weird thing is, either this hang happens on the first true or not at all.
        # Only rarely it happens on the e.g., the 25th try.
        for i in range(100):
            print(f"Seek to 0 the {i}-th time.")
            fd.seek(0)
            print("Seek to 1000")
            fd.seek(1000)

    compressed_file.unlink()


def test_issue_26_bzip2(tmp_path):
    def create_temp_bzip2_file(size_in_MiB):
        path = tmp_path / f"random-{size_in_MiB}MiB.bz2"
        with path.open('wb') as file:
            with bz2.BZ2File(file, mode='wb', compresslevel=1) as gz:
                for _ in range(size_in_MiB):
                    gz.write(os.urandom(1024 * 1024))
        return path

    print('Create random bzip2 file...')
    # Hang does not happen for smaller sizes (tried with 30 MiB to no avail).
    compressed_file = create_temp_bzip2_file(size_in_MiB=50)

    # Hang requires a Python wrapper so that rapidbzip2 has to use the Python C-API and lock the GIL.
    bzip2_fd = io.BytesIO(compressed_file.read_bytes())
    # Hang is less likely to happen smaller parallelization.
    with rapidgzip.IndexedBzip2File(bzip2_fd, parallelization=8) as fd:
        # The weird thing is, either this hang happens on the first true or not at all.
        # Only rarely it happens on the e.g., the 25th try.
        for i in range(100):
            print(f"Seek to 0 the {i}-th time.")
            fd.seek(0)
            print("Seek to 1000")
            fd.seek(1000)

    compressed_file.unlink()


@pytest.mark.parametrize('inner_parallelization', [0, 1, 2])
@pytest.mark.parametrize('outer_parallelization', [0, 1, 2])
def test_issue_51(tmp_path, inner_parallelization, outer_parallelization):
    """
    This tests recursive usage for different parallelization scenarios, i.e.,
     - main thread calling into main thread
     - main thread calling into main thread waiting for another C thread
     - C thread calling main thread
     - C thread calling main thread waiting for another C thread
    """
    path = tmp_path / "foo.gz.gz"
    data = b"Hello World!"
    with gzip.open(path, 'wb') as file:
        file.write(gzip.compress(data))

    with path.open('rb') as zfd:
        unpacked_once = rapidgzip.open(zfd, parallelization=outer_parallelization)
        unpacked = rapidgzip.open(unpacked_once, parallelization=inner_parallelization)

        assert unpacked.read() == data
        unpacked.close()
        unpacked_once.close()


@pytest.mark.parametrize('inner_parallelization', [0, 1, 2])
@pytest.mark.parametrize('outer_parallelization', [0, 1, 2])
def test_issue_51_bzip2(tmp_path, inner_parallelization, outer_parallelization):
    path = tmp_path / "foo.bz2.bz2"
    data = b"Hello World!"
    with bz2.open(path, 'wb') as file:
        file.write(bz2.compress(b"Hello World!"))

    with path.open('rb') as zfd:
        unpacked_once = rapidgzip.IndexedBzip2File(zfd, parallelization=outer_parallelization)
        unpacked = rapidgzip.IndexedBzip2File(unpacked_once, parallelization=inner_parallelization)

        assert unpacked.read() == data
        unpacked.close()
        unpacked_once.close()
