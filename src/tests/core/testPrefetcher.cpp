
#include <cassert>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <unordered_map>

#include <Cache.hpp>
#include <common.hpp>
#include <Prefetcher.hpp>
#include <TestHelpers.hpp>


using namespace FetchingStrategy;


/**
 * This is the access pattern from the following real-world test (with prefetching disabled
 * and duplicate block accesses removed):
 *
 * @verbatim
 * mkdir 10k-1MiB-files; cd -- "$_"
 * for (( i=0; i <10000; ++i )); do
 *     base64 /dev/urandom | head -c $(( 1024 * 1024 )) > "$i.base64"
 * done
 * cd ..
 * tar -czf 10k-1MiB-files{.tar.gz,}
 *
 * ratarmount 10k-1MiB-files.tar.gz mounted
 * time find mounted -type f -print0 | xargs -0 crc32
 *     real 1m38.168s
 *     user 0m10.820s
 *     sys  0m4.012s
 * @endverbatim
 *
 * The statistics:
 *
 * @verbatim
 * Parallelization                   : 24
 * Accesses
 *     Total Accesses                : 110624
 *     Cache Hits                    : 109987
 *     Cache Misses                  : 637
 *     Prefetch Queue Hit            : 0
 *     Hit Rate                      : 0.994242
 *     Unused Cache Entries          : 0
 * Access Patterns
 *     Total Accesses                : 110624
 *     Duplicate Block Accesses      : 109038
 *     Sequential Block Accesses     : 663
 *     Block Seeks Back              : 471
 *     Block Seeks Forward           : 452
 * Blocks
 *    Total Existing                 : 625
 *    Total Fetched                  : 637
 *    Prefetched                     : 0
 *    Fetched On-demand              : 637
 * Prefetch Stall by BlockFinder     : 0
 * Time spent in:
 *     bzip2::readBlockData          : 0 s
 *     decodeBlock                   : 79.727 s
 *     std::future::get              : 79.7533 s
 *     get                           : 80.0425 s
 * @endverbatim
 *
 * There are still a lot of quasi-sequential accesses when viewing it as different streams!
 * There are surprisingly always different blocks that are referenced very frequently interpersed with the
 * other accesses. I don't understand why. It's not like we have to access folder metadata because we already
 * have all of that in the SQLite database.
 * One access stream is much more prevalent than the other, which might make it hard to detect the smaller one.
 */
