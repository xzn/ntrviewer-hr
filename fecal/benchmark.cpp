// /*
//     Copyright (c) 2017 Christopher A. Taylor.  All rights reserved.

//     Redistribution and use in source and binary forms, with or without
//     modification, are permitted provided that the following conditions are met:

//     * Redistributions of source code must retain the above copyright notice,
//       this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above copyright notice,
//       this list of conditions and the following disclaimer in the documentation
//       and/or other materials provided with the distribution.
//     * Neither the name of Fecal nor the names of its contributors may be
//       used to endorse or promote products derived from this software without
//       specific prior written permission.

//     THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
//     AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
//     IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
//     ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
//     LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
//     CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
//     SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
//     INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
//     CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
//     ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
//     POSSIBILITY OF SUCH DAMAGE.
// */

// #include "FecalCommon.h"
// #include "fecal.h"

// #include <list>
// #include <memory>
// #include <iostream>
// #include <vector>
// #include <string>
// using namespace std;

// // #define TEST_DATA_ALL_SAME
// // #define TEST_LOSE_FIRST_K_PACKETS
// // #define STATIC_ENCODER

// //------------------------------------------------------------------------------
// // Windows

// #ifdef _WIN32
//     #define WIN32_LEAN_AND_MEAN

//     #ifndef _WINSOCKAPI_
//         #define DID_DEFINE_WINSOCKAPI
//         #define _WINSOCKAPI_
//     #endif
//     #ifndef NOMINMAX
//         #define NOMINMAX
//     #endif
//     #ifndef _WIN32_WINNT
//         #define _WIN32_WINNT 0x0601 /* Windows 7+ */
//     #endif

//     #include <windows.h>
// #endif

// #ifdef DID_DEFINE_WINSOCKAPI
//     #undef _WINSOCKAPI_
//     #undef DID_DEFINE_WINSOCKAPI
// #endif


// //------------------------------------------------------------------------------
// // Threads

// static bool SetCurrentThreadPriority()
// {
// #if defined(__3DS__)
//     return false;
// #elif defined(_WIN32)
//     return 0 != ::SetThreadPriority(::GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
// #else
//     return -1 != nice(2);
// #endif
// }


// //------------------------------------------------------------------------------
// // Timing

// static uint64_t GetTimeUsec()
// {
// #if defined(__3DS__)
//     return svcGetSystemTick() * 1000000 / SYSCLOCK_ARM11;
// #elif defined(_WIN32)
//     LARGE_INTEGER timeStamp = {};
//     if (!::QueryPerformanceCounter(&timeStamp))
//         return 0;
//     static double PerfFrequencyInverse = 0.;
//     if (PerfFrequencyInverse == 0.)
//     {
//         LARGE_INTEGER freq = {};
//         if (!::QueryPerformanceFrequency(&freq) || freq.QuadPart == 0)
//             return 0;
//         PerfFrequencyInverse = 1000000. / (double)freq.QuadPart;
//     }
//     return (uint64_t)(PerfFrequencyInverse * timeStamp.QuadPart);
// #else
//     struct timeval tv;
//     gettimeofday(&tv, nullptr);
//     return 1000000 * tv.tv_sec + tv.tv_usec;
// #endif // _WIN32
// }


// //------------------------------------------------------------------------------
// // Self-Checking Packet

// static void WriteRandomSelfCheckingPacket(fecal::PCGRandom& prng, void* packet, unsigned bytes)
// {
//     uint8_t* buffer = (uint8_t*)packet;
// #ifdef TEST_DATA_ALL_SAME
//     if (bytes != 0)
// #else
//     if (bytes < 16)
// #endif
//     {
//         FECAL_DEBUG_ASSERT(bytes >= 2);
//         buffer[0] = (uint8_t)prng.Next();
//         for (unsigned i = 1; i < bytes; ++i)
//         {
//             buffer[i] = buffer[0];
//         }
//     }
//     else
//     {
//         uint32_t crc = bytes;
//         *(uint32_t*)(buffer + 4) = bytes;
//         for (unsigned i = 8; i < bytes; ++i)
//         {
//             uint8_t v = (uint8_t)prng.Next();
//             buffer[i] = v;
//             crc = (crc << 3) | (crc >> (32 - 3));
//             crc += v;
//         }
//         *(uint32_t*)buffer = crc;
//     }
// }

// static bool CheckPacket(const void* packet, unsigned bytes)
// {
//     uint8_t* buffer = (uint8_t*)packet;
// #ifdef TEST_DATA_ALL_SAME
//     if (bytes != 0)
// #else
//     if (bytes < 16)
// #endif
//     {
//         if (bytes < 2)
//             return false;

