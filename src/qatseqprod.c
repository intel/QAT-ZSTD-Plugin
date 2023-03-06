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

/**
 *****************************************************************************
 *      Dependencies
 *****************************************************************************/
#define ZSTD_STATIC_LINKING_ONLY
#include "zstd.h"

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <limits.h> /* INT_MAX */
#include <string.h> /* memset */
#include <stdarg.h>

#include "cpa.h"
#include "cpa_dc.h"
#include "icp_sal_poll.h"
#include "icp_sal_user.h"

#ifdef ENABLE_USDM_DRV
#include "qae_mem.h"
#endif

#define KB                             (1024)

#define COMP_LVL_MINIMUM               (1)
#define COMP_LVL_MAXIMUM               (12)
#define NUM_BLOCK_OF_RETRY_INTERVAL    (1000)

#define MAX_GRAB_RETRY                 (10)
#define MAX_SEND_REQUEST_RETRY         (5)
#define MAX_DEVICES                    (256)

#define INTER_SZ(src_sz) (2 * (src_sz))
#define COMPRESS_SRC_BUFF_SZ (ZSTD_BLOCKSIZE_MAX)
#define DC_CEIL_DIV(x, y) (((x) + (y)-1) / (y))
/* Formula for GEN4 LZ4S:
* sourceLen + Ceil(sourceLen/2000) * 11 + 1024 */
#define INTERMEDIATE_BUFFER_SZ (ZSTD_BLOCKSIZE_MAX + 1024 + DC_CEIL_DIV(ZSTD_BLOCKSIZE_MAX, 2000) * 11)

#define ML_BITS 4
#define ML_MASK ((1U << ML_BITS) - 1)
#define RUN_BITS (8 - ML_BITS)
#define RUN_MASK ((1U << RUN_BITS) - 1)

#define LZ4MINMATCH 2

/* Max latency of polling in the worst condition */
#define MAXTIMEOUT 2000000

#define TIMESPENT(a, b) ((a.tv_sec * 1000000 + a.tv_usec) - (b.tv_sec * 1000000 + b.tv_usec))

/** QZSTD_Status_e:
 *  Error code indicates status
 */
typedef enum {
    QZSTD_OK = 0,       /* Success */
    QZSTD_STARTED = 1,  /* QAT device started */
    QZSTD_FAIL = -1     /* Unspecified error */
} QZSTD_Status_e;

/** QZSTD_Session_T:
 *  This structure contains all session parameters including a buffer used to store
 *  lz4s output for current session and other parameters
 */
typedef struct QZSTD_Session_S {
    int instHint; /*which instance we last used*/
    unsigned char *qatIntermediateBuf;  /* Buffer to store lz4s output for decoding */
    unsigned char reqPhyContMem; /* 1: QAT requires physically contiguous memory */
    CpaDcSessionSetupData sessionSetupData; /* Session set up data for this session */
    unsigned int failOffloadCnt; /* Failed offloading requests counter */
} QZSTD_Session_T;

/** QZSTD_Instance_T:
 *  This structure cantains instance parameter, every session need to grab one
 *  instance to sumbit request
 */
typedef struct QZSTD_Instance_S {
    CpaInstanceInfo2 instanceInfo;
    CpaDcInstanceCapabilities instanceCap;
    CpaStatus jobStatus;
    CpaDcSessionSetupData sessionSetupData;
    CpaDcSessionHandle cpaSessHandle;
    CpaDcRqResults res;
    Cpa32U buffMetaSize;
    CpaStatus instStartStatus;
    unsigned char reqPhyContMem; /* 1: QAT requires physically contiguous memory */

    /* Tracks memory where the intermediate buffers reside. */
    CpaBufferList **intermediateBuffers;
    Cpa16U intermediateCnt;
    CpaBufferList *srcBuffer;
    CpaBufferList *destBuffer;

    unsigned int lock;
    unsigned char memSetup;
    unsigned char cpaSessSetup;
    unsigned char dcInstSetup;
    unsigned int numRetries;

    unsigned int seqNumIn;
    unsigned int seqNumOut;
    int cbStatus;
} QZSTD_Instance_T;

/** QZSTD_ProcessData_T:
 *  Process data for controling instance resource
 */
typedef struct QZSTD_ProcessData_S {
    int qzstdInitStatus;
    CpaInstanceHandle *dcInstHandle;
    QZSTD_Instance_T *qzstdInst;
    Cpa16U numInstances;
    pthread_mutex_t mutex;
} QZSTD_ProcessData_T;

typedef struct QZSTD_InstanceList_S {
    QZSTD_Instance_T instance;
    CpaInstanceHandle dcInstHandle;
    struct QZSTD_InstanceList_S *next;
} QZSTD_InstanceList_T;

typedef struct QZSTD_Hardware_S {
    QZSTD_InstanceList_T devices[MAX_DEVICES];
    unsigned int devNum;
    unsigned int maxDevId;
} QZSTD_Hardware_T;

QZSTD_ProcessData_T gProcess = {
    .qzstdInitStatus = QZSTD_FAIL,
    .mutex = PTHREAD_MUTEX_INITIALIZER
};

extern CpaStatus icp_adf_get_numDevices(Cpa32U *);

#ifdef ENABLE_DEBUG
static inline void DEBUG(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    vfprintf(stdout, format, args);
    va_end(args);
}
#else
#define DEBUG(...)
#endif

static inline void ERROR(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
}

/** QZSTD_calloc:
 *    This function is used to allocate contiguous or discontiguous memory(initialized to zero)
 *  according to parameter and return pointer to allocated memory
 */
