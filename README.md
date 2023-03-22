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

Zstandard library of version 1.5.4+

[Intel® QAT Driver for Linux* Hardware v2.0][1]

## Limitations

 1. Supports compression levels L1 to L12
 2. ZSTD sequence producer only support ZSTD compression API which respects advanced parameters, such as `ZSTD_compress2`, `ZSTD_compressStream2`.
 3. The ZSTD_c_enableLongDistanceMatching cParam is not currently supported. Compression will fail if it is enabled and tries to compress with qatsequenceproducer.
 4. Dictionaries are not currently supported. Compression will succeed if the dictionary is referenced, but the dictionary will have no effect.
 5. Stream history is not currently supported. All advanced ZSTD compression APIs, including streaming APIs, work with qatsequenceproducer, but each block is treated as an independent chunk without history from previous blocks.
 6. Multi-threading within a single compression is not currently supported. In other words, compression will fail if ZSTD_c_nbWorkers > 0 and an external sequence producer is registered. Multi-threading across compressions is fine: simply create one CCtx per thread.

For more details about ZSTD sequence producer, please refer to [zstd.h][2].

## Installation Instructions

### Build and install Intel® QuickAssist Technology Driver

Download from [Intel® QAT Driver for Linux* Hardware v2.0][1], follow the guidance: [Intel® QAT Software for Linux—Getting Started Guide: Hardware v2.0][3].

After installing QAT driver, please refer to [QAT's programmer’s guide][4] to update QAT configuration file according to your requirements.

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

### Build qatseqprod library with SVM mode

Shared Virtual Memory (SVM) allows direct submission of an applications buffer, thus removing the memcpy cycle cost, cache thrashing, and memory bandwidth. The SVM feature enables passing virtual addresses to the QAT hardware for processing acceleration requests.

To enable SVM, please refer to [QAT's programmer’s guide][4] chapter 3.3 to update the BIOS and driver configuration.

```bash
    make
```

If you didn't install ZSTD 1.5.4 library, you can specify path to ZSTD lib source root by compile variable "ZSTDLIB".

```bash
    make ZSTDLIB=[PATH TO ZSTD LIB SOURCE]
```

### Build qatseqprod library with USDM mode

If SVM is not enabled, memory passed to Intel® QuickAssist Technology hardware must be DMA’able.

Intel provides a User Space DMA-able Memory (USDM) component (kernel driver and corresponding user space library) which allocates/frees DMA-able memory, mapped to user space, performs virtual to physical address translation on memory allocated by this library. Please refer to [QAT's programmer’s guide][4] chapter 3.3.

Please compile USDM mode with "ENABLE_USDM_DRV=1".

```bash
    make ENABLE_USDM_DRV=1
```

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

**Initialization**

Start and initialize the QAT device.

Create sequence producer state for `qatSequenceProducer`, then call `ZSTD_registerSequenceProducer` to register it in your application source code.

```c
    ZSTD_CCtx* const zc = ZSTD_createCCtx();
    /* Start QAT device, you can start QAT device
    at any time before compression job started */
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
```

**Compression API**

No changes to your application with calling ZSTD compression API, just keep calling `ZSTD_compress2`, `ZSTD_compressStream2` or `ZSTD_compressStream` to compress.

```c
    /* Compress */
    ZSTD_compress2(zc, dstBuffer, dstBufferSize, srcBuffer, srcbufferSize);
```

**Free resources and shutdown QAT device**

```c
    /* Free sequence producer state */
    QZSTD_freeSeqProdState(sequenceProducerState);
    /* Stop QAT device, please call this function when
    you won't use QAT anymore or before the process exits */
    QZSTD_stopQatDevice();
```

Then link to libzstd and libqatseqprod like test program did.

[1]:https://www.intel.com/content/www/us/en/download/765501.html
[2]:https://github.com/facebook/zstd/blob/dev/lib/zstd.h
[3]:https://intel.github.io/quickassist/
[4]:https://www.intel.com/content/www/us/en/content-details/743912/intel-quickassist-technology-intel-qat-software-for-linux-programmers-guide-hardware-version-2-0.html