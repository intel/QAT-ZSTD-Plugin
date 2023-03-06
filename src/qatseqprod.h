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

#ifndef QATSEQPROD_H
#define QATSEQPROD_H

#define ZSTD_STATIC_LINKING_ONLY
#include "zstd.h"

/** qatSequenceProducer:
 *    Block-level sequence producer with QAT, this implementation can be registered to
 *  zstd by ZSTD_registerSequenceProducer for replacing internal block-level
 *  sequence producer. With this sequence producer, zstd can offload the process of producing
 *  block level sequeces to QAT device
 */
size_t qatSequenceProducer(
    void* sequenceProducerState, ZSTD_Sequence* outSeqs, size_t outSeqsCapacity,
    const void* src, size_t srcSize,
    const void* dict, size_t dictSize,
    int compressionLevel,
    size_t windowSize
);

/** QZSTD_startQatDevice:
 *    Start QAT device
 */
int QZSTD_startQatDevice(void);

/** QZSTD_stopQatDevice:
 *    Stop QAT device
 */
void QZSTD_stopQatDevice(void);

/** QZSTD_createSeqProdState:
 *    Create sequence producer state for qatSequenceProducer
 */
void *QZSTD_createSeqProdState(void);

/** QZSTD_freeSeqProdState:
 *    Free sequence producer state qatSequenceProducer used
 */
void QZSTD_freeSeqProdState(void *sequenceProducerState);

#endif /* QATSEQPROD_H */