static void *QZSTD_calloc(size_t nb, size_t size, unsigned char reqPhyContMem)
{
    if(!reqPhyContMem) {
        return calloc(nb, size);
    } else {
#ifdef ENABLE_USDM_DRV
        return qaeMemAllocNUMA(nb * size, 0, 64);
#else
        return NULL;
#endif
    }
}

/** QZSTD_free:
 *    This function needs to be called in pairs with QZSTD_calloc
 *  to free memory by QZSTD_calloc.
 */
static void QZSTD_free(void *ptr, unsigned char reqPhyContMem)
{
    if(!reqPhyContMem) {
        free(ptr);
    } else {
#ifdef ENABLE_USDM_DRV
        qaeMemFreeNUMA(&ptr);
#else
        ERROR("Don't support QAT USDM driver\n");
#endif
    }
}

/** QZSTD_virtToPhys:
 *    Convert virtual address to physical
 */
static __inline CpaPhysicalAddr QZSTD_virtToPhys(void *virtAddr)
{
#ifdef ENABLE_USDM_DRV
    return (CpaPhysicalAddr)qaeVirtToPhysNUMA(virtAddr);
#else
    return (CpaPhysicalAddr)virtAddr;
#endif
}

static QZSTD_InstanceList_T *QZSTD_getInstance(unsigned int devId,
                                              QZSTD_Hardware_T *qatHw)
{
    QZSTD_InstanceList_T *instances;
    QZSTD_InstanceList_T *firstInstance;
    int i;

    if (devId >= MAX_DEVICES || NULL == qatHw) {
        return NULL;
    }

    instances = &qatHw->devices[devId];
    firstInstance = instances->next;

    /* no instance */
    if (NULL == firstInstance) {
        goto exit;
    }

    instances->next = firstInstance->next;

    /* last instance */
    if (NULL == instances->next && qatHw->devNum > 0) {
        qatHw->devNum--;
        if (qatHw->maxDevId > 0 && devId == qatHw->maxDevId) {
            for (i = qatHw->maxDevId - 1; i >= 0; i--) {
                if (qatHw->devices[i].next) {
                    qatHw->maxDevId = i;
                    break;
                }
            }
        }
    }

exit:
    return firstInstance;
}

static void QZSTD_clearDevices(QZSTD_Hardware_T *qatHw)
{
    unsigned int i;
    if (NULL == qatHw || 0 == qatHw->devNum) {
        return;
    }

    for (i = 0; i <= qatHw->maxDevId; i++) {
        QZSTD_InstanceList_T *inst = QZSTD_getInstance(i, qatHw);
        while (inst) {
            free(inst);
            inst = QZSTD_getInstance(i, qatHw);
        }
    }
}

/** QZSTD_stopQat:
 *    Stop DC instance and QAT device
 */
static void QZSTD_stopQat(void)
{
    int i;
    CpaStatus status = CPA_STATUS_SUCCESS;

    DEBUG("Call stopQat.\n");
    if (NULL != gProcess.dcInstHandle &&
        NULL != gProcess.qzstdInst) {
        for (i = 0; i < gProcess.numInstances; i++) {
            if (0 != gProcess.qzstdInst[i].dcInstSetup) {
                status = cpaDcStopInstance(gProcess.dcInstHandle[i]);
                if (CPA_STATUS_SUCCESS != status) {
                    ERROR("Stop instance failed, status=%d\n", status);
                }
            }
        }

        free(gProcess.dcInstHandle);
        gProcess.dcInstHandle = NULL;
        free(gProcess.qzstdInst);
        gProcess.qzstdInst = NULL;
    }

    (void)icp_sal_userStop();

    gProcess.numInstances = (Cpa16U)0;
    gProcess.qzstdInitStatus = QZSTD_FAIL;
}

static void QZSTD_removeSession(int i)
{
    unsigned char reqPhyContMem = gProcess.qzstdInst[i].reqPhyContMem;
    int rc;

    if (0 == gProcess.qzstdInst[i].cpaSessSetup) {
        return;
    }

    /* Remove session */
    if ((NULL != gProcess.dcInstHandle[i]) &&
        (NULL != gProcess.qzstdInst[i].cpaSessHandle)) {
        /* polling here if there still are some reponse haven't beed polled
        *  if didn't poll there response, cpaDcRemoveSession will raise error message
        */
        do {
            rc = icp_sal_DcPollInstance(gProcess.dcInstHandle[i], 0);
        } while(CPA_STATUS_SUCCESS == rc);
        cpaDcRemoveSession(gProcess.dcInstHandle[i],
                           gProcess.qzstdInst[i].cpaSessHandle);
        QZSTD_free(gProcess.qzstdInst[i].cpaSessHandle, reqPhyContMem);
        gProcess.qzstdInst[i].cpaSessHandle = NULL;
        gProcess.qzstdInst[i].cpaSessSetup = 0;
    }
}

/** QZSTD_cleanUpInstMem:
 *    Release the memory bound to corresponding instance
 */
