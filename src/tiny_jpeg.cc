/**
 * tiny_jpeg.cc
 *
 * Tiny JPEG Encoder
 *  - Sergio Gonzalez
 *
 * This is intended to be a readable and simple JPEG encoder.
 *   (only the baseline DCT method is implemented.)
 *
 * Tested on:
 *  Windows, MSVC
 *  OSX
 *
 * Public domain.
 *
 */

// ============================================================
// Public interface:
// ============================================================
//
//  In case of name-clashing of types, define TJE_DONT_CREATE_TYPES.

// Usage:
//  Takes src_data as 32 bit, 0xRRGGBBxx data.
//  Writes encoded image to dest_path.
int tje_encode_to_file(
        const unsigned char* src_data,
        const int width,
        const int height,
        const char* dest_path);
// TODO:
//  encode to buffer
//  specify quantization tables

// ============================================================
// Internal
// ============================================================
#ifdef TJE_IMPLEMENTATION
// ============================================================
// ============================================================

// C std lib
#include <inttypes.h>
#include <stdio.h>  // FILE, puts
#include <math.h>  // floorf, ceilf

#ifdef _WIN32

#include <windows.h>
#define snprintf sprintf_s  // Not quite the same but it works for us

#endif


#ifndef tje_malloc
#include <stdlib.h> // malloc, free
#define tje_malloc malloc
#ifdef tje_free
#error "tje_free is defined but tje_malloc is not"
#endif
#define tje_free(mem)\
    free(mem);\
    mem = NULL;
#endif

#ifdef TJE_DEBUG

#ifdef _WIN32

#define tje_log(msg) OutputDebugStringA(msg)

#elif defined(__linux__) || defined(__MACH__)
#include <stdio.h>
#define tje_log(msg) puts(msg)


#endif // WIN32 debug

#define tje_assert(expr) if (!(expr)) tje_assert_(#expr, __FILE__, __LINE__);

#else  // ELSE TJE_DEBUG

#define tje_log(msg)

#define tje_assert(expr)

#endif  // TJE_DEBUG
// ============================================================
// Define types
// ============================================================
#ifndef TJE_DONT_CREATE_TYPES
typedef char          int8;
typedef unsigned char uint8;
typedef int16_t       int16;
typedef uint16_t      uint16;
typedef int32_t       int32;
typedef uint32_t      uint32;
typedef int64_t       int64;
typedef uint64_t      uint64;
typedef int32         bool32;
#endif

typedef struct
{
    uint8*  ehuffsize[4];
    uint16* ehuffcode[4];

    uint8* ht_bits[4];
    uint8* ht_vals[4];

    uint8* qt_luma;
    uint8* qt_chroma;
} TJEState;

// ============================================================
// Table definitions.
//
// The spec defines default reasonably good quantization matrices and huffman
// specification tables.
//
//
// Instead of hard-coding the final huffman table, we only hard-code the table
// spec suggested by the specification, and then derive the full table from
// there.  This is only for didactic purposes but it might be useful if there
// ever is the case that we need to swap huffman tables from various sources.
// ============================================================


// K.1 - suggested luminance QT
static uint8 default_qt_luma[] =
{
   16,11,10,16, 24, 40, 51, 61,
   12,12,14,19, 26, 58, 60, 55,
   14,13,16,24, 40, 57, 69, 56,
   14,17,22,29, 51, 87, 80, 62,
   18,22,37,56, 68,109,103, 77,
   24,35,55,64, 81,104,113, 92,
   49,64,78,87,103,121,120,101,
   72,92,95,98,112,100,103, 99,
};

static uint8 default_qt_chroma_from_spec[] =
{
    // K.1 - suggested chrominance QT

   17,18,24,47,99,99,99,99,
   18,21,26,66,99,99,99,99,
   24,26,56,99,99,99,99,99,
   47,66,99,99,99,99,99,99,
   99,99,99,99,99,99,99,99,
   99,99,99,99,99,99,99,99,
   99,99,99,99,99,99,99,99,
   99,99,99,99,99,99,99,99,
};

