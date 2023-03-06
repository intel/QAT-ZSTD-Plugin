# Intel&reg; QuickAssist Technology (QAT) Zstandard(ZSTD) Plugin Library

## Table of Contents

- [Introduction](#introduction)
- [Hardware Requirements](#hardware-requirements)
- [Software Requirements](#software-requirements)
- [Limitations](#limitations)
- [Installation Instructions](#installation-instructions)

## Introduction

Intel&reg; QuickAssist Technology (QAT) zstandard (ZSTD) plugin, Zstandard is a fast lossless compression algorithm, targeting real-time compression scenarios at zlib-level and better compression ratios. QAT sequence producer can accelerate the process of producing block level sequences of ZSTD.

## Hardware Requirements

Intel® 4xxx (Intel® QuickAssist Technology Gen 4)

## Software Requirements

zstd library of version 1.5.4

## Limitations

 1. ZSTD sequence producer only support zstd compression API which respect advanced parameters.
 2. The ZSTD_c_enableLongDistanceMatching cParam is not currently supported. Compression will fail if it is enabled and tries to compress with qatsequenceproducer.
 3. Dictionaries are not currently supported. Compression will not fail if the user references a dictionary, but the dictionary won't have any effect.
 4. Stream history is not currently supported. All advanced ZSTD compression APIs, including streaming APIs, work with qatsequenceproducer, but each block is treated as an independent chunk without history from previous blocks.
 5. Multi-threading within a single compression is not currently supported. In other words, compression will fail if ZSTD_c_nbWorkers > 0 and an external sequence producer is registered. Multi-threading across compressions is fine: simply create one CCtx per thread.

For more details about ZSTD sequence producer, please refer to [zstd.h][*]

[*]:[https://github.com/facebook/zstd/blob/dev/lib/zstd.h]

## Installation Instructions

### Build and install Intel&reg; QuickAssist Technology Driver

Download from [Linux* Hardware v2.0][*] and follow the guidance: [Intel® QAT Software for Linux—Getting Started Guide: Hardware v2.0][**]

[*]:https://www.intel.com/content/www/us/en/download/765501.html
[**]:https://cdrdv2.intel.com/v1/dl/getContent/632506

### Build qatseqprod library with SVM mode

SVM environment must be prepared before using SVM mode.

```bash
    # You can choose whether to install zstd 1.5.4,
    # if not, you can manually specify path to zstd.h
    make ZSTDLIB=[PATH TO ZSTD LIB]
```

### Build qatseqprod library with USDM mode

USDM is also supported, just choose one of these two modes accordding to your needs.

```bash
    # You can choose whether to install zstd 1.5.4,
    # if not, you can manually specify path
    make ENABLE_USDM_DRV=1 ZSTDLIB=[PATH TO ZSTD LIB]
```

### Build and run test program

```bash
    # Specify filename of libzstd.a and libqatseqprod.a
    # if you didn't install them
    make test ZSTDLIB=[PATH TO ZSTD LIB]
    ./test/test [TEST FILENAME]
```

### Build and run benchmark tool

```bash

```

###

## How to integrate qatsequenceproducer in your application

**Register qatsequenceproducer in your code**

Call ZSTD_registerSequenceProducer to register qatsequenceproducer in your application source code

```c
    /* Example code */
    ZSTD_CCtx* const zc = ZSTD_createCCtx();
    /* Start QAT device */
    QZSTD_startQatDevice();
    /* Create sequence producer state for qatSequenceProducer */
    void *sequenceProducerState = QZSTD_createSeqProdState();
    /* register qatSequenceProducer */
    ZSTD_registerSequenceProducer(
        zc,
        sequenceProducerState,
        qatSequenceProducer
    );
    /* Enable sequence producer fallback */
    ZSTD_CCtx_setParameter(zc, ZSTD_c_enableSeqProducerFallback, 1);
    /* Compress */
    ZSTD_compress2(zc, dstBuffer, dstBufferSize, srcBuffer, srcbufferSize);
    /* Free sequence producer state */
    QZSTD_freeSeqProdState(sequenceProducerState);
    /* Stop QAT device */
    QZSTD_stopQatDevice();
```

Then link to libzstd and libqatseqprod like test program did.