static void QZSTD_cleanUpInstMem(int i)
{
    int j;
    QZSTD_Instance_T *qzstdInst = &(gProcess.qzstdInst[i]);
    unsigned char reqPhyContMem = qzstdInst->reqPhyContMem;

    if (NULL != qzstdInst->intermediateBuffers) {
        for (j = 0; j < qzstdInst->intermediateCnt; j++) {
            if (NULL != qzstdInst->intermediateBuffers[j]) {
                if (NULL != qzstdInst->intermediateBuffers[j]->pBuffers) {
                    if (NULL != qzstdInst->intermediateBuffers[j]->pBuffers->pData) {
                        QZSTD_free(qzstdInst->intermediateBuffers[j]->pBuffers->pData, reqPhyContMem);
                        qzstdInst->intermediateBuffers[j]->pBuffers->pData = NULL;
                    }
                    QZSTD_free(qzstdInst->intermediateBuffers[j]->pBuffers, reqPhyContMem);
                    qzstdInst->intermediateBuffers[j]->pBuffers = NULL;
                }
                if (NULL != qzstdInst->intermediateBuffers[j]->pPrivateMetaData) {
                    QZSTD_free(qzstdInst->intermediateBuffers[j]->pPrivateMetaData, reqPhyContMem);
                    qzstdInst->intermediateBuffers[j]->pPrivateMetaData = NULL;
                }
                QZSTD_free(qzstdInst->intermediateBuffers[j], 0);
                qzstdInst->intermediateBuffers[j] = NULL;
            }
        }
        QZSTD_free(qzstdInst->intermediateBuffers, 0);
        qzstdInst->intermediateBuffers = NULL;
    }

    /*src buffer*/
    if (NULL != qzstdInst->srcBuffer) {
        if (reqPhyContMem && qzstdInst->srcBuffer->pBuffers) {
            if (NULL != qzstdInst->srcBuffer->pBuffers->pData) {
                QZSTD_free(qzstdInst->srcBuffer->pBuffers->pData, reqPhyContMem);
                qzstdInst->srcBuffer->pBuffers->pData = NULL;
            }
        }
        if (NULL != qzstdInst->srcBuffer->pBuffers) {
            QZSTD_free(qzstdInst->srcBuffer->pBuffers, reqPhyContMem);
            qzstdInst->srcBuffer->pBuffers = NULL;
        }
        if (NULL != qzstdInst->srcBuffer->pPrivateMetaData) {
            QZSTD_free(qzstdInst->srcBuffer->pPrivateMetaData, reqPhyContMem);
            qzstdInst->srcBuffer->pPrivateMetaData = NULL;
        }
        QZSTD_free(qzstdInst->srcBuffer, 0);
        qzstdInst->srcBuffer = NULL;
    }

    /*dest buffer*/
    if (NULL != qzstdInst->destBuffer) {
        if (NULL != qzstdInst->destBuffer->pBuffers) {
            QZSTD_free(qzstdInst->destBuffer->pBuffers, reqPhyContMem);
            qzstdInst->destBuffer->pBuffers = NULL;
        }
        if (NULL != qzstdInst->destBuffer->pPrivateMetaData) {
            QZSTD_free(qzstdInst->destBuffer->pPrivateMetaData, reqPhyContMem);
            qzstdInst->destBuffer->pPrivateMetaData = NULL;
        }
        QZSTD_free(qzstdInst->destBuffer, 0);
        qzstdInst->destBuffer = NULL;
    }
}

void QZSTD_stopQatDevice()
{
    pthread_mutex_lock(&gProcess.mutex);
    if (QZSTD_OK == gProcess.qzstdInitStatus) {
        int i = 0;

        for (i = 0; i < gProcess.numInstances; i++) {
            if (0 != gProcess.qzstdInst[i].cpaSessSetup) {
                QZSTD_removeSession(i);
            }
            if (0 != gProcess.qzstdInst[i].memSetup) {
                QZSTD_cleanUpInstMem(i);
            }
        }
        QZSTD_stopQat();
    }
    if (QZSTD_STARTED == gProcess.qzstdInitStatus) {
        (void)icp_sal_userStop();
        gProcess.qzstdInitStatus = QZSTD_FAIL;
    }
    pthread_mutex_unlock(&gProcess.mutex);
}

static int QZSTD_setInstance(unsigned int devId,
                             QZSTD_InstanceList_T *newInstance,
                             QZSTD_Hardware_T *qatHw)
{
    QZSTD_InstanceList_T *instances;

    if (devId >= MAX_DEVICES || NULL == newInstance || NULL == qatHw ||
        NULL != newInstance->next) {
        return QZSTD_FAIL;
    }

    instances = &qatHw->devices[devId];

    /* first instance */
    if (NULL == instances->next) {
        qatHw->devNum++;
    }

    while (instances->next) {
        instances = instances->next;
    }
    instances->next = newInstance;

    if (devId > qatHw->maxDevId) {
        qatHw->maxDevId = devId;
    }

    return QZSTD_OK;
}

static int QZSTD_salUserStart(void)
{
    Cpa32U pcieCount;

    if (CPA_STATUS_SUCCESS != icp_adf_get_numDevices(&pcieCount)) {
        return QZSTD_FAIL;
    }

    if (0 == pcieCount) {
        return QZSTD_FAIL;
    }

    if (CPA_STATUS_SUCCESS != icp_sal_userStart("SHIM")) {
        ERROR("icp_sal_userStart failed\n");
        return QZSTD_FAIL;
    }

    return QZSTD_OK;
}