static uint8 default_qt_chroma_from_paper[] =
{
    // Example QT from JPEG paper
   16,  12, 14, 14, 18, 24, 49, 72,
   11,  10, 16, 24, 40, 51, 61, 12,
   13,  17, 22, 35, 64, 92, 14, 16,
   22,  37, 55, 78, 95, 19, 24, 29,
   56,  64, 87, 98, 26, 40, 51, 68,
   81, 103, 112, 58, 57, 87, 109, 104,
   121,100, 60, 69, 80, 103, 113, 120,
   103, 55, 56, 62, 77, 92, 101, 99,
};

static uint8 default_qt_all_ones[] =
{
    1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1,
};

static uint8* default_qt_chroma = default_qt_chroma_from_spec;

// == Procedure to 'deflate' the huffman tree: JPEG spec, C.2

// Number of 16 bit values for every code length. (K.3.3.1)
static uint8 default_ht_luma_dc_len[16] =
{
    0,1,5,1,1,1,1,1,1,0,0,0,0,0,0,0
};
// values
static uint8 default_ht_luma_dc[12] =
{
    0,1,2,3,4,5,6,7,8,9,10,11
};

// Number of 16 bit values for every code length. (K.3.3.1)
static uint8 default_ht_chroma_dc_len[16] =
{
    0,3,1,1,1,1,1,1,1,1,1,0,0,0,0,0
};
// values
static uint8 default_ht_chroma_dc[12] =
{
    0,1,2,3,4,5,6,7,8,9,10,11
};

// Same as above, but AC coefficients.
static uint8 default_ht_luma_ac_len[16] =
{
    0,2,1,3,3,2,4,3,5,5,4,4,0,0,1,0x7d
};
static uint8 default_ht_luma_ac[] =
{
    0x01, 0x02, 0x03, 0x00, 0x04, 0x11, 0x05, 0x12, 0x21, 0x31, 0x41, 0x06, 0x13, 0x51, 0x61, 0x07,
    0x22, 0x71, 0x14, 0x32, 0x81, 0x91, 0xA1, 0x08, 0x23, 0x42, 0xB1, 0xC1, 0x15, 0x52, 0xD1, 0xF0,
    0x24, 0x33, 0x62, 0x72, 0x82, 0x09, 0x0A, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x25, 0x26, 0x27, 0x28,
    0x29, 0x2A, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3A, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49,
    0x4A, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5A, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69,
    0x6A, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7A, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89,
    0x8A, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9A, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7,
    0xA8, 0xA9, 0xAA, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6, 0xB7, 0xB8, 0xB9, 0xBA, 0xC2, 0xC3, 0xC4, 0xC5,
    0xC6, 0xC7, 0xC8, 0xC9, 0xCA, 0xD2, 0xD3, 0xD4, 0xD5, 0xD6, 0xD7, 0xD8, 0xD9, 0xDA, 0xE1, 0xE2,
    0xE3, 0xE4, 0xE5, 0xE6, 0xE7, 0xE8, 0xE9, 0xEA, 0xF1, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7, 0xF8,
    0xF9, 0xFA
};

static uint8 default_ht_chroma_ac_len[16] =
{
    0,2,1,2,4,4,3,4,7,5,4,4,0,1,2,0x77
};
static uint8 default_ht_chroma_ac[] =
{
    0x00, 0x01, 0x02, 0x03, 0x11, 0x04, 0x05, 0x21, 0x31, 0x06, 0x12, 0x41, 0x51, 0x07, 0x61, 0x71,
    0x13, 0x22, 0x32, 0x81, 0x08, 0x14, 0x42, 0x91, 0xA1, 0xB1, 0xC1, 0x09, 0x23, 0x33, 0x52, 0xF0,
    0x15, 0x62, 0x72, 0xD1, 0x0A, 0x16, 0x24, 0x34, 0xE1, 0x25, 0xF1, 0x17, 0x18, 0x19, 0x1A, 0x26,
    0x27, 0x28, 0x29, 0x2A, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3A, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48,
    0x49, 0x4A, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5A, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68,
    0x69, 0x6A, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7A, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87,
    0x88, 0x89, 0x8A, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9A, 0xA2, 0xA3, 0xA4, 0xA5,
    0xA6, 0xA7, 0xA8, 0xA9, 0xAA, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6, 0xB7, 0xB8, 0xB9, 0xBA, 0xC2, 0xC3,
    0xC4, 0xC5, 0xC6, 0xC7, 0xC8, 0xC9, 0xCA, 0xD2, 0xD3, 0xD4, 0xD5, 0xD6, 0xD7, 0xD8, 0xD9, 0xDA,
    0xE2, 0xE3, 0xE4, 0xE5, 0xE6, 0xE7, 0xE8, 0xE9, 0xEA, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7, 0xF8,
    0xF9, 0xFA
};


