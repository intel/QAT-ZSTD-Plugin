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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/types.h>
#include <unistd.h>

#define ZSTD_STATIC_LINKING_ONLY
#include "zstd.h"
#include "zstd_errors.h"
#include "qatmatchfinder.h"

int main(int argc, char *argv[]) {
    char *inputFileName = NULL;
    int inputFile = -1;
    struct stat inputFileStat;
    long inputFileSize = 0;
    long dstBufferSize = 0;
    unsigned char *srcBuffer = NULL;
    unsigned char *dstBuffer = NULL;
    unsigned char *decompBuffer = NULL;
    unsigned long bytesRead = 0;
    size_t cSize = 0;
    size_t res = 0;
    ZSTD_CCtx* const zc = ZSTD_createCCtx();
    void *matchState = ZSTD_QAT_createMatchState();

    if (argc != 2) {
        printf("Usage: test <file>\n");
        return 1;
    }

    inputFileName = argv[1];
    inputFile = open(inputFileName, O_RDONLY);
    if (inputFile < 0) {
        printf("Cannot open input file: %s\n", inputFileName);
        return 1;
    }
    if (fstat(inputFile, &inputFileStat)) {
        printf("Cannot get file stat\n");
        close(inputFile);
        return 1;
    }

    /* get input file size */
    inputFileSize = lseek(inputFile, 0, SEEK_END);
    lseek(inputFile, 0, SEEK_SET);
    dstBufferSize = ZSTD_compressBound(inputFileSize);

    srcBuffer = (unsigned char*)malloc(inputFileSize);
    assert(srcBuffer != NULL);
    dstBuffer = (unsigned char*)malloc(dstBufferSize);
    assert(dstBuffer != NULL);

    bytesRead = read(inputFile, srcBuffer, inputFileSize);

    decompBuffer = malloc(bytesRead);
    assert(decompBuffer);

    /* register qatmatchfinder */
    ZSTD_registerSequenceProducer(
        zc,
        matchState,
        qatMatchfinder
    );

    res = ZSTD_CCtx_setParameter(zc, ZSTD_c_enableSeqProducerFallback, 1);
    if ((int)res <= 0) {
        printf("Failed to set fallback\n");
        goto exit;
    }

    /* compress */
    cSize = ZSTD_compress2(zc, dstBuffer, dstBufferSize, srcBuffer, bytesRead);
    if ((int)cSize <= 0) {
        printf("Compress failed\n");
        goto exit;
    }

    /* decompress */
    res = ZSTD_decompress(decompBuffer, inputFileSize, dstBuffer, cSize);
    if (res != bytesRead) {
        printf("Decompressed size in not equal to sourece size\n");
        goto exit;
    }

    /* compare original buffer with decompressed output */
    if (memcmp(decompBuffer, srcBuffer, bytesRead) == 0) {
        printf("Compression and decompression were successful!\n");
        printf("Source size: %lu\n", bytesRead);
        printf("Compressed size: %lu\n", cSize);
    } else {
        printf("ERROR: input and validation buffers don't match!\n");
    }

exit:
    ZSTD_freeCCtx(zc);
    ZSTD_QAT_freeMatchState(matchState);
    free(srcBuffer);
    free(dstBuffer);
    free(decompBuffer);
    return 0;
}