static int QZSTD_getAndShuffleInstance(void)
{
    int i;
    unsigned int devId = 0;
    QZSTD_Hardware_T *qatHw = NULL;
    unsigned int instanceFound = 0;
    unsigned int instanceMatched = 0;
    QZSTD_InstanceList_T *newInst;
    if (CPA_STATUS_SUCCESS != cpaDcGetNumInstances(&gProcess.numInstances)) {
        ERROR("cpaDcGetNumInstances failed\n");
        goto exit;
    }

    gProcess.dcInstHandle = (CpaInstanceHandle *)calloc(
        gProcess.numInstances, sizeof(CpaInstanceHandle));
    gProcess.qzstdInst = (QZSTD_Instance_T *)calloc(gProcess.numInstances,
                                                     sizeof(QZSTD_Instance_T));
    if (NULL == gProcess.dcInstHandle || NULL == gProcess.qzstdInst) {
        ERROR("calloc for qzstdInst failed\n");
        goto exit;
    }

    if (CPA_STATUS_SUCCESS != cpaDcGetInstances(
            gProcess.numInstances, gProcess.dcInstHandle)) {
        ERROR("cpaDcGetInstances failed\n");
        goto exit;
    }

    qatHw = (QZSTD_Hardware_T *)calloc(1, sizeof(QZSTD_Hardware_T));
    if (NULL == qatHw) {
        ERROR("calloc for qatHw failed\n");
        goto exit;
    }
    for (i = 0; i < gProcess.numInstances; i++) {
        newInst = (QZSTD_InstanceList_T *)calloc(1, sizeof(QZSTD_InstanceList_T));
        if (NULL == newInst) {
            ERROR("calloc failed\n");
            goto exit;
        }

        if (CPA_STATUS_SUCCESS != cpaDcInstanceGetInfo2(
            gProcess.dcInstHandle[i], &newInst->instance.instanceInfo)) {
            ERROR("cpaDcInstanceGetInfo2 failed\n");
            free(newInst);
            goto exit;
        }

        if (CPA_STATUS_SUCCESS != cpaDcQueryCapabilities(
            gProcess.dcInstHandle[i], &newInst->instance.instanceCap)) {
            ERROR("cpaDcQueryCapabilities failed\n");
            free(newInst);
            goto exit;
        }

        newInst->instance.lock = 0;
        newInst->instance.memSetup = 0;
        newInst->instance.cpaSessSetup = 0;
        newInst->instance.dcInstSetup = 0;
        newInst->instance.numRetries = 0;
        newInst->dcInstHandle = gProcess.dcInstHandle[i];

        devId = newInst->instance.instanceInfo.physInstId.packageId;
        if (QZSTD_OK != QZSTD_setInstance(devId, newInst, qatHw)) {
            ERROR("QZSTD_setInstance on device %d failed\n", devId);
            free(newInst);
            goto exit;
        }
    }

    /* shuffle instance */
    for (i = 0; instanceFound < gProcess.numInstances; i++) {
        devId = i % (qatHw->maxDevId + 1);
        newInst = QZSTD_getInstance(devId, qatHw);
        if (NULL == newInst) {
            continue;
        }
        instanceFound++;

        /* check lz4s support */
        if (!newInst->instance.instanceCap.checksumXXHash32 ||
            !newInst->instance.instanceCap.statelessLZ4SCompression) {
            free(newInst);
            continue;
        }

#ifdef ENABLE_USDM_DRV
        /* If enable USDM driver support, we always use physically contiguous memory without
         * checking instance flag requiresPhysicallyContiguousMemory.
         */
        newInst->instance.reqPhyContMem = 1;
#else
        if (newInst->instance.instanceInfo.requiresPhysicallyContiguousMemory) {
            free(newInst);
            continue;
        }
        newInst->instance.reqPhyContMem = 0;
#endif

        memcpy(&gProcess.qzstdInst[instanceMatched], &newInst->instance,
                    sizeof(QZSTD_Instance_T));
        gProcess.dcInstHandle[instanceMatched] = newInst->dcInstHandle;
        free(newInst);
        instanceMatched++;
    }

    if (instanceMatched == 0) {
        goto exit;
    }

    QZSTD_clearDevices(qatHw);
    free(qatHw);

    return QZSTD_OK;

exit:
    if (qatHw) {
        QZSTD_clearDevices(qatHw);
        free(qatHw);
    }
    if (NULL != gProcess.dcInstHandle) {
        free(gProcess.dcInstHandle);
        gProcess.dcInstHandle = NULL;
    }
    if (NULL != gProcess.qzstdInst) {
        free(gProcess.qzstdInst);
        gProcess.qzstdInst = NULL;
    }
    return QZSTD_FAIL;
}

static void ZSTD_QAT_dcCallback(void *cbDataTag, CpaStatus stat)
{
    if (NULL != cbDataTag) {
        QZSTD_Instance_T *qzstdInst = (QZSTD_Instance_T *)cbDataTag;
        qzstdInst->seqNumOut++;
        if (qzstdInst->seqNumIn != qzstdInst->seqNumOut) {
            return;
        }

        if (CPA_DC_OK == stat) {
            qzstdInst->cbStatus = QZSTD_OK;
        } else {
            qzstdInst->cbStatus = QZSTD_FAIL;
        }
    }
}

/** QZSTD_allocInstMem:
 *    Allocate memory for corresponding instance
 */