constexpr std::array<int, 1586> REAL_ACCESS_PATTERN_1 = {
    // *INDENT-OFF*
      1,   6,  60,  61,   6,  61,  62,   6,  62,  63,  64,   6,  64,  65,   6,  65,  66,   6,  66,  67,   6,  67,
     68,  69,   6,   7,   8,   6,  69,  70,   6,  70,  71,   6,  71,  72,  73,   6,  73,  74,   6,  74,  75,   6,
      7,  75,  76,   7,  76,  77,   7,  77,  78,   7,  78,  79,  80, 107,   8,   9,  83,  80,  81,  83,  81,  82,
     83,  82,  83,  84,  85,  83,  84,  85,  86,  87,  84,  87,  88,  84,  88,  89,  84,  89,  90,  84,  90,  91,
     84,   9,  10,  84,  91,  92,  93,  84,  93,  94,  84,  94,  95,  84,  95,  96,  84,  96,  97,  84,  97,  98,
     99,  84,  99, 100, 107, 100, 101, 106, 101, 102, 103, 106,  10,  11, 106, 103, 104, 106, 107, 104, 105, 107,
    105, 106, 107, 109, 110, 107, 110, 111, 107, 111, 112, 107, 112, 113, 114, 107, 114, 115, 107, 115, 116, 107,
     11,  12,  13, 107, 116, 117, 107, 117, 118, 107, 118, 119, 120, 107, 120, 121, 107, 121, 122, 107, 108, 122,
    123, 131, 123, 124, 131, 124, 125, 126, 131,  13,  14, 131, 126, 127, 131, 127, 128, 129, 131, 129, 130, 131,
    130, 131, 132, 133, 131, 133, 134, 131, 132, 134, 135, 136, 132, 136, 137, 132, 137, 138, 132,  14,  15, 132,
    138, 139, 132, 139, 140, 132, 140, 141, 132, 141, 142, 143, 108, 143, 144, 153, 144, 145, 153, 145, 146, 153,
    154, 146, 147, 154, 147, 148, 149, 154,  15,  16, 154, 149, 150, 154, 150, 151, 154, 151, 152, 154, 152, 153,
    154, 155, 156, 154, 156, 157, 154, 157, 158, 154, 158, 159, 154, 159, 160, 161, 154,   1,   2, 154, 161, 162,
    154, 162, 163, 108, 163, 164, 165, 176, 165, 166, 176, 177, 166, 167, 177, 167, 168, 177, 168, 169, 177, 169,
    170, 177, 170, 171, 172, 177,  16,  17, 177, 172, 173, 177, 173, 174, 177, 174, 175, 177, 175, 176, 177, 178,
    179, 177, 179, 180, 177, 180, 181, 177, 181, 182, 177, 182, 183, 177,  17,  18,  19, 108, 183, 184, 185, 200,
    185, 186, 200, 186, 187, 200, 187, 188, 200, 188, 189, 200, 189, 190, 191, 200, 191, 192, 200, 192, 193, 194,
    200,  19,  20, 200, 194, 195, 200, 195, 196, 200, 196, 197, 200, 197, 198, 200, 198, 199, 200, 201, 202, 200,
    202, 203, 200, 201, 203, 204, 201, 204, 205, 108,  20,  21, 223, 205, 206, 207, 223, 207, 208, 223, 208, 209,
    223, 209, 210, 223, 210, 211, 223, 211, 212, 223, 212, 213, 214, 223, 214, 215, 223, 215, 216, 217, 223,  21,
     22, 223, 217, 218, 223, 218, 219, 223, 219, 220, 223, 220, 221, 223, 224, 221, 222, 223, 224, 225, 224, 225,
    226, 108, 226, 227, 245, 227, 228, 245,  22,  23, 245, 228, 229, 230, 245, 230, 231, 245, 231, 232, 245, 232,
    233, 245, 233, 234, 245, 246, 234, 235, 236, 246, 236, 237, 246, 237, 238, 246, 238, 239, 246,  23,  24,  25,
    246, 239, 240, 246, 240, 241, 246, 241, 242, 243, 246, 243, 244, 246, 244, 245, 246, 247, 248, 246, 248, 249,
    246, 249, 250, 251, 246,  25,  26, 246, 251, 252, 246, 252, 253, 246, 247, 253, 254, 255, 247, 255, 256, 247,
    256, 257, 247, 257, 258, 247, 258, 259, 247, 259, 260, 261, 247, 261, 262, 108,  26,  27,  45, 262, 263,  45,
    263, 264,  45, 264, 265,  45, 265, 266, 267,  45, 267, 268,  45, 268, 269,  45, 270, 271,  45, 271, 272,  45,
    272, 273, 274,  45,   2,   3,  45, 274, 275,  45, 275, 276, 277,  45,  46, 277, 278,  46, 278, 279,  46, 279,
    280,  46, 280, 281,  46, 281, 282, 108, 282, 283, 284, 292, 284, 285, 292,  27,  28, 292, 285, 286, 292, 286,
    287, 292, 287, 288, 292, 288, 289, 290, 292, 290, 291, 292, 291, 292, 293, 294, 293, 294, 295, 293, 295, 296,
    297, 293,  28,  29, 293, 297, 298, 293, 298, 299, 293, 299, 300, 293, 300, 301, 293, 301, 302, 303, 108, 303,
    304, 314, 304, 305, 314, 315, 305, 306, 315, 306, 307, 315,  29,  30, 315, 307, 308, 315, 308, 309, 310, 315,
    310, 311, 315, 311, 312, 315, 312, 313, 315, 313, 314, 315, 316, 317, 315, 317, 318, 315,  30,  31,  32, 315,
    318, 319, 320, 315, 320, 321, 315, 321, 322, 315, 322, 323, 108, 323, 324, 338, 324, 325, 326, 338, 326, 327,
    338, 327, 328, 338, 328, 329, 330, 338,  32,  33, 338, 330, 331, 338, 339, 331, 332, 339, 332, 333, 339, 333,
    334, 335, 339, 335, 336, 339, 336, 337, 339, 337, 338, 339, 340, 339, 340, 341, 342, 339,  33,  34, 339, 342,
    343, 339, 343, 344, 108, 344, 345, 361, 345, 346, 361, 346, 347, 361, 347, 348, 349, 361, 349, 350, 361, 350,
    351, 361, 351, 352, 361,  34,  35, 361, 352, 353, 361, 353, 354, 355, 361, 355, 356, 361, 356, 357, 361, 357,
    358, 361, 358, 359, 361, 359, 360, 361, 362, 363, 361, 362, 363, 364, 362,  35,  36, 108, 364, 365, 384, 365,
    366, 384, 366, 367, 368, 384, 368, 369, 384, 369, 370, 384, 370, 371, 384, 371, 372, 384, 372, 373, 374, 384,
     36,  37,  38, 384, 374, 375, 384, 375, 376, 384, 376, 377, 378, 384, 378, 379, 384, 379, 380, 384, 380, 381,
    384, 385, 381, 382, 385, 382, 383, 384, 385, 386, 108,   3,   4, 407, 386, 387, 388, 407, 388, 389, 407, 389,
    390, 407, 390, 391, 407, 391, 392, 407, 392, 393, 394, 407, 394, 395, 407, 395, 396, 407, 396, 397, 407,  38,
     39, 407, 397, 398, 407, 398, 399, 400, 407, 408, 400, 401, 408, 401, 402, 408, 402, 403, 408, 403, 404, 408,
    404, 405, 406, 408, 406, 407, 408, 409, 108,  39,  40, 269, 409, 410, 269, 410, 411, 269, 411, 412, 413, 269,
    413, 414, 269, 414, 415, 269, 415, 416, 269, 416, 417, 269, 417, 418, 419, 269, 419, 420, 269, 270,  40,  41,
    270, 420, 421, 270, 421, 422, 270, 422, 423, 270, 423, 424, 425, 270, 425, 426, 270, 426, 427, 270, 427, 428,
    108, 428, 429, 432, 429, 430, 432,  41,  42, 432, 430, 431, 432, 433, 434, 432, 434, 435, 432, 435, 436, 432,
    436, 437, 432, 437, 438, 439, 432, 439, 440, 432, 440, 441, 432, 441, 442, 432,  42,  43,  44, 432, 442, 443,
    432, 433, 443, 444, 445, 433, 445, 446, 433, 446, 447, 433, 447, 448, 108, 448, 449, 454, 449, 450, 451, 454,
    451, 452, 454, 452, 453, 454,  44,  45, 454, 453, 454, 455, 456, 454, 455, 456, 457, 458, 455, 458, 459, 455,
    459, 460, 455, 460, 461, 455, 461, 462, 455, 462, 463, 455, 463, 464, 465, 455,  46,  47, 455, 465, 466, 455,
    466, 467, 455, 467, 468, 108, 109, 468, 469, 477, 469, 470, 471, 477, 471, 472, 477, 472, 473, 477, 473, 474,
    477, 474, 475, 477,  47,  48, 477, 475, 476, 477, 478, 479, 477, 479, 480, 477, 480, 481, 477, 481, 482, 477,
    482, 483, 484, 477, 484, 485, 477, 485, 486, 477, 486, 487, 477, 478,  48,  49, 478, 487, 488, 109, 488, 489,
    499, 489, 490, 491, 499, 491, 492, 499, 492, 493, 499, 493, 494, 499, 494, 495, 499, 495, 496, 497, 499, 497,
    498, 499,   4,   5,   6, 499, 500, 498, 499, 500, 501, 500, 501, 502, 500, 502, 503, 504, 500, 504, 505, 500,
    505, 506, 500, 506, 507, 500, 507, 508, 500, 508, 509, 510, 511, 109,  49,  50,  51, 523, 511, 512, 523, 512,
    513, 523, 513, 514, 523, 514, 515, 523, 515, 516, 517, 523, 517, 518, 523, 518, 519, 523, 519, 520, 523, 520,
    521, 523,  51,  52, 523, 521, 522, 523, 524, 525, 523, 525, 526, 523, 526, 527, 523, 524, 527, 528, 524, 528,
    529, 530, 524, 530, 531, 109, 531, 532, 545, 532, 533, 545,  52,  53, 545, 533, 534, 545, 534, 535, 536, 545,
    536, 537, 545, 537, 538, 545, 538, 539, 545, 546, 539, 540, 546, 540, 541, 546, 541, 542, 543, 546, 543, 544,
    546,  53,  54, 546, 544, 545, 546, 547, 546, 547, 548, 546, 548, 549, 550, 546, 550, 551, 109, 551, 552, 567,
    568, 552, 553, 568, 553, 554, 568, 554, 555, 556, 568,  54,  55, 568, 556, 557, 568, 557, 558, 568, 558, 559,
    568, 559, 560, 568, 560, 561, 562, 568, 562, 563, 568, 563, 564, 568, 564, 565, 568, 565, 566, 568,  55,  56,
    568, 566, 567, 568, 569, 570, 568, 569, 570, 571, 109, 571, 572, 590, 572, 573, 590, 573, 574, 590, 574, 575,
    576, 590, 576, 577, 590, 577, 578, 590,  56,  57,  58, 590, 578, 579, 590, 579, 580, 590, 580, 581, 582, 590,
    591, 582, 583, 591, 583, 584, 591, 584, 585, 591, 585, 586, 591, 586, 587, 588, 591, 588, 589, 591,  58,  59,
    591, 589, 590, 109, 591, 592, 612, 592, 593, 612, 613, 593, 594, 595, 613, 595, 596, 613, 596, 597, 613, 597,
    598, 613, 598, 599, 613, 599, 600, 613,  59,  60, 613, 602, 603, 613, 603, 604, 613, 604, 605, 613, 605, 606,
    613, 606, 607, 608, 613, 608, 609, 613, 609, 610, 613, 610, 611, 613, 611, 612, 613, 614, 600, 601, 602, 614,
    615, 614, 615, 616, 614, 616, 617, 618, 614, 618, 619, 614, 619, 620, 614, 620, 621, 614, 621, 622, 614, 622,
    623, 624
    // *INDENT-ON*
};


