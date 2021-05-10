/*
 * Copyright (c) 2020-2021, NVIDIA CORPORATION. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

/**
 * @file binary_file.hpp
 * @author Nikolaus Binder, NVIDIA
 * @details Reader/writer for binary files containing a dict of multidimensional float arrays
 *          Files are either generated by converter.py from a matlab file or stored using BinaryFile::write.
 *
 * Structure of the binary file
 * num_keys: i32
 * keys: for all keys: length of the key (i32), followed by the chars
 * data: for all keys: number of components (i32), followed by the size of each component (i32), followed by the actual data
 *
 * Example (foo: 13x32x32x2, bar: 12x19):
 * 2
 * 3
 * foo
 * 3
 * bar
 * 4
 * 13
 * 32
 * 32
 * 2
 * [13*32*32*2 floats]
 * 2
 * 12
 * 19
 * [12*19 floats]
 */

#pragma once

#include <map>
#include <stdexcept>
#include <stdio.h>
#include <string.h> // for memcpy
#include <string>
#include <vector>

namespace BinaryFile
{
/**
	 * Reads an int32_t from the file
	 */
inline int32_t read_int32( FILE* fp )
{
    int32_t i;
    if ( fread( &i, sizeof( int32_t ), 1, fp ) != 1 )
        throw std::runtime_error( "File truncated." );
    return i;
}

/**
	 * Writes an int32_t to the file
	 */
inline void write_int32( FILE* fp, const int32_t i )
{
    if ( fwrite( &i, sizeof( int32_t ), 1, fp ) != 1 )
        throw std::runtime_error( "Could not write int32_t to file." );
}

/**
	 * Reads `len` characters from the file and stores them in `dst`
	 */
inline void read_chars( FILE* fp, char* dst, const size_t len )
{
    if ( fread( dst, sizeof( char ), len, fp ) != len )
        throw std::runtime_error( "File truncated." );
}

/**
	 * Reads `len` characters from the file and returns them as a std::string
	 */
inline std::string read_string( FILE* fp, const int32_t len )
{
    std::string result( len, ' ' );
    read_chars( fp, &result[ 0 ], len );
    return result;
}

/**
	 * Writes a string to the file
	 */
inline void write_string( FILE* fp, const std::string& s )
{
    write_int32( fp, s.length() );
    if ( fwrite( s.data(), sizeof( char ), s.length(), fp ) != s.length() )
        throw std::runtime_error( "Could not write string to file." );
}

/**
	 * Reads a total number of `len` 32-bit floats from the file
	 */
inline void read_floats( FILE* fp, float* dst, const size_t len )
{
    if ( fread( dst, sizeof( float ), len, fp ) != len )
        throw std::runtime_error( "File truncated." );
}

/**
	 * Writes a total number of `len` 32-bit floats to the file
	 */
inline void write_floats( FILE* fp, const float* data, const size_t len )
{
    if ( fwrite( data, sizeof( float ), len, fp ) != len )
        throw std::runtime_error( "Could not write float array to file." );
}

/**
	 * RAII wrapper for FILE*
	 * Ensures that file is always properly closed (incl. exceptions).
	 */
class FPWrapper
{
    FILE* m_fp = nullptr;

public:
    inline FPWrapper( const char* filename, const char* mode )
    {
        m_fp = fopen( filename, mode );
        if ( !m_fp )
            throw std::runtime_error( "File not found or not accessible." );
    }
    inline FPWrapper& operator=( FPWrapper& ) = delete; // cannot be copied (would cause double free), only constructed
    inline ~FPWrapper()
    {
        if ( m_fp != nullptr )
            fclose( m_fp );
    }
    inline operator FILE*() { return m_fp; }
};

/**
	 * A multidimensional array in row-major order. Stores the size of each component in `sizes`.
	 * Example for calculating the index:
	 * const int32_t index = i * sizes[1] * sizes[2] + j * sizes[2] + k;
	 */
struct MultidimArray
{
    std::vector<int32_t> sizes;
    std::vector<float> data;

    inline MultidimArray() {}

    inline MultidimArray( const std::vector<size_t>& new_sizes )
    {
        sizes.resize( new_sizes.size() );

        size_t overall_size = 1, i = 0;
        for ( const auto s : new_sizes )
            overall_size *= sizes[ i++ ] = s;

        data.resize( overall_size );
    }
};

/**
	 * Reads the binary data from the file and returns a std::map that contains the MultidimArrays
	 */
inline std::map<std::string, MultidimArray> read( const char* filename, const bool verbose = false )
{
    try
    {
        std::map<std::string, MultidimArray> data;

        FPWrapper fp( filename, "rb" );

        std::vector<std::string> keys( read_int32( fp ) );
        for ( auto& k : keys )
            k = read_string( fp, read_int32( fp ) );

        for ( const auto& k : keys )
        {
            if ( verbose )
                printf( "%s: [", k.c_str() );

            data[ k ] = MultidimArray();
            data[ k ].sizes.resize( read_int32( fp ) );

            int32_t overall_size = 1;
            for ( auto& s : data[ k ].sizes )
            {
                s = read_int32( fp );
                overall_size *= s;
                if ( verbose )
                    printf( "%i%s", s, &s != &data[ k ].sizes.back() ? ", " : "]\n" );
            }

            data[ k ].data.resize( overall_size );
            read_floats( fp, data[ k ].data.data(), overall_size );
        }

        return data;
    }
    catch ( const std::runtime_error& err )
    {
        throw std::runtime_error( std::string( "Error loading the file " ) + filename + ": " + err.what() );
    }
}

/**
	 * Writes the std::map that contains the MultidimArrays to a file with the specified name
	 */
inline void write( const char* filename, const std::map<std::string, MultidimArray>& data )
{
    try
    {
        FPWrapper fp( filename, "wb" );

        write_int32( fp, data.size() );

        for ( const auto& d : data )
            write_string( fp, d.first );

        for ( const auto& d : data )
        {
            write_int32( fp, d.second.sizes.size() );

            for ( const auto& s : d.second.sizes )
                write_int32( fp, s );

            write_floats( fp, d.second.data.data(), d.second.data.size() );
        }
    }
    catch ( const std::runtime_error& err )
    {
        throw std::runtime_error( std::string( "Error writing to the file " ) + filename + ": " + err.what() );
    }
}
};