//         uint8_t v = buffer[0];
//         for (unsigned i = 1; i < bytes; ++i)
//         {
//             if (buffer[i] != v)
//                 return false;
//         }
//     }
//     else
//     {
//         uint32_t crc = bytes;
//         uint32_t readBytes = *(uint32_t*)(buffer + 4);
//         if (readBytes != bytes)
//             return false;
//         for (unsigned i = 8; i < bytes; ++i)
//         {
//             uint8_t v = buffer[i];
//             crc = (crc << 3) | (crc >> (32 - 3));
//             crc += v;
//         }
//         uint32_t readCRC = *(uint32_t*)buffer;
//         if (readCRC != crc)
//             return false;
//     }
//     return true;
// }


// //------------------------------------------------------------------------------
// // FunctionTimer

// class FunctionTimer
// {
// public:
//     FunctionTimer(const std::string& name)
//     {
//         FunctionName = name;
//     }
//     void BeginCall()
//     {
//         FECAL_DEBUG_ASSERT(t0 == 0);
//         t0 = GetTimeUsec();
//     }
//     void EndCall()
//     {
//         FECAL_DEBUG_ASSERT(t0 != 0);
//         uint64_t t1 = GetTimeUsec();
//         ++Invokations;
//         TotalUsec += t1 - t0;
//         t0 = 0;
//     }
//     void Reset()
//     {
//         FECAL_DEBUG_ASSERT(t0 == 0);
//         t0 = 0;
//         Invokations = 0;
//         TotalUsec = 0;
//     }
//     void Print(unsigned trials)
//     {
//         cout << FunctionName << " called " << Invokations / (float)trials << " times per trial (avg).  " << TotalUsec / (double)Invokations << " usec avg for all invokations.  " << TotalUsec / (float)trials << " usec (avg) of " << trials << " trials" << endl;
//     }

//     uint64_t t0 = 0;
//     uint64_t Invokations = 0;
//     uint64_t TotalUsec = 0;
//     std::string FunctionName;
// };


// //------------------------------------------------------------------------------
// // Utility: Deck Shuffling function

// /*
//     Given a PRNG, generate a deck of cards in a random order.
//     The deck will contain elements with values between 0 and count - 1.
// */

// static void ShuffleDeck16(fecal::PCGRandom &prng, uint16_t * GF256_RESTRICT deck, uint32_t count)
// {
//     deck[0] = 0;

//     // If we can unroll 4 times,
//     if (count <= 256)
//     {
//         for (uint32_t ii = 1;;)
//         {
//             uint32_t jj, rv = prng.Next();

//             // 8-bit unroll
//             switch (count - ii)
//             {
//             default:
//                 jj = (uint8_t)rv % ii;
//                 deck[ii] = deck[jj];
//                 deck[jj] = ii;
//                 ++ii;
//                 jj = (uint8_t)(rv >> 8) % ii;
//                 deck[ii] = deck[jj];
//                 deck[jj] = ii;
//                 ++ii;
//                 jj = (uint8_t)(rv >> 16) % ii;
//                 deck[ii] = deck[jj];
//                 deck[jj] = ii;
//                 ++ii;
//                 jj = (uint8_t)(rv >> 24) % ii;
//                 deck[ii] = deck[jj];
//                 deck[jj] = ii;
//                 ++ii;
//                 break;

//             case 3:
//                 jj = (uint8_t)rv % ii;
//                 deck[ii] = deck[jj];
//                 deck[jj] = ii;
//                 ++ii;
//             case 2:
//                 jj = (uint8_t)(rv >> 8) % ii;
//                 deck[ii] = deck[jj];
//                 deck[jj] = ii;
//                 ++ii;
//             case 1:
//                 jj = (uint8_t)(rv >> 16) % ii;
//                 deck[ii] = deck[jj];
//                 deck[jj] = ii;
//             case 0:
//                 return;
//             }
//         }
//     }
//     else
//     {
//         // For each deck entry,
//         for (uint32_t ii = 1;;)
//         {
//             uint32_t jj, rv = prng.Next();

//             // 16-bit unroll
//             switch (count - ii)
//             {
//             default:
//                 jj = (uint16_t)rv % ii;
//                 deck[ii] = deck[jj];
//                 deck[jj] = ii;
//                 ++ii;
//                 jj = (uint16_t)(rv >> 16) % ii;
//                 deck[ii] = deck[jj];
//                 deck[jj] = ii;
//                 ++ii;
//                 break;

//             case 1:
//                 jj = (uint16_t)rv % ii;
//                 deck[ii] = deck[jj];
//                 deck[jj] = ii;
//             case 0:
//                 return;
//             }
//         }
//     }
// }


// //------------------------------------------------------------------------------
// // Tests