void
testFindAdjacentIf()
{
    const auto findAdjacentIncreasing =
        [] ( const auto& container ) {
            return findAdjacentIf( container.begin(), container.end(),
                                   [] ( const auto& current, const auto& next ) { return current + 1 == next; } );
        };

    /* Empty */
    {
        std::vector<int> values;
        const auto [sequenceBegin, sequenceEnd] = findAdjacentIncreasing( values );
        REQUIRE( sequenceBegin == values.end() );
        REQUIRE( sequenceEnd == values.end() );
    }

    /* One */
    {
        std::vector<int> values = { 1 };
        const auto [sequenceBegin, sequenceEnd] = findAdjacentIncreasing( values );
        REQUIRE( sequenceBegin == values.end() );
        REQUIRE( sequenceEnd == values.end() );
    }

    /* Consecutive */
    {
        for ( size_t size : { 2, 3, 10, 20 } ) {
            std::vector<int> values( size );
            std::iota( values.begin(), values.end(), 1 );
            const auto [sequenceBegin, sequenceEnd] = findAdjacentIncreasing( values );
            REQUIRE( sequenceBegin == values.begin() );
            REQUIRE( sequenceEnd == values.end() );
        }
    }

    /* Non-consecutive because of inversed order */
    {
        for ( size_t size : { 2, 3, 10, 20 } ) {
            std::vector<int> values( size );
            std::iota( values.rbegin(), values.rend(), 1 );
            const auto [sequenceBegin, sequenceEnd] = findAdjacentIncreasing( values );
            REQUIRE( sequenceBegin == values.end() );
            REQUIRE( sequenceEnd == values.end() );
        }
    }

    /* Partially consecutive */
    {
        std::vector<int> values = { 0, 10, 11, 100 };
        const auto [sequenceBegin, sequenceEnd] = findAdjacentIncreasing( values );
        REQUIRE( sequenceBegin == values.begin() + 1 );
        REQUIRE( sequenceEnd == values.end() - 1 );
    }

    /* Consecutive end-sequence */
    {
        std::vector<int> values = { 0, 3, 10, 11};
        const auto [sequenceBegin, sequenceEnd] = findAdjacentIncreasing( values );
        REQUIRE( sequenceBegin == values.begin() + 2 );
        REQUIRE( sequenceEnd == values.end() );
    }
}


void
testFetchNext()
{
    FetchNext strategy;
    strategy.fetch( 23 );
    REQUIRE_EQUAL( strategy.prefetch( 3 ), std::vector<size_t>( { 24, 25, 26 } ) );
    REQUIRE_EQUAL( strategy.prefetch( 3 ), std::vector<size_t>( { 24, 25, 26 } ) );
    strategy.fetch( 23 );
    REQUIRE_EQUAL( strategy.prefetch( 3 ), std::vector<size_t>( { 24, 25, 26 } ) );
    strategy.fetch( 24 );
    REQUIRE_EQUAL( strategy.prefetch( 3 ), std::vector<size_t>( { 25, 26, 27 } ) );
    strategy.fetch( 1 );
    REQUIRE_EQUAL( strategy.prefetch( 5 ), std::vector<size_t>( { 2, 3, 4, 5, 6 } ) );
}


template<typename FetchingStrategy>
void
testLinearAccess()
{
    FetchingStrategy strategy;
    strategy.fetch( 23 );
    {
        const auto prefetched = strategy.prefetch( 3 );
        REQUIRE_EQUAL( prefetched, std::vector<size_t>( { 24, 25, 26 } ) );
        REQUIRE_EQUAL( prefetched, std::vector<size_t>( { 24, 25, 26 } ) );
    }

    /* Strictly speaking, this is not a consecutive access and therefore an empty list could be correct.
     * However, duplicate fetches should not alter the returned prefetch list so that, if there was not
     * enough time in the last call to prefetch everything, now, on this call, those missing prefetch suggestions
     * can be added to the cache. */
    strategy.fetch( 23 );
    REQUIRE_EQUAL( strategy.prefetch( 3 ), std::vector<size_t>( { 24, 25, 26 } ) );

    for ( size_t index = 24; index < 40; ++index ) {
        strategy.fetch( index );

        const auto maxPrefetchCount = 8;
        std::vector<size_t> expectedResult( maxPrefetchCount );
        std::iota( expectedResult.begin(), expectedResult.end(), index + 1 );
        REQUIRE_EQUAL( strategy.prefetch( maxPrefetchCount ), expectedResult );
    }

    /* A single random seek after a lot of consecutive ones should not result in an empty list at once. */
    strategy.fetch( 3 );
    for ( auto prefetchCount = 1; prefetchCount < 10; ++prefetchCount ) {
        const auto prefetched = strategy.prefetch( prefetchCount );
        REQUIRE( !prefetched.empty() );
        if ( !prefetched.empty() ) {
            REQUIRE_EQUAL( prefetched.front(), size_t( 4 ) );
        }
    }

    /* After a certain amount of non-consecutive fetches, an empty prefetch list should be returned. */
    {
        const size_t prefetchCount = 10;
        for ( size_t i = 0; i < 10000 * prefetchCount; i += prefetchCount ) {
            strategy.fetch( i );
        }
        REQUIRE_EQUAL( strategy.prefetch( prefetchCount ), std::vector<size_t>() );
    }
}


void
testFetchMulti()
{
    /* For purely sequential access like decoding a file with pragzip without any seek, FetchNextMulti should
     * decay into FetchNextSmart. With this, it is proven that it does not degrade parallelized gzip decoding
     * performance for pragzip -d while at the same time improving performance for multi-stream sequential access
     * when used as ratarmount backend. */
    FetchNextSmart fetchNextSmart;
    FetchNextMulti fetchNextMulti;

    for ( size_t i = 0; i < 100; ++i ) {
        fetchNextSmart.fetch( i );
        fetchNextMulti.fetch( i );
        REQUIRE_EQUAL( fetchNextSmart.prefetch( 8 ), fetchNextMulti.prefetch( 8 ) );
    }
}


template<typename FetchingStrategy>
void
testInterleavedLinearAccess( size_t streamCount )
{
    const size_t memorySize{ 3 };
    FetchingStrategy strategy( memorySize, streamCount );

    REQUIRE_EQUAL( strategy.prefetch( 3 ), std::vector<size_t>() );

    if ( streamCount == 0 ) {
        throw std::invalid_argument( "Counts must be non-zero." );
    }

    std::vector<size_t> secondValues;
    for ( size_t stream = 0; stream < streamCount; ++stream ) {
        secondValues.push_back( stream * 1000 + 1 );
    }

    /* The very first accesses should prefetch as far as possible. */
    for ( size_t stream = 0; stream < streamCount; ++stream ) {
        strategy.fetch( stream * 1000 );
        const auto maxAmountToPrefetch = streamCount;

        std::vector<std::vector<size_t> > prefetchedPerStream( stream + 1 );
        for ( size_t i = 0; i < prefetchedPerStream.size(); ++i ) {
            for ( size_t j = 0; j < maxAmountToPrefetch; ++j ) {
                prefetchedPerStream[i].push_back( i * 1000 + 1 + j );
            }
        }

        auto expected = interleave( prefetchedPerStream );
        expected.resize( maxAmountToPrefetch );
        REQUIRE_EQUAL( strategy.prefetch( maxAmountToPrefetch ), expected );
    }

    /* After memory size * stream count accesses, the maximum should be prefetched. */
    for ( size_t i = 1; i < memorySize; ++i ) {
        for ( size_t stream = 0; stream < streamCount; ++stream ) {
            strategy.fetch( stream * 1000 + i );
        }
    }

    std::vector<size_t> interleavedPrefetches;
    for ( size_t i = memorySize; i < memorySize + 4U; ++i ) {
        for ( size_t stream = 0; stream < streamCount; ++stream ) {
            interleavedPrefetches.push_back( stream * 1000 + i );
        }
    }

    REQUIRE_EQUAL( strategy.prefetch( 4 * streamCount ),
                   std::vector<size_t>( interleavedPrefetches.begin(),
                                        interleavedPrefetches.begin() + 4 * streamCount ) );
}