// ============================================================
// Code
// ============================================================

// Zig-zag order:
static uint8 zig_zag_indices[64] =
{
   0,  1,  5,  6, 14, 15, 27, 28,
   2,  4,  7, 13, 16, 26, 29, 42,
   3,  8, 12, 17, 25, 30, 41, 43,
   9, 11, 18, 24, 31, 40, 44, 53,
   10, 19, 23, 32, 39, 45, 52, 54,
   20, 22, 33, 38, 46, 51, 55, 60,
   21, 34, 37, 47, 50, 56, 59, 61,
   35, 36, 48, 49, 57, 58, 62, 63
};

// Memory order as big endian. 0xhilo -> 0xlohi which looks as 0xhilo in memory.
static uint16 be_word(uint16 le_word)
{
    uint8 lo = (uint8)(le_word & 0x00ff);
    uint8 hi = (uint8)((le_word & 0xff00) >> 8);
    return (((uint16)lo) << 8) | hi;
}

// avoid including standard header files. ====

static void tje_memcpy(void* dest, void* src, size_t num_bytes)
{
    uint8* dest_ptr = (uint8*)dest;
    uint8* src_ptr  = (uint8*)src;
    while(num_bytes--)
    {
        *dest_ptr++ = *src_ptr++;
    }
}

static void tje_memset(void* dest, uint8 val, size_t num_bytes)
{
    uint8* start = (uint8*)dest;
    while(num_bytes--)
    {
        *start++ = val;
    }
}
// ====

// ============================================================
// The following structs exist only for code clarity, debugability, and
// readability. They are used when writing to disk, but it is useful to have
// 1-packed-structs to document how the format works, and to inspect memory
// while developing.
// ============================================================

static const uint8 k_jfif_id[] = "JFIF";
static const uint8 k_com_str[] = "Created by Tiny JPEG Encoder";

#pragma pack(push)
#pragma pack(1)
typedef struct JPEGHeader_s
{
    uint16 SOI;
    // JFIF header.
    uint16 APP0;
    uint16 jfif_len;
    uint8  jfif_id[5];
    uint16 version;
    uint8  units;
    uint16 x_density;
    uint16 y_density;
    uint16  thumb_size;
    // Our signature
    uint16 com;
    uint16 com_len;
    char com_str[sizeof(k_com_str) - 1];
} JPEGHeader;

#pragma pack(1)
// Helper struct for FrameHeader (below).
typedef struct ComponentSpec_s
{
    uint8  component_id;
    uint8  sampling_factors;    // most significant 4 bits: horizontal. 4 LSB: vertical (A.1.1)
    uint8  qt;                  // Quantization table selector.
} ComponentSpec;

#pragma pack(1)
typedef struct FrameHeader_s
{
    uint16 SOF;
    uint16 len;                         // 8 + 3 * frame.num_components
    uint8  precision;                   // Sample precision (bits per sample).
    uint16 height;                      // aka. number of lines.
    uint16 width;                       // aka. number of samples per line.
    uint8  num_components;              // For this implementation, will be equal to 3.
    ComponentSpec component_spec[3];
} FrameHeader;

#pragma pack(1)
typedef struct FrameComponentSpec_s
{
    uint8 component_id;                 // Just as with ComponentSpec
    uint8 dc_ac;                        // (dc|ac)
} FrameComponentSpec;

#pragma pack(1)
typedef struct ScanHeader_s
{
    uint16 SOS;
    uint16 len;
    uint8 num_components;  // 3.
    FrameComponentSpec component_spec[3];
    uint8 first;  // 0
    uint8 last;  // 63
    uint8 ah_al;  // o
} ScanHeader;
#pragma pack(pop)

#ifdef TJE_DEBUG
static void tje_assert_(const char* expr, const char* file, int line)
{
    const size_t sz = 256;
    char buffer[sz];
    snprintf(buffer, sz, "Assertion Failed: \"%s\" -- %s : %d\n", expr, file, line);
    tje_log(buffer);
    exit(-1);
}
#endif