// static void BasicTest(unsigned input_count, unsigned symbol_bytes, unsigned seed = 0)
// {
//     cout << "Testing performance for input_count=" << input_count << " and symbol_bytes=" << symbol_bytes << endl;

//     static const unsigned final_bytes = symbol_bytes;

// #ifdef STATIC_ENCODER
//     int encoder_size = fecal_encoder_size();
//     FecalEncoder encoder = (FecalEncoder)malloc(encoder_size);
//     if (!encoder)
//     {
//         cout << "Error: Unable to create encoder" << endl;
//         FECAL_DEBUG_BREAK;
//         return;
//     }
// #endif

//     for (unsigned lossCount = 1; lossCount <= input_count; ++lossCount)
//     {
//         const uint64_t total_bytes = (input_count - 1) * symbol_bytes + final_bytes;

//         FunctionTimer t_fecal_encoder_create("fecal_encoder_create");
//         FunctionTimer t_fecal_decoder_create("fecal_decoder_create");
//         FunctionTimer t_fecal_encode("fecal_encode");
//         FunctionTimer t_fecal_decoder_add_original("fecal_decoder_add_original");
//         FunctionTimer t_fecal_decoder_add_recovery("fecal_decoder_add_recovery");
//         FunctionTimer t_fecal_decode("fecal_decode");

//         static const unsigned kTrials = 100;

//         uint64_t recoveryRequired = 0;

//         for (unsigned trial = 0; trial < kTrials; ++trial)
//         {
//             fecal::PCGRandom prng;
//             prng.Seed(seed, lossCount * kTrials + trial);

//             std::vector<uint8_t> OriginalData((size_t)total_bytes + 1);
//             OriginalData[total_bytes] = 0xfe;
//             std::vector<void*> input_data(input_count);

//             uint8_t* data_buffer = &OriginalData[0];
//             for (unsigned ii = 0; ii < input_count - 1; ++ii)
//             {
//                 input_data[ii] = data_buffer;
//                 WriteRandomSelfCheckingPacket(prng, data_buffer, symbol_bytes);
//                 data_buffer += symbol_bytes;
//             }
//             input_data[input_count - 1] = data_buffer;
//             WriteRandomSelfCheckingPacket(prng, data_buffer, final_bytes);

//             t_fecal_encoder_create.BeginCall();
// #ifndef STATIC_ENCODER
//             FecalEncoder encoder = fecal_encoder_create(input_count, &input_data[0], total_bytes);
// #else
//             int result = fecal_encoder_init(encoder, input_count, &input_data[0], total_bytes);
// #endif
//             t_fecal_encoder_create.EndCall();

// #ifndef STATIC_ENCODER
//             if (!encoder)
//             {
//                 cout << "Error: Unable to create encoder" << endl;
//                 FECAL_DEBUG_BREAK;
//                 return;
//             }
// #else
//             if (result)
//             {
//                 cout << "Error: Error initializing encoder" << endl;
//                 FECAL_DEBUG_BREAK;
//                 return;
//             }
// #endif

//             t_fecal_decoder_create.BeginCall();
//             FecalDecoder decoder = fecal_decoder_create(input_count, total_bytes);
//             t_fecal_decoder_create.EndCall();

//             if (!decoder)
//             {
//                 cout << "Error: Unable to create decoder" << endl;
//                 FECAL_DEBUG_BREAK;
//                 return;
//             }

// #ifndef TEST_LOSE_FIRST_K_PACKETS
//             FECAL_DEBUG_ASSERT(input_count <= 65536);
//             std::vector<uint16_t> deck(input_count);
//             ShuffleDeck16(prng, &deck[0], input_count);
// #endif

//             for (unsigned i = 0; i < input_count; ++i)
//             {
//                 bool isLost = false;
// #ifdef TEST_LOSE_FIRST_K_PACKETS
//                 if (i < lossCount)
//                     isLost = true;
// #else
//                 for (unsigned k = 0; k < lossCount; ++k)
//                 {
//                     if (i == deck[k])
//                     {
//                         isLost = true;
//                         break;
//                     }
//                 }
// #endif
//                 if (isLost)
//                     continue;

//                 FecalSymbol original;
//                 original.Data = input_data[i];
//                 original.Bytes = symbol_bytes;
//                 if (i == input_count - 1)
//                     original.Bytes = final_bytes;
//                 original.Index = i;

//                 t_fecal_decoder_add_original.BeginCall();
//                 int result = fecal_decoder_add_original(decoder, &original);
//                 t_fecal_decoder_add_original.EndCall();

//                 if (result)
//                 {
//                     cout << "Error: Unable to add original data to decoder. error=" << result << endl;
//                     FECAL_DEBUG_BREAK;
//                     return;
//                 }
//             }

