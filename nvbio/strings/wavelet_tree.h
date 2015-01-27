/*
 * nvbio
 * Copyright (c) 2011-2014, NVIDIA CORPORATION. All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *    * Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *    * Redistributions in binary form must reproduce the above copyright
 *      notice, this list of conditions and the following disclaimer in the
 *      documentation and/or other materials provided with the distribution.
 *    * Neither the name of the NVIDIA CORPORATION nor the
 *      names of its contributors may be used to endorse or promote products
 *      derived from this software without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL NVIDIA CORPORATION BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once

#include <nvbio/strings/string.h>
#include <nvbio/basic/packed_vector.h>
#include <nvbio/basic/primitives.h>
#include <nvbio/basic/cuda/sort.h>
#include <thrust/sort.h>
#include <algorithm>
#include <stack>

namespace nvbio {

///@addtogroup Strings
///@{

///
/// A shallow Wavelet Tree class, holding iterators to a bit-string representing the different bit-planes of the
/// output Wavelet Tree, and the tree structure itself, a binary heap, recording the sequence split of each node.
///
/// \tparam BitStreamIterator   the output bit-string type: must possess an iterator interface,
///                             and define a valid implementation of nvbio::assign();
///                             iterator_system<BitStreamIterator>::type is used to determine whether
///                             to apply host or device algorithms
///
/// \tparam IndexIterator       an iterator to an integer array representing the sequence splits of the
///                             Wavelet Tree's nodes, encoded as a full binary heap; the array must
///                             contain at least (2^SYMBOL_SIZE) - 1 entries
///
template <typename BitStreamIterator, typename IndexIterator>
struct WaveletTree
{
    // define the system tag
    typedef typename iterator_system<BitStreamIterator>::type       system_tag;
    typedef typename stream_traits<BitStreamIterator>::index_type   index_type;
    typedef BitStreamIterator                                       bit_iterator;
    typedef IndexIterator                                           index_iterator;

    /// constructor
    ///
    NVBIO_HOST_DEVICE
    WaveletTree(
        const uint32            _symbol_size = 0,
        const index_type        _size        = 0,
        const BitStreamIterator _bits        = BitStreamIterator(),
        const IndexIterator     _nodes       = IndexIterator(),
        const IndexIterator     _occ         = IndexIterator()) :
        m_symbol_size( _symbol_size ),
        m_size  ( _size ),
        m_bits  ( _bits ),
        m_nodes ( _nodes ),
        m_occ   ( _occ ) {}

    /// resize the tree
    ///
    NVBIO_HOST_DEVICE
    void resize(const uint32 _size, const uint32 _symbol_size)
    {
        m_size        = _size;
        m_symbol_size = _symbol_size;
    }

    /// return the number of bits per symbol
    ///
    NVBIO_HOST_DEVICE
    uint32 symbol_size() const { return m_symbol_size; }

    /// return the number of symbols
    ///
    NVBIO_HOST_DEVICE
    index_type size() const { return m_size; }

    /// return the bit string
    ///
    NVBIO_HOST_DEVICE
    BitStreamIterator bits() const { return m_bits; }

    /// return the node splits
    ///
    NVBIO_HOST_DEVICE
    IndexIterator splits() const { return m_nodes; }

    /// return the occurrences
    ///
    NVBIO_HOST_DEVICE
    IndexIterator occ() const { return m_occ; }

    /// return the number of bits set to b in the range [0,r] within node n at level l
    ///
    NVBIO_HOST_DEVICE
    index_type rank(const uint32 l, const uint32 node, const index_type node_begin, const index_type r, const uint8 b) const;

    /// return the i-th symbol
    ///
    NVBIO_HOST_DEVICE
    uint8 operator[] (const uint32 i) const;

    uint32              m_symbol_size;
    index_type          m_size;
    BitStreamIterator   m_bits;
    IndexIterator       m_nodes;
    IndexIterator       m_occ;
};

///
/// A Wavelet Tree storage class, holding a bit-string representing the different bit-planes of the
/// output Wavelet Tree, and the tree structure itself, a binary heap, recording the sequence split of each node.
///
/// \tparam SystemTag           the system memory space where this object's data is allocated
/// \tparam IndexType           the type of integers used to index this string
///
template <typename SystemTag, typename IndexType = uint32>
struct WaveletTreeStorage
{
    // define the system tag
    typedef SystemTag                                       system_tag;
    typedef IndexType                                       index_type;

    typedef PackedVector<system_tag,1u,true,index_type>     bit_vector_type;
    typedef nvbio::vector<system_tag,index_type>            index_vector_type;
    typedef typename bit_vector_type::iterator                    bit_iterator;
    typedef typename bit_vector_type::const_iterator        const_bit_iterator;
    typedef typename index_vector_type::iterator                  index_iterator;
    typedef typename index_vector_type::const_iterator      const_index_iterator;

    typedef WaveletTree<      bit_iterator,      index_iterator>       plain_view_type;
    typedef WaveletTree<const_bit_iterator,const_index_iterator> const_plain_view_type;

    /// constructor
    ///
    WaveletTreeStorage() :
        m_symbol_size( 0u ),
        m_size( 0u ) {}

    /// resize the tree
    ///
    void resize(const uint32 _size, const uint32 _symbol_size)
    {
        m_size        = _size;
        m_symbol_size = _symbol_size;

        const uint32 n_symbols = 1u << _symbol_size;

        m_bits.resize( m_size * m_symbol_size );
        m_nodes.resize( n_symbols );
        m_occ.resize( n_symbols + util::divide_ri( m_size * m_symbol_size, 32u ) );
    }

    /// return the number of bits per symbol
    ///
    NVBIO_HOST_DEVICE
    uint32 symbol_size() const { return m_symbol_size; }

    /// return the number of symbols
    ///
    NVBIO_HOST_DEVICE
    index_type size() const { return m_size; }

    /// return the bit string
    ///
    bit_iterator bits() { return m_bits.begin(); }

    /// return the nodes
    ///
    index_iterator splits() { return m_nodes.begin(); }

    /// return the occurrences
    ///
    index_iterator occ() { return m_occ.begin(); }

    /// return the bit string
    ///
    const_bit_iterator bits() const { return m_bits.begin(); }

    /// return the nodes
    ///
    const_index_iterator splits() const { return m_nodes.begin(); }

    /// return the occurrences
    ///
    const_index_iterator occ() const { return m_occ.begin(); }

    operator plain_view_type()
    {
        return plain_view_type(
            m_symbol_size,
            m_size,
            bits(),
            splits(),
            occ() );
    }
    operator const_plain_view_type() const
    {
        return const_plain_view_type(
            m_symbol_size,
            m_size,
            bits(),
            splits(),
            occ() );
    }

    uint32              m_symbol_size;
    index_type          m_size;
    bit_vector_type     m_bits;
    index_vector_type   m_nodes;
    index_vector_type   m_occ;
};


/// \relates WaveletTree
/// \relates WaveletTreeStorage
///
/// build a Wavelet Tree out of a string: the output consists of a bit-string representing
/// the different bit-planes of the output Wavelet Tree, and the tree structure itself,
/// a binary heap, recording the sequence split of each node.
///
/// \tparam string_type         the string type: must provide a random access iterator
///                             interface as well as define a proper stream_traits<StringType>
///                             expansion; particularly, stream_traits<StringType>::SYMBOL_SIZE
///                             is used to infer the number of bits needed to represent the
///                             symbols in the string's alphabet
///
template <typename system_tag, typename string_iterator, typename index_type>
void setup(
    const index_type                                        string_len,
    const string_iterator&                                  string,
    WaveletTreeStorage<system_tag,index_type>&              out_tree);

/// \relates WaveletTree
/// fetch the text character at position i in the rank dictionary
///
template <typename BitStreamIterator, typename IndexIterator, typename IndexType>
NVBIO_FORCEINLINE NVBIO_HOST_DEVICE
uint8 text(const WaveletTree<BitStreamIterator,IndexIterator>& tree, const IndexType i);

/// \relates WaveletTree
/// fetch the number of occurrences of character c in the substring [0,i]
///
/// \param dict         the rank dictionary
/// \param i            the end of the query range [0,i]
/// \param c            the query character
///
template <typename BitStreamIterator, typename IndexIterator, typename IndexType>
NVBIO_FORCEINLINE NVBIO_HOST_DEVICE
IndexType rank(
    const WaveletTree<BitStreamIterator,IndexIterator>& tree, const IndexType i, const uint32 c);

/// \relates WaveletTreeStorage
///
/// plain_view specialization
///
template <typename SystemTag, typename IndexType>
typename WaveletTreeStorage<SystemTag,IndexType>::plain_view_type plain_view(WaveletTreeStorage<SystemTag,IndexType>& tree)
{
    return tree;
}
/// \relates WaveletTreeStorage
///
/// plain_view specialization
///
template <typename SystemTag, typename IndexType>
typename WaveletTreeStorage<SystemTag,IndexType>::const_plain_view_type plain_view(const WaveletTreeStorage<SystemTag,IndexType>& tree)
{
    return tree;
}

///@} Strings

} // namespace nvbio

#include <nvbio/strings/wavelet_tree_inl.h>