static void write_DQT(FILE* fd, uint8* matrix, uint8 id)
{
    tje_assert(fd);

    int16 DQT = be_word(0xffdb);
    fwrite(&DQT, sizeof(int16), 1, fd);
    int16 len = be_word(0x0043); // 2(len) + 1(id) + 64(matrix) = 67 = 0x43
    fwrite(&len, sizeof(int16), 1, fd);
    tje_assert(id < 4);
    uint8 precision_and_id = id;  // 0x0000 8 bits | 0x00id
    fwrite(&precision_and_id, sizeof(uint8), 1, fd);
    // Write matrix
    fwrite(matrix, 64*sizeof(uint8), 1, fd);
}

typedef enum
{
    DC = 0,
    AC = 1
} HuffmanTableClass;

static void write_DHT(
        FILE *fd,
        uint8* matrix_len,
        uint8* matrix_val,
        HuffmanTableClass ht_class,
        uint8 id)
{
    tje_assert(fd);

    int num_values = 0;
    for (int i = 0; i < 16; ++i)
    {
        num_values += matrix_len[i];
    }
    tje_assert(num_values <= 0xffff);

    int16 DHT = be_word(0xffc4);
    // 2(len) + 1(Tc|th) + 16 (num lengths) + ?? (num values)
    uint16 len = be_word(2 + 1 + 16 + (uint16)num_values);
    tje_assert(id < 4);
    uint8 tc_th = ((((uint8)ht_class) << 4) | id);

    fwrite(&DHT, sizeof(uint16), 1, fd);
    fwrite(&len, sizeof(uint16), 1, fd);
    fwrite(&tc_th, sizeof(uint8), 1, fd);
    fwrite(matrix_len, sizeof(uint8), 16, fd);
    fwrite(matrix_val, sizeof(uint8), num_values, fd);
}
// ============================================================
//  Huffman deflation code.
// ============================================================

// Returns all code sizes from the BITS specification (JPEG C.3)
static uint8* huff_get_code_lengths(uint8* bits, int64 size)
{
    // Add 1 for the trailing 0, used as a terminator in huff_get_codes()
    size_t huffsize_sz = sizeof(uint8) * (size + 1);
    uint8* huffsize = (uint8*)tje_malloc(huffsize_sz);

    int k = 0;
    for (int i = 0; i < 16; ++i)
    {
        for (int j = 0; j < bits[i]; ++j)
        {
            tje_assert(k < size);
            huffsize[k++] = (uint8)(i + 1);
        }
        tje_assert(k < size + 1);
        huffsize[k] = 0;
    }
    return huffsize;
}

static uint16* huff_get_codes(uint8* huffsize, int64 size)
{
    uint16 code = 0;
    int k = 0;
    uint8 sz = huffsize[0];
    uint16* codes = (uint16*)tje_malloc(sizeof(uint16) * (size));
    for(;;)
    {
        do
        {
            tje_assert(k < size)
            codes[k++] = code++;
        }
        while (huffsize[k] == sz);
        if (huffsize[k] == 0)
        {
            return codes;
        }
        do
        {
            code = code << 1;
            ++sz;
        }
        while(huffsize[k] != sz);
    }
}

static void huff_get_extended(
        uint8* huffval,
        uint8* huffsize,
        uint16* huffcode, int64 count,
        uint8** out_ehuffsize,
        uint16** out_ehuffcode)
{
    uint8* ehuffsize  = (uint8*)tje_malloc(sizeof(uint8) * 256);
    uint16* ehuffcode = (uint16*)tje_malloc(sizeof(uint16) * 256);

    tje_memset(ehuffsize, 0, sizeof(uint8) * 256);
    tje_memset(ehuffcode, 0, sizeof(uint16) * 256);

    int k = 0;
    do
    {
        uint8 val = huffval[k];
        ehuffcode[val] = huffcode[k];
        ehuffsize[val] = huffsize[k];
        k++;
    }
    while (k < count);

    *out_ehuffsize = ehuffsize;
    *out_ehuffcode = ehuffcode;
}
// ============================================================