struct BlockFetcherStatistics
{
public:
    using Result = size_t;
    using BlockCache = Cache</** block offset in bits */ size_t, Result>;

public:
    [[nodiscard]] double
    cacheHitRate() const
    {
        return static_cast<double>( cache.hits + prefetchCache.hits + prefetchDirectHits )
               / static_cast<double>( gets );
    }

    [[nodiscard]] double
    uselessPrefetches() const
    {
        const auto totalFetches = prefetchCount + onDemandFetchCount;
        if ( totalFetches == 0 ) {
            return 0;
        }
        return static_cast<double>( prefetchCache.unusedEntries ) / static_cast<double>( totalFetches );
    }

    [[nodiscard]] std::string
    printShort() const
    {
        std::stringstream out;
        out << "Hit Rate : " << cacheHitRate() * 100
            << " %  Useless Prefetches : " << uselessPrefetches() * 100 << " %";
        return out.str();
    }

    [[nodiscard]] std::string
    print() const
    {
        std::stringstream out;
        out << "\n   Parallelization         : " << parallelization
            << "\n   Blocks"
            << "\n       Total Accesses      : " << gets
            << "\n       Total Existing      : " << blockCount
            << "\n       Total Fetched       : " << prefetchCount + onDemandFetchCount
            << "\n       Prefetched          : " << prefetchCount
            << "\n       Fetched On-demand   : " << onDemandFetchCount
            << "\n   Cache"
            << "\n       Capacity            : " << cache.capacity
            << "\n       Hits                : " << cache.hits
            << "\n       Misses              : " << cache.misses
            << "\n       Unused Entries      : " << cache.unusedEntries;
        if ( prefetchCache.capacity > 0 ) {
            out << "\n   Prefetch Cache"
                << "\n       Capacity            : " << prefetchCache.capacity
                << "\n       Hits                : " << prefetchCache.hits
                << "\n       Misses              : " << prefetchCache.misses
                << "\n       Unused Entries      : " << prefetchCache.unusedEntries
                << "\n       Prefetch Queue Hit  :" << prefetchDirectHits;
        }
        /* I think we can concentrate on optimizing these two values while keeping the cache sizes within memory
         * constraints. */
        out << "\n   Hit Rate                : " << cacheHitRate() * 100 << " %"
            << "\n   Useless Prefetches      : " << uselessPrefetches() * 100 << " %"
            << "\n";
        return out.str();
    }

public:
    size_t parallelization{ 0 };
    size_t blockCount{ 0 };

    typename BlockCache::Statistics cache;
    typename BlockCache::Statistics prefetchCache;

    size_t gets{ 0 };
    size_t onDemandFetchCount{ 0 };
    size_t prefetchCount{ 0 };
    size_t prefetchDirectHits{ 0 };
};


template<typename FetchingStrategy>
class SimpleBlockFetcher
{
public:
    using Result = size_t;

public:
    explicit
    SimpleBlockFetcher( size_t parallelization ) :
        m_parallelization( parallelization )
    {}

    /**
     * Fetches, prefetches, caches, and returns result.
     */
    Result
    get( size_t dataBlockIndex )
    {
        /* Access cache before data might get evicted! */
        const auto result = m_cache.get( dataBlockIndex );

        m_fetchingStrategy.fetch( dataBlockIndex );
        auto blocksToPrefetch = m_fetchingStrategy.prefetch( m_parallelization - 1 /* fetched block */ );

        for ( auto blockIndexToPrefetch : blocksToPrefetch ) {
            /* Do not prefetch already cached/prefetched blocks. */
            if ( m_cache.test( blockIndexToPrefetch ) ) {
                continue;
            }

            ++m_prefetchCount;
            /* Put directly into cache, assuming no computation time or rather because the multithreading is stripped */
            m_cache.insert( blockIndexToPrefetch, blockIndexToPrefetch );
        }

        /* Return cached result */
        if ( result ) {
            return *result;
        }

        m_cache.insert( dataBlockIndex, dataBlockIndex );
        return dataBlockIndex;
    }

    [[nodiscard]] size_t
    prefetchCount() const
    {
        return m_prefetchCount;
    }

    void
    resetPrefetchCount()
    {
        m_prefetchCount = 0;
    }

    [[nodiscard]] auto&
    cache()
    {
        return m_cache;
    }

    [[nodiscard]] const auto&
    cache() const
    {
        return m_cache;
    }

private:
    size_t m_prefetchCount{ 0 };

    const size_t m_parallelization;

    Cache</** block offset in bits */ size_t, Result> m_cache{ 16 + m_parallelization };
    FetchingStrategy m_fetchingStrategy;
};


/**
 * Trimmed down BlockFetcher class without the bzip2 decoding and without threading.
 * Threading is simulated and assumes that all task finish in equal time.
 * Conversion between block offsets and block indexes is obviously also stripped.
 */
template<typename FetchingStrategy>
class BlockFetcher
{
public:
    using Result = size_t;
    using BlockCache = Cache</** block offset in bits */ size_t, Result>;

public:
    explicit
    BlockFetcher( size_t                parallelization,
                  std::optional<size_t> totalCacheSize = {},
                  size_t                prefetchCacheSize = 0,
                  bool                  activelyAvoidPrefetchCachePollution = false,
                  size_t                blockCount = std::numeric_limits<size_t>::max() ) :
        m_parallelization( parallelization ),
        m_blockCount( blockCount ),
        m_activelyAvoidPrefetchCachePollution( activelyAvoidPrefetchCachePollution ),
        m_cache( ( totalCacheSize ? *totalCacheSize : 16 + parallelization ) - prefetchCacheSize ),
        m_prefetchCache( prefetchCacheSize )
    {}

    /**
     * Fetches, prefetches, caches, and returns result.
     */
    Result
    get( size_t dataBlockIndex )
    {
    #if 0
        std::cerr << "[BlockFetcher::get] block index " << dataBlockIndex << " ";
        if ( m_cache.test( dataBlockIndex ) ) {
            std::cerr << "found in cache";
        } else if ( m_prefetchCache.test( dataBlockIndex ) ) {
            std::cerr << "found in prefetch cache";
        } else {
            std::cerr << "NOT found in any cache";
        }
        std::cerr << "\n";
    #endif

        ++m_statistics.gets;

        auto result = takeFromPrefetchQueue( dataBlockIndex );

        /* Access cache before data might get evicted! */
        if ( !result ) {
            if ( ( m_prefetchCache.capacity() == 0 ) || m_cache.test( dataBlockIndex ) ) {
                result = m_cache.get( dataBlockIndex );
            } else {
                result = m_prefetchCache.get( dataBlockIndex );
                if ( result ) {
                    m_prefetchCache.evict( dataBlockIndex );
                    m_cache.insert( dataBlockIndex, *result );
                }
            }
        }

        processReadyPrefetches();
        prefetchNewBlocks( dataBlockIndex );

        /* Return cached result */
        if ( result ) {
            return *result;
        }

        /* Cache miss */
        ++m_statistics.onDemandFetchCount;
        m_cache.insert( dataBlockIndex, dataBlockIndex );
        return dataBlockIndex;
    }

    [[nodiscard]] size_t
    prefetchCount() const
    {
        return m_statistics.prefetchCount;
    }

    void
    resetPrefetchCount()
    {
        m_statistics.prefetchCount = 0;
    }

    [[nodiscard]] auto&
    cache()
    {
        return m_cache;
    }

    [[nodiscard]] const auto&
    cache() const
    {
        return m_cache;
    }

