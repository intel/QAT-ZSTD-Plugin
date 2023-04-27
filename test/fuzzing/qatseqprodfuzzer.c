/***************************************************************************
 *
 *   BSD LICENSE
 *
 *   Copyright(c) 2007-2023 Intel Corporation. All rights reserved.
 *   All rights reserved.
 *
 *   Redistribution and use in source and binary forms, with or without
 *   modification, are permitted provided that the following conditions
 *   are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in
 *       the documentation and/or other materials provided with the
 *       distribution.
 *     * Neither the name of Intel Corporation nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *   OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 ***************************************************************************/

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "qatseqprod.h"

size_t FUZZ_seqProdSetup(void)
{
    return QZSTD_startQatDevice();
}

size_t FUZZ_seqProdTearDown(void)
{
    return 0;
}

void *FUZZ_createSeqProdState(void)
{
    return QZSTD_createSeqProdState();
}

size_t FUZZ_freeSeqProdState(void *state)
{
    QZSTD_freeSeqProdState(state);
    return 0;
}

size_t FUZZ_thirdPartySeqProd(
    void *sequenceProducerState,
    ZSTD_Sequence *outSeqs, size_t outSeqsCapacity,
    const void *src, size_t srcSize,
    const void *dict, size_t dictSize,
    int compressionLevel,
    size_t windowSize
)
{
    return qatSequenceProducer(sequenceProducerState, outSeqs,
                               outSeqsCapacity, src, srcSize, dict, dictSize,
                               compressionLevel, windowSize);
}