/*
    Copyright (c) 2017 Christopher A. Taylor.  All rights reserved.

    Redistribution and use in source and binary forms, with or without
    modification, are permitted provided that the following conditions are met:

    * Redistributions of source code must retain the above copyright notice,
      this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright notice,
      this list of conditions and the following disclaimer in the documentation
      and/or other materials provided with the distribution.
    * Neither the name of Fecal nor the names of its contributors may be
      used to endorse or promote products derived from this software without
      specific prior written permission.

    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
    AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
    IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
    ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
    LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
    CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
    SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
    INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
    CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
    ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
    POSSIBILITY OF SUCH DAMAGE.
*/

#pragma once

/*
    Siamese Decoder Data Recovery Process

    (1) Collect data:

    This collects original data packets and recovery packets, until a solution
    may be possible (recovery is possible about 99.9% of the time).

    (2) Generate recovery matrix:

    The recovery matrix is a square GF(2^^8) where the width of the matrix is
    the number of losses we are trying to recover.  The recovery matrix elements
    are sampled from a larger matrix that is implicit (not actually constructed),
    where the columns correspond to original data and the rows correspond to
    recovery packets.

    (3) Solve recovery matrix:

    We experimentally perform Gaussian elimination on the matrix to put it in
    upper triangular form.  If this is successful, then recovery can proceed.
    Note that we have done no operations on the original data yet, so this step
    is fairly inexpensive.

    To speed up this step with the density of the matrix in mind, we attempt
    GE without pivoting first and then switch to a pivoting algorithm as zeroes
    are encountered.

    If this fails we attempt to build a larger recovery matrix involving more
    received recovery packets, which may also involve more lost original data.
    If recovery is not possible with the received data, then we wait for more.

    (4) Eliminate received data:

    This step involves most of the received data and takes the most time.
    Its complexity is slightly less than that of the encoder.  As a result,
    and improvement in encoder performance will translate to a faster decoder.

    For each recovery packet involved in solution we need to eliminate original
    data that is outside of the recovery matrix, so that the recovery matrix can
    be applied to recover the lost data.

    We construct the sums of received original data for each row as in the encoder,
    and roll the sums up as the left side is eliminated from later recovery packets.
    The sums are reused on multiple rows to eliminate data faster.

    (5) Recover original data:

    The same operations performed to arrive at the GE solution earlier are now
    performed on the recovery data packets.  We then multiply by the upper
    triangle of the recovery matrix in back substitution order.  Finally the
    diagonal is eliminated by dividing each recovery packet by the diagonal.
    The recovery packets now contain original data.

    The original data are prefixed by a length field so that the original data
    length can be recovered, since we support variable length input data.
*/

#include "FecalCommon.h"

#include <unordered_set>

namespace fecal {


//------------------------------------------------------------------------------
// DecoderAppDataWindow

struct RecoveryInfo
{
    uint8_t* Data = nullptr;
    unsigned Row = 0;
    bool UsedForSolution = false;
};

struct OriginalInfo
{
    uint8_t* Data = nullptr;
    unsigned RecoveryMatrixColumn = 0;
};

// Keep this number of columns in each subwindow
static const unsigned kSubwindowSize = kColumnLaneCount * 8;

struct Subwindow
{
    CustomBitSet<kSubwindowSize> Got;
    unsigned GotCount = 0;
};

// Decoder-specialized app data window
struct DecoderAppDataWindow : AppDataWindow
{
    // Received original data
    std::vector<OriginalInfo> OriginalData;

    // Received recovery data
    std::vector<RecoveryInfo> RecoveryData;

    // Track which entries are filled in
    unsigned SubwindowCount = 0;
    std::vector<Subwindow> Subwindows;

    // Number of unique originals received so far
    unsigned OriginalGotCount = 0;

    // Check if row has been seen yet
    std::unordered_set<unsigned> RowSet;


    // Allocate originals
    void AllocateOriginals();

    // Add symbol data
    // Returns false if we already have the data
    bool AddRecovery(uint8_t* data, unsigned row);

    // Add original data
    // Returns false if we already have the data
    bool AddOriginal(unsigned column, uint8_t* data);

    // Mark that we got an element
    void MarkGotElement(unsigned element);

    // Returns Count if no more elements were lost
    // Otherwise returns the next element that was lost at or after the given one
    unsigned FindNextLostElement(unsigned elementStart);
};


//------------------------------------------------------------------------------
// RecoveryMatrixState

/*
    We maintain a GF(2^^8) byte matrix that can grow a little in rows and
    columns to reattempt solving with a larger matrix that includes more
    lost columns and received recovery data, in the case that recovery fails.
    It is expected that recovery fails around 1% of the time.

    The matrix is also a bit oversized to allow us to prefetch the next row,
    and to align memory addresses with cache line boundaries for speed.
*/

class RecoveryMatrixState
{
public:
    DecoderAppDataWindow* Window = nullptr;