    [[nodiscard]] auto&
    prefetchCache()
    {
        return m_prefetchCache;
    }

    [[nodiscard]] BlockFetcherStatistics
    statistics() const
    {
        auto result = m_statistics;
        result.parallelization = m_parallelization;
        result.blockCount = m_blockCount;
        result.cache = m_cache.statistics();
        result.prefetchCache = m_prefetchCache.statistics();
        return result;
    }

    [[nodiscard]] std::string
    printShortStats() const
    {
        return statistics().printShort();
    }

    [[nodiscard]] std::string
    printStats() const
    {
        return statistics().print();
    }

private:
    [[nodiscard]] std::optional<Result>
    takeFromPrefetchQueue( size_t dataBlockIndex )
    {
        auto match = std::find( m_prefetching.begin(), m_prefetching.end(), dataBlockIndex );
        if ( match == m_prefetching.end() ) {
            return std::nullopt;
        }

        ++m_statistics.prefetchDirectHits;
        const auto result = *match;
        m_prefetching.erase( match );
        return result;
    }

    void
    processReadyPrefetches()
    {
        for ( const auto dataBlockIndex : m_prefetching ) {
            auto& cacheToUse = m_prefetchCache.capacity() > 0 ? m_prefetchCache : m_cache;
            cacheToUse.insert( dataBlockIndex, dataBlockIndex );
        }
        m_prefetching.clear();
    }

    void
    prefetchNewBlocks( size_t dataBlockIndex )
    {
        m_fetchingStrategy.fetch( dataBlockIndex );
        const auto blocksToPrefetch = m_fetchingStrategy.prefetch( /* maxAmountToPrefetch */ m_parallelization );

        const auto touchInCacheIfExists =
            [this] ( size_t blockIndex )
            {
                if ( m_prefetchCache.test( blockIndex ) ) {
                    m_prefetchCache.touch( blockIndex );
                }
                if ( m_cache.test( blockIndex ) ) {
                    m_cache.touch( blockIndex );
                }
            };

        /* Touch all blocks to be prefetched to avoid evicting them while doing the prefetching of other blocks! */
        if ( m_activelyAvoidPrefetchCachePollution ) {
            for ( auto blockIndexToPrefetch : blocksToPrefetch ) {
                touchInCacheIfExists( blockIndexToPrefetch );
            }
        }

        for ( auto blockIndexToPrefetch : blocksToPrefetch ) {
            if ( m_prefetching.size() + /* thread with the requested block */ 1 >= m_parallelization ) {
                break;
            }

            if ( blockIndexToPrefetch == dataBlockIndex ) {
                throw std::logic_error( "The fetching strategy should not return the "
                                        "last fetched block for prefetching!" );
            }

            if ( blockIndexToPrefetch >= m_blockCount ) {
                continue;
            }

            /* Do not prefetch already cached/prefetched blocks or block indexes which are not yet in the block map. */
            touchInCacheIfExists( blockIndexToPrefetch );
            if ( ( contains( m_prefetching, blockIndexToPrefetch ) )
                 || m_cache.test( blockIndexToPrefetch )
                 || m_prefetchCache.test( blockIndexToPrefetch ) )
            {
                continue;
            }

            /* Avoid cache pollution by stopping prefetching when we would evict usable results. */
            if ( m_activelyAvoidPrefetchCachePollution ) {
                if ( m_prefetchCache.size() >= m_prefetchCache.capacity() ) {
                    const auto toBeEvicted = m_prefetchCache.cacheStrategy().nextEviction();
                    if ( toBeEvicted && contains( blocksToPrefetch, *toBeEvicted ) ) {
                        break;
                    }
                }
            }

            ++m_statistics.prefetchCount;
            m_prefetching.emplace_back( blockIndexToPrefetch );
        }
    }

private:
    BlockFetcherStatistics m_statistics;

    const size_t m_parallelization;
    const size_t m_blockCount;
    const bool m_activelyAvoidPrefetchCachePollution;

    BlockCache m_cache;

    /**
     * This mockup BlockFetcher can be configured to use a separate prefetch cache in order to avoid
     * cache pollution for the cache of actually accessed blocks.
     * @ref m_cache and @ref m_prefetchCache should contain no duplicate values! After after a cache hit
     * inside @ref m_prefetchCache, that value should be moved into @ref m_cache.
     */
    BlockCache m_prefetchCache;

    FetchingStrategy m_fetchingStrategy;
    std::vector<size_t> m_prefetching;
};


void
benchmarkFetchNext()
{
    std::cerr << "FetchNext strategy:\n";

    const size_t parallelization = 16;
    SimpleBlockFetcher<FetchNext> blockFetcher( parallelization );
    const auto cacheSize = blockFetcher.cache().capacity();

    size_t indexToGet = 0;

    /* Consecutive access should basically only result in a single miss at the beginning, rest is prefetched! */
    {
        constexpr size_t nConsecutive = 1000;
        for ( size_t i = 0; i < nConsecutive; ++i ) {
            blockFetcher.get( indexToGet + i );
        }
        indexToGet += nConsecutive;

        const auto hits = blockFetcher.cache().statistics().hits;
        const auto misses = blockFetcher.cache().statistics().misses;
        const auto unusedEntries = blockFetcher.cache().statistics().unusedEntries;
        const auto prefetches = blockFetcher.prefetchCount();

        std::cerr << "  Sequential access: prefetches: " << prefetches
                  << ", misses: " << misses << ", hits: " << hits
                  << ", unused cache entries:" << unusedEntries << "\n";

        REQUIRE_EQUAL( hits + misses, nConsecutive );
        REQUIRE_EQUAL( misses, size_t( 1 ) );
        REQUIRE_EQUAL( prefetches,
                       nConsecutive + parallelization
                       - /* first element does not get prefetched */ 1
                       - /* at the tail end only parallelization - 1 are prefetched */ 1 );
    }

    /* Even for random accesses always prefetch the next n elements */
    {
        indexToGet += parallelization;
        const size_t nRandomCoolDown = blockFetcher.cache().capacity();
        for ( size_t i = 0; i < nRandomCoolDown; ++i  ) {
            blockFetcher.get( indexToGet + i * parallelization );
        }
        indexToGet += nRandomCoolDown * cacheSize * 2;

        blockFetcher.resetPrefetchCount();
        blockFetcher.cache().resetStatistics();

        constexpr size_t nRandom = 1000;
        for ( size_t i = 0; i < nRandom; ++i ) {
            blockFetcher.get( indexToGet + i * parallelization );
        }

        const auto hits = blockFetcher.cache().statistics().hits;
        const auto misses = blockFetcher.cache().statistics().misses;
        const auto unusedEntries = blockFetcher.cache().statistics().unusedEntries;
        const auto prefetches = blockFetcher.prefetchCount();

        std::cerr << "  Random access: prefetches: " << prefetches
                  << ", misses: " << misses << ", hits: " << hits
                  << ", unused cache entries:" << unusedEntries << "\n";

        REQUIRE_EQUAL( misses, nRandom );
        REQUIRE_EQUAL( hits, size_t( 0 ) );
        REQUIRE_EQUAL( prefetches, nRandom * ( parallelization - 1 ) );
    }

    /* Always fetch the next n elements even after changing from random access to consecutive again. */
    {
        blockFetcher.resetPrefetchCount();
        blockFetcher.cache().resetStatistics();

        blockFetcher.get( 0 );

        REQUIRE_EQUAL( blockFetcher.cache().statistics().misses, size_t( 1 ) );
        REQUIRE_EQUAL( blockFetcher.cache().statistics().hits, size_t( 0 ) );
        REQUIRE_EQUAL( blockFetcher.prefetchCount(), parallelization - 1 );

        blockFetcher.get( 1 );

        REQUIRE_EQUAL( blockFetcher.cache().statistics().misses, size_t( 1 ) );
        REQUIRE_EQUAL( blockFetcher.cache().statistics().hits, size_t( 1 ) );
        REQUIRE_EQUAL( blockFetcher.prefetchCount(), parallelization );
    }
}