static int QZSTD_allocInstMem(int i)
{
    int j;
    CpaStatus status;
    CpaStatus rc;
    unsigned int srcSz;
    unsigned int interSz;
    unsigned char reqPhyContMem = gProcess.qzstdInst[i].reqPhyContMem;

    rc = QZSTD_OK;
    srcSz = COMPRESS_SRC_BUFF_SZ;
    interSz = INTER_SZ(srcSz);

    status =
        cpaDcBufferListGetMetaSize(gProcess.dcInstHandle[i], 1,
                                   &(gProcess.qzstdInst[i].buffMetaSize));
    if (CPA_STATUS_SUCCESS != status) {
        goto cleanup;
    }

    status = cpaDcGetNumIntermediateBuffers(
        gProcess.dcInstHandle[i], &(gProcess.qzstdInst[i].intermediateCnt));
    if (CPA_STATUS_SUCCESS != status) {
        goto cleanup;
    }
    gProcess.qzstdInst[i].intermediateBuffers =
        (CpaBufferList **)QZSTD_calloc((size_t)gProcess.qzstdInst[i].intermediateCnt,
                                 sizeof(CpaBufferList *), 0);
    if (NULL == gProcess.qzstdInst[i].intermediateBuffers) {
        goto cleanup;
    }

    for (j = 0; j < gProcess.qzstdInst[i].intermediateCnt; j++) {
        gProcess.qzstdInst[i].intermediateBuffers[j] =
            (CpaBufferList *)QZSTD_calloc(1, sizeof(CpaBufferList), 0);
        if (NULL == gProcess.qzstdInst[i].intermediateBuffers[j]) {
            goto cleanup;
        }
        if (0 != gProcess.qzstdInst[i].buffMetaSize) {
            gProcess.qzstdInst[i].intermediateBuffers[j]->pPrivateMetaData =
                QZSTD_calloc(1, (size_t)(gProcess.qzstdInst[i].buffMetaSize), reqPhyContMem);
            if (NULL ==
                gProcess.qzstdInst[i].intermediateBuffers[j]->pPrivateMetaData) {
                goto cleanup;
            }
        }

        gProcess.qzstdInst[i].intermediateBuffers[j]->pBuffers =
            (CpaFlatBuffer *)QZSTD_calloc(1, sizeof(CpaFlatBuffer), reqPhyContMem);
        if (NULL == gProcess.qzstdInst[i].intermediateBuffers[j]->pBuffers) {
            goto cleanup;
        }

        gProcess.qzstdInst[i].intermediateBuffers[j]->pBuffers->pData =
            (Cpa8U *)QZSTD_calloc(1, interSz, reqPhyContMem);
        if (NULL ==
            gProcess.qzstdInst[i].intermediateBuffers[j]->pBuffers->pData) {
            goto cleanup;
        }

        gProcess.qzstdInst[i].intermediateBuffers[j]->pBuffers->dataLenInBytes =
            interSz;
    }
    gProcess.qzstdInst[i].srcBuffer =
        (CpaBufferList *)QZSTD_calloc(1, sizeof(CpaBufferList), 0);
    if (NULL == gProcess.qzstdInst[i].srcBuffer) {
        goto cleanup;
    }
    gProcess.qzstdInst[i].srcBuffer->numBuffers = 1;

    if (0 != gProcess.qzstdInst[i].buffMetaSize) {
        gProcess.qzstdInst[i].srcBuffer->pPrivateMetaData =
            QZSTD_calloc(1, (size_t)gProcess.qzstdInst[i].buffMetaSize, reqPhyContMem);
        if (NULL == gProcess.qzstdInst[i].srcBuffer->pPrivateMetaData) {
            goto cleanup;
        }
    }

    gProcess.qzstdInst[i].srcBuffer->pBuffers =
        (CpaFlatBuffer *)QZSTD_calloc(1, sizeof(CpaFlatBuffer), reqPhyContMem);
    if (NULL == gProcess.qzstdInst[i].srcBuffer->pBuffers) {
        goto cleanup;
    }
    if (reqPhyContMem) {
        gProcess.qzstdInst[i].srcBuffer->pBuffers->pData =
            (Cpa8U *)QZSTD_calloc(1, srcSz, reqPhyContMem);
        if (NULL == gProcess.qzstdInst[i].srcBuffer->pBuffers->pData) {
            goto cleanup;
        }
    } else {
        gProcess.qzstdInst[i].srcBuffer->pBuffers->pData = NULL;
    }

    gProcess.qzstdInst[i].destBuffer =
        (CpaBufferList *)QZSTD_calloc(1, sizeof(CpaBufferList), 0);
    if (NULL == gProcess.qzstdInst[i].destBuffer) {
        goto cleanup;
    }
    gProcess.qzstdInst[i].destBuffer->numBuffers = 1;

    if (0 != gProcess.qzstdInst[i].buffMetaSize) {
        gProcess.qzstdInst[i].destBuffer->pPrivateMetaData =
            QZSTD_calloc(1, (size_t)gProcess.qzstdInst[i].buffMetaSize, reqPhyContMem);
        if (NULL == gProcess.qzstdInst[i].destBuffer->pPrivateMetaData) {
            goto cleanup;
        }
    }

    gProcess.qzstdInst[i].destBuffer->pBuffers =
        (CpaFlatBuffer *)QZSTD_calloc(1, sizeof(CpaFlatBuffer), reqPhyContMem);
    if (NULL == gProcess.qzstdInst[i].destBuffer->pBuffers) {
        goto cleanup;
    }

    gProcess.qzstdInst[i].memSetup = 1;

done_inst:
    return rc;

cleanup:
    QZSTD_cleanUpInstMem(i);
    rc = QZSTD_FAIL;
    goto done_inst;
}

static int QZSTD_startDcInstance(int i)
{
    int rc = QZSTD_OK;

    if (CPA_STATUS_SUCCESS != cpaDcSetAddressTranslation(
            gProcess.dcInstHandle[i], (CpaVirtualToPhysical)QZSTD_virtToPhys)) {
        rc = QZSTD_FAIL;
        goto done;
    }

    gProcess.qzstdInst[i].instStartStatus = cpaDcStartInstance(
        gProcess.dcInstHandle[i], gProcess.qzstdInst[i].intermediateCnt,
        gProcess.qzstdInst[i].intermediateBuffers);
    if (CPA_STATUS_SUCCESS != gProcess.qzstdInst[i].instStartStatus) {
        rc = QZSTD_FAIL;
        goto done;
    }

    gProcess.qzstdInst[i].seqNumIn = 0;
    gProcess.qzstdInst[i].seqNumOut = 0;
    gProcess.qzstdInst[i].dcInstSetup = 1;

done:
    return rc;
}

