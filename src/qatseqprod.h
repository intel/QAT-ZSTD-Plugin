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
#if defined (__cplusplus)
extern "C" {
#endif

#ifndef QATSEQPROD_H
#define QATSEQPROD_H

#ifndef ZSTD_STATIC_LINKING_ONLY
#define ZSTD_STATIC_LINKING_ONLY
#endif
#include "zstd.h"

/**
 * Version
 */
#define QZSTD_VERSION          "0.2.0"
#define QZSTD_VERSION_MAJOR    0
#define QZSTD_VERSION_MINOR    2
#define QZSTD_VERSION_RELEASE  0
#define QZSTD_VERSION_NUMBER  (QZSTD_VERSION_MAJOR *100*100 + QZSTD_VERSION_MINOR *100 \
                                + QZSTD_VERSION_RELEASE)

/** QZSTD_Status_e:
 *  Error code indicates status
 */
typedef enum {
    QZSTD_OK = 0,       /* Success */
    QZSTD_STARTED = 1,  /* QAT device started */
    QZSTD_FAIL = -1,    /* Unspecified error */
    QZSTD_UNSUPPORTED = -2 /* Unsupport */
} QZSTD_Status_e;

/** QZSTD_version:
 *    Return the version of QAT Zstd Plugin.
 *
 * @retval const char*  Version string.
 */
const char *QZSTD_version(void);

/** qatSequenceProducer:
 *    Block-level sequence producer with QAT
 *  This implementation can be registered to zstd by ZSTD_registerSequenceProducer
 *  for replacing internal block-level sequence producer. With this sequence producer,
 *  zstd can offload the process of producing block-level sequences to QAT device.
 *
 * @param sequenceProducerState  A pointer to a user-managed state for QAT sequence producer,
 *                               users need to call QZSTD_createSeqProdState to create it, and
 *                               call QZSTD_freeSeqProdState to free it.
 * @param outSeqs                The output buffer for QAT sequence producer. The memory backing
 *                               outSeqs is managed by the CCtx.
 * @param outSeqsCapacity        outSeqsCapacity is guaranteed >= ZSTD_sequenceBound(srcSize).
 * @param src                    An input buffer for the sequence producer to parse.
 * @param srcSize                The size of input buffer which is guaranteed to be <= ZSTD_BLOCKSIZE_MAX.
 * @param dict                   Dict buffer for sequence producer to reference. Currently, it's a NULL pointer,
 *                               and will be supported in the future.
 * @param dictSize               The size of dict. Currently, zstd will always pass zero into sequence producer.
 * @param compressionLevel       Zstd compression level, only support L1-L12.
 * @param windowSize             Representing the maximum allowed offset for sequences
 *
 * @retval size_t                Return number of sequences QAT sequence producer produced
 *                               or error code: ZSTD_SEQUENCE_PRODUCER_ERROR.
 * *** LIMITATIONS ***
 *  - Only support compression level from L1 to L12.
 *  - ZSTD sequence producer only support zstd compression API which respect advanced parameters.
 *  - The ZSTD_c_enableLongDistanceMatching cParam is not currently supported. Compression will fail
 *    if it is enabled and tries to compress with qatsequenceproducer.
 *  - Dictionaries are not currently supported. Compression will not fail if the user references
 *    a dictionary, but the dictionary won't have any effect.
 *  - Stream history is not currently supported. All advanced ZSTD compression APIs, including
 *    streaming APIs, work with qatsequenceproducer, but each block is treated as an independent
 *    chunk without history from previous blocks.
 *  - Multi-threading within a single compression is not currently supported. In other words,
 *    compression will fail if ZSTD_c_nbWorkers > 0 and an external sequence producer is registered.
 *    Multi-threading across compressions is fine: simply create one CCtx per thread.
 */
size_t qatSequenceProducer(
    void *sequenceProducerState, ZSTD_Sequence *outSeqs, size_t outSeqsCapacity,
    const void *src, size_t srcSize,
    const void *dict, size_t dictSize,
    int compressionLevel,
    size_t windowSize
);

/** QZSTD_startQatDevice:
 *    Start QAT device
 *  This function is used to initialize the QAT hardware. If qatSequenceProducer
 *  is registered, the QAT device must also be started before the compression
 *  work starts.
 *
 *  @retval QZSTD_OK        QAT device is fully successfully been started.
 *  @retval QZSTD_STARTED   QAT device is started, but the capability does not
 *                          meet the requirements.
 *  @retval QZSTD_FAIL      Failed to start QAT device.
 *  @retval QZSTD_UNSUPPORTED QAT device or current configuration didn't support LZ4s and postprocessing.
 */
int QZSTD_startQatDevice(void);

/** QZSTD_stopQatDevice:
 *    Stop QAT device
 *  This function is used to free hardware resources. Users need to call this
 *  function after all compression jobs are finished.
 */
void QZSTD_stopQatDevice(void);

/** QZSTD_createSeqProdState:
 *    Create sequence producer state for qatSequenceProducer
 *  The pointer returned by this function is required for registering qatSequenceProducer.
 *  One ZSTD CCtx can share one sequence producer state, no need to reallocate for every
 *  compression job.
 */
void *QZSTD_createSeqProdState(void);

/** QZSTD_freeSeqProdState:
 *    Free sequence producer state qatSequenceProducer used
 *  After all compression jobs are finished, users must free the sequence producer state.
 */
void QZSTD_freeSeqProdState(void *sequenceProducerState);

#endif /* QATSEQPROD_H */

#if defined (__cplusplus)
}
#endif