// ============================================================
// +++ MAGIC +++
//  FDCT
//      Note -- The DCT code is taken from Jon Olick's JPEG implementation. He
//      appears to be take it from libjpeg. This code gets passed around so
//      much I am surprised it doesn't carry an unpronounceable disease.
//      Consider DCT code as magic unless one gets a copy of the Pennebaker &
//      Mitchel's JPEG book, or can read Japanese and has a copy of the paper
//      by Arai, Agui and Nakajima.
// ============================================================
static void fdct(
        float* d0,
        float* d1,
        float* d2,
        float* d3,
        float* d4,
        float* d5,
        float* d6,
        float* d7
        )
{
    float t0 = *d0 + *d7;
    float t7 = *d0 - *d7;
    float t1 = *d1 + *d6;
    float t6 = *d1 - *d6;
    float t2 = *d2 + *d5;
    float t5 = *d2 - *d5;
    float t3 = *d3 + *d4;
    float t4 = *d3 - *d4;

    // Even part

    float t10 = t0 + t3;
    float t13 = t0 - t3;
    float t11 = t1 + t2;
    float t12 = t1 - t2;

    *d0 = t10 + t11;
    *d4 = t10 - t11;

    float z1 = (t12 + t13) * 0.707106781f;

    *d2 = t13 + z1;
    *d6 = t13 - z1;

    // Odd part

    t10 = t4 + t5;
    t11 = t5 + t6;
    t12 = t6 + t7;

    float z5 = (t10 - t12) * 0.382683433f;
    float z2 = t10 * 0.5411961f + z5;
    float z4 = t12 * 1.306562965f + z5;
    float z3 = t11 * 0.707106781f;

    float z11 = t7 + z3;
    float z13 = t7 - z3;

    *d5 = z13 + z2;
    *d3 = z13 - z2;
    *d1 = z11 + z4;
    *d7 = z11 - z4;
}
// ============================================================
// ++++
// ============================================================

static void calculate_variable_length_int(int value, uint16 out[2])
{
    int abs_val = value;
    if ( value < 0 )
    {
        abs_val = -abs_val;
        --value;
    }
    out[1] = 1;
    while( abs_val >>= 1 )
    {
        ++out[1];
    }
    out[0] = value & ((1 << out[1]) - 1);
}

// Write bits to file.
static void write_bits(FILE* fd, uint32* bitbuffer, uint32* location, uint16 num_bits, uint16 bits)
{
    //   v-- location
    //  [                     ]   <-- bit buffer
    // 32                     0
    //
    // This call pushes to the bitbuffer and saves the location. Data is pushed
    // from most significant to less significant.
    // When we can write a full byte, we write a byte and shift.

    // Push the stack.
    *location += num_bits;
    *bitbuffer |= (bits << (32 - *location));
    while (*location >= 8)
    {
        // Grab the most significant byte.
        uint8 c = (uint8)((*bitbuffer) >> 24);
        // Write it to file.
        putc(c, fd);
        if (c == 0xff)  // Special case: tell JPEG this is not a marker.
        {
            putc(0, fd);
        }
        // Pop the stack.
        *bitbuffer <<= 8;
        *location -= 8;
    }
}