static int QZSTD_cpaInitSess(QZSTD_Session_T *sess, int i)
{
    Cpa32U sessionSize = 0;
    Cpa32U ctxSize = 0;
    unsigned char reqPhyContMem = gProcess.qzstdInst[i].reqPhyContMem;

    /*setup and start DC session*/
    if (CPA_STATUS_SUCCESS != cpaDcGetSessionSize(gProcess.dcInstHandle[i],
                            &sess->sessionSetupData, &sessionSize, &ctxSize)) {
        return QZSTD_FAIL;
    }

    gProcess.qzstdInst[i].cpaSessHandle = QZSTD_calloc(1, (size_t)(sessionSize), reqPhyContMem);
    if (NULL == gProcess.qzstdInst[i].cpaSessHandle) {
        return QZSTD_FAIL;
    }

    if (CPA_STATUS_SUCCESS != cpaDcInitSession(
        gProcess.dcInstHandle[i], gProcess.qzstdInst[i].cpaSessHandle,
        &sess->sessionSetupData, NULL, ZSTD_QAT_dcCallback)) {
        QZSTD_free(gProcess.qzstdInst[i].cpaSessHandle, reqPhyContMem);
        gProcess.qzstdInst[i].cpaSessHandle = NULL;
        return QZSTD_FAIL;
    }

    gProcess.qzstdInst[i].sessionSetupData = sess->sessionSetupData;
    gProcess.qzstdInst[i].cpaSessSetup = 1;

    return QZSTD_OK;
}

static int QZSTD_cpaUpdateSess(QZSTD_Session_T *sess, int i)
{
    unsigned char reqPhyContMem = gProcess.qzstdInst[i].reqPhyContMem;
    gProcess.qzstdInst[i].cpaSessSetup = 0;

    /* Remove session */
    if (CPA_STATUS_SUCCESS != cpaDcRemoveSession(
        gProcess.dcInstHandle[i], gProcess.qzstdInst[i].cpaSessHandle)) {
        return QZSTD_FAIL;
    }

    QZSTD_free(gProcess.qzstdInst[i].cpaSessHandle, reqPhyContMem);

    return QZSTD_cpaInitSess(sess, i);
}

static int QZSTD_grabInstance(int hint)
{
    int i, j, rc, f;

    if (hint >= gProcess.numInstances || hint < 0) {
        hint = 0;
    }

    /*otherwise loop through all of them*/
    f = 0;
    for (j = 0; j < MAX_GRAB_RETRY; j++) {
        for (i = 0; i < gProcess.numInstances; i++) {
            if (f == 0) {
                i = hint;
                f = 1;
            };
            rc = __sync_lock_test_and_set(&(gProcess.qzstdInst[i].lock), 1);
            if (0 == rc) {
                return i;
            }
        }
    }
    return -1;
}

static void QZSTD_releaseInstance(int i)
{
    __sync_lock_release(&(gProcess.qzstdInst[i].lock));
}

static void QZSTD_setupSess(QZSTD_Session_T *zstdSess)
{
    zstdSess->instHint = -1;
    zstdSess->sessionSetupData.compType = CPA_DC_LZ4S;
    zstdSess->sessionSetupData.autoSelectBestHuffmanTree = CPA_DC_ASB_DISABLED;
    zstdSess->sessionSetupData.sessDirection = CPA_DC_DIR_COMPRESS;
    zstdSess->sessionSetupData.sessState = CPA_DC_STATELESS;
    zstdSess->sessionSetupData.checksum = CPA_DC_XXHASH32;
    zstdSess->sessionSetupData.huffType = CPA_DC_HT_STATIC;
    zstdSess->sessionSetupData.minMatch = CPA_DC_MIN_3_BYTE_MATCH;
    zstdSess->failOffloadCnt = 0;
}

int QZSTD_startQatDevice(void)
{
    pthread_mutex_lock(&gProcess.mutex);

    if (QZSTD_FAIL == gProcess.qzstdInitStatus) {
        gProcess.qzstdInitStatus = QZSTD_OK == QZSTD_salUserStart() ? QZSTD_STARTED : QZSTD_FAIL;
    }

    if (QZSTD_STARTED == gProcess.qzstdInitStatus) {
        gProcess.qzstdInitStatus = QZSTD_OK == QZSTD_getAndShuffleInstance() ? QZSTD_OK : QZSTD_STARTED;
    }
    pthread_mutex_unlock(&gProcess.mutex);
    return gProcess.qzstdInitStatus;
}

static unsigned isLittleEndian(void)
{
    const union {
        unsigned int u;
        unsigned char c[4];
    } one = {1}; /* don't use static : performance detrimental */
    return one.c[0];
}

static unsigned short read16(const void *memPtr)
{
    unsigned short val;
    memcpy(&val, memPtr, sizeof(val));
    return val;
}

static unsigned short readLE16(const void *memPtr)
{
    if (isLittleEndian()) {
        return read16(memPtr);
    } else {
        const unsigned char *p = (const unsigned char *)memPtr;
        return (unsigned short)((unsigned short)p[0] + (p[1] << 8));
    }
}