void
benchmarkFetchNextSmart()
{
    std::cerr << "FetchNextSmart strategy:\n";

    const auto printShortStats =
        [] ( const auto& blockFetcher )
        {
            const auto& cacheStats = blockFetcher.cache().statistics();
            std::stringstream out;
            out << "prefetches: " << blockFetcher.prefetchCount()
                << ", misses: " << cacheStats.misses
                << ", hits: " << cacheStats.hits
                << ", unused cache entries:" << cacheStats.unusedEntries;
            return out.str();
        };

    const size_t parallelization = 16;
    SimpleBlockFetcher<FetchNextSmart> blockFetcher( parallelization );
    const auto cacheSize = blockFetcher.cache().capacity();

    size_t indexToGet = 0;

    /* Consecutive access should basically only result in a single miss at the beginning, rest is prefetched! */
    {
        constexpr size_t nConsecutive = 1000;

        blockFetcher.get( indexToGet );

        REQUIRE_EQUAL( blockFetcher.cache().statistics().misses, size_t( 1 ) );
        REQUIRE_EQUAL( blockFetcher.cache().statistics().hits, size_t( 0 ) );
        REQUIRE_EQUAL( blockFetcher.prefetchCount(), parallelization - 1 );

        blockFetcher.get( indexToGet + 1 );

        REQUIRE_EQUAL( blockFetcher.cache().statistics().misses, size_t( 1 ) );
        REQUIRE_EQUAL( blockFetcher.cache().statistics().hits, size_t( 1 ) );
        REQUIRE_EQUAL( blockFetcher.prefetchCount(), parallelization );

        for ( size_t i = 0; i < nConsecutive - 2; ++i ) {
            blockFetcher.get( indexToGet + 2 + i );
        }
        indexToGet += nConsecutive;

        const auto hits = blockFetcher.cache().statistics().hits;
        const auto misses = blockFetcher.cache().statistics().misses;
        const auto prefetches = blockFetcher.prefetchCount();

        std::cerr << "  Sequential access: " << printShortStats( blockFetcher ) << "\n";

        REQUIRE_EQUAL( hits + misses, nConsecutive );
        REQUIRE_EQUAL( misses, size_t( 1 ) );
        REQUIRE_EQUAL( prefetches,
                       nConsecutive + parallelization
                       - /* first element does not get prefetched */ 1
                       - /* at the tail end only parallelization - 1 are prefetched */ 1 );
    }

    /* Random accesses should after a time, not prefetch anything anymore. */
    {
        indexToGet += parallelization;
        const size_t nRandomCoolDown = blockFetcher.cache().capacity();
        for ( size_t i = 0; i < nRandomCoolDown; ++i  ) {
            blockFetcher.get( indexToGet + i * parallelization );
        }
        indexToGet += nRandomCoolDown * cacheSize * 2;

        blockFetcher.resetPrefetchCount();
        blockFetcher.cache().resetStatistics();

        constexpr size_t nRandom = 1000;
        for ( size_t i = 0; i < nRandom; ++i ) {
            blockFetcher.get( indexToGet + i * parallelization );
        }

        const auto hits = blockFetcher.cache().statistics().hits;
        const auto misses = blockFetcher.cache().statistics().misses;
        const auto prefetches = blockFetcher.prefetchCount();

        std::cerr << "  Random access: " << printShortStats( blockFetcher ) << "\n";

        REQUIRE_EQUAL( misses, nRandom );
        REQUIRE_EQUAL( hits, size_t( 0 ) );
        REQUIRE_EQUAL( prefetches, size_t( 0 ) );
    }

    /* Double access to same should be cached. */
    {
        blockFetcher.resetPrefetchCount();
        blockFetcher.cache().resetStatistics();

        blockFetcher.get( 100 );

        REQUIRE_EQUAL( blockFetcher.cache().statistics().misses, size_t( 1 ) );
        REQUIRE_EQUAL( blockFetcher.cache().statistics().hits, size_t( 0 ) );
        REQUIRE_EQUAL( blockFetcher.prefetchCount(), size_t( 0 ) );

        blockFetcher.get( 100 );

        REQUIRE_EQUAL( blockFetcher.cache().statistics().misses, size_t( 1 ) );
        REQUIRE_EQUAL( blockFetcher.cache().statistics().hits, size_t( 1 ) );
        REQUIRE_EQUAL( blockFetcher.prefetchCount(), size_t( 0 ) );
    }

    /* After random accesses, consecutive accesses, should start prefetching again. */
    {
        blockFetcher.resetPrefetchCount();
        blockFetcher.cache().resetStatistics();

        /* First access still counts as random one because last access was to a very high index! */
        blockFetcher.get( 0 );

        REQUIRE_EQUAL( blockFetcher.cache().statistics().misses, size_t( 1 ) );
        REQUIRE_EQUAL( blockFetcher.cache().statistics().hits, size_t( 0 ) );
        REQUIRE_EQUAL( blockFetcher.prefetchCount(), size_t( 0 ) );

        /* After 1st consecutive access begin to slowly prefetch with exponential speed up to maxPrefetchCount! */
        blockFetcher.get( 1 );

        std::cerr << "  After 2nd new consecutive access: " << printShortStats( blockFetcher ) << "\n";

        REQUIRE_EQUAL( blockFetcher.cache().statistics().misses, size_t( 2 ) );
        REQUIRE_EQUAL( blockFetcher.cache().statistics().hits, size_t( 0 ) );
        REQUIRE( blockFetcher.prefetchCount() >= 1 );

        blockFetcher.get( 2 );

        std::cerr << "  After 3rd new consecutive access: " << printShortStats( blockFetcher ) << "\n";

        REQUIRE_EQUAL( blockFetcher.cache().statistics().misses, size_t( 2 ) );
        REQUIRE_EQUAL( blockFetcher.cache().statistics().hits, size_t( 1 ) );
        REQUIRE( blockFetcher.prefetchCount() >= 1 );

        /* At the latest after four consecutive acceses should it prefetch at full parallelization! */
        blockFetcher.get( 3 );

        std::cerr << "  After 3rd new consecutive access: " << printShortStats( blockFetcher ) << "\n";

        REQUIRE_EQUAL( blockFetcher.cache().statistics().misses, size_t( 2 ) );
        REQUIRE_EQUAL( blockFetcher.cache().statistics().hits, size_t( 2 ) );
        REQUIRE( blockFetcher.prefetchCount() > parallelization );
    }
}

using CheckStatistics = std::function<void ( BlockFetcherStatistics, /* fetching strategy */ const std::string& )>;

