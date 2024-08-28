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

#include "FecalDecoder.h"

namespace fecal {


//------------------------------------------------------------------------------
// DecoderAppDataWindow

void DecoderAppDataWindow::AllocateOriginals()
{
    OriginalData.resize(InputCount);

    // Allocate some space for recovery data too (20% of original data size)
    RecoveryData.reserve(InputCount / 5 + 1);

    SubwindowCount = (InputCount + kSubwindowSize - 1) / kSubwindowSize;
    Subwindows.resize(SubwindowCount);
}

bool DecoderAppDataWindow::AddOriginal(unsigned column, uint8_t* data)
{
    // If we already have this one:
    if (OriginalData[column].Data)
        return false;

    // Record this one
    OriginalData[column].Data = data;
    MarkGotElement(column);
    ++OriginalGotCount;

    return true;
}

bool DecoderAppDataWindow::AddRecovery(uint8_t* data, unsigned row)
{
    FECAL_DEBUG_ASSERT(InputCount > 0); // SetParameters() must be called first

    // Trying to insert with duplicate ID: It will not be inserted
    auto res = RowSet.insert(row);
    if (!res.second)
        return false;

    RecoveryInfo info;
    info.Data = data;
    info.Row = row;
    info.UsedForSolution = false;
    RecoveryData.push_back(info);

    return true;
}

void DecoderAppDataWindow::MarkGotElement(unsigned element)
{
    FECAL_DEBUG_ASSERT(element < InputCount);
    Subwindow& subwindow = Subwindows[element / kSubwindowSize];
    FECAL_DEBUG_ASSERT(!subwindow.Got.Check(element % kSubwindowSize));
    subwindow.Got.Set(element % kSubwindowSize);
    subwindow.GotCount++;
}

unsigned DecoderAppDataWindow::FindNextLostElement(unsigned elementStart)
{
    if (elementStart >= InputCount)
        return InputCount;

    const unsigned subwindowEnd = SubwindowCount;
    unsigned subwindowIndex = elementStart / kSubwindowSize;
    unsigned bitIndex = elementStart % kSubwindowSize;
    FECAL_DEBUG_ASSERT(subwindowEnd <= SubwindowCount);
    FECAL_DEBUG_ASSERT(subwindowIndex < SubwindowCount);

    while (subwindowIndex < subwindowEnd)
    {
        // If there may be any lost packets in this subwindow:
        if (Subwindows[subwindowIndex].GotCount < kSubwindowSize)
        {
            for (;;)
            {
                // Seek next clear bit
                bitIndex = Subwindows[subwindowIndex].Got.FindFirstClear(bitIndex);

                // If there were none, skip this subwindow
                if (bitIndex >= kSubwindowSize)
                    break;

                // Calculate element index and stop if we hit the end of the valid data
                unsigned nextElement = subwindowIndex * kSubwindowSize + bitIndex;
                if (nextElement > InputCount)
                    nextElement = InputCount;

                return nextElement;
            }
        }

        // Reset bit index to the front of the next subwindow
        bitIndex = 0;

        // Check next subwindow
        ++subwindowIndex;
    }

    return InputCount;
}


//------------------------------------------------------------------------------
// Decoder

FecalResult Decoder::Initialize(unsigned input_count, uint64_t total_bytes)
{
    RecoveryMatrix.Window = &Window;

    if (!Window.SetParameters(input_count, total_bytes))
    {
        FECAL_DEBUG_BREAK; // Invalid input
        return Fecal_InvalidInput;
    }
    Window.AllocateOriginals();

    return Fecal_Success;
}

FecalResult Decoder::AddOriginal(const FecalSymbol& symbol)
{
    if (symbol.Index >= Window.InputCount ||
        symbol.Data == nullptr ||
        symbol.Bytes != Window.GetColumnBytes(symbol.Index))
    {
        FECAL_DEBUG_BREAK; // Invalid input
        return Fecal_InvalidInput;
    }

    if (Window.AddOriginal(symbol.Index, (uint8_t*)symbol.Data))
        RecoveryAttempted = false;

    return Fecal_Success;
}

FecalResult Decoder::AddRecovery(const FecalSymbol& symbol)
{
    if (symbol.Data == nullptr ||
        symbol.Bytes != Window.SymbolBytes)
    {
        FECAL_DEBUG_BREAK; // Invalid input
        return Fecal_InvalidInput;
    }

    if (Window.AddRecovery((uint8_t*)symbol.Data, symbol.Index))
        RecoveryAttempted = false;

    return Fecal_Success;
}

FecalResult Decoder::GetOriginal(unsigned column, FecalSymbol& symbol)
{
    symbol.Index = column;
    symbol.Data = nullptr;
    symbol.Bytes = 0;

    if (column >= Window.InputCount)
    {
        FECAL_DEBUG_BREAK; // Invalid input
        return Fecal_InvalidInput;
    }

    symbol.Data = Window.OriginalData[column].Data;
    if (symbol.Data == nullptr)
        return Fecal_NeedMoreData;

    symbol.Bytes = Window.GetColumnBytes(column);
    return Fecal_Success;
}

FecalResult Decoder::Decode(RecoveredSymbols& symbols)
{
    // Default return values
    symbols.Symbols = nullptr;
    symbols.Count = 0;

    // If all original data arrived:
    if (Window.OriginalGotCount >= Window.InputCount)
        return Fecal_Success;

    // If we have not received enough data to try to decode:
    if (Window.OriginalGotCount + static_cast<unsigned>(Window.RecoveryData.size()) < Window.InputCount)
        return Fecal_NeedMoreData;

    // If recovery was already attempted:
    if (RecoveryAttempted)
        return Fecal_NeedMoreData;
    RecoveryAttempted = true;

    // Generate updated recovery matrix
    if (!RecoveryMatrix.GenerateMatrix())
        return Fecal_OutOfMemory;

    // Attempt to solve the linear system
    if (!RecoveryMatrix.GaussianElimination())
        return Fecal_NeedMoreData;

    FecalResult result = EliminateOriginalData();
    if (result != Fecal_Success)
        return result;

    MultiplyLowerTriangle();

    result = BackSubstitution();

    if (result == Fecal_Success)
    {
        symbols.Symbols = &RecoveredData[0];
        symbols.Count = static_cast<unsigned>(RecoveredData.size());
    }

    return result;
}

FecalResult Decoder::EliminateOriginalData()
{
    // Allocate workspace
    const unsigned symbolBytes = Window.SymbolBytes;
    if (!ProductWorkspace.Allocate(symbolBytes))
        return Fecal_OutOfMemory;

    const unsigned rows = static_cast<unsigned>(Window.RecoveryData.size());

    // Eliminate data in sorted row order regardless of pivot order:
    for (unsigned matrixRowIndex = 0; matrixRowIndex < rows; ++matrixRowIndex)
    {
        const RecoveryInfo& recovery = Window.RecoveryData[matrixRowIndex];
        if (!recovery.UsedForSolution)
            continue;

        // Zero the product sum
        memset(ProductWorkspace.Data, 0, symbolBytes);

        XORSummer summer1;
        summer1.Initialize(recovery.Data, symbolBytes);
        XORSummer summerRX;
        summerRX.Initialize(ProductWorkspace.Data, symbolBytes);

        // Eliminate dense recovery data outside of matrix:
        for (unsigned laneIndex = 0; laneIndex < kColumnLaneCount; ++laneIndex)
        {
            const unsigned opcode = GetRowOpcode(laneIndex, recovery.Row);

            // For summations into the RecoveryPacket buffer:
            unsigned mask = 1;
            for (unsigned sumIndex = 0; sumIndex < kColumnSumCount; ++sumIndex)
            {
                if (opcode & mask)
                    summer1.Add(GetLaneSum(laneIndex, sumIndex));
                mask <<= 1;
            }

            // For summations into the ProductWorkspace buffer:
            for (unsigned sumIndex = 0; sumIndex < kColumnSumCount; ++sumIndex)
            {
                if (opcode & mask)
                    summerRX.Add(GetLaneSum(laneIndex, sumIndex));
                mask <<= 1;
            }
        }

        // Eliminate light recovery data outside of matrix:
        const unsigned inputCount = Window.InputCount;
        PCGRandom prng;
        prng.Seed(recovery.Row, inputCount);

        const unsigned pairCount = (inputCount + kPairAddRate - 1) / kPairAddRate;
        for (unsigned i = 0; i < pairCount; ++i)
        {
            const unsigned element1 = prng.Next() % inputCount;
            const uint8_t* original1 = Window.OriginalData[element1].Data;
            if (original1)
            {
                if (element1 == inputCount - 1)
                    gf256_add_mem(recovery.Data, original1, Window.FinalBytes);
                else
                    summer1.Add(original1);
            }

            const unsigned elementRX = prng.Next() % inputCount;
            const uint8_t* originalRX = Window.OriginalData[elementRX].Data;
            if (originalRX)
            {
                if (elementRX == inputCount - 1)
                    gf256_add_mem(ProductWorkspace.Data, originalRX, Window.FinalBytes);
                else
                    summerRX.Add(originalRX);
            }
        }

        summer1.Finalize();
        summerRX.Finalize();

        const uint8_t RX = GetRowValue(recovery.Row);
        gf256_muladd_mem(recovery.Data, RX, ProductWorkspace.Data, symbolBytes);
    }

    return Fecal_Success;
}

const uint8_t* Decoder::GetLaneSum(unsigned laneIndex, unsigned sumIndex)
{
    AlignedDataBuffer& sum = LaneSums[laneIndex][sumIndex];
    if (sum.Data)
        return sum.Data;

    const unsigned symbolBytes = Window.SymbolBytes;
    if (!sum.Allocate(symbolBytes))
        return nullptr;

    memset(sum.Data, 0, symbolBytes);

    const unsigned inputEnd = Window.InputCount - 1;
    if (sumIndex == 0)
    {
        XORSummer summer;
        summer.Initialize(sum.Data, symbolBytes);

        // For each input column:
        for (unsigned column = laneIndex; column < inputEnd; column += kColumnLaneCount)
        {
            const uint8_t* data = Window.OriginalData[column].Data;
            if (data)
                summer.Add(data);
        }
        if (inputEnd % kColumnLaneCount == laneIndex)
        {
            const uint8_t* data = Window.OriginalData[inputEnd].Data;
            if (data)
                gf256_add_mem(sum.Data, data, Window.FinalBytes);
        }

        summer.Finalize();
        return sum.Data;
    }

    // For each input column:
    for (unsigned column = laneIndex; column < inputEnd; column += kColumnLaneCount)
    {
        const uint8_t* data = Window.OriginalData[column].Data;
        if (!data)
            continue;

        uint8_t CX_or_CX2 = GetColumnValue(column);
        if (sumIndex == 2)
            CX_or_CX2 = gf256_sqr(CX_or_CX2);

        gf256_muladd_mem(sum.Data, CX_or_CX2, data, symbolBytes);
    }
    if (inputEnd % kColumnLaneCount == laneIndex)
    {
        const uint8_t* data = Window.OriginalData[inputEnd].Data;
        if (data)
        {
            uint8_t CX_or_CX2 = GetColumnValue(inputEnd);
            if (sumIndex == 2)
                CX_or_CX2 = gf256_sqr(CX_or_CX2);

            gf256_muladd_mem(sum.Data, CX_or_CX2, data, Window.FinalBytes);
        }
    }

    return sum.Data;

    static_assert(kColumnSumCount == 3, "Update this");
}

void Decoder::MultiplyLowerTriangle()
{
    const unsigned columns = static_cast<unsigned>(RecoveryMatrix.Columns.size());
    const unsigned srcBytes = Window.SymbolBytes;

    // Multiply lower triangle following solution order from left to right:
    for (unsigned col_i = 0; col_i < columns - 1; ++col_i)
    {
        const unsigned matrixRowIndex_i = RecoveryMatrix.Pivots[col_i];
        const uint8_t* srcData = Window.RecoveryData[matrixRowIndex_i].Data;
        FECAL_DEBUG_ASSERT(srcData && srcBytes > 0);

        for (unsigned col_j = col_i + 1; col_j < columns; ++col_j)
        {
            const unsigned matrixRowIndex_j = RecoveryMatrix.Pivots[col_j];
            const uint8_t y = RecoveryMatrix.Matrix.Get(matrixRowIndex_j, col_i);

            if (y == 0)
                continue;

            uint8_t* destData = Window.RecoveryData[matrixRowIndex_j].Data;
            gf256_muladd_mem(destData, y, srcData, srcBytes);
        }
    }
}

FecalResult Decoder::BackSubstitution()
{
    const unsigned columns = static_cast<unsigned>(RecoveryMatrix.Columns.size());
    const unsigned srcBytes = Window.SymbolBytes;

    RecoveredData.resize(columns);

    // For each column starting with the right-most column:
    for (int col_i = columns - 1; col_i >= 0; --col_i)
    {
        const unsigned matrixRowIndex = RecoveryMatrix.Pivots[col_i];
        uint8_t* recovery = Window.RecoveryData[matrixRowIndex].Data;
        const uint8_t y = RecoveryMatrix.Matrix.Get(matrixRowIndex, col_i);
        FECAL_DEBUG_ASSERT(y != 0);
        const unsigned originalColumn = RecoveryMatrix.Columns[col_i].Column;
        const unsigned originalBytes = Window.GetColumnBytes(originalColumn);

        gf256_div_mem(recovery, recovery, y, originalBytes);

        Window.OriginalData[originalColumn].Data = recovery;

        // Write recovered packet data
        RecoveredData[col_i].Data = recovery;
        RecoveredData[col_i].Bytes = originalBytes;
        RecoveredData[col_i].Index = originalColumn;

        // Eliminate from all other pivot rows above it:
        for (unsigned col_j = 0; col_j < (unsigned)col_i; ++col_j)
        {
            unsigned pivot_j = RecoveryMatrix.Pivots[col_j];
            const uint8_t x = RecoveryMatrix.Matrix.Get(pivot_j, col_i);

            if (x == 0)
                continue;

            gf256_muladd_mem(Window.RecoveryData[pivot_j].Data, x, recovery, originalBytes);
        }
    }

    return Fecal_Success;
}


//------------------------------------------------------------------------------
// RecoveryMatrixState

void RecoveryMatrixState::PopulateColumns(const unsigned columns)
{
    Columns.resize(columns);

    unsigned nextSearchColumn = 0;
    for (unsigned matrixColumn = 0; matrixColumn < columns; ++matrixColumn)
    {
        unsigned lostColumn = Window->FindNextLostElement(nextSearchColumn);
        if (lostColumn >= Window->InputCount)
        {
            FECAL_DEBUG_BREAK; // Should never happen
            break;
        }
        nextSearchColumn = lostColumn + 1;

        ColumnInfo& columnInfo = Columns[matrixColumn];
        columnInfo.Column = lostColumn;
        columnInfo.CX = GetColumnValue(lostColumn);

        Window->OriginalData[lostColumn].RecoveryMatrixColumn = matrixColumn;
    }
}

bool RecoveryMatrixState::GenerateMatrix()
{
    const unsigned input_count = Window->InputCount;
    const unsigned columns = input_count - Window->OriginalGotCount;
    const unsigned rows = static_cast<unsigned>(Window->RecoveryData.size());
    FECAL_DEBUG_ASSERT(rows >= columns);

    // If column count changed:
    if (columns != (unsigned)Columns.size())
    {
        PopulateColumns(columns);

        // Reset everything
        Pivots.clear();
        GEResumePivot = 0;
        FilledRows = 0;

        if (!Matrix.Initialize(rows, columns))
            return false;
    }
    else
    {
        // Otherwise we just added rows
        FECAL_DEBUG_ASSERT(FilledRows < rows);
        if (!Matrix.Resize(rows, columns))
            return false;
    }

    const unsigned stride = Matrix.AllocatedColumns;
    uint8_t* rowData = Matrix.Data + FilledRows * stride;

    // For each row to fill:
    for (unsigned ii = FilledRows; ii < rows; ++ii, rowData += stride)
    {
        const unsigned row = Window->RecoveryData[ii].Row;

        // Calculate row multiplier RX
        const uint8_t RX = GetRowValue(row);

        // Fill columns from left for new rows:
        for (unsigned j = 0; j < columns; ++j)
        {
            const unsigned column = Columns[j].Column;

            // Generate opcode and parameters
            const uint8_t CX = Columns[j].CX;
            const uint8_t CX2 = gf256_sqr(CX);
            const unsigned lane = column % kColumnLaneCount;
            const unsigned opcode = GetRowOpcode(lane, row);

            unsigned value = opcode & 1;
            if (opcode & 2)
                value ^= CX;
            if (opcode & 4)
                value ^= CX2;
            if (opcode & 8)
                value ^= RX;
            if (opcode & 16)
                value ^= gf256_mul(CX, RX);
            if (opcode & 32)
                value ^= gf256_mul(CX2, RX);
            rowData[j] = (uint8_t)value;
        }

        PCGRandom prng;
        prng.Seed(row, input_count);

        const unsigned pairCount = (input_count + kPairAddRate - 1) / kPairAddRate;

        for (unsigned k = 0; k < pairCount; ++k)
        {
            const unsigned element1 = prng.Next() % input_count;
            if (!Window->OriginalData[element1].Data)
            {
                const unsigned matrixColumn = Window->OriginalData[element1].RecoveryMatrixColumn;
                rowData[matrixColumn] ^= 1;
            }

            const unsigned elementRX = prng.Next() % input_count;
            if (!Window->OriginalData[elementRX].Data)
            {
                const unsigned matrixColumn = Window->OriginalData[elementRX].RecoveryMatrixColumn;
                rowData[matrixColumn] ^= RX;
            }
        } // for each pair of random columns
    } // for each recovery row

    // Fill in revealed column pivots with their own value
    Pivots.resize(rows);
    for (unsigned i = FilledRows; i < rows; ++i)
        Pivots[i] = i;

    // If we have already performed some GE, then we need to eliminate new
    // row data and we need to carry on elimination for new columns
    if (GEResumePivot > 0)
        ResumeGE(FilledRows, rows);

    FilledRows = rows;

    return true;
}

void RecoveryMatrixState::ResumeGE(const unsigned oldRows, const unsigned rows)
{
    // If we did not add any new rows:
    if (oldRows >= rows)
    {
        FECAL_DEBUG_ASSERT(oldRows == rows);
        return;
    }

    const unsigned stride = Matrix.AllocatedColumns;
    const unsigned columns = Matrix.Columns;

    // For each pivot we have determined already:
    for (unsigned pivot_i = 0; pivot_i < GEResumePivot; ++pivot_i)
    {
        // Get the row for that pivot
        const unsigned matrixRowIndex_i = Pivots[pivot_i];
        const uint8_t* ge_row = Matrix.Data + stride * matrixRowIndex_i;
        const uint8_t val_i = ge_row[pivot_i];
        FECAL_DEBUG_ASSERT(val_i != 0);

        uint8_t* rem_row = Matrix.Data + stride * oldRows;

        // For each new row that was added:
        for (unsigned newRowIndex = oldRows; newRowIndex < rows; ++newRowIndex, rem_row += stride)
        {
            EliminateRow(ge_row, rem_row, pivot_i, columns, val_i);

            FECAL_DEBUG_ASSERT(Pivots[newRowIndex] == newRowIndex);
        }
    }
}

bool RecoveryMatrixState::GaussianElimination()
{
    // Attempt to solve as much of the matrix as possible without using a pivots array
    // since that requires extra memory operations.  Since the matrix will be dense we
    // have a good chance of going pretty far before we hit a zero

    if (GEResumePivot > 0)
        return PivotedGaussianElimination(GEResumePivot);

    const unsigned columns = Matrix.Columns;
    const unsigned stride = Matrix.AllocatedColumns;
    const unsigned rows = Matrix.Rows;
    uint8_t* ge_row = Matrix.Data;

    for (unsigned pivot_i = 0; pivot_i < columns; ++pivot_i, ge_row += stride)
    {
        const uint8_t val_i = ge_row[pivot_i];
        if (val_i == 0)
            return PivotedGaussianElimination(pivot_i);

        RecoveryInfo& rowInfo = Window->RecoveryData[pivot_i];
        rowInfo.UsedForSolution = true;

        uint8_t* rem_row = ge_row;

        // For each remaining row:
        for (unsigned pivot_j = pivot_i + 1; pivot_j < rows; ++pivot_j)
        {
            rem_row += stride;
            EliminateRow(ge_row, rem_row, pivot_i, columns, val_i);
        }
    }

    return true;
}

bool RecoveryMatrixState::PivotedGaussianElimination(unsigned pivot_i)
{
    const unsigned columns = Matrix.Columns;
    const unsigned stride = Matrix.AllocatedColumns;
    const unsigned rows = Matrix.Rows;

    // Resume from next row down...
    // Note: This is designed to be called by the non-pivoted version
    unsigned pivot_j = pivot_i + 1;
    goto UsePivoting;

    // For each pivot to determine:
    for (; pivot_i < columns; ++pivot_i)
    {
        pivot_j = pivot_i;
UsePivoting:
        for (; pivot_j < rows; ++pivot_j)
        {
            const unsigned matrixRowIndex_j = Pivots[pivot_j];
            const uint8_t* ge_row = Matrix.Data + stride * matrixRowIndex_j;
            const uint8_t val_i = ge_row[pivot_i];
            if (val_i == 0)
                continue;

            // Swap out the pivot index for this one
            if (pivot_i != pivot_j)
            {
                const unsigned temp = Pivots[pivot_i];
                Pivots[pivot_i] = Pivots[pivot_j];
                Pivots[pivot_j] = temp;
            }

            RecoveryInfo& rowInfo = Window->RecoveryData[matrixRowIndex_j];
            rowInfo.UsedForSolution = true;

            // Skip eliminating extra rows in the case that we just solved the matrix
            if (pivot_i >= columns - 1)
                return true;

            // For each remaining row:
            for (unsigned pivot_k = pivot_i + 1; pivot_k < rows; ++pivot_k)
            {
                const unsigned matrixRowIndex_k = Pivots[pivot_k];
                uint8_t* rem_row = Matrix.Data + stride * matrixRowIndex_k;

                EliminateRow(ge_row, rem_row, pivot_i, columns, val_i);
            }

            goto NextPivot;
        }

        // Remember where we failed last time
        GEResumePivot = pivot_i;

        return false;
NextPivot:;
    }

    return true;
}


} // namespace fecal
