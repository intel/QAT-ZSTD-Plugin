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

[Intel® QAT Driver for Linux* Hardware v2.0][*], follow the guidance: [Intel® QAT Software for Linux—Getting Started Guide: Hardware v2.0][**]

After installing QAT driver, you need to update configuration files according to your requirements refer to QAT's programmer’s guide.

Intel&reg; QAT Zstd Plugin needs a [SHIM] section by default.
There are two ways to change:
* QAT driver default conf file does not contain a [SHIM] section which the Intel&reg; QAT Zstd Plugin
  requires by default. You can add a [SHIM] section for Intel&reg; QAT Zstd Plugin.
* The default section name in the Intel&reg; QAT Zstd Plugin can be modified if required by setting the environment
variable "QAT_SECTION_NAME".

After updating configuration files, please restart QAT.

```bash
    service qat_service restart
```

[*]:https://www.intel.com/content/www/us/en/download/765501.html
[**]:https://intel.github.io/quickassist/

## Limitations
 1. Only support compression level from L1 to L12
 2. ZSTD sequence producer only support zstd compression API which respect advanced parameters.
 3. The ZSTD_c_enableLongDistanceMatching cParam is not currently supported. Compression will fail if it is enabled and tries to compress with qatsequenceproducer.
 4. Dictionaries are not currently supported. Compression will not fail if the user references a dictionary, but the dictionary won't have any effect.
 5. Stream history is not currently supported. All advanced ZSTD compression APIs, including streaming APIs, work with qatsequenceproducer, but each block is treated as an independent chunk without history from previous blocks.
 6. Multi-threading within a single compression is not currently supported. In other words, compression will fail if ZSTD_c_nbWorkers > 0 and an external sequence producer is registered. Multi-threading across compressions is fine: simply create one CCtx per thread.

For more details about ZSTD sequence producer, please refer to [zstd.h][*]

[*]:[https://github.com/facebook/zstd/blob/dev/lib/zstd.h]

## Installation Instructions

### Build qatseqprod library with SVM mode

Before using SVM, you need to modify BIOS and driver configuration files to enable SVM refer to [QAT's programmer’s guide][*].

[*]:https://www.intel.com/content/www/us/en/content-details/743912/intel-quickassist-technology-intel-qat-software-for-linux-programmers-guide-hardware-version-2-0.html

```bash
    make
```

If you didn't install zstd 1.5.4 library, you can specify path to zstd lib source root by compile variable "ZSTDLIB".

### Build qatseqprod library with USDM mode

USDM is also supported, just choose one of these two modes according to your needs.

### Build and run test program

```bash
    make test
    ./test/test [TEST FILENAME]
```

### Build and run benchmark tool

```bash
    make benchmark
    ./test/benchmark [TEST FILENAME]
```

###

## How to integrate qatsequenceproducer in your application

**Register qatsequenceproducer in your code**

Call ZSTD_registerSequenceProducer to register qatsequenceproducer in your application source code.

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