void *QZSTD_createSeqProdState()
{
    QZSTD_Session_T *zstdSess = (QZSTD_Session_T *)calloc(1, sizeof(QZSTD_Session_T));
    QZSTD_setupSess(zstdSess);
    return (void*)zstdSess;
}

void QZSTD_freeSeqProdState(void *sequenceProducerState)
{
    QZSTD_Session_T *zstdSess = (QZSTD_Session_T *)sequenceProducerState;
    if (zstdSess) {
        if (zstdSess->qatIntermediateBuf) {
            QZSTD_free(zstdSess->qatIntermediateBuf, zstdSess->reqPhyContMem);
        }
        free(zstdSess);
    }
}

static size_t QZSTD_decLz4s(ZSTD_Sequence *outSeqs, size_t outSeqsCapacity,
    unsigned char *lz4sBuff, unsigned int lz4sBufSize)
{
    unsigned char *ip = lz4sBuff;
    unsigned char *endip = lz4sBuff + lz4sBufSize;
    unsigned int histLiteralLen = 0;

    size_t seqsIdx = 0;

     while (ip < endip && lz4sBufSize > 0) {
        size_t length = 0;
        size_t offset = 0;
        unsigned int literalLen = 0, matchlen = 0;
        /* get literal length */
        unsigned const token = *ip++;
        if ((length = (token >> ML_BITS)) == RUN_MASK) {
            unsigned s;
            do {
                s = *ip++;
                 length += s;
              } while (s == 255);
        }
         literalLen = (unsigned short)length;
        ip += length;
        if (ip == endip) { /* Meet the end of the LZ4 sequence */
            literalLen += histLiteralLen;
            outSeqs[seqsIdx].litLength = literalLen;
            outSeqs[seqsIdx].offset = offset;
            outSeqs[seqsIdx].matchLength = matchlen;
            break;
        }

        /* get matchPos */
        offset = readLE16(ip);
        ip += 2;

        /* get match length */
        length = token & ML_MASK;
        if (length == ML_MASK) {
            unsigned s;
            do {
                s = *ip++;
                length += s;
            } while (s == 255);
        }
        if (length != 0) {
            length += LZ4MINMATCH;
            matchlen = (unsigned short)length;
            literalLen += histLiteralLen;

            /* update ZSTD_Sequence */
            outSeqs[seqsIdx].offset = offset;
            outSeqs[seqsIdx].litLength = literalLen;
            outSeqs[seqsIdx].matchLength = matchlen;
            histLiteralLen = 0;
            ++seqsIdx;
            assert(seqsIdx < (outSeqsCapacity - 1));
        } else {
            if (literalLen > 0) {
                /* When match length is 0, the literalLen needs to be
                temporarily stored and processed together with the next data
                block.*/
                histLiteralLen += literalLen;
            }
        }
    }
    assert(ip == endip);
    return ++seqsIdx;
}

static inline void QZSTD_castConstPointer(unsigned char **dest, const void **src)
{
    memcpy(dest, src, sizeof(char *));
}

static inline int QZSTD_isTimeOut(struct timeval timeStart, struct timeval timeNow)
{
    long long timeSpent = TIMESPENT(timeNow, timeStart);
    return timeSpent > MAXTIMEOUT ? 1 : 0;
}