static void encode_and_write_DU(
        FILE* fd,
        float* mcu, float* qt,
        uint8* huff_dc_len, uint16* huff_dc_code,
        uint8* huff_ac_len, uint16* huff_ac_code,
        int* pred, uint32* bitbuffer, uint32* location)
{
    // +++ MAGIC +++
    //
    // Apply DCT to block
    // Rows:
    for (int i = 0; i < 64; i += 8)
    {
        fdct(
                &mcu[i + 0],
                &mcu[i + 1],
                &mcu[i + 2],
                &mcu[i + 3],
                &mcu[i + 4],
                &mcu[i + 5],
                &mcu[i + 6],
                &mcu[i + 7]);
    }
    // Columns
    for (int i = 0; i < 8; ++i)
    {
        fdct(
                &mcu[i + 0],
                &mcu[i + 8],
                &mcu[i + 16],
                &mcu[i + 24],
                &mcu[i + 32],
                &mcu[i + 40],
                &mcu[i + 48],
                &mcu[i + 56]);
    }
    // ++++

    int8 du[64];  // Data unit in zig-zag order

    for (int i = 0; i < 64; ++i)
    {
        float fval = mcu[i] * qt[i];
        // TODO: implement a rounding function to avoid dependence on math.h
        int8 val = (int8)((fval > 0) ? floorf(fval + 0.5f) : ceilf(fval - 0.5f));
        du[zig_zag_indices[i]] = val;
    }

    uint16 bits[2];

    // Encode DC coefficient.
    int diff = du[0] - *pred;
    *pred = du[0];
    if (diff != 0)
    {
        calculate_variable_length_int(diff, bits);
        write_bits(fd, bitbuffer, location, huff_dc_len[bits[1]], huff_dc_code[bits[1]]);   // (SIZE)
        write_bits(fd, bitbuffer, location, bits[1], bits[0]);                              // (AMPLITUDE)
    }
    else
    {
        write_bits(fd, bitbuffer, location, huff_dc_len[0], huff_dc_code[0]);
    }

    // ==== Encode AC coefficients ====

    int last_non_zero_i = 63;
    // Find the last non-zero element.
    for (int i = 63; i >= 0; --i)
    {
        if (du[i] != 0)
        {
            last_non_zero_i = i;
            break;
        }
    }

    if (last_non_zero_i == 63)
    {
        // EOB
        write_bits(fd, bitbuffer, location, huff_ac_len[0], huff_ac_code[0]);
        return;
    }

    for (int i = 1; i <= last_non_zero_i; ++i)
    {
        // If zero, increase count. If >=15, encode (FF,00)
        int zero_count = 0;
        while (du[i] == 0)
        {
            ++zero_count;
            ++i;
            if (zero_count == 16)
            {
                // encode (ff,00) == 0xf0
                write_bits(fd, bitbuffer, location, huff_ac_len[0xf0], huff_ac_code[0xf0]);
                zero_count = 0;
            }
        }
        {
            calculate_variable_length_int(du[i], bits);

            tje_assert(zero_count <= 0xf);
            tje_assert(bits[1] <= 10);

            uint16 sym1 = ((uint16)zero_count << 4) | bits[1];

            tje_assert(huff_ac_len[sym1] != 0);

            // Write symbol 1
            write_bits(fd, bitbuffer, location, huff_ac_len[sym1], huff_ac_code[sym1]);     // (RUNLENGTH, SIZE)
            // Write symbol 2
            write_bits(fd, bitbuffer, location, bits[1], bits[0]);                          // (AMPLITUDE)
        }
    }

    if (last_non_zero_i != 63)
    {
        // write EOB HUFF(00,00)
        write_bits(fd, bitbuffer, location, huff_ac_len[0], huff_ac_code[0]);
    }

    return;
}

enum
{
    LUMA_DC,
    LUMA_AC,
    CHROMA_DC,
    CHROMA_AC,
};

// Set up huffman tables in state.
static void tje_init (TJEState* state)
{
    uint64 spec_tables_len[4] = {};
    for (int i = 0; i < 4; ++i)
    {
        for (int k = 0; k < 16; ++k)
        {
            spec_tables_len[i] += state->ht_bits[i][k];
        }
    }

    // Fill out the extended tables..
    {
        uint8* huffsize[4] = {};
        uint16* huffcode[4] = {};
        for (int i = 0; i < 4; ++i)
        {
            huffsize[i] = huff_get_code_lengths(state->ht_bits[i], spec_tables_len[i]);
            huffcode[i] = huff_get_codes(huffsize[i], spec_tables_len[i]);
        }
        for (int i = 0; i < 4; ++i)
        {
            int64 count = spec_tables_len[i];
            huff_get_extended(
                    state->ht_vals[i],
                    huffsize[i],
                    huffcode[i], count,
                    &(state->ehuffsize[i]),
                    &(state->ehuffcode[i]));
        }
        // Free non-ordered huffman data.
        for (int i = 0; i < 4; ++i)
        {
            tje_free(huffsize[i]);
            tje_free(huffcode[i]);
        }
    }

}