void
benchmarkAccessPattern( const VectorView<int>  pattern,
                        const CheckStatistics& checkStatistics = {} )
{
    if ( pattern.empty() ) {
        return;
    }

    const size_t parallelization = 16;
    const auto blockCount = *std::max_element( pattern.begin(), pattern.end() ) + 1;

    const auto benchmarkPattern =
        [&] ( auto   fetchingStrategy,
              size_t cacheSize,
              size_t prefetchCacheSize,
              bool activelyAvoidPrefetchCachePollution )
        {
            using Strategy = std::decay_t<decltype( fetchingStrategy )>;

            const size_t totalCacheSize = cacheSize + prefetchCacheSize;
            BlockFetcher<Strategy> blockFetcher( parallelization, totalCacheSize, prefetchCacheSize,
                                                 activelyAvoidPrefetchCachePollution, blockCount );
            for ( auto i : pattern ) {
                blockFetcher.get( i );
            }

            const std::unordered_map<size_t, const char*> STRATEGY_NAMES = {
                { typeid( FetchNext ).hash_code(), "FetchNext" },
                { typeid( FetchNextSmart ).hash_code(), "FetchNextSmart" },
                { typeid( FetchNextMulti ).hash_code(), "FetchNextMulti" }
            };

            auto const* const strategyName = STRATEGY_NAMES.at( typeid( Strategy ).hash_code() );

        #if 0
            std::cerr << "=== Using fetching stratgey " << strategyName << " ===\n"
                      << blockFetcher.printStats() << "\n";
        #else
            std::cerr << strategyName << " : " << blockFetcher.printShortStats() << "\n";
        #endif

            if ( checkStatistics ) {
                checkStatistics( blockFetcher.statistics(), strategyName );
            }
        };

    for ( const auto activelyAvoidPrefetchCachePollution : { false, true } ) {
        std::cerr << "= Testing access pattern " << ( activelyAvoidPrefetchCachePollution ? "while" : "without" )
                  << " actively avoiding prefetch cache pollution =\n\n";
        std::cerr << "== Testing without dedicated prefetch cache ==\n\n";

        /* Without a dedicated prefetch cache, there is no pollution avoidance scheme anyway, so skip this. */
        if ( !activelyAvoidPrefetchCachePollution ) {
            benchmarkPattern( FetchNext     (), 16 + parallelization, 0, activelyAvoidPrefetchCachePollution );
            benchmarkPattern( FetchNextSmart(), 16 + parallelization, 0, activelyAvoidPrefetchCachePollution );
            benchmarkPattern( FetchNextMulti(), 16 + parallelization, 0, activelyAvoidPrefetchCachePollution );
        }

        std::cerr << "== Testing with dedicated prefetch cache ==\n\n";

        benchmarkPattern( FetchNext     (), 16, parallelization, activelyAvoidPrefetchCachePollution );
        benchmarkPattern( FetchNextSmart(), 16, parallelization, activelyAvoidPrefetchCachePollution );
        benchmarkPattern( FetchNextMulti(), 16, parallelization, activelyAvoidPrefetchCachePollution );

        std::cerr << "== Testing with dedicated prefetch cache twice the size ==\n\n";

        benchmarkPattern( FetchNext     (), 16, 2 * parallelization, activelyAvoidPrefetchCachePollution );
        benchmarkPattern( FetchNextSmart(), 16, 2 * parallelization, activelyAvoidPrefetchCachePollution );
        benchmarkPattern( FetchNextMulti(), 16, 2 * parallelization, activelyAvoidPrefetchCachePollution );
    }
}


int
main()
{
    testFetchMulti();

    {
        std::cerr << "\n= Recorded Accesses Pattern =\n";
        benchmarkAccessPattern( { REAL_ACCESS_PATTERN_1.data(), REAL_ACCESS_PATTERN_1.size() } );
    }

    {
        std::cerr << "\n= Sequential Accesses =\n";
        std::vector<int> sequentialAccesses( 1000 );
        std::iota( sequentialAccesses.begin(), sequentialAccesses.end(), 0 );

        const auto checkStatistics =
            [] ( const auto& statistics, const std::string& /* fetchingStrategy */ )
            {
                REQUIRE( statistics.cacheHitRate() > 0.995 );
            };

        benchmarkAccessPattern( sequentialAccesses, checkStatistics );
    }

    {
        std::cerr << "\n= Backward Accesses =\n";
        /* For most prefetchers, a backward pattern should be similar to a random pattern,
         * i.e., no prefetching at all */
        std::vector<int> backwardAccesses( 1000 );
        std::iota( backwardAccesses.rbegin(), backwardAccesses.rend(), 0 );

        const auto checkStatistics =
            [] ( const auto& statistics, const std::string& fetchingStrategy )
            {
                if ( fetchingStrategy != "FetchNext" ) {
                    /* The very first access may trigger prefetching with full parallelization as a heuristic.
                     * Without the double prefetch cache size, it seems that twice the amount of unused entries
                     * is possible. @todo That could be a bug to be further analyzed. */
                    REQUIRE( statistics.prefetchCache.unusedEntries <= 2 * statistics.parallelization );
                }
            };

        benchmarkAccessPattern( backwardAccesses, checkStatistics );
    }

    {
        std::cerr << "\n= Strided Accesses =\n";
        /* Strided accesses should behave similar to a random pattern for the block prefetch strategies,
         * i.e., no prefetching at all. */
        std::vector<int> stridedAccesses( 1000 );
        for ( size_t i = 0; i < stridedAccesses.size(); ++i ) {
            stridedAccesses[i] = 2 * static_cast<int>( i );
        }

        const auto checkStatistics =
            [] ( const auto& statistics, const std::string& fetchingStratgegy )
            {
                if ( fetchingStratgegy != "FetchNext" ) {
                    /* The very first access may trigger prefetching with full parallelization as a heuristic.
                     * Without the double prefetch cache size, it seems that twice the amount of unused entries
                     * is possible. @todo That could be a bug to be further analyzed. */
                    REQUIRE( statistics.prefetchCache.unusedEntries <= 2 * statistics.parallelization );
                }
            };

        benchmarkAccessPattern( stridedAccesses, checkStatistics );
    }

    {
        std::cerr << "\n= Random Accesses =\n";
        /* The maximum random value should be much larger than the amount of values produced to minimize
         * randomly sequential accesses. */
        std::vector<int> randomAccesses( 1000 );
        for ( auto& value : randomAccesses ) {
            value = std::rand();
        }

        const auto checkStatistics =
            [] ( const auto& statistics, const std::string& fetchingStratgegy )
            {
                if ( fetchingStratgegy != "FetchNext" ) {
                    /* The very first access may trigger prefetching with full parallelization as a heuristic.
                     * Without the double prefetch cache size, it seems that twice the amount of unused entries
                     * is possible. @todo That could be a bug to be further analyzed. */
                    REQUIRE( statistics.prefetchCache.unusedEntries <= 2 * statistics.parallelization );
                }
            };

        benchmarkAccessPattern( randomAccesses, checkStatistics );
    }

    testFindAdjacentIf();

    testFetchNext();
    testLinearAccess<FetchNextSmart>();
    testLinearAccess<FetchNextMulti>();
    testInterleavedLinearAccess<FetchNextMulti>( 1 );
    testInterleavedLinearAccess<FetchNextMulti>( 2 );

    benchmarkFetchNext();
    benchmarkFetchNextSmart();

    std::cout << "Tests successful: " << ( gnTests - gnTestErrors ) << " / " << gnTests << "\n";

    return gnTestErrors;
}