size_t qatSequenceProducer(
    void* sequenceProducerState, ZSTD_Sequence* outSeqs, size_t outSeqsCapacity,
    const void* src, size_t srcSize,
    const void* dict, size_t dictSize,
    int compressionLevel,
    size_t windowSize)
{
    int i;
    size_t rc = ZSTD_SEQUENCE_PRODUCER_ERROR;
    int qrc = CPA_STATUS_FAIL;
    CpaDcOpData opData = {};
    int retry_cnt = MAX_SEND_REQUEST_RETRY;
    struct timeval timeStart;
    struct timeval timeNow;
    QZSTD_Session_T *zstdSess = (QZSTD_Session_T *)sequenceProducerState;

    if (windowSize < (srcSize < 32 * KB ? srcSize : 32 * KB) || dictSize > 0) {
        DEBUG("Currently not use windowsSize and not support dictionary\n");
        return ZSTD_SEQUENCE_PRODUCER_ERROR;
    }

    /* QAT only support L1-L12 */
    if (compressionLevel < COMP_LVL_MINIMUM ||
        compressionLevel > COMP_LVL_MAXIMUM) {
        DEBUG("Only can offload L1-L12 to QAT, current compression level: %d\n", compressionLevel);
        return ZSTD_SEQUENCE_PRODUCER_ERROR;
    }

    /* check hardware initialization status */
    if (gProcess.qzstdInitStatus != QZSTD_OK) {
        zstdSess->failOffloadCnt++;
        if (zstdSess->failOffloadCnt >= NUM_BLOCK_OF_RETRY_INTERVAL) {
            zstdSess->failOffloadCnt = 0;
            if (QZSTD_startQatDevice() != QZSTD_OK)
                return ZSTD_SEQUENCE_PRODUCER_ERROR;
        } else {
            return ZSTD_SEQUENCE_PRODUCER_ERROR;
        }
    }

    zstdSess->sessionSetupData.compLevel = (CpaDcCompLvl)compressionLevel;

    i = QZSTD_grabInstance(zstdSess->instHint);
    if (-1 == i) {
        return ZSTD_SEQUENCE_PRODUCER_ERROR;
    }

    zstdSess->instHint = i;
    zstdSess->reqPhyContMem = gProcess.qzstdInst[i].reqPhyContMem;

    /* allocate instance's buffer */
    if (0 == gProcess.qzstdInst[i].memSetup) {
        if (QZSTD_OK != QZSTD_allocInstMem(i)) {
            rc = ZSTD_SEQUENCE_PRODUCER_ERROR;
            goto exit;
        }
    }

    /* start Dc Instance */
    if (0 == gProcess.qzstdInst[i].dcInstSetup) {
        if (QZSTD_OK != QZSTD_startDcInstance(i)) {
            rc = ZSTD_SEQUENCE_PRODUCER_ERROR;
            goto exit;
        }
    }

    /* init cpaSessHandle */
    if (0 == gProcess.qzstdInst[i].cpaSessSetup) {
        if (QZSTD_OK != QZSTD_cpaInitSess(zstdSess, i)) {
            rc = ZSTD_SEQUENCE_PRODUCER_ERROR;
            goto exit;
        }
    }

    /* update cpaSessHandle */
    if (0 != memcmp(&zstdSess->sessionSetupData,
        &gProcess.qzstdInst[i].sessionSetupData,
        sizeof(CpaDcSessionSetupData))) {
        if (QZSTD_OK != QZSTD_cpaUpdateSess(zstdSess, i)) {
            rc = ZSTD_SEQUENCE_PRODUCER_ERROR;
            goto exit;
        }
    }

    if (NULL == zstdSess->qatIntermediateBuf) {
        zstdSess->qatIntermediateBuf =
        (unsigned char *)QZSTD_calloc(1, INTERMEDIATE_BUFFER_SZ,
            zstdSess->reqPhyContMem);
        if (NULL == zstdSess->qatIntermediateBuf) {
            rc = ZSTD_SEQUENCE_PRODUCER_ERROR;
            goto exit;
        }
    }

    if (zstdSess->reqPhyContMem) {
        memcpy(gProcess.qzstdInst[i].srcBuffer->pBuffers->pData, src, srcSize);
    } else {
        QZSTD_castConstPointer(&(gProcess.qzstdInst[i].srcBuffer->pBuffers->pData), &src);
    }
    gProcess.qzstdInst[i].destBuffer->pBuffers->pData =
            (Cpa8U *)zstdSess->qatIntermediateBuf;

    gProcess.qzstdInst[i].srcBuffer->pBuffers->dataLenInBytes = srcSize;
    gProcess.qzstdInst[i].destBuffer->pBuffers->dataLenInBytes = INTERMEDIATE_BUFFER_SZ;

    opData.inputSkipData.skipMode = CPA_DC_SKIP_DISABLED;
    opData.outputSkipData.skipMode = CPA_DC_SKIP_DISABLED;
    opData.compressAndVerify = CPA_TRUE;
    opData.flushFlag = CPA_DC_FLUSH_FINAL;

    gProcess.qzstdInst[i].res.checksum = 0;

    do {
        qrc = cpaDcCompressData2(gProcess.dcInstHandle[i],
                                gProcess.qzstdInst[i].cpaSessHandle,
                                gProcess.qzstdInst[i].srcBuffer,
                                gProcess.qzstdInst[i].destBuffer, &opData,
                                &gProcess.qzstdInst[i].res, (void *)&gProcess.qzstdInst[i]);
        retry_cnt--;
    } while (CPA_STATUS_RETRY == qrc && retry_cnt > 0);

    if (CPA_STATUS_SUCCESS != qrc) {
        rc = ZSTD_SEQUENCE_PRODUCER_ERROR;
        goto error;
    }

    gProcess.qzstdInst[i].seqNumIn++;

    (void)gettimeofday(&timeStart, NULL);

    do {
        qrc = icp_sal_DcPollInstance(gProcess.dcInstHandle[i], 0);
        (void)gettimeofday(&timeNow, NULL);
        if (QZSTD_isTimeOut(timeStart, timeNow)) {
            break;
        }
    } while(CPA_STATUS_RETRY == qrc || (CPA_STATUS_SUCCESS == qrc && gProcess.qzstdInst[i].seqNumIn != gProcess.qzstdInst[i].seqNumOut));

    if (CPA_STATUS_FAIL == qrc) {
        gProcess.qzstdInst[i].seqNumOut++;
        rc = ZSTD_SEQUENCE_PRODUCER_ERROR;
        goto error;
    }

    if (CPA_STATUS_RETRY == qrc) {
        rc = ZSTD_SEQUENCE_PRODUCER_ERROR;
        goto error;
    }
    if (gProcess.qzstdInst[i].cbStatus == QZSTD_FAIL) {
        rc = ZSTD_SEQUENCE_PRODUCER_ERROR;
        goto error;
    }

    if (gProcess.qzstdInst[i].res.consumed < srcSize ||
        gProcess.qzstdInst[i].res.produced == 0 ||
        gProcess.qzstdInst[i].res.produced > INTERMEDIATE_BUFFER_SZ ||
        CPA_STATUS_SUCCESS != gProcess.qzstdInst[i].res.status) {
        rc = ZSTD_SEQUENCE_PRODUCER_ERROR;
        goto error;
    }

    rc = QZSTD_decLz4s(outSeqs, outSeqsCapacity, zstdSess->qatIntermediateBuf, gProcess.qzstdInst[i].res.produced);
    assert(rc < (outSeqsCapacity - 1));

error:
    /* reset pData */
    if (!zstdSess->reqPhyContMem) {
        gProcess.qzstdInst[i].srcBuffer->pBuffers->pData = NULL;
    }
    gProcess.qzstdInst[i].destBuffer->pBuffers->pData = NULL;

exit:
    /* release QAT instance */
    QZSTD_releaseInstance(i);
    return rc;
}