//             typedef std::shared_ptr< std::vector<uint8_t> > vecptr_t;
//             std::list<vecptr_t> recoveryData;

// #ifndef TEST_LOSE_FIRST_K_PACKETS
//             deck.clear();
//             deck.resize(input_count);
//             ShuffleDeck16(prng, &deck[0], input_count);
// #endif

//             for (unsigned recoveryIndex = 0;; ++recoveryIndex)
//             {
//                 vecptr_t data = std::make_shared< std::vector<uint8_t> >(symbol_bytes);
//                 recoveryData.push_back(data);

//                 FecalSymbol recovery;
//                 recovery.Index = recoveryIndex;
//                 recovery.Data = &data->at(0);
//                 recovery.Bytes = symbol_bytes;

//                 {
//                     t_fecal_encode.BeginCall();
//                     int result = fecal_encode(encoder, &recovery);
//                     t_fecal_encode.EndCall();

//                     if (result)
//                     {
//                         FECAL_DEBUG_BREAK;
//                         cout << "Error: Unable to generate encoded data. error=" << result << endl;
//                         return;
//                     }
//                 }

//                 ++recoveryRequired;

//                 bool isLost = false;
//                 unsigned i = recoveryIndex;
// #ifdef TEST_LOSE_FIRST_K_PACKETS
//                 if (i < lossCount)
//                     isLost = true;
// #else
//                 for (unsigned k = 0; k < lossCount; ++k)
//                 {
//                     if (i == deck[k])
//                     {
//                         isLost = true;
//                         break;
//                     }
//                 }
// #endif
//                 if (isLost)
//                     continue;

//                 {
//                     t_fecal_decoder_add_recovery.BeginCall();
//                     int result = fecal_decoder_add_recovery(decoder, &recovery);
//                     t_fecal_decoder_add_recovery.EndCall();
//                     if (result)
//                     {
//                         cout << "Error: Unable to add recovery data to decoder. error=" << result << endl;
//                         FECAL_DEBUG_BREAK;
//                         return;
//                     }
//                 }

//                 RecoveredSymbols recovered;

//                 t_fecal_decode.BeginCall();
//                 int decodeResult = fecal_decode(decoder, &recovered);
//                 t_fecal_decode.EndCall();

//                 if (decodeResult == Fecal_Success)
//                 {
//                     for (unsigned i = 0; i < recovered.Count; ++i)
//                     {
//                         if (!CheckPacket(
//                             recovered.Symbols[i].Data,
//                             recovered.Symbols[i].Bytes))
//                         {
//                             cout << "Error: Packet check failed for " << i << endl;
//                             FECAL_DEBUG_BREAK;
//                             return;
//                         }
//                     }

//                     // Decode success!
//                     break;
//                 }
//                 else if (decodeResult == Fecal_NeedMoreData)
//                 {
//                     //cout << "Needed more data to decode");
//                 }
//                 else
//                 {
//                     cout << "Error: Decode returned " << decodeResult << endl;
//                     FECAL_DEBUG_BREAK;
//                     return;
//                 }
//             }

//             // Decode success!

// #ifndef STATIC_ENCODER
//             fecal_free_encode(encoder);
// #endif
//             fecal_free_decode(decoder);

//             if (OriginalData[total_bytes] != 0xfe)
//             {
//                 cout << "Error: Corruption after final symbol" << endl;
//                 FECAL_DEBUG_BREAK;
//                 return;
//             }
//         }

//         float avgRecoveryRequired = recoveryRequired / (float)kTrials;

// #ifdef TEST_PRINT_API_TIMINGS
//         t_fecal_encoder_create.Print(kTrials);
//         t_fecal_encode.Print(kTrials);
//         t_fecal_decoder_create.Print(kTrials);
//         t_fecal_decoder_add_original.Print(kTrials);
//         t_fecal_decoder_add_recovery.Print(kTrials);
//         t_fecal_decode.Print(kTrials);
// #endif

//         float encode_input_MBPS = total_bytes * kTrials / (float)(t_fecal_encoder_create.TotalUsec + t_fecal_encode.TotalUsec);
//         float encode_setup_MBPS = total_bytes * kTrials / (float)t_fecal_encoder_create.TotalUsec;
//         float encode_output_MBPS = avgRecoveryRequired * symbol_bytes * kTrials / (float)(t_fecal_encoder_create.TotalUsec + t_fecal_encode.TotalUsec);
//         float decode_input_MBPS = total_bytes * kTrials / (float)(t_fecal_decode.TotalUsec);
//         float decode_output_MBPS = lossCount * symbol_bytes * kTrials / (float)(t_fecal_decode.TotalUsec);