/*
Results for benchmarkAccessPattern (parallelization = 16, REAL_ACCESS_PATTERN_1):

    Blocks
       Total Accesses : 1586
       Total Existing : 625

    = Testing access pattern without actively avoiding prefetch cache pollution =
    == Testing without dedicated prefetch cache ==
    FetchNext      : Hit Rate : 0.870113 %
    FetchNextSmart : Hit Rate : 0.892182 %
    FetchNextMulti : Hit Rate : 0.935057 %

    == Testing with dedicated prefetch cache ==
    FetchNext      : Hit Rate : 0.787516 %  Useless Prefetches : 0.953225 %
    FetchNextSmart : Hit Rate : 0.883354 %  Useless Prefetches : 0.691707 %
    FetchNextMulti : Hit Rate : 0.936318 %  Useless Prefetches : 0.656334 %

    == Testing with dedicated prefetch cache twice the size ==
    FetchNext      : Hit Rate : 0.908575 %  Useless Prefetches : 0.765974 %
    FetchNextSmart : Hit Rate : 0.954603 %  Useless Prefetches : 0.179747 %
    FetchNextMulti : Hit Rate : 0.957125 %  Useless Prefetches : 0.0946667 %

    = Testing access pattern while actively avoiding prefetch cache pollution =
    == Testing with dedicated prefetch cache ==
    FetchNext      : Hit Rate : 0.738966 %  Useless Prefetches : 0.924986 %
    FetchNextSmart : Hit Rate : 0.854351 %  Useless Prefetches : 0.670407 %
    FetchNextMulti : Hit Rate : 0.913619 %  Useless Prefetches : 0.50571 %

    == Testing with dedicated prefetch cache twice the size ==
    FetchNext      : Hit Rate : 0.86633  %  Useless Prefetches : 0.751051 %
    FetchNextSmart : Hit Rate : 0.947667 %  Useless Prefetches : 0.171651 %
    FetchNextMulti : Hit Rate : 0.956494 %  Useless Prefetches : 0.0934579 %

 => For this access pattern, FetchNextMulti always has the highest hit rate with at the same time the lowest useless prefetches!
 => The dedicated prefetch cache while keeping the total cache sizes / memory usage constant decreases the hit rate
    a bit and even leads to a bit more unused entries.
 => actively avoiding cache pollution does not help very much for FetchNextMulti and decreases the hit rate
    -> Maybe better to not simply stop prefetching on detected cache pollution but instead touch all those to be prefetched first if they already exist!

Repeated tests with first touching blocks to be prefetched before actively testing for prefetch cache pollution:

    = Testing access pattern while actively avoiding prefetch cache pollution =
    == Testing with dedicated prefetch cache ==
    FetchNext      : Hit Rate : 0.755359 %  Useless Prefetches : 0.956552 %
    FetchNextSmart : Hit Rate : 0.894704 %  Useless Prefetches : 0.637235 %
    FetchNextMulti : Hit Rate : 0.954603 %  Useless Prefetches : 0.329703 %

    == Testing with dedicated prefetch cache twice the size ==
    FetchNext      : Hit Rate : 0.912358 %  Useless Prefetches : 0.686254 %
    FetchNextSmart : Hit Rate : 0.955233 %  Useless Prefetches : 0.138482 %
    FetchNextMulti : Hit Rate : 0.958386 %  Useless Prefetches : 0.039604 %

 => This is better in all metrics than the previous pollution prevention except for FetchNext but that strategy is
    the worst anyway and can be ignored for further analyses, I think.

Instead of doubling the prefetch cache, try halfing the maximum prefetch size:

    = Testing access pattern while actively avoiding prefetch cache pollution =
    == Testing without dedicated prefetch cache ==
    == Testing with dedicated prefetch cache ==
    FetchNext      : Hit Rate : 0.9029   %  Useless Prefetches : 0.608321 %
    FetchNextSmart : Hit Rate : 0.946406 %  Useless Prefetches : 0.135593 %
    FetchNextMulti : Hit Rate : 0.949559 %  Useless Prefetches : 0.0479042 %

 => This yields similar results to doubling the prefetch cache size.
    => Either, I'll have to make the FetchingStrategy return prefetching candidates much more conservatively,
       e.g., only return the maximum when all of the recorded last accesses have been sequential accesses,
       which would also probably lead less effective parallelization on average.
       Or, I'll have to live with the increased memory usage for the larger cache.

Simple sequential access is not a problem for any of the methods:

    = Testing access pattern without actively avoiding prefetch cache pollution =
    == Testing without dedicated prefetch cache ==
    FetchNext      : Hit Rate : 0.999 %  Useless Prefetches : 0 %
    FetchNextSmart : Hit Rate : 0.999 %  Useless Prefetches : 0 %
    FetchNextMulti : Hit Rate : 0.999 %  Useless Prefetches : 0 %

    == Testing with dedicated prefetch cache ==
    FetchNext      : Hit Rate : 0.999 %  Useless Prefetches : 0 %
    FetchNextSmart : Hit Rate : 0.999 %  Useless Prefetches : 0 %
    FetchNextMulti : Hit Rate : 0.999 %  Useless Prefetches : 0 %

    == Testing with dedicated prefetch cache twice the size ==
    FetchNext      : Hit Rate : 0.999 %  Useless Prefetches : 0 %
    FetchNextSmart : Hit Rate : 0.999 %  Useless Prefetches : 0 %
    FetchNextMulti : Hit Rate : 0.999 %  Useless Prefetches : 0 %

    = Testing access pattern while actively avoiding prefetch cache pollution =
    == Testing without dedicated prefetch cache ==
    == Testing with dedicated prefetch cache ==

    FetchNext      : Hit Rate : 0.999 %  Useless Prefetches : 0 %
    FetchNextSmart : Hit Rate : 0.999 %  Useless Prefetches : 0 %
    FetchNextMulti : Hit Rate : 0.999 %  Useless Prefetches : 0 %

    == Testing with dedicated prefetch cache twice the size ==
    FetchNext      : Hit Rate : 0.999 %  Useless Prefetches : 0 %
    FetchNextSmart : Hit Rate : 0.999 %  Useless Prefetches : 0 %
    FetchNextMulti : Hit Rate : 0.999 %  Useless Prefetches : 0 %

A backwards access pattern, currently, results in a bug for FetchNextMulti because it simply sorts all last values!

    = Testing access pattern without actively avoiding prefetch cache pollution =

    == Testing without dedicated prefetch cache ==
    FetchNext : Hit Rate : 0 %  Useless Prefetches : 0 %
    FetchNextSmart : Hit Rate : 0 %  Useless Prefetches : 0 %
    FetchNextMulti : Hit Rate : 0 %  Useless Prefetches : 0 %

    == Testing with dedicated prefetch cache ==
    FetchNext : Hit Rate : 0 %  Useless Prefetches : 0.0433145 %
    FetchNextSmart : Hit Rate : 0 %  Useless Prefetches : 0 %
    FetchNextMulti : Hit Rate : 0 %  Useless Prefetches : 0 %

    == Testing with dedicated prefetch cache twice the size ==
    FetchNext : Hit Rate : 0 %  Useless Prefetches : 0.0282486 %
    FetchNextSmart : Hit Rate : 0 %  Useless Prefetches : 0 %
    FetchNextMulti : Hit Rate : 0 %  Useless Prefetches : 0 %

    = Testing access pattern while actively avoiding prefetch cache pollution =
    == Testing without dedicated prefetch cache ==
    == Testing with dedicated prefetch cache ==

    FetchNext : Hit Rate : 0 %  Useless Prefetches : 0.0433145 %
    FetchNextSmart : Hit Rate : 0 %  Useless Prefetches : 0 %
    FetchNextMulti : Hit Rate : 0 %  Useless Prefetches : 0 %

    == Testing with dedicated prefetch cache twice the size ==
    FetchNext : Hit Rate : 0 %  Useless Prefetches : 0.0282486 %
    FetchNextSmart : Hit Rate : 0 %  Useless Prefetches : 0 %
    FetchNextMulti : Hit Rate : 0 %  Useless Prefetches : 0 %
*/
