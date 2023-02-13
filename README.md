# Intel&reg; QuickAssist Technology (QAT) Zstandard(ZSTD) matchfinder Library

## Table of Contents

- [Introduction](#introduction)
- [Hardware Requirements](#hardware-requirements)
- [Limitations](#limitations)
- [Installation Instructions](#installation-instructions)

## Introduction

Intel&reg; QuickAssist Technology (QAT) zstandard (ZSTD) matchfinder, Zstandard is a fast lossless compression algorithm, targeting real-time compression scenarios at zlib-level and better compression ratios. QAT matchfinder can accelerate the process of finding matched sequences of ZSTD.

## Hardware Requirements

Intel® 4xxx (Intel® QuickAssist Technology Gen 4)

## Limitations

 1. ZSTD sequence producer only support zstd compression API which respect advanced parameters.
 2. The ZSTD_c_enableLongDistanceMatching cParam is not currently supported. Compression will fail if it is enabled and tries to compress with qatmatchfinder.
 3. Dictionaries are not currently supported. Compression will not fail if the user references a dictionary, but the dictionary won't have any effect.
 4. Stream history is not currently supported. All advanced ZSTD compression APIs, including streaming APIs, work with qatmatchfinder, but each block is treated as an independent chunk without history from previous blocks.
 5. Multi-threading within a single compression is not currently supported. In other words, compression will fail if ZSTD_c_nbWorkers > 0 and an external sequence producer is registered. Multi-threading across compressions is fine: simply create one CCtx per thread.

For more details about ZSTD sequence producer, please refer to [zstd.h][*]

[*]:[https://github.com/facebook/zstd/blob/dev/lib/zstd.h]

## Installation Instructions

### Build and install Intel&reg; QuickAssist Technology Driver

Download from [Linux* Hardware v2.0][*] and follow the guidance: [Intel® QAT Software for Linux—Getting Started Guide: Hardware v2.0][**]

[*]:https://www.intel.com/content/www/us/en/download/765501.html
[**]:https://cdrdv2.intel.com/v1/dl/getContent/632506

### Build and run test program

**compile and install libqatmatchfinder**

You can build and install libqatmatchfinder with/without SVM enabled.
SVM-Shared Virtual Memory, before using it, you need to set up it on your machine.

```bash
    # Build with SVM enabled
    make
    make install

    # Build without SVM enabled(need to enable usdm)
    make ENABLE_USDM_DRV=1
    make install
```

**compile libzstd and install with qatmatchfinder patch**

```bash
    # Apply patch to zstd 1.5.4
    git clone https://github.com/facebook/zstd.git
    cd zstd
    git checkout v1.5.4
    git apply qat.patch

    # Compile libzstd with qat patch
    cd lib
    make QAT_SUPPORT=1
    make install
```

**Compile and run test program**

```bash
    make test
    ./test [Filename]
```

**Compile and run benchmark tool**

```bash

```

## How to integrate qatmatchfinder in your application

**Register qatmatchfinder in your code**

Call ZSTD_registerSequenceProducer to register qatmatchfinder in your application source code

```c
    /* Example code */
    /* Create matchState for qatmatchfinder */
    void *matchState = ZSTD_QAT_createMatchState();
    /* register qatMatchfinder */
    ZSTD_registerSequenceProducer(
        zc,
        matchState,
        qatMatchfinder
    );
    /* Enable match finder fallback */
    ZSTD_CCtx_setParameter(zc, ZSTD_c_enableMatchFinderFallback, 1);
    /* Compress */
    ZSTD_compress2(zc, dstBuffer, dstBufferSize, srcBuffer, srcbufferSize);
    /* Free match state */
    ZSTD_QAT_freeMatchState(matchState);
```