//         //cout << "Using " << avgRecoveryRequired << " average recovery packets for " << lossCount << " losses of " << input_count << " original packets:" << endl;
//         cout << "Encoder(" << total_bytes / 1000000.f << " MB in " << input_count << " pieces, " << lossCount << " losses): Input=" << encode_input_MBPS << " MB/s, Output=" << encode_output_MBPS << " MB/s, (Encode create: " << encode_setup_MBPS << " MB/s)" << endl;
//         cout << "Decoder(" << total_bytes / 1000000.f << " MB in " << input_count << " pieces, " << lossCount << " losses): Input=" << decode_input_MBPS << " MB/s, Output=" << decode_output_MBPS << " MB/s, (Overhead = " << avgRecoveryRequired - lossCount << " pieces)" << endl << endl;
//     }

// #ifdef STATIC_ENCODER 
//     free(encoder);
// #endif
// }


// #define PACKET_SIZE 1448
// #define FEC_OVERHEAD_SIZE 2
// #define FEC_DATA_SIZE (PACKET_SIZE - FEC_OVERHEAD_SIZE)

// //------------------------------------------------------------------------------
// // Entrypoint

// extern "C" int main_benchmark(int argc, char **argv)
// {
//     SetCurrentThreadPriority();

//     FunctionTimer t_fecal_init("fecal_init");

//     t_fecal_init.BeginCall();
//     if (0 != fecal_init())
//     {
//         cout << "Failed to initialize" << endl;
//         return -1;
//     }
//     t_fecal_init.EndCall();
//     t_fecal_init.Print(1);

//     unsigned input_count = 5;
// #ifdef FECAL_DEBUG
//     unsigned symbol_bytes = 20;
// #else
//     unsigned symbol_bytes = FEC_DATA_SIZE;
// #endif

//     if (argc >= 2)
//         input_count = atoi(argv[1]);
//     if (argc >= 3)
//         symbol_bytes = atoi(argv[2]);

//     BasicTest(input_count, symbol_bytes);

//     getchar();

//     return 0;
// }

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
#include "fecal.h"

#include <list>
#include <memory>
#include <iostream>
#include <string>
using namespace std;

//#define TEST_DATA_ALL_SAME
//#define TEST_LOSE_FIRST_K_PACKETS


//------------------------------------------------------------------------------
// Windows

#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN

    #ifndef _WINSOCKAPI_
        #define DID_DEFINE_WINSOCKAPI
        #define _WINSOCKAPI_
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #ifndef _WIN32_WINNT
        #define _WIN32_WINNT 0x0601 /* Windows 7+ */
    #endif

    #include <windows.h>
#endif

#ifdef DID_DEFINE_WINSOCKAPI
    #undef _WINSOCKAPI_
    #undef DID_DEFINE_WINSOCKAPI
#endif


//------------------------------------------------------------------------------
// Threads

