/***************************************************************************
 *
 *   BSD LICENSE
 *
 *   Copyright(c) 2022 Intel Corporation. All rights reserved.
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
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <pthread.h>
#include <unistd.h>


#define ZSTD_STATIC_LINKING_ONLY
#include "zstd.h"
#include "zstd_errors.h"
#include "../src/qatmatchfinder.h"

#define NANOSEC (1000000000ULL) /* 1 second */
#define MB (1000000)   /* 1MB */

#ifndef MIN
    #define MIN(a,b) ((a)<(b)?(a):(b))
#endif

#define DISPLAY(...)  fprintf(stderr, __VA_ARGS__)

#define GETTIME(now) {clock_gettime(CLOCK_MONOTONIC, &now);};
#define GETDIFFTIME(start_ticks, end_ticks) (1000000000ULL*( end_ticks.tv_sec - start_ticks.tv_sec ) + ( end_ticks.tv_nsec - start_ticks.tv_nsec ))

typedef struct {
    size_t blockSize; /* the max chunk size of ZSTD_compress2 */
    size_t srcSize;  /* Input file size */
    unsigned cLevel; /* Compression Level 1 - 12 */
    unsigned nbIterations; /* Number test loops, default is 1 */
    char benchMode; /* 0: software compression, 1: QAT compression*/
    const unsigned char* srcBuffer; /* Input data point */
} threadArgs_t;

static pthread_barrier_t g_threadBarrier;

static int usage(const char* exe)
{
    DISPLAY("Usage:\n");
    DISPLAY("      %s [arg] filename\n", exe);
    DISPLAY("Options:\n");
    DISPLAY("    -t#       Set maximum threads [1 - 128] (default: 1)\n");
    DISPLAY("    -l#       Set iteration loops [1 - 1000000](default: 1)\n");
    DISPLAY("    -b#       Set block size (default: 4K)\n");
    DISPLAY("    -L#       Set compression level [1 - 12] (default: 1)\n");
    DISPLAY("    -m#       Benchmark mode, 0: software compression; 1:QAT compression(default: 1) \n");
    DISPLAY("    -h/H      Print this help message\n");
    return 0;
}

/* this function to convert string to unsigned int,
 * the string MUST BE starting with numeric and can be
 * end with "K" or "M". Such as:
 * if input string is "128K" and output will be 131072.
 * if input string is "65536" and output will be 65536.
 */
static unsigned stringToU32(const char** s)
{
    unsigned value = 0;
    while ((**s >='0') && (**s <='9')) {
        if (value > ((((unsigned)(-1)) / 10) - 1)) {
            DISPLAY("ERROR: numeric value is too large\n");
            exit(1);
        }
        value *= 10;
        value += (unsigned)(**s - '0');
        (*s)++ ;
    }
    if ((**s=='K') || (**s=='M')) {
        if (value > ((unsigned)(-1)) >> 10) {
            DISPLAY("ERROR: numeric value is too large\n");
            exit(1);
        }
        value <<= 10;
        if (**s=='M') {
            if (value > ((unsigned)(-1)) >> 10) {
                DISPLAY("ERROR: numeric value is too large\n");
                exit(1);
            }
            value <<= 10;
        }
        (*s)++;
    }
    return value;
}