    struct ColumnInfo
    {
        // Column number for the missing data
        unsigned Column = 0;

        // Column multiplier
        uint8_t CX = 0;
    };
    std::vector<ColumnInfo> Columns;

    // Recovery matrix
    GrowingAlignedByteMatrix Matrix;

    // Array of pivots used for when rows need to be swapped
    // This allows us to swap indices rather than swap whole rows to reduce memory accesses
    std::vector<unsigned> Pivots;

    // Pivot to resume at when we get more data
    unsigned GEResumePivot = 0;

    // Number of matrix rows we already filled
    unsigned FilledRows = 0;


    // Populate Rows and Columns arrays
    void PopulateColumns(const unsigned columns);

    // Generate the matrix
    bool GenerateMatrix();

    // Attempt to put the matrix in upper-triangular form
    bool GaussianElimination();

protected:
    // Resume GE from a previous failure point
    void ResumeGE(const unsigned oldRows, const unsigned rows);

    // Run GE with pivots after a column is found to be zero
    bool PivotedGaussianElimination(unsigned pivot_i);

    // rem_row[] += ge_row[] * y
    GF256_FORCE_INLINE void MulAddRows(
        const uint8_t* ge_row, uint8_t* rem_row, unsigned columnStart,
        const unsigned columnEnd, uint8_t y)
    {
#ifdef GF256_ALIGNED_ACCESSES
        // Do unaligned operations first
        // Note: Each row starts at an aliged address
        unsigned unalignedEnd = NextAlignedOffset(columnStart);
        if (unalignedEnd > columnEnd)
            unalignedEnd = columnEnd;
        for (; columnStart < unalignedEnd; ++columnStart)
            rem_row[columnStart] ^= gf256_mul(ge_row[columnStart], y);
        if (columnStart >= columnEnd)
            return;
#endif

        gf256_muladd_mem(rem_row + columnStart, y, ge_row + columnStart, columnEnd - columnStart);
    }

    // Internal function common to both GE functions, used to eliminate a row of data
    GF256_FORCE_INLINE void EliminateRow(
        const uint8_t* ge_row, uint8_t* rem_row, const unsigned pivot_i,
        const unsigned columnEnd, const uint8_t val_i)
    {
        // Skip if the element j,i is already zero
        const uint8_t val_j = rem_row[pivot_i];
        if (val_j == 0)
            return;

        // Calculate element j,i elimination constant based on pivot row value
        const uint8_t y = gf256_div(val_j, val_i);

        // Remember what value was used to zero element j,i
        rem_row[pivot_i] = y;

        MulAddRows(ge_row, rem_row, pivot_i + 1, columnEnd, y);
    }
};


//------------------------------------------------------------------------------
// Decoder

class Decoder : public ICodec
{
public:
    virtual ~Decoder() {}

    // Initialize the decoder
    FecalResult Initialize(unsigned input_count, uint64_t total_bytes);

    // Add original data
    FecalResult AddOriginal(const FecalSymbol& symbol);

    // Add recovery data
    FecalResult AddRecovery(const FecalSymbol& symbol);

    // Try to decode
    FecalResult Decode(RecoveredSymbols& symbols);

    // Get original data
    FecalResult GetOriginal(unsigned column, FecalSymbol& symbol);

protected:
    // Window of original data
    DecoderAppDataWindow Window;

    // Matrix containing recovery packets that may admit a solution
    RecoveryMatrixState RecoveryMatrix;

    // Has recovery been attempted with the latest inputs?
    bool RecoveryAttempted = false;

    // Recovered data array returned to application
    std::vector<FecalSymbol> RecoveredData;

    // Sums for each lane
    AlignedDataBuffer LaneSums[kColumnLaneCount][kColumnSumCount];

    // Output workspace
    AlignedDataBuffer ProductWorkspace;


    // Recovery step: Eliminate original data that was successfully received
    FecalResult EliminateOriginalData();

    // Get lane sum for original data we have
    const uint8_t* GetLaneSum(unsigned laneIndex, unsigned sumIndex);

    // Recovery step: Multiply lower triangle following solution order
    void MultiplyLowerTriangle();

    // Recovery step: Back-substitute upper triangle to reveal original data
    FecalResult BackSubstitution();
};


} // namespace fecal
