#!/usr/bin/env python

# Copyright (c) 2018, DIANA-HEP
# All rights reserved.
# 
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
# 
# * Redistributions of source code must retain the above copyright notice, this
#   list of conditions and the following disclaimer.
# 
# * Redistributions in binary form must reproduce the above copyright notice,
#   this list of conditions and the following disclaimer in the documentation
#   and/or other materials provided with the distribution.
# 
# * Neither the name of the copyright holder nor the names of its
#   contributors may be used to endorse or promote products derived from
#   this software without specific prior written permission.
# 
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
# DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
# FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
# SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
# CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
# OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

import numbers

import numpy

import awkward.array.base
import awkward.util
from awkward.array.chunked import ChunkedArray, PartitionedArray, AppendableArray
from awkward.array.indexed import IndexedArray, ByteIndexedArray, IndexedMaskedArray, UnionArray
from awkward.array.jagged import JaggedArray, ByteJaggedArray
from awkward.array.masked import MaskedArray, BitMaskedArray
from awkward.array.sparse import SparseArray
from awkward.array.table import Table
from awkward.array.virtual import VirtualArray, VirtualObjectArray, PersistentArray

def fromiter(iterable, chunksize=1024, references=False):
    if references:
        raise NotImplementedError    # keep all ids in a hashtable to create pointers

    tobytes = lambda x: x.tobytes()

    def add(chunks, offsets, newchunk, ismine, promote, fillobj):
        if len(chunks) == 0 or offsets[-1] - offsets[-2] == len(chunks[-1]):
            chunks.append(newchunk())
            offsets.append(offsets[-1])

        if ismine(chunks[-1]):
            chunks[-1] = promote(chunks[-1])
            fillobj(chunks[-1], offsets[-1] - offsets[-2])
            offsets[-1] += 1

        elif isinstance(chunks[-1], IndexedMaskedArray) and len(chunks[-1]._content) == 0:
            chunks[-1]._content = newchunk()

            nextindex = chunks[-1]._nextindex
            chunks[-1]._nextindex += 1
            chunks[-1]._index[offsets[-1] - offsets[-2]] = nextindex

            chunks[-1]._content = promote(chunks[-1]._content)
            fillobj(chunks[-1]._content, nextindex)
            offsets[-1] += 1

        elif isinstance(chunks[-1], IndexedMaskedArray) and ismine(chunks[-1]._content):
            nextindex = chunks[-1]._nextindex
            chunks[-1]._nextindex += 1
            chunks[-1]._index[offsets[-1] - offsets[-2]] = nextindex

            chunks[-1]._content = promote(chunks[-1]._content)
            fillobj(chunks[-1]._content, nextindex)
            offsets[-1] += 1

        else:
            if not isinstance(chunks[-1], UnionArray):
                chunks[-1] = UnionArray(numpy.empty(chunksize, dtype=awkward.array.base.AwkwardArray.INDEXTYPE),
                                        numpy.empty(chunksize, dtype=awkward.array.base.AwkwardArray.INDEXTYPE),
                                        [chunks[-1]])
                chunks[-1]._nextindex = [offsets[-1] - offsets[-2]]
                chunks[-1]._tags[: offsets[-1] - offsets[-2]] = 0
                chunks[-1]._index[: offsets[-1] - offsets[-2]] = numpy.arange(offsets[-1] - offsets[-2], dtype=awkward.array.base.AwkwardArray.INDEXTYPE)
                chunks[-1]._contents = list(chunks[-1]._contents)

            if not any(ismine(content) for content in chunks[-1]._contents):
                chunks[-1]._nextindex.append(0)
                chunks[-1]._contents.append(newchunk())

            for tag in range(len(chunks[-1]._contents)):
                if ismine(chunks[-1]._contents[tag]):
                    nextindex = chunks[-1]._nextindex[tag]
                    chunks[-1]._nextindex[tag] += 1

                    chunks[-1]._contents[tag] = promote(chunks[-1]._contents[tag])
                    fillobj(chunks[-1]._contents[tag], nextindex)

                    chunks[-1]._tags[offsets[-1] - offsets[-2]] = tag
                    chunks[-1]._index[offsets[-1] - offsets[-2]] = nextindex

                    offsets[-1] += 1
                    break

    def recurse(obj, chunks, offsets):
        if obj is None:
            # anything with None -> IndexedMaskedArray

            if len(chunks) == 0 or offsets[-1] - offsets[-2] == len(chunks[-1]):
                chunks.append(IndexedMaskedArray(numpy.empty(chunksize, dtype=awkward.array.base.AwkwardArray.INDEXTYPE), []))
                chunks[-1]._nextindex = 0
                offsets.append(offsets[-1])

            if not isinstance(chunks[-1], IndexedMaskedArray):
                chunks[-1] = IndexedMaskedArray(numpy.empty(chunksize, dtype=awkward.array.base.AwkwardArray.INDEXTYPE), chunks[-1])
                chunks[-1]._index[: offsets[-1] - offsets[-2]] = numpy.arange(offsets[-1] - offsets[-2], dtype=awkward.array.base.AwkwardArray.INDEXTYPE)
                chunks[-1]._nextindex = offsets[-1] - offsets[-2]

            chunks[-1]._index[offsets[-1] - offsets[-2]] = chunks[-1]._maskedwhen
            offsets[-1] += 1

        elif isinstance(obj, (bool, numpy.bool, numpy.bool_)):
            # bool -> Numpy bool_

            def newchunk():
                return numpy.empty(chunksize, dtype=numpy.bool_)

            def ismine(x):
                return isinstance(x, numpy.ndarray) and x.dtype == numpy.dtype(numpy.bool_)

            def promote(x):
                return x

            def fillobj(array, where):
                array[where] = obj

            add(chunks, offsets, newchunk, ismine, promote, fillobj)

        elif isinstance(obj, (numbers.Integral, numpy.integer)):
            # int -> Numpy int64, float64, or complex128 (promotes to largest)

            def newchunk():
                return numpy.empty(chunksize, dtype=numpy.int64)

            def ismine(x):
                return isinstance(x, numpy.ndarray) and issubclass(x.dtype.type, numpy.number)

            def promote(x):
                return x

            def fillobj(array, where):
                array[where] = obj

            add(chunks, offsets, newchunk, ismine, promote, fillobj)

        elif isinstance(obj, (numbers.Real, numpy.floating)):
            # float -> Numpy int64, float64, or complex128 (promotes to largest)

            def newchunk():
                return numpy.empty(chunksize, dtype=numpy.int64)

            def ismine(x):
                return isinstance(x, numpy.ndarray) and issubclass(x.dtype.type, numpy.number)

            def promote(x):
                if issubclass(x.dtype.type, numpy.floating):
                    return x
                else:
                    return x.astype(numpy.float64)

            def fillobj(array, where):
                array[where] = obj

            add(chunks, offsets, newchunk, ismine, promote, fillobj)

        elif isinstance(obj, (numbers.Complex, numpy.complex, numpy.complexfloating)):
            # complex -> Numpy int64, float64, or complex128 (promotes to largest)

            def newchunk():
                return numpy.empty(chunksize, dtype=numpy.complex128)

            def ismine(x):
                return isinstance(x, numpy.ndarray) and issubclass(x.dtype.type, numpy.number)

            def promote(x):
                if issubclass(x.dtype.type, numpy.complexfloating):
                    return x
                else:
                    return x.astype(numpy.complex128)

            def fillobj(array, where):
                array[where] = obj

            add(chunks, offsets, newchunk, ismine, promote, fillobj)

        elif isinstance(obj, bytes):
            # bytes -> VirtualObjectArray of JaggedArray

            def newchunk():
                out = VirtualObjectArray(tobytes, JaggedArray.fromoffsets(
                    numpy.zeros(chunksize + 1, dtype=awkward.array.base.AwkwardArray.INDEXTYPE),
                    AppendableArray.empty(lambda: numpy.empty(chunksize, dtype=awkward.array.base.AwkwardArray.CHARTYPE))))
                out._content._starts[0] = 0
                return out

            def ismine(x):
                return isinstance(x, VirtualObjectArray) and x._generator is tobytes

            def promote(x):
                return x

            def fillobj(array, where):
                array._content._stops[where] = array._content._starts[where] + len(obj)
                array._content._content.extend(numpy.fromstring(obj, dtype=awkward.array.base.AwkwardArray.CHARTYPE))
                
            add(chunks, offsets, newchunk, ismine, promote, fillobj)

        elif isinstance(obj, awkward.util.string):
            raise NotImplementedError

        elif isinstance(obj, dict):
            HERE

        elif isinstance(obj, tuple):
            raise NotImplementedError

        else:
            try:
                it = iter(obj)

            except TypeError:
                HERE

            else:
                # iterable -> JaggedArray (and recurse)

                def newchunk():
                    out = JaggedArray.fromoffsets(numpy.zeros(chunksize + 1, dtype=awkward.array.base.AwkwardArray.INDEXTYPE), PartitionedArray([0], []))
                    out._starts[0] = 0
                    out._content._offsets = [0]  # as an appendable list, not a Numpy array
                    return out

                def ismine(x):
                    return isinstance(x, JaggedArray)

                def promote(x):
                    return x

                def fillobj(array, where):
                    array._stops[where] = array._starts[where]
                    for x in it:
                        recurse(x, array._content._chunks, array._content._offsets)
                        array._stops[where] += 1

                add(chunks, offsets, newchunk, ismine, promote, fillobj)

    chunks = []
    offsets = [0]
    for x in iterable:
        recurse(x, chunks, offsets)

    return PartitionedArray(offsets, chunks)