static bool SetCurrentThreadPriority()
{
#ifdef _WIN32
    return 0 != ::SetThreadPriority(::GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
#else
    return -1 != nice(2);
#endif
}


//------------------------------------------------------------------------------
// Timing

static uint64_t GetTimeUsec()
{
#ifdef _WIN32
    LARGE_INTEGER timeStamp = {};
    if (!::QueryPerformanceCounter(&timeStamp))
        return 0;
    static double PerfFrequencyInverse = 0.;
    if (PerfFrequencyInverse == 0.)
    {
        LARGE_INTEGER freq = {};
        if (!::QueryPerformanceFrequency(&freq) || freq.QuadPart == 0)
            return 0;
        PerfFrequencyInverse = 1000000. / (double)freq.QuadPart;
    }
    return (uint64_t)(PerfFrequencyInverse * timeStamp.QuadPart);
#else
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    return 1000000 * tv.tv_sec + tv.tv_usec;
#endif // _WIN32
}


//------------------------------------------------------------------------------
// Self-Checking Packet

static void WriteRandomSelfCheckingPacket(fecal::PCGRandom& prng, void* packet, unsigned bytes)
{
    uint8_t* buffer = (uint8_t*)packet;
#ifdef TEST_DATA_ALL_SAME
    if (bytes != 0)
#else
    if (bytes < 16)
#endif
    {
        FECAL_DEBUG_ASSERT(bytes >= 2);
        buffer[0] = (uint8_t)prng.Next();
        for (unsigned i = 1; i < bytes; ++i)
        {
            buffer[i] = buffer[0];
        }
    }
    else
    {
        uint32_t crc = bytes;
        *(uint32_t*)(buffer + 4) = bytes;
        for (unsigned i = 8; i < bytes; ++i)
        {
            uint8_t v = (uint8_t)prng.Next();
            buffer[i] = v;
            crc = (crc << 3) | (crc >> (32 - 3));
            crc += v;
        }
        *(uint32_t*)buffer = crc;
    }
}

static bool CheckPacket(const void* packet, unsigned bytes)
{
    uint8_t* buffer = (uint8_t*)packet;
#ifdef TEST_DATA_ALL_SAME
    if (bytes != 0)
#else
    if (bytes < 16)
#endif
    {
        if (bytes < 2)
            return false;

        uint8_t v = buffer[0];
        for (unsigned i = 1; i < bytes; ++i)
        {
            if (buffer[i] != v)
                return false;
        }
    }
    else
    {
        uint32_t crc = bytes;
        uint32_t readBytes = *(uint32_t*)(buffer + 4);
        if (readBytes != bytes)
            return false;
        for (unsigned i = 8; i < bytes; ++i)
        {
            uint8_t v = buffer[i];
            crc = (crc << 3) | (crc >> (32 - 3));
            crc += v;
        }
        uint32_t readCRC = *(uint32_t*)buffer;
        if (readCRC != crc)
            return false;
    }
    return true;
}


//------------------------------------------------------------------------------
// FunctionTimer

class FunctionTimer
{
public:
    FunctionTimer(const std::string& name)
    {
        FunctionName = name;
    }
    void BeginCall()
    {
        FECAL_DEBUG_ASSERT(t0 == 0);
        t0 = GetTimeUsec();
    }
    void EndCall()
    {
        FECAL_DEBUG_ASSERT(t0 != 0);
        uint64_t t1 = GetTimeUsec();
        ++Invokations;
        TotalUsec += t1 - t0;
        t0 = 0;
    }
    void Reset()
    {
        FECAL_DEBUG_ASSERT(t0 == 0);
        t0 = 0;
        Invokations = 0;
        TotalUsec = 0;
    }
    void Print(unsigned trials)
    {
        cout << FunctionName << " called " << Invokations / (float)trials << " times per trial (avg).  " << TotalUsec / (double)Invokations << " usec avg for all invokations.  " << TotalUsec / (float)trials << " usec (avg) of " << trials << " trials" << endl;
    }

    uint64_t t0 = 0;
    uint64_t Invokations = 0;
    uint64_t TotalUsec = 0;
    std::string FunctionName;
};


//------------------------------------------------------------------------------
// Utility: Deck Shuffling function

/*
    Given a PRNG, generate a deck of cards in a random order.
    The deck will contain elements with values between 0 and count - 1.
*/

static void ShuffleDeck16(fecal::PCGRandom &prng, uint16_t * GF256_RESTRICT deck, uint32_t count)
{
    deck[0] = 0;

    // If we can unroll 4 times,
    if (count <= 256)
    {
        for (uint32_t ii = 1;;)
        {
            uint32_t jj, rv = prng.Next();

            // 8-bit unroll
            switch (count - ii)
            {
            default:
                jj = (uint8_t)rv % ii;
                deck[ii] = deck[jj];
                deck[jj] = ii;
                ++ii;
                jj = (uint8_t)(rv >> 8) % ii;
                deck[ii] = deck[jj];
                deck[jj] = ii;
                ++ii;
                jj = (uint8_t)(rv >> 16) % ii;
                deck[ii] = deck[jj];
                deck[jj] = ii;
                ++ii;
                jj = (uint8_t)(rv >> 24) % ii;
                deck[ii] = deck[jj];
                deck[jj] = ii;
                ++ii;
                break;

            case 3:
                jj = (uint8_t)rv % ii;
                deck[ii] = deck[jj];
                deck[jj] = ii;
                ++ii;
            case 2:
                jj = (uint8_t)(rv >> 8) % ii;
                deck[ii] = deck[jj];
                deck[jj] = ii;
                ++ii;
            case 1:
                jj = (uint8_t)(rv >> 16) % ii;
                deck[ii] = deck[jj];
                deck[jj] = ii;
            case 0:
                return;
            }
        }
    }
    else
    {
        // For each deck entry,
        for (uint32_t ii = 1;;)
        {
            uint32_t jj, rv = prng.Next();

            // 16-bit unroll
            switch (count - ii)
            {
            default:
                jj = (uint16_t)rv % ii;
                deck[ii] = deck[jj];
                deck[jj] = ii;
                ++ii;
                jj = (uint16_t)(rv >> 16) % ii;
                deck[ii] = deck[jj];
                deck[jj] = ii;
                ++ii;
                break;

            case 1:
                jj = (uint16_t)rv % ii;
                deck[ii] = deck[jj];
                deck[jj] = ii;
            case 0:
                return;
            }
        }
    }
}


//------------------------------------------------------------------------------
// Tests

static void BasicTest(unsigned input_count, unsigned symbol_bytes, unsigned seed = 0)
{
    cout << "Testing performance for input_count=" << input_count << " and symbol_bytes=" << symbol_bytes << endl;

    static const unsigned final_bytes = symbol_bytes;

    for (unsigned lossCount = 1; lossCount <= input_count; ++lossCount)
    {
        const uint64_t total_bytes = (input_count - 1) * symbol_bytes + final_bytes;

        FunctionTimer t_fecal_encoder_create("fecal_encoder_create");
        FunctionTimer t_fecal_decoder_create("fecal_decoder_create");
        FunctionTimer t_fecal_encode("fecal_encode");
        FunctionTimer t_fecal_decoder_add_original("fecal_decoder_add_original");
        FunctionTimer t_fecal_decoder_add_recovery("fecal_decoder_add_recovery");
        FunctionTimer t_fecal_decode("fecal_decode");

        static const unsigned kTrials = 100;

        uint64_t recoveryRequired = 0;

        for (unsigned trial = 0; trial < kTrials; ++trial)
        {
            fecal::PCGRandom prng;
            prng.Seed(seed, lossCount * kTrials + trial);

            std::vector<uint8_t> OriginalData((size_t)total_bytes + 1);
            OriginalData[total_bytes] = 0xfe;
            std::vector<void*> input_data(input_count);

            uint8_t* data_buffer = &OriginalData[0];
            for (unsigned ii = 0; ii < input_count - 1; ++ii)
            {
                input_data[ii] = data_buffer;
                WriteRandomSelfCheckingPacket(prng, data_buffer, symbol_bytes);
                data_buffer += symbol_bytes;
            }
            input_data[input_count - 1] = data_buffer;
            WriteRandomSelfCheckingPacket(prng, data_buffer, final_bytes);

            t_fecal_encoder_create.BeginCall();
            FecalEncoder encoder = fecal_encoder_create(input_count, &input_data[0], total_bytes);
            t_fecal_encoder_create.EndCall();

            if (!encoder)
            {
                cout << "Error: Unable to create encoder" << endl;
                FECAL_DEBUG_BREAK;
                return;
            }

            t_fecal_decoder_create.BeginCall();
            FecalDecoder decoder = fecal_decoder_create(input_count, total_bytes);
            t_fecal_decoder_create.EndCall();

            if (!decoder)
            {
                cout << "Error: Unable to create decoder" << endl;
                FECAL_DEBUG_BREAK;
                return;
            }

#ifndef TEST_LOSE_FIRST_K_PACKETS
            FECAL_DEBUG_ASSERT(input_count <= 65536);
            std::vector<uint16_t> deck(input_count);
            ShuffleDeck16(prng, &deck[0], input_count);
#endif

            for (unsigned i = 0; i < input_count; ++i)
            {
                bool isLost = false;
#ifdef TEST_LOSE_FIRST_K_PACKETS
                if (i < lossCount)
                    isLost = true;
#else
                for (unsigned k = 0; k < lossCount; ++k)
                {
                    if (i == deck[k])
                    {
                        isLost = true;
                        break;
                    }
                }
#endif
                if (isLost)
                    continue;

                FecalSymbol original;
                original.Data = input_data[i];
                original.Bytes = symbol_bytes;
                if (i == input_count - 1)
                    original.Bytes = final_bytes;
                original.Index = i;

                t_fecal_decoder_add_original.BeginCall();
                int result = fecal_decoder_add_original(decoder, &original);
                t_fecal_decoder_add_original.EndCall();

                if (result)
                {
                    cout << "Error: Unable to add original data to decoder. error=" << result << endl;
                    FECAL_DEBUG_BREAK;
                    return;
                }
            }

            typedef std::shared_ptr< std::vector<uint8_t> > vecptr_t;
            std::list<vecptr_t> recoveryData;

#ifndef TEST_LOSE_FIRST_K_PACKETS
            deck.clear();
            deck.resize(input_count);
            ShuffleDeck16(prng, &deck[0], input_count);
#endif

            for (unsigned recoveryIndex = 0;; ++recoveryIndex)
            {
                vecptr_t data = std::make_shared< std::vector<uint8_t> >(symbol_bytes);
                recoveryData.push_back(data);

                FecalSymbol recovery;
                recovery.Index = recoveryIndex;
                recovery.Data = &data->at(0);
                recovery.Bytes = symbol_bytes;

                {
                    t_fecal_encode.BeginCall();
                    int result = fecal_encode(encoder, &recovery);
                    t_fecal_encode.EndCall();

                    if (result)
                    {
                        FECAL_DEBUG_BREAK;
                        cout << "Error: Unable to generate encoded data. error=" << result << endl;
                        return;
                    }
                }

                ++recoveryRequired;

                bool isLost = false;
                unsigned i = recoveryIndex;
#ifdef TEST_LOSE_FIRST_K_PACKETS
                if (i < lossCount)
                    isLost = true;
#else
                for (unsigned k = 0; k < lossCount; ++k)
                {
                    if (i == deck[k])
                    {
                        isLost = true;
                        break;
                    }
                }
#endif
                if (isLost)
                    continue;

                {
                    t_fecal_decoder_add_recovery.BeginCall();
                    int result = fecal_decoder_add_recovery(decoder, &recovery);
                    t_fecal_decoder_add_recovery.EndCall();
                    if (result)
                    {
                        cout << "Error: Unable to add recovery data to decoder. error=" << result << endl;
                        FECAL_DEBUG_BREAK;
                        return;
                    }
                }

                RecoveredSymbols recovered;

                t_fecal_decode.BeginCall();
                int decodeResult = fecal_decode(decoder, &recovered);
                t_fecal_decode.EndCall();

                if (decodeResult == Fecal_Success)
                {
                    for (unsigned i = 0; i < recovered.Count; ++i)
                    {
                        if (!CheckPacket(
                            recovered.Symbols[i].Data,
                            recovered.Symbols[i].Bytes))
                        {
                            cout << "Error: Packet check failed for " << i << endl;
                            FECAL_DEBUG_BREAK;
                            return;
                        }
                    }

                    // Decode success!
                    break;
                }
                else if (decodeResult == Fecal_NeedMoreData)
                {
                    //cout << "Needed more data to decode");
                }
                else
                {
                    cout << "Error: Decode returned " << decodeResult << endl;
                    FECAL_DEBUG_BREAK;
                    return;
                }
            }

            // Decode success!

            fecal_free(encoder);
            fecal_free(decoder);

            if (OriginalData[total_bytes] != 0xfe)
            {
                cout << "Error: Corruption after final symbol" << endl;
                FECAL_DEBUG_BREAK;
                return;
            }
        }

        float avgRecoveryRequired = recoveryRequired / (float)kTrials;

#ifdef TEST_PRINT_API_TIMINGS
        t_fecal_encoder_create.Print(kTrials);
        t_fecal_encode.Print(kTrials);
        t_fecal_decoder_create.Print(kTrials);
        t_fecal_decoder_add_original.Print(kTrials);
        t_fecal_decoder_add_recovery.Print(kTrials);
        t_fecal_decode.Print(kTrials);
#endif

        float encode_input_MBPS = total_bytes * kTrials / (float)(t_fecal_encoder_create.TotalUsec + t_fecal_encode.TotalUsec);
        float encode_setup_MBPS = total_bytes * kTrials / (float)t_fecal_encoder_create.TotalUsec;
        float encode_output_MBPS = avgRecoveryRequired * symbol_bytes * kTrials / (float)(t_fecal_encoder_create.TotalUsec + t_fecal_encode.TotalUsec);
        float decode_input_MBPS = total_bytes * kTrials / (float)(t_fecal_decode.TotalUsec);
        float decode_output_MBPS = lossCount * symbol_bytes * kTrials / (float)(t_fecal_decode.TotalUsec);

        //cout << "Using " << avgRecoveryRequired << " average recovery packets for " << lossCount << " losses of " << input_count << " original packets:" << endl;
        cout << "Encoder(" << total_bytes / 1000000.f << " MB in " << input_count << " pieces, " << lossCount << " losses): Input=" << encode_input_MBPS << " MB/s, Output=" << encode_output_MBPS << " MB/s, (Encode create: " << encode_setup_MBPS << " MB/s)" << endl;
        cout << "Decoder(" << total_bytes / 1000000.f << " MB in " << input_count << " pieces, " << lossCount << " losses): Input=" << decode_input_MBPS << " MB/s, Output=" << decode_output_MBPS << " MB/s, (Overhead = " << avgRecoveryRequired - lossCount << " pieces)" << endl << endl;
    }
}


//------------------------------------------------------------------------------
// Entrypoint

extern "C" int main_benchmark(int argc, char **argv)
{
    SetCurrentThreadPriority();

    FunctionTimer t_fecal_init("fecal_init");

    t_fecal_init.BeginCall();
    if (0 != fecal_init())
    {
        cout << "Failed to initialize" << endl;
        return -1;
    }
    t_fecal_init.EndCall();
    t_fecal_init.Print(1);

    unsigned input_count = 5;
#ifdef FECAL_DEBUG
    unsigned symbol_bytes = 20;
#else
    unsigned symbol_bytes = 1446;
#endif

    if (argc >= 2)
        input_count = atoi(argv[1]);
    if (argc >= 3)
        symbol_bytes = atoi(argv[2]);

    BasicTest(input_count, symbol_bytes);

    getchar();

    return 0;
}
