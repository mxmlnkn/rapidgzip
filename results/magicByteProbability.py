#!/usr/bin/env python3

import numpy as np


def transition_matrix_for_string_matching( string_to_find, letter_probabilities ):
    """
    string_to_find: An iterable, whose elements are interpreted as letters. E.g., "01001110" or [0,1,0,0,1,1,1,0].
    letter_probabilities: If it is a number, then it will be assumed that all letters are equiprobable and the given
                          number is that probability. If it is a dictionary, then all elements in string_to_find are
                          expected to appear as keys in letter_probabilities and the values are the respective
                          probabilities.
    returns the transition matrix. It's states are the n+1 with n=len(string_to_find) possible states for having matched
    a substring of length k already. The transition matrix element M[k,l], then contains the probability that given
    a matched substring of length k, after reading the next letter, we will have matched a substring of length l.
    """

    if isinstance( letter_probabilities, dict ):
        assert abs( sum( letter_probabilities.values() ) - 1 ) < 1e-6

    def p( letter ):
        if isinstance( letter_probabilities, dict ):
            return letter_probabilities[letter]
        return letter_probabilities

    n = len( string_to_find )

    # We can never suddenly match a substring which is longer than k+1 after reading only one additional
    # letter, therefore the upper triangle of the matrix is zero, M[k,l]=0 for all l > k+1.
    M = np.zeros( [ n+1, n+1 ] )

    # Because we are only interested in finding at least one string, the probability a full match
    # after already having found a full match, is 1 and going to any other state should have probability 0.
    M[n,n] = 1

    # If the next letter to read does not match, then we might not go back all the way to no matched substring at all.
    # Also, each state k>0 contains the read letter and therefore is well-defined making the probability to reach it
    # from any other letter either 0 or the probability for the last latter in that state,
    # M[k,l] in {0,p(last letter of l)} for all k>=0 and k<n and l>0 and l<=n.
    for k in range(n):
        # The upper minor diagonal are the probabilities that the matched substring grows by one letter, which
        # is the probability to find the next letter in the string to find
        M[k,k+1] = p( string_to_find[k] )

        # Each letter only should go to one new state. So if there are multiple possible new states, choose the longest!
        lettersUsed = { string_to_find[k] }
        for l in range(1,k+1)[::-1]:
            letter = string_to_find[l-1]
            if letter in lettersUsed:
                continue

            # Consider string_to_find = "ABCABD". Aftter having matched the substring "ABCAB",
            # the next letter might be a "D" to match "ABCABD", which would be the minor diagonal filled above.
            # But, it also might be a "C" resulting in the last 6 letters adding up to "ABCABC" and now the
            # longest matching substring becomes the latter half "ABC". So, we only go back to state k=3.
            if ( string_to_find[:k] + letter ).endswith( string_to_find[:l] ):
                lettersUsed |= { letter }
                M[k,l] = p( letter )

    # M is a transition matrix and the probability to go to any of the states should add up to 1.
    # This means, the sum over rows should add up to 1, giving us a simply way to calculate the probability
    # we end up with a matched substring of length 0, i.e., column 0, which contains the last elements to be filled in.
    for k in range(n):
        M[k,0] = 1 - sum( M[k,1:] )

    return M

print( transition_matrix_for_string_matching( "AAAA", 0.25 ) )
assert np.all( transition_matrix_for_string_matching( "AAAA", 0.25 ) ==
               np.array( [ [0.75, 0.25, 0.  , 0.  , 0.  ],
                           [0.75, 0.  , 0.25, 0.  , 0.  ],
                           [0.75, 0.  , 0.  , 0.25, 0.  ],
                           [0.75, 0.  , 0.  , 0.  , 0.25],
                           [0.  , 0.  , 0.  , 0.  , 1.  ] ] ) )

print()
print( transition_matrix_for_string_matching( "ACTAGC", { "A": 0.25, "C": 0.125, "G": 0.5, "T": 0.125 } ) )
assert np.all( transition_matrix_for_string_matching( "ACTAGC", { "A": 0.25, "C": 0.125, "G": 0.5, "T": 0.125 } ) ==
               np.array( [ [0.75 , 0.25, 0.   , 0.   , 0.  , 0. , 0.   ],
                           [0.625, 0.25, 0.125, 0.   , 0.  , 0. , 0.   ],
                           [0.625, 0.25, 0.   , 0.125, 0.  , 0. , 0.   ],
                           [0.75 , 0.  , 0.   , 0.   , 0.25, 0. , 0.   ],
                           [0.125, 0.25, 0.125, 0.   , 0.  , 0.5, 0.   ],
                           [0.625, 0.25, 0.   , 0.   , 0.  , 0. , 0.125],
                           [0.   , 0.  , 0.   , 0.   , 0.  , 0. , 1.   ] ] ) )


def probability_to_find_at_least_one_string( string_to_find, letter_probabilities, number_of_letters_to_search ):
    # Consider a row vector representing the probability for each state at the current time.
    # After adding one more letter, the new probabilities can be derived by right-multiplying with the
    # transition matrix.
    # For finding a string, we start with 100% probability to find a substring of length 0 because we have
    # no letters yet. After adding number_of_letters_to_search letters, we are interested in the probability
    # to find a substring of length equal to string_to_find. Instead of consecutively right-multiplying, the
    # matrix power is calculated and then only right-multiplied once, which in this case boils down to extracting
    # the 0-th row from the powered up matrix. And from that we take the len( string_to_find )-th element.
    M = transition_matrix_for_string_matching( string_to_find, letter_probabilities )
    # @todo calculate M to the npower of number_of_letters_to_search using eigenvalue decomposition?
    return np.linalg.matrix_power( M, number_of_letters_to_search )[0,len( string_to_find )]

print( probability_to_find_at_least_one_string( "ACTAGC", { "A": 0.25, "C": 0.125, "G": 0.5, "T": 0.125 }, 100000 ) )
# 0.9977688993088842
print( probability_to_find_at_least_one_string( "ACTAGC", { "A": 0.25, "C": 0.125, "G": 0.5, "T": 0.125 }, 1000000 ) )
# 0.9999999999999789

magic_bytes = 0x314159265359
string_to_find = f"{magic_bytes:048b}"
print( probability_to_find_at_least_one_string( string_to_find, 0.5, 1024**4*8 ) )
# 0.03076686427812536
# Not sure how numerically stable, but it seems we only find the string in 3 out of 100 cases when searching 1 TiB
# After this, this probability increases rather linearly as you would expect for
# number_of_letters_to_search >> len( string_to_find ). So, in general, we would find one false positive every ~32TiB!
# ToDo: Now that it seems at least somewhat feasible, find such a bz2 file be search consecutive 1GiB files, I guess...

# exit(0)

import matplotlib.pyplot as plt

xmax = 512*1024**4*8  # 512 TiB in bits
x = ( 10**np.linspace( 0, np.log10( xmax ), 200 ) ).astype( dtype = 'int' )
y = [ probability_to_find_at_least_one_string( string_to_find, 0.5, n ) for n in x ]
file = "bz2-magic-bytes-probabilities"

fig = plt.figure()
ax = fig.add_subplot( 111, xscale = 'log', yscale = 'log' )
ax.plot( x, y, '.-' )
ax.axhline( 1, color = 'k', linestyle = '--' )
ax.axvline( len( string_to_find ), color = 'k', linestyle = '--' )
fig.tight_layout()
fig.savefig( file + ".pdf" )
fig.savefig( file + ".png" )

plt.show()