static int tje_encode_main(
        TJEState* state,
        const unsigned char* src_data,
        const int width,
        const int height,
        const char* dest_path)
{
    int res = 0;

    // TODO: support arbitrary resolutions.
    if (((height % 8) != 0) || ((width % 8) != 0))
    {
        tje_log("Supported resolutions are width and height multiples of 8.");
        return 1;
    }

    FILE* file_out = fopen(dest_path, "wb");
    if (!file_out)
    {
        tje_log("Could not open file for writing.");
        return 1;
    }

    // ============================================================
    //  Actual write-to-file.
    // ============================================================

    { // Write header
        JPEGHeader header;
        // JFIF header.
        header.SOI = be_word(0xffd8);  // Sequential DCT
        header.APP0 = be_word(0xffe0);
        header.jfif_len = be_word(0x0016);
        tje_memcpy(header.jfif_id, (void*)k_jfif_id, 5);
        header.version = be_word(0x0102);
        header.units = 0x01;
        header.x_density = be_word(0x0060);  // 96 DPI
        header.y_density = be_word(0x0060);  // 96 DPI
        header.thumb_size = 0;
        // Comment
        header.com = be_word(0xfffe);
        tje_memcpy(header.com_str, (void*)k_com_str, sizeof(k_com_str) - 1); // Skip the 0-bit
        fwrite(&header, sizeof(JPEGHeader), 1, file_out);
    }

    // Write quantization tables.
    write_DQT(file_out, state->qt_luma, 0x00);
    write_DQT(file_out, state->qt_chroma, 0x01);

    {  // Write the frame marker.
        FrameHeader header;
        header.SOF = be_word(0xffc0);
        header.len = be_word(8 + 3 * 3);
        header.precision = 8;
        tje_assert(width <= 0xffff);
        tje_assert(height <= 0xffff);
        header.height = be_word((uint16)height);
        header.width = be_word((uint16)width);
        header.num_components = 3;
        uint8 tables[3] =
        {
            0,  // Luma component gets luma table (see write_DQT call above.)
            1,  // Chroma component gets chroma table
            1,  // Chroma component gets chroma table
        };
        for (int i = 0; i < 3; ++i)
        {
            ComponentSpec spec;
            spec.component_id = (uint8)(i + 1);  // No particular reason. Just 1, 2, 3.
            spec.sampling_factors = (uint8)0x11;
            spec.qt = tables[i];

            header.component_spec[i] = spec;
        }
        // Write to file.
        fwrite(&header, sizeof(FrameHeader), 1, file_out);
    }

    write_DHT(file_out, state->ht_bits[LUMA_DC], state->ht_vals[LUMA_DC], DC, 0);
    write_DHT(file_out, state->ht_bits[LUMA_AC], state->ht_vals[LUMA_AC], AC, 0);
    write_DHT(file_out, state->ht_bits[CHROMA_DC], state->ht_vals[CHROMA_DC], DC, 1);
    write_DHT(file_out, state->ht_bits[CHROMA_AC], state->ht_vals[CHROMA_AC], AC, 1);

    // Write start of scan
    {
        ScanHeader header;
        header.SOS = be_word(0xffda);
        header.len = be_word((uint16)(6 + (2 * 3)));
        header.num_components = 3;

        uint8 tables[3] =
        {
            0x00,
            0x11,
            0x11,
        };
        for (int i = 0; i < 3; ++i)
        {
            FrameComponentSpec cs;
            // Must be equal to component_id from frame header above.
            cs.component_id = (uint8)(i + 1);
            cs.dc_ac = (uint8)tables[i];

            header.component_spec[i] = cs;
        }
        header.first = 0;
        header.last  = 63;
        header.ah_al = 0;
        fwrite(&header, sizeof(ScanHeader), 1, file_out);
    }

    // Write compressed data.
    static const int bytes_per_pixel = 3;  // Only supporting RGB right now..

    // Convert qt_luma and qt_chroma to what the magic algorithm expects.
    float fdtbl_luma[64];
    float fdtbl_chroma[64];

    // +++ MAGIC +++
    {
        static const float aasf[] =
        {
            1.0f * 2.828427125f, 1.387039845f * 2.828427125f, 1.306562965f * 2.828427125f,
            1.175875602f * 2.828427125f, 1.0f * 2.828427125f, 0.785694958f * 2.828427125f,
            0.541196100f * 2.828427125f, 0.275899379f * 2.828427125f
        };

        {
            int k = 0;
            for (int j = 0; j < 8; ++j)
            {
                for (int i = 0; i < 8; ++i)
                {
                    fdtbl_luma[k]   = 1.0f / (state->qt_luma  [zig_zag_indices[k]] * aasf[i] * aasf[j]);
                    fdtbl_chroma[k] = 1.0f / (state->qt_chroma[zig_zag_indices[k]] * aasf[i] * aasf[j]);
                    k++;
                }
            }
        }

    }
    // ++++

    float du_y[64];
    float du_b[64];
    float du_r[64];

    // Set diff to 0.
    int pred_y = 0;
    int pred_b = 0;
    int pred_r = 0;

    // Bit stack
    uint32 bitbuffer = 0;
    uint32 location = 0;

    for (int y = 0; y < height; y += 8)
    {
        for (int x = 0; x < width; x += 8)
        {
            // Block loop: ====
            for (int off_y = 0; off_y < 8; ++off_y)
            {
                for (int off_x = 0; off_x < 8; ++off_x)
                {
                    int index = (((y + off_y) * width) + (x + off_x)) * bytes_per_pixel;
                    tje_assert(index < width * height * bytes_per_pixel);

                    uint8 r = src_data[index + 0];
                    uint8 g = src_data[index + 1];
                    uint8 b = src_data[index + 2];

                    float y  = 0.29900f * r + 0.58700f * g + 0.11400f * b - 128;
                    float cb = -0.1687f * r - 0.3312f  * g + 0.50000f * b;
                    float cr = 0.50000f * r - 0.41869f * g - 0.08131f * b;


                    int block_index = (off_y * 8 + off_x);
                    du_y[block_index] = y;
                    du_b[block_index] = cb;
                    du_r[block_index] = cr;
                }
            }
            // ====

            // Process block:

            encode_and_write_DU(file_out,
                    du_y, fdtbl_luma,
                    state->ehuffsize[LUMA_DC], state->ehuffcode[LUMA_DC],
                    state->ehuffsize[LUMA_AC], state->ehuffcode[LUMA_AC],
                    &pred_y, &bitbuffer, &location);
            encode_and_write_DU(file_out,
                    du_b, fdtbl_chroma,
                    state->ehuffsize[CHROMA_DC], state->ehuffcode[CHROMA_DC],
                    state->ehuffsize[CHROMA_AC], state->ehuffcode[CHROMA_AC],
                    &pred_b, &bitbuffer, &location);
            encode_and_write_DU(file_out,
                    du_r, fdtbl_chroma,
                    state->ehuffsize[CHROMA_DC], state->ehuffcode[CHROMA_DC],
                    state->ehuffsize[CHROMA_AC], state->ehuffcode[CHROMA_AC],
                    &pred_r, &bitbuffer, &location);
        }
    }

    // Finish the image.
    {
        // flush
        {
            tje_assert(location < 8);
            write_bits(file_out, &bitbuffer, &location, (uint16)(8 - location), 0xff);
        }
        uint16 EOI = be_word(0xffd9);
        fwrite(&EOI, sizeof(uint16), 1, file_out);
    }
    res |= fclose(file_out);

    return res;
}

