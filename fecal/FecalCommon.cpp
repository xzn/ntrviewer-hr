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

#include "FecalCommon.h"

namespace fecal {


//------------------------------------------------------------------------------
// AppDataWindow

bool AppDataWindow::SetParameters(unsigned input_count, uint64_t total_bytes)
{
    if (input_count <= 0 || total_bytes < input_count)
    {
        FECAL_DEBUG_BREAK; // Invalid input
        return false;
    }

    InputCount = input_count;
    TotalBytes = total_bytes;

    SymbolBytes = static_cast<unsigned>((total_bytes + input_count - 1) / input_count);
    FinalBytes = static_cast<unsigned>(total_bytes % SymbolBytes);
    if (FinalBytes <= 0)
        FinalBytes = SymbolBytes;

    FECAL_DEBUG_ASSERT(SymbolBytes >= FinalBytes && FinalBytes != 0);

    return true;
}


//------------------------------------------------------------------------------
// AlignedDataBuffer

AlignedDataBuffer::~AlignedDataBuffer()
{
    SIMDSafeFree(Data);
}

bool AlignedDataBuffer::Allocate(unsigned bytes)
{
    FECAL_DEBUG_ASSERT(bytes > 0);
    SIMDSafeFree(Data);
    Data = SIMDSafeAllocate(bytes);
    return Data != nullptr;
}


//------------------------------------------------------------------------------
// GrowingAlignedByteMatrix

GrowingAlignedByteMatrix::~GrowingAlignedByteMatrix()
{
    SIMDSafeFree(Data);
}

void GrowingAlignedByteMatrix::Free()
{
    if (Data)
    {
        SIMDSafeFree(Data);
        Data             = nullptr;
        AllocatedRows    = 0;
        AllocatedColumns = 0;
    }
}

bool GrowingAlignedByteMatrix::Initialize(unsigned rows, unsigned columns)
{
    Rows    = rows;
    Columns = columns;
    AllocatedRows    = rows + kExtraRows;
    AllocatedColumns = NextAlignedOffset(columns + kMinExtraColumns);

    SIMDSafeFree(Data);
    Data = SIMDSafeAllocate(AllocatedRows * AllocatedColumns);

    return Data != nullptr;
}

bool GrowingAlignedByteMatrix::Resize(unsigned rows, unsigned columns)
{
    FECAL_DEBUG_ASSERT(rows > 0 && columns > 0);
    if (rows <= AllocatedRows && columns <= AllocatedColumns)
    {
        Rows    = rows;
        Columns = columns;
        return true;
    }

    const unsigned allocatedRows    = rows + kExtraRows;
    const unsigned allocatedColumns = NextAlignedOffset(columns + kMinExtraColumns);

    uint8_t* buffer = SIMDSafeAllocate(allocatedRows * allocatedColumns);
    if (!buffer)
    {
        Free();
        return false;
    }

    // If we already allocated a buffer:
    if (Data)
    {
        uint8_t* oldBuffer        = Data;
        const unsigned oldColumns = Columns;

        if (oldColumns > 0)
        {
            // Maintain old data
            const unsigned oldRows   = Rows;
            const unsigned oldStride = AllocatedColumns;
            uint8_t* destRow = buffer;
            uint8_t* srcRow  = oldBuffer;

            unsigned copyCount = oldColumns;
            if (copyCount > columns)
            {
                FECAL_DEBUG_BREAK; // Should never happen
                copyCount = columns;
            }

            for (unsigned i = 0; i < oldRows; ++i, destRow += allocatedColumns, srcRow += oldStride)
                memcpy(destRow, srcRow, copyCount);
        }

        SIMDSafeFree(oldBuffer);
    }

    AllocatedRows    = allocatedRows;
    AllocatedColumns = allocatedColumns;
    Rows    = rows;
    Columns = columns;
    Data    = buffer;
    return true;
}


} // namespace fecal
