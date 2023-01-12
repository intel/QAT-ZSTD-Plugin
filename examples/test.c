#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#define ZSTD_STATIC_LINKING_ONLY
#include "zstd.h"
#include "zstd_errors.h"
#include "../src/qatmatchfinder.h"

int main(int argc, char *argv[]) {
    char *inputFileName = NULL;
    int inputFile = -1;
    struct stat inputFileStat;
    long inputFileSize = 0;
    long dstBufferSize = 0;
    unsigned char *srcBuffer = NULL;
    unsigned char *dstBuffer = NULL;
    unsigned char *decompBuffer = NULL;
    long bytesRead = 0;
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

    // get input file size
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

    // register qatMatchfinder
    ZSTD_registerExternalMatchFinder(
        zc,
        matchState,
        qatMatchfinder
    );

    res = ZSTD_CCtx_setParameter(zc, ZSTD_c_enableMatchFinderFallback, 1);
    if ((int)res <= 0) {
        printf("Failed to set fallback\n");
        goto exit;
    }

    // compress
    cSize = ZSTD_compress2(zc, dstBuffer, dstBufferSize, srcBuffer, bytesRead);
    if ((int)cSize <= 0) {
        printf("Compress failed\n");
        goto exit;
    }

    // decompress
    res = ZSTD_decompress(decompBuffer, inputFileSize, dstBuffer, cSize);
    if (res != bytesRead) {
        printf("Decompressed size in not equal to sourece size\n");
        goto exit;
    }

    // compare original buffer with decompress output
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