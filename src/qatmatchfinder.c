/**
 *****************************************************************************
 *      Dependencies
 *****************************************************************************/
#include "qatmatchfinder.h"

/**
 *****************************************************************************
 *      Global variable
 *****************************************************************************/
ZSTD_QAT_ProcessData_T gProcess = {
    .qzstdInitStatus = ZSTD_QAT_FAIL,
    .mutex = PTHREAD_MUTEX_INITIALIZER,
    .cctxNum = 0,
};

/**
 *****************************************************************************
 *      Macro
 *****************************************************************************/
#define ML_BITS 4
#define ML_MASK ((1U << ML_BITS) - 1)
#define RUN_BITS (8 - ML_BITS)
#define RUN_MASK ((1U << RUN_BITS) - 1)
#ifdef SET_QATMINMATCH_TOFOUR
#define LZ4MINMATCH 3
#else
#define LZ4MINMATCH 2
#endif

/* Max latency of polling in the worst condition */
#define MAXTIMEOUT 2000000

#define ZSTD_QAT_TIMESPENT(a, b) ((a.tv_sec * 1000000 + a.tv_usec) - (b.tv_sec * 1000000 + b.tv_usec))

/**
 *****************************************************************************
 *      Functions
 *****************************************************************************/
size_t ZSTD_convertBlockSequencesToSeqStore(ZSTD_CCtx *cctx,
                                            const ZSTD_Sequence *inSeqs,
                                            size_t inSeqsSize, const void *src,
                                            size_t srcSize);


static void *ZSTD_QAT_calloc(size_t nb, size_t size, unsigned char reqPhyContMem)
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

static void ZSTD_QAT_free(void *ptr, unsigned char reqPhyContMem)
{
    if(!reqPhyContMem) {
        free(ptr);
    } else {
#ifdef ENABLE_USDM_DRV
        qaeMemFreeNUMA(&ptr);
#else
        printf("Don't support QAT USDM driver\n");
#endif
    }
}

static __inline CpaPhysicalAddr ZSTD_QAT_virtToPhys(void *virtAddr)
{
#ifdef ENABLE_USDM_DRV
    return (CpaPhysicalAddr)qaeVirtToPhysNUMA(virtAddr);
#else
    return (CpaPhysicalAddr)virtAddr;
#endif
}