void* benchmark(void *args)
{
    threadArgs_t* threadArgs = (threadArgs_t*)args;
    size_t rc = 0;
    unsigned loops;
    int verifyResult = 0; /* 1: pass, 0: fail */
    size_t* chunkSizes = NULL;
    size_t* compSizes = NULL;
    size_t nanosec = 0;
    double speed = 0, ratio = 0;
    size_t csCount, nbChunk, destSize, cSize;
    struct timespec startTicks, endTicks;
    unsigned char* destBuffer = NULL, *decompBuffer = NULL;
    const unsigned char* srcBuffer = threadArgs->srcBuffer;
    size_t srcSize = threadArgs->srcSize;
    size_t blockSize = threadArgs->blockSize;
    unsigned nbIterations = threadArgs->nbIterations;
    unsigned cLevel = threadArgs->cLevel;
    ZSTD_CCtx* const zc = ZSTD_createCCtx();
    void *matchState = NULL;

    csCount = srcSize / blockSize + (srcSize % blockSize ? 1 : 0);
    chunkSizes = (size_t *)malloc(csCount * sizeof(size_t));
    compSizes = (size_t *)malloc(csCount * sizeof(size_t));
    assert(chunkSizes && compSizes);
    size_t tmpSize = srcSize;
    for (nbChunk = 0; nbChunk < csCount; nbChunk++) {
        chunkSizes[nbChunk] = MIN(tmpSize, blockSize);
        tmpSize -= chunkSizes[nbChunk];
    }

    destSize = ZSTD_compressBound(srcSize);
    destBuffer = (unsigned char*)malloc(destSize);
    decompBuffer = (unsigned char*)malloc(srcSize);
    assert(destBuffer != NULL);

    if (threadArgs->benchMode == 1) {
        matchState = ZSTD_QAT_createMatchState();
        ZSTD_registerSequenceProducer(zc, matchState, qatMatchfinder);
    }
    rc = ZSTD_CCtx_setParameter(zc, ZSTD_c_compressionLevel, cLevel);
    if ((int)rc <= 0) {
        DISPLAY("Failed to set fallback\n");
        goto exit;
    }

    /* Waiting all threads */
    pthread_barrier_wait(&g_threadBarrier);

    /* Start benchmark */
    GETTIME(startTicks);
    for (loops = 0; loops < nbIterations; loops++) {
        unsigned char* tmpDestBuffer = destBuffer;
        const unsigned char* tmpSrcBuffer = srcBuffer;
        size_t tmpDestSize = destSize;
        for (nbChunk = 0; nbChunk < csCount; nbChunk++) {
            cSize = ZSTD_compress2(zc, tmpDestBuffer, tmpDestSize, tmpSrcBuffer, chunkSizes[nbChunk]);
            if ((int)cSize <= 0) {
                DISPLAY("Compress failed\n");
                goto exit;
            }
            tmpDestBuffer += cSize;
            tmpDestSize -= cSize;
            tmpSrcBuffer += chunkSizes[nbChunk];
            compSizes[nbChunk] = cSize;
        }
    }
    GETTIME(endTicks);

    cSize = 0;
    for (nbChunk = 0; nbChunk < csCount; nbChunk++) {
        cSize += compSizes[nbChunk];
    }

    /* Verify the compression result */
    rc = ZSTD_decompress(decompBuffer, srcSize, destBuffer, cSize);
    if (rc != srcSize) {
        DISPLAY("Decompressed size in not equal to source size\n");
        goto exit;
    }
    /* Compare original buffer with decompress output */
    if (!memcmp(decompBuffer, srcBuffer, srcSize)) {
        verifyResult = 1;
    } else {
        verifyResult = 0;
    }

    ratio = (double) cSize / (double)srcSize;
    nanosec = GETDIFFTIME(startTicks, endTicks);
    speed = (double)(srcSize * nbIterations * NANOSEC) / nanosec;
    DISPLAY("Thread %lu: Compression: %lu -> %lu, Throughput:%6.f MB/s, Compression Ratio: %6.2f %% %s\n",pthread_self(), srcSize,
             cSize, (double) speed/MB, ratio * 100, verifyResult ? "PASS" : "FAIL");

exit:
    ZSTD_freeCCtx(zc);
    if (threadArgs->benchMode == 1 && matchState) {
        ZSTD_QAT_freeMatchState(matchState);
    }
    if (chunkSizes) {
        free(chunkSizes);
    }
    if (compSizes) {
        free(compSizes);
    }
    if (destBuffer) {
        free(destBuffer);
    }
    if(decompBuffer) {
        free(decompBuffer);
    }
    return NULL;
}

int main(int argc, const char** argv)
{
    int argNb, threadNb;
    int nbThreads = 1;
    pthread_t threads[128];
    size_t srcSize, bytesRead;
    unsigned char* srcBuffer = NULL;
    const char* fileName = NULL;
    int inputFile = -1;
    threadArgs_t threadArgs;

    if (argc < 2)
        return usage(argv[0]);

    /* Set default value */
    threadArgs.blockSize = 4096;
    threadArgs.nbIterations = 1;
    threadArgs.cLevel = 1;
    threadArgs.benchMode = 1;

    for (argNb = 1; argNb < argc; argNb++) {
        const char* arg = argv[argNb];
        if (arg[0] == '-') {
            arg++;
            while (arg[0] != 0) {
                switch(arg[0])
                {
                    /* Display help message */
                case 'h':
                case 'H': return usage(argv[0]);
                    /* Set maximum threads */
                case 't':
                    arg++;
                    nbThreads = stringToU32(&arg);
                    if (nbThreads > 128) {
                        DISPLAY("Invalid thread parameter, maximum is 128\n");
                        return -1;
                    }
                    break;
                    /* Set block size */
                case 'b':
                    arg++;
                    threadArgs.blockSize = stringToU32(&arg);
                    break;
                    /* Set iterations */
                case 'l':
                    arg++;
                    threadArgs.nbIterations = stringToU32(&arg);
                    break;
                    /* Set benchmark mode */
                case 'm':
                    arg++;
                    threadArgs.benchMode = stringToU32(&arg);
                    break;
                    /* Set compression level */
                case 'L':
                    arg++;
                    threadArgs.cLevel = stringToU32(&arg);
                    break;
                    /* Unknown argument */
                default : return usage(argv[0]);
                }
            }
            continue;
        }
        if (!fileName) {
            fileName = arg;
            continue;
        }
    }
    if (!fileName) {
        return usage(argv[0]);
    }

    /* Load input file */
    inputFile = open(fileName, O_RDONLY);
    if (inputFile < 0) {
        DISPLAY("Cannot open input file: %s\n", fileName);
        return -1;
    }
    srcSize = lseek(inputFile, 0, SEEK_END);
    lseek(inputFile, 0, SEEK_SET);
    srcBuffer = (unsigned char*)malloc(srcSize);
    assert(srcBuffer != NULL);

    bytesRead = 0;
    while(bytesRead != srcSize) {
        bytesRead += read(inputFile, srcBuffer + bytesRead, srcSize - bytesRead);
    }
    assert(bytesRead == srcSize);

    threadArgs.srcBuffer = srcBuffer;
    threadArgs.srcSize = srcSize;

    pthread_barrier_init(&g_threadBarrier, NULL, nbThreads);
    for(threadNb = 0; threadNb < nbThreads; threadNb++){
        pthread_create(&threads[threadNb], NULL, benchmark, &threadArgs);
    }

    for(threadNb = 0; threadNb < nbThreads; threadNb++){
        pthread_join(threads[threadNb], NULL);
    }

    pthread_barrier_destroy(&g_threadBarrier);
    close(inputFile);
    free(srcBuffer);
    return 0;
}
