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

#ifndef ZSTD_QAT_H
#define ZSTD_QAT_H

#define ZSTD_STATIC_LINKING_ONLY
#include "zstd.h"

/** qatMatchfinder:
 *    Block-level matchfinder with QAT, this implementation can be registered to
 *  zstd by ZSTD_registerExternalMatchFinder for replacing internal block-level
 *  matchfinder. With this matchfinder, zstd can offload the process of searching
 *  matched sequeces to QAT device
 */
size_t qatMatchfinder(
    void* externalMatchState, ZSTD_Sequence* outSeqs, size_t outSeqsCapacity,
    const void* src, size_t srcSize,
    const void* dict, size_t dictSize,
    int compressionLevel,
    size_t windowSize
);

/** ZSTD_QAT_startQatDevice:
 *    Start QAT device
 */
int ZSTD_QAT_startQatDevice(ZSTD_CCtx *cctx);

/** ZSTD_QAT_stopQatDevice:
 *    Stop QAT device
 */
void ZSTD_QAT_stopQatDevice(void);

/** ZSTD_QAT_createMatchState:
 *    Create externalMatchState for qatMatchfinder
 */
void *ZSTD_QAT_createMatchState(void);

/** ZSTD_QAT_freeMatchState:
 *    Free externalMatchState qatMatchfinder used
 */
void ZSTD_QAT_freeMatchState(void *matchState);

#endif /* ZSTD_QAT_H */