static ZSTD_QAT_InstanceList_T *ZSTD_QAT_getInstance(unsigned int devId,
                                              ZSTD_QAT_Hardware_T *qatHw)
{
    ZSTD_QAT_InstanceList_T *instances;
    ZSTD_QAT_InstanceList_T *firstInstance;
    int i;

    if (devId >= ZSTD_QAT_MAX_DEVICES || NULL == qatHw) {
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

static void ZSTD_QAT_clearDevices(ZSTD_QAT_Hardware_T *qatHw)
{
    unsigned int i;
    if (NULL == qatHw || 0 == qatHw->devNum) {
        return;
    }

    for (i = 0; i <= qatHw->maxDevId; i++) {
        ZSTD_QAT_InstanceList_T *inst = ZSTD_QAT_getInstance(i, qatHw);
        while (inst) {
            free(inst);
            inst = ZSTD_QAT_getInstance(i, qatHw);
        }
    }
}

static void ZSTD_QAT_stopQat(void)
{
    int i;
    CpaStatus status = CPA_STATUS_SUCCESS;

    printf("Call stopQat.\n");
    if (NULL != gProcess.dcInstHandle &&
        NULL != gProcess.qzstdInst) {
        for (i = 0; i < gProcess.numInstances; i++) {
            if (0 != gProcess.qzstdInst[i].dcInstSetup) {
                status = cpaDcStopInstance(gProcess.dcInstHandle[i]);
                if (CPA_STATUS_SUCCESS != status) {
                    printf("Stop instance failed, status=%d\n", status);
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
    gProcess.qzstdInitStatus = ZSTD_QAT_FAIL;
}

static void ZSTD_QAT_removeSession(int i)
{
    unsigned char reqPhyContMem = gProcess.qzstdInst[i].reqPhyContMem;
    int rc;

    if (0 == gProcess.qzstdInst[i].cpaSessSetup) {
        return;
    }

    /* Remove session */
    if ((NULL != gProcess.dcInstHandle[i]) &&
        (NULL != gProcess.qzstdInst[i].cpaSessHandle)) {
        // polling here if there still are some reponse haven't beed polled
        // if didn't poll there response, cpaDcRemoveSession will raise error message
        do {
            rc = icp_sal_DcPollInstance(gProcess.dcInstHandle[i], 0);
        } while(CPA_STATUS_SUCCESS == rc);
        cpaDcRemoveSession(gProcess.dcInstHandle[i],
                           gProcess.qzstdInst[i].cpaSessHandle);
        ZSTD_QAT_free(gProcess.qzstdInst[i].cpaSessHandle, reqPhyContMem);
        gProcess.qzstdInst[i].cpaSessHandle = NULL;
        gProcess.qzstdInst[i].cpaSessSetup = 0;
    }
}

static void ZSTD_QAT_cleanUpInstMem(int i)
{
    int j;
    ZSTD_QAT_Instance_T *qzstdInst = &(gProcess.qzstdInst[i]);
    unsigned char reqPhyContMem = qzstdInst->reqPhyContMem;

    if (NULL != qzstdInst->intermediateBuffers) {
        for (j = 0; j < qzstdInst->intermediateCnt; j++) {
            if (NULL != qzstdInst->intermediateBuffers[j]) {
                if (NULL != qzstdInst->intermediateBuffers[j]->pBuffers) {
                    if (NULL != qzstdInst->intermediateBuffers[j]->pBuffers->pData) {
                        ZSTD_QAT_free(qzstdInst->intermediateBuffers[j]->pBuffers->pData, reqPhyContMem);
                        qzstdInst->intermediateBuffers[j]->pBuffers->pData = NULL;
                    }
                    ZSTD_QAT_free(qzstdInst->intermediateBuffers[j]->pBuffers, reqPhyContMem);
                    qzstdInst->intermediateBuffers[j]->pBuffers = NULL;
                }
                if (NULL != qzstdInst->intermediateBuffers[j]->pPrivateMetaData) {
                    ZSTD_QAT_free(qzstdInst->intermediateBuffers[j]->pPrivateMetaData, reqPhyContMem);
                    qzstdInst->intermediateBuffers[j]->pPrivateMetaData = NULL;
                }
                ZSTD_QAT_free(qzstdInst->intermediateBuffers[j], 0);
                qzstdInst->intermediateBuffers[j] = NULL;
            }
        }
        ZSTD_QAT_free(qzstdInst->intermediateBuffers, 0);
        qzstdInst->intermediateBuffers = NULL;
    }

    /*src buffer*/
    if (NULL != qzstdInst->srcBuffer) {
        if (reqPhyContMem && qzstdInst->srcBuffer->pBuffers) {
            if (NULL != qzstdInst->srcBuffer->pBuffers->pData) {
                ZSTD_QAT_free(qzstdInst->srcBuffer->pBuffers->pData, reqPhyContMem);
                qzstdInst->srcBuffer->pBuffers->pData = NULL;
            }
        }
        if (NULL != qzstdInst->srcBuffer->pBuffers) {
            ZSTD_QAT_free(qzstdInst->srcBuffer->pBuffers, reqPhyContMem);
            qzstdInst->srcBuffer->pBuffers = NULL;
        }
        if (NULL != qzstdInst->srcBuffer->pPrivateMetaData) {
            ZSTD_QAT_free(qzstdInst->srcBuffer->pPrivateMetaData, reqPhyContMem);
            qzstdInst->srcBuffer->pPrivateMetaData = NULL;
        }
        ZSTD_QAT_free(qzstdInst->srcBuffer, 0);
        qzstdInst->srcBuffer = NULL;
    }

    /*dest buffer*/
    if (NULL != qzstdInst->destBuffer) {
        if (NULL != qzstdInst->destBuffer->pBuffers) {
            ZSTD_QAT_free(qzstdInst->destBuffer->pBuffers, reqPhyContMem);
            qzstdInst->destBuffer->pBuffers = NULL;
        }
        if (NULL != qzstdInst->destBuffer->pPrivateMetaData) {
            ZSTD_QAT_free(qzstdInst->destBuffer->pPrivateMetaData, reqPhyContMem);
            qzstdInst->destBuffer->pPrivateMetaData = NULL;
        }
        ZSTD_QAT_free(qzstdInst->destBuffer, 0);
        qzstdInst->destBuffer = NULL;
    }
}

void ZSTD_QAT_stopQatDevice()
{
    pthread_mutex_lock(&gProcess.mutex);
    if (0 == --gProcess.cctxNum) {
        if (ZSTD_QAT_OK == gProcess.qzstdInitStatus) {
            int i = 0;

            for (i = 0; i < gProcess.numInstances; i++) {
                if (0 != gProcess.qzstdInst[i].cpaSessSetup) {
                    ZSTD_QAT_removeSession(i);
                }
                if (0 != gProcess.qzstdInst[i].memSetup) {
                    ZSTD_QAT_cleanUpInstMem(i);
                }
            }
            ZSTD_QAT_stopQat();
        }
        if (ZSTD_QAT_STARTED == gProcess.qzstdInitStatus) {
            (void)icp_sal_userStop();
            gProcess.qzstdInitStatus = ZSTD_QAT_FAIL;
        }
    }
    pthread_mutex_unlock(&gProcess.mutex);
}

static int ZSTD_QAT_setInstance(unsigned int devId,
                             ZSTD_QAT_InstanceList_T *newInstance,
                             ZSTD_QAT_Hardware_T *qatHw)
{
    ZSTD_QAT_InstanceList_T *instances;

    if (devId >= ZSTD_QAT_MAX_DEVICES || NULL == newInstance || NULL == qatHw ||
        NULL != newInstance->next) {
        return ZSTD_QAT_FAIL;
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

    return ZSTD_QAT_OK;
}

static int ZSTD_QAT_salUserStart(void)
{
    Cpa32U pcieCount;

    if (CPA_STATUS_SUCCESS != icp_adf_get_numDevices(&pcieCount)) {
        return ZSTD_QAT_FAIL;
    }

    if (0 == pcieCount) {
        return ZSTD_QAT_FAIL;
    }

    if (CPA_STATUS_SUCCESS != icp_sal_userStart("SHIM")) {
        printf("icp_sal_userStart failed\n");
        return ZSTD_QAT_FAIL;
    }

    return ZSTD_QAT_OK;
}

static int ZSTD_QAT_getAndShuffleInstance(void)
{
    int i;
    unsigned int devId = 0;
    ZSTD_QAT_Hardware_T *qatHw = NULL;
    unsigned int instanceFound = 0;
    unsigned int instanceMatched = 0;
    ZSTD_QAT_InstanceList_T *newInst;
    if (CPA_STATUS_SUCCESS != cpaDcGetNumInstances(&gProcess.numInstances)) {
        printf("cpaDcGetNumInstances failed\n");
        goto exit;
    }

    gProcess.dcInstHandle = (CpaInstanceHandle *)calloc(
        gProcess.numInstances, sizeof(CpaInstanceHandle));
    gProcess.qzstdInst = (ZSTD_QAT_Instance_T *)calloc(gProcess.numInstances,
                                                     sizeof(ZSTD_QAT_Instance_T));
    if (NULL == gProcess.dcInstHandle || NULL == gProcess.qzstdInst) {
        printf("calloc for qzstdInst failed\n");
        goto exit;
    }

    if (CPA_STATUS_SUCCESS != cpaDcGetInstances(
            gProcess.numInstances, gProcess.dcInstHandle)) {
        printf("cpaDcGetInstances failed\n");
        goto exit;
    }

    qatHw = (ZSTD_QAT_Hardware_T *)calloc(1, sizeof(ZSTD_QAT_Hardware_T));
    if (NULL == qatHw) {
        printf("calloc for qatHw failed\n");
        goto exit;
    }
    for (i = 0; i < gProcess.numInstances; i++) {
        newInst = (ZSTD_QAT_InstanceList_T *)calloc(1, sizeof(ZSTD_QAT_InstanceList_T));
        if (NULL == newInst) {
            printf("calloc failed\n");
            goto exit;
        }

        if (CPA_STATUS_SUCCESS != cpaDcInstanceGetInfo2(
            gProcess.dcInstHandle[i], &newInst->instance.instanceInfo)) {
            printf("cpaDcInstanceGetInfo2 failed\n");
            free(newInst);
            goto exit;
        }

        if (CPA_STATUS_SUCCESS != cpaDcQueryCapabilities(
            gProcess.dcInstHandle[i], &newInst->instance.instanceCap)) {
            printf("cpaDcQueryCapabilities failed\n");
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
        if (ZSTD_QAT_OK != ZSTD_QAT_setInstance(devId, newInst, qatHw)) {
            printf("ZSTD_QAT_setInstance on device %d failed\n", devId);
            free(newInst);
            goto exit;
        }
    }

    /* shuffle instance */
    for (i = 0; instanceFound < gProcess.numInstances; i++) {
        devId = i % (qatHw->maxDevId + 1);
        newInst = ZSTD_QAT_getInstance(devId, qatHw);
        if (NULL == newInst) {
            continue;
        }
        instanceFound++;

        // check lz4s support
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
                    sizeof(ZSTD_QAT_Instance_T));
        gProcess.dcInstHandle[instanceMatched] = newInst->dcInstHandle;
        free(newInst);
        instanceMatched++;
    }

    if (instanceMatched == 0) {
        goto exit;
    }

    ZSTD_QAT_clearDevices(qatHw);
    free(qatHw);

    return ZSTD_QAT_OK;

exit:
    if (qatHw) {
        ZSTD_QAT_clearDevices(qatHw);
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
    return ZSTD_QAT_FAIL;
}

static void ZSTD_QAT_dcCallback(void *cbDataTag, CpaStatus stat)
{
    if (NULL != cbDataTag) {
        ZSTD_QAT_Instance_T *qzstdInst = (ZSTD_QAT_Instance_T *)cbDataTag;
        qzstdInst->seqNumOut++;
        if (qzstdInst->seqNumIn != qzstdInst->seqNumOut) {
            return;
        }

        if (CPA_DC_OK == stat) {
            qzstdInst->cbStatus = ZSTD_QAT_OK;
        } else {
            qzstdInst->cbStatus = ZSTD_QAT_FAIL;
        }
    }
}

/* TODO: Need to add huge page support if SVM is not well supported in driver */
static int ZSTD_QAT_allocInstMem(int i)
{
    int j;
    CpaStatus status;
    CpaStatus rc;
    unsigned int srcSz;
    unsigned int interSz;
    unsigned char reqPhyContMem = gProcess.qzstdInst[i].reqPhyContMem;

    rc = ZSTD_QAT_OK;
    srcSz = ZSTD_QAT_COMPRESS_SRC_BUFF_SZ;
    interSz = ZSTD_QAT_INTER_SZ(srcSz);

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
        (CpaBufferList **)ZSTD_QAT_calloc((size_t)gProcess.qzstdInst[i].intermediateCnt,
                                 sizeof(CpaBufferList *), 0);
    if (NULL == gProcess.qzstdInst[i].intermediateBuffers) {
        goto cleanup;
    }

    for (j = 0; j < gProcess.qzstdInst[i].intermediateCnt; j++) {
        gProcess.qzstdInst[i].intermediateBuffers[j] =
            (CpaBufferList *)ZSTD_QAT_calloc(1, sizeof(CpaBufferList), 0);
        if (NULL == gProcess.qzstdInst[i].intermediateBuffers[j]) {
            goto cleanup;
        }
        if (0 != gProcess.qzstdInst[i].buffMetaSize) {
            gProcess.qzstdInst[i].intermediateBuffers[j]->pPrivateMetaData =
                ZSTD_QAT_calloc(1, (size_t)(gProcess.qzstdInst[i].buffMetaSize), reqPhyContMem);
            if (NULL ==
                gProcess.qzstdInst[i].intermediateBuffers[j]->pPrivateMetaData) {
                goto cleanup;
            }
        }

        gProcess.qzstdInst[i].intermediateBuffers[j]->pBuffers =
            (CpaFlatBuffer *)ZSTD_QAT_calloc(1, sizeof(CpaFlatBuffer), reqPhyContMem);
        if (NULL == gProcess.qzstdInst[i].intermediateBuffers[j]->pBuffers) {
            goto cleanup;
        }

        gProcess.qzstdInst[i].intermediateBuffers[j]->pBuffers->pData =
            (Cpa8U *)ZSTD_QAT_calloc(1, interSz, reqPhyContMem);
        if (NULL ==
            gProcess.qzstdInst[i].intermediateBuffers[j]->pBuffers->pData) {
            goto cleanup;
        }

        gProcess.qzstdInst[i].intermediateBuffers[j]->pBuffers->dataLenInBytes =
            interSz;
    }
    gProcess.qzstdInst[i].srcBuffer =
        (CpaBufferList *)ZSTD_QAT_calloc(1, sizeof(CpaBufferList), 0);
    if (NULL == gProcess.qzstdInst[i].srcBuffer) {
        goto cleanup;
    }
    gProcess.qzstdInst[i].srcBuffer->numBuffers = 1;

    if (0 != gProcess.qzstdInst[i].buffMetaSize) {
        gProcess.qzstdInst[i].srcBuffer->pPrivateMetaData =
            ZSTD_QAT_calloc(1, (size_t)gProcess.qzstdInst[i].buffMetaSize, reqPhyContMem);
        if (NULL == gProcess.qzstdInst[i].srcBuffer->pPrivateMetaData) {
            goto cleanup;
        }
    }

    gProcess.qzstdInst[i].srcBuffer->pBuffers =
        (CpaFlatBuffer *)ZSTD_QAT_calloc(1, sizeof(CpaFlatBuffer), reqPhyContMem);
    if (NULL == gProcess.qzstdInst[i].srcBuffer->pBuffers) {
        goto cleanup;
    }
    if (reqPhyContMem) {
        gProcess.qzstdInst[i].srcBuffer->pBuffers->pData =
            (Cpa8U *)ZSTD_QAT_calloc(1, srcSz, reqPhyContMem);
        if (NULL == gProcess.qzstdInst[i].srcBuffer->pBuffers->pData) {
            goto cleanup;
        }
    } else {
        gProcess.qzstdInst[i].srcBuffer->pBuffers->pData = NULL;
    }

    gProcess.qzstdInst[i].destBuffer =
        (CpaBufferList *)ZSTD_QAT_calloc(1, sizeof(CpaBufferList), 0);
    if (NULL == gProcess.qzstdInst[i].destBuffer) {
        goto cleanup;
    }
    gProcess.qzstdInst[i].destBuffer->numBuffers = 1;

    if (0 != gProcess.qzstdInst[i].buffMetaSize) {
        gProcess.qzstdInst[i].destBuffer->pPrivateMetaData =
            ZSTD_QAT_calloc(1, (size_t)gProcess.qzstdInst[i].buffMetaSize, reqPhyContMem);
        if (NULL == gProcess.qzstdInst[i].destBuffer->pPrivateMetaData) {
            goto cleanup;
        }
    }

    gProcess.qzstdInst[i].destBuffer->pBuffers =
        (CpaFlatBuffer *)ZSTD_QAT_calloc(1, sizeof(CpaFlatBuffer), reqPhyContMem);
    if (NULL == gProcess.qzstdInst[i].destBuffer->pBuffers) {
        goto cleanup;
    }

    gProcess.qzstdInst[i].memSetup = 1;

done_inst:
    return rc;

cleanup:
    ZSTD_QAT_cleanUpInstMem(i);
    rc = ZSTD_QAT_FAIL;
    goto done_inst;
}

static int ZSTD_QAT_startDcInstance(int i)
{
    int rc = ZSTD_QAT_OK;

    if (CPA_STATUS_SUCCESS != cpaDcSetAddressTranslation(
            gProcess.dcInstHandle[i], (CpaVirtualToPhysical)ZSTD_QAT_virtToPhys)) {
        rc = ZSTD_QAT_FAIL;
        goto done;
    }

    gProcess.qzstdInst[i].instStartStatus = cpaDcStartInstance(
        gProcess.dcInstHandle[i], gProcess.qzstdInst[i].intermediateCnt,
        gProcess.qzstdInst[i].intermediateBuffers);
    if (CPA_STATUS_SUCCESS != gProcess.qzstdInst[i].instStartStatus) {
        rc = ZSTD_QAT_FAIL;
        goto done;
    }

    gProcess.qzstdInst[i].seqNumIn = 0;
    gProcess.qzstdInst[i].seqNumOut = 0;
    gProcess.qzstdInst[i].dcInstSetup = 1;

done:
    return rc;
}

static int ZSTD_QAT_cpaInitSess(ZSTD_QAT_Session_T *sess, int i)
{
    Cpa32U sessionSize = 0;
    Cpa32U ctxSize = 0;
    unsigned char reqPhyContMem = gProcess.qzstdInst[i].reqPhyContMem;

    /*setup and start DC session*/
    if (CPA_STATUS_SUCCESS != cpaDcGetSessionSize(gProcess.dcInstHandle[i],
                            &sess->sessionSetupData, &sessionSize, &ctxSize)) {
        return ZSTD_QAT_FAIL;
    }

    gProcess.qzstdInst[i].cpaSessHandle = ZSTD_QAT_calloc(1, (size_t)(sessionSize), reqPhyContMem);
    if (NULL == gProcess.qzstdInst[i].cpaSessHandle) {
        return ZSTD_QAT_FAIL;
    }

    if (CPA_STATUS_SUCCESS != cpaDcInitSession(
        gProcess.dcInstHandle[i], gProcess.qzstdInst[i].cpaSessHandle,
        &sess->sessionSetupData, NULL, ZSTD_QAT_dcCallback)) {
        free(gProcess.qzstdInst[i].cpaSessHandle);
        gProcess.qzstdInst[i].cpaSessHandle = NULL;
        return ZSTD_QAT_FAIL;
    }

    gProcess.qzstdInst[i].sessionSetupData = sess->sessionSetupData;
    gProcess.qzstdInst[i].cpaSessSetup = 1;

    return ZSTD_QAT_OK;
}

static int ZSTD_QAT_cpaUpdateSess(ZSTD_QAT_Session_T *sess, int i)
{
    unsigned char reqPhyContMem = gProcess.qzstdInst[i].reqPhyContMem;
    gProcess.qzstdInst[i].cpaSessSetup = 0;

    /* Remove session */
    if (CPA_STATUS_SUCCESS != cpaDcRemoveSession(
        gProcess.dcInstHandle[i], gProcess.qzstdInst[i].cpaSessHandle)) {
        return ZSTD_QAT_FAIL;
    }

    ZSTD_QAT_free(gProcess.qzstdInst[i].cpaSessHandle, reqPhyContMem);

    return ZSTD_QAT_cpaInitSess(sess, i);
}

static int ZSTD_QAT_grabInstance(int hint)
{
    int i, j, rc, f;

    if (hint >= gProcess.numInstances || hint < 0) {
        hint = 0;
    }

    /*otherwise loop through all of them*/
    f = 0;
    for (j = 0; j < ZSTD_QAT_MAX_GRAB_RETRY; j++) {
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

static void ZSTD_QAT_releaseInstance(int i)
{
    __sync_lock_release(&(gProcess.qzstdInst[i].lock));
}

static void setupZstdSess(ZSTD_QAT_Session_T *zstdSess)
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

int ZSTD_QAT_startQatDevice(ZSTD_CCtx *cctx)
{
    pthread_mutex_lock(&gProcess.mutex);

    if (NULL != cctx) {
        gProcess.cctxNum++;
    }

    if (ZSTD_QAT_FAIL == gProcess.qzstdInitStatus) {
        gProcess.qzstdInitStatus = ZSTD_QAT_OK == ZSTD_QAT_salUserStart() ? ZSTD_QAT_STARTED : ZSTD_QAT_FAIL;
    }

    if (ZSTD_QAT_STARTED == gProcess.qzstdInitStatus) {
        gProcess.qzstdInitStatus = ZSTD_QAT_OK == ZSTD_QAT_getAndShuffleInstance() ? ZSTD_QAT_OK : ZSTD_QAT_STARTED;
    }
    pthread_mutex_unlock(&gProcess.mutex);
    return gProcess.qzstdInitStatus;
}

static unsigned LZ4S_isLittleEndian(void)
{
    const union {
        unsigned int u;
        unsigned char c[4];
    } one = {1}; /* don't use static : performance detrimental */
    return one.c[0];
}

static unsigned short LZ4S_read16(const void *memPtr)
{
    unsigned short val;
    memcpy(&val, memPtr, sizeof(val));
    return val;
}

static unsigned short LZ4S_readLE16(const void *memPtr)
{
    if (LZ4S_isLittleEndian()) {
        return LZ4S_read16(memPtr);
    } else {
        const unsigned char *p = (const unsigned char *)memPtr;
        return (unsigned short)((unsigned short)p[0] + (p[1] << 8));
    }
}

void *ZSTD_QAT_createMatchState()
{
    ZSTD_QAT_Session_T *zstdSess = (ZSTD_QAT_Session_T *)calloc(1, sizeof(ZSTD_QAT_Session_T));
    setupZstdSess(zstdSess);
    return (void*)zstdSess;
}

void ZSTD_QAT_freeMatchState(void *matchState)
{
    ZSTD_QAT_Session_T *zstdSess = (ZSTD_QAT_Session_T *)matchState;
    if (zstdSess) {
        if (zstdSess->qatIntermediateBuf) {
            ZSTD_QAT_free(zstdSess->qatIntermediateBuf, zstdSess->reqPhyContMem);
        }
        free(zstdSess);
    }
}

static size_t ZSTD_QAT_decLz4s(ZSTD_Sequence *outSeqs, size_t outSeqsCapacity,
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
        if (ip == endip) { // Meet the end of the LZ4 sequence
            literalLen += histLiteralLen;
            outSeqs[seqsIdx].litLength = literalLen;
            outSeqs[seqsIdx].offset = offset;
            outSeqs[seqsIdx].matchLength = matchlen;
            break;
        }

        /* get matchPos */
        offset = LZ4S_readLE16(ip);
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

static inline void ZSTD_QAT_castConstPointer(unsigned char **dest, const void **src)
{
    memcpy(dest, src, sizeof(char *));
}

static inline int ZSTD_QAT_isTimeOut(struct timeval timeStart, struct timeval timeNow)
{
    long long timeSpent = ZSTD_QAT_TIMESPENT(timeNow, timeStart);
    return timeSpent > MAXTIMEOUT ? 1 : 0;
}

size_t qatMatchfinder(
    void* externalMatchState, ZSTD_Sequence* outSeqs, size_t outSeqsCapacity,
    const void* src, size_t srcSize,
    const void* dict, size_t dictSize,
    int compressionLevel,
    size_t windowSize)
{
    int i;
    size_t rc = ZSTD_EXTERNAL_MATCHFINDER_ERROR;
    int qrc = CPA_STATUS_FAIL;
    CpaDcOpData opData = {};
    int retry_cnt = ZSTD_QAT_MAX_SEND_REQUEST_RETRY;
    struct timeval timeStart;
    struct timeval timeNow;
    ZSTD_QAT_Session_T *zstdSess = (ZSTD_QAT_Session_T *)externalMatchState;

    // QAT only support L1-L12
    if (compressionLevel < ZSTD_QAT_COMP_LVL_MINIMUM ||
        compressionLevel > ZSTD_QAT_COMP_LVL_MAXIMUM) {
        return ZSTD_EXTERNAL_MATCHFINDER_ERROR;
    }

    // check hardware initialization status
    if (gProcess.qzstdInitStatus != ZSTD_QAT_OK) {
        zstdSess->failOffloadCnt++;
        if (zstdSess->failOffloadCnt >= ZSTD_QAT_NUM_BLOCK_OF_RETRY_INTERVAL) {
            zstdSess->failOffloadCnt = 0;
            if (ZSTD_QAT_startQatDevice(NULL) != ZSTD_QAT_OK)
                return ZSTD_EXTERNAL_MATCHFINDER_ERROR;
        } else {
            return ZSTD_EXTERNAL_MATCHFINDER_ERROR;
        }
    }

    zstdSess->sessionSetupData.compLevel = (CpaDcCompLvl)compressionLevel;

    i = ZSTD_QAT_grabInstance(zstdSess->instHint);
    if (-1 == i) {
        return ZSTD_EXTERNAL_MATCHFINDER_ERROR;
    }

    zstdSess->instHint = i;
    zstdSess->reqPhyContMem = gProcess.qzstdInst[i].reqPhyContMem;

    // allocate buffer
    if (0 == gProcess.qzstdInst[i].memSetup) {
        if (ZSTD_QAT_OK != ZSTD_QAT_allocInstMem(i)) {
            rc = ZSTD_EXTERNAL_MATCHFINDER_ERROR;
            goto exit;
        }
    }

    // start Dc Instance
    if (0 == gProcess.qzstdInst[i].dcInstSetup) {
        if (ZSTD_QAT_OK != ZSTD_QAT_startDcInstance(i)) {
            rc = ZSTD_EXTERNAL_MATCHFINDER_ERROR;
            goto exit;
        }
    }

    // init cpaSessHandle
    if (0 == gProcess.qzstdInst[i].cpaSessSetup) {
        if (ZSTD_QAT_OK != ZSTD_QAT_cpaInitSess(zstdSess, i)) {
            rc = ZSTD_EXTERNAL_MATCHFINDER_ERROR;
            goto exit;
        }
    }

    // update cpaSessHandle
    if (0 != memcmp(&zstdSess->sessionSetupData,
        &gProcess.qzstdInst[i].sessionSetupData,
        sizeof(CpaDcSessionSetupData))) {
        if (ZSTD_QAT_OK != ZSTD_QAT_cpaUpdateSess(zstdSess, i)) {
            rc = ZSTD_EXTERNAL_MATCHFINDER_ERROR;
            goto exit;
        }
    }

    if (NULL == zstdSess->qatIntermediateBuf) {
        zstdSess->qatIntermediateBuf =
        (unsigned char *)ZSTD_QAT_calloc(1, ZSTD_QAT_INTERMEDIATE_BUFFER_SZ,
            zstdSess->reqPhyContMem);
        if (NULL == zstdSess->qatIntermediateBuf) {
            rc = ZSTD_EXTERNAL_MATCHFINDER_ERROR;
            goto exit;
        }
    }

    if (zstdSess->reqPhyContMem) {
        memcpy(gProcess.qzstdInst[i].srcBuffer->pBuffers->pData, src, srcSize);
    } else {
        ZSTD_QAT_castConstPointer(&(gProcess.qzstdInst[i].srcBuffer->pBuffers->pData), &src);
    }
    gProcess.qzstdInst[i].destBuffer->pBuffers->pData =
            (Cpa8U *)zstdSess->qatIntermediateBuf;

    gProcess.qzstdInst[i].srcBuffer->pBuffers->dataLenInBytes = srcSize;
    gProcess.qzstdInst[i].destBuffer->pBuffers->dataLenInBytes = ZSTD_QAT_INTERMEDIATE_BUFFER_SZ;

    opData.inputSkipData.skipMode = CPA_DC_SKIP_DISABLED;
    opData.outputSkipData.skipMode = CPA_DC_SKIP_DISABLED;
    opData.compressAndVerify = CPA_TRUE;
    opData.flushFlag = CPA_DC_FLUSH_FINAL;

    gProcess.qzstdInst[i].res.checksum = 0;

    do {
        qrc = cpaDcCompressData2(gProcess.dcInstHandle[i], // todo: use local var
                                gProcess.qzstdInst[i].cpaSessHandle,
                                gProcess.qzstdInst[i].srcBuffer,
                                gProcess.qzstdInst[i].destBuffer, &opData,
                                &gProcess.qzstdInst[i].res, (void *)&gProcess.qzstdInst[i]);
        retry_cnt--;
    } while (CPA_STATUS_RETRY == qrc && retry_cnt > 0);

    if (CPA_STATUS_SUCCESS != qrc) {
        rc = ZSTD_EXTERNAL_MATCHFINDER_ERROR;
        goto error;
    }

    gProcess.qzstdInst[i].seqNumIn++;

    (void)gettimeofday(&timeStart, NULL);

    do {
        qrc = icp_sal_DcPollInstance(gProcess.dcInstHandle[i], 0);
        (void)gettimeofday(&timeNow, NULL);
        if (ZSTD_QAT_isTimeOut(timeStart, timeNow)) {
            break;
        }
    } while(CPA_STATUS_RETRY == qrc || (CPA_STATUS_SUCCESS == qrc && gProcess.qzstdInst[i].seqNumIn != gProcess.qzstdInst[i].seqNumOut));

    if (CPA_STATUS_FAIL == qrc) {
        gProcess.qzstdInst[i].seqNumOut++;
        rc = ZSTD_EXTERNAL_MATCHFINDER_ERROR;
        goto error;
    }

    if (CPA_STATUS_RETRY == qrc) {
        rc = ZSTD_EXTERNAL_MATCHFINDER_ERROR;
        goto error;
    }
    if (gProcess.qzstdInst[i].cbStatus == ZSTD_QAT_FAIL) {
        rc = ZSTD_EXTERNAL_MATCHFINDER_ERROR;
        goto error;
    }

    // TODO: need to check why lz4s bound formula is not right in some case
    // Workaround solution: if compressed size is smaller than source size,
    // fall back to software
    if (gProcess.qzstdInst[i].res.consumed < srcSize ||
        gProcess.qzstdInst[i].res.produced == 0 ||
        gProcess.qzstdInst[i].res.produced > ZSTD_QAT_INTERMEDIATE_BUFFER_SZ ||
        CPA_STATUS_SUCCESS != gProcess.qzstdInst[i].res.status) {
        rc = ZSTD_EXTERNAL_MATCHFINDER_ERROR;
        goto error;
    }

    rc = ZSTD_QAT_decLz4s(outSeqs, outSeqsCapacity, zstdSess->qatIntermediateBuf, gProcess.qzstdInst[i].res.produced);
    assert(rc < (outSeqsCapacity - 1));

error:
    // reset pData
    if (!zstdSess->reqPhyContMem) {
        gProcess.qzstdInst[i].srcBuffer->pBuffers->pData = NULL;
    }
    gProcess.qzstdInst[i].destBuffer->pBuffers->pData = NULL;

exit:
    // release QAT instance
    ZSTD_QAT_releaseInstance(i);
    return rc;
}