void tje_deinit(TJEState* state)
{
    // Free huffman tables.
    for (int i = 0; i < 4; ++i)
    {
        tje_free(state->ehuffcode[i]);
        tje_free(state->ehuffsize[i]);
    }

}

// Define public interface.
int tje_encode_to_file(
        const unsigned char* src_data,
        const int width,
        const int height,
        const char* dest_path)
{
    TJEState state = {};

    state.ht_bits[LUMA_DC]      = default_ht_luma_dc_len;
    state.ht_bits[LUMA_AC]      = default_ht_luma_ac_len;
    state.ht_bits[CHROMA_DC]    = default_ht_chroma_dc_len;
    state.ht_bits[CHROMA_AC]    = default_ht_chroma_ac_len;

    state.qt_luma   = default_qt_luma;
    state.qt_chroma = default_qt_chroma;

    state.ht_vals[LUMA_DC]      = default_ht_luma_dc;
    state.ht_vals[LUMA_AC]     = default_ht_luma_ac;
    state.ht_vals[CHROMA_DC]    = default_ht_chroma_dc;
    state.ht_vals[CHROMA_AC]    = default_ht_chroma_ac;

    tje_init(&state);

    int result = tje_encode_main(&state, src_data, width, height, dest_path);
    tje_assert(result == 0);

    tje_deinit(&state);

    return result;
}
// ============================================================
#endif // TJE_IMPLEMENTATION
// ============================================================
