#ifndef ZSTD_QAT_H
#define ZSTD_QAT_H

/**
 *****************************************************************************
 *      Dependencies
 *****************************************************************************/
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

#include "cpa.h"
#include "cpa_dc.h"
#include "icp_sal_poll.h"
#include "icp_sal_user.h"

#ifdef ENABLE_USDM_DRV
#include "qae_mem.h"
#endif

#define ZSTD_STATIC_LINKING_ONLY
#include "zstd.h"

/**
 *****************************************************************************
 *
 *      Macro variable
 *
 *****************************************************************************/
/**
 *****************************************************************************
 * @ingroup qatZstd
 *      QATZSTD Session Status definitions and function return codes
 *
 * @description
 *      This list identifies valid values for session status and function
 *    return codes.
 *
 *****************************************************************************/
typedef enum {
    ZSTD_QAT_OK = 0,       /* Success */
    ZSTD_QAT_STARTED = 1,  /* QAT device started */
    ZSTD_QAT_FAIL = -1     /* Unspecified error */
} ZSTD_QAT_Status_e;

#define ZSTD_QAT_COMP_LVL_DEFAULT               (1)
#define ZSTD_QAT_COMP_LVL_MINIMUM               (1)
#define ZSTD_QAT_COMP_LVL_MAXIMUM               (12)
#define ZSTD_QAT_NUM_BLOCK_OF_RETRY_INTERVAL    (1000)

#define ZSTD_QAT_MAX_GRAB_RETRY                 (10)
#define ZSTD_QAT_MAX_SEND_REQUEST_RETRY         (5)
#define ZSTD_QAT_MAX_DEVICES                    (256)

/**
 *****************************************************************************
 *      Macro functions
 *****************************************************************************/
#define ZSTD_QAT_INTER_SZ(src_sz) (2 * (src_sz))
#define ZSTD_QAT_COMPRESS_SRC_BUFF_SZ (ZSTD_BLOCKSIZE_MAX)
#define ZSTD_QAT_DC_CEIL_DIV(x, y) (((x) + (y)-1) / (y))
/* Formula for GEN4 LZ4S:
* sourceLen + Ceil(sourceLen/2000) * 11 + 1024 */
/* TODO: If use the formula driver provide, it won't pass zstreamtest,
need to check why the buffer size is not aligned with driver formula */
#define ZSTD_QAT_INTERMEDIATE_BUFFER_SZ (ZSTD_BLOCKSIZE_MAX + 1024 + ZSTD_QAT_DC_CEIL_DIV(ZSTD_BLOCKSIZE_MAX, 2000) * 11)

/**
 *****************************************************************************
 *      Data struct
 *****************************************************************************/
/**
 *****************************************************************************
 * @ingroup qatZstd
 *      QATzstd Session opaque data storage
 *
 * @description
 *      This structure contains a pointer to a structure with
 *    session state.
 *
 *****************************************************************************/
typedef struct ZSTD_QAT_Session_S {
    int instHint; /*which instance we last used*/
    unsigned char *qatIntermediateBuf;
    unsigned char reqPhyContMem; /* 1: QAT requires physically contiguous memory */
    CpaDcSessionSetupData sessionSetupData;
    unsigned int failOffloadCnt;
} ZSTD_QAT_Session_T;

/**
 *****************************************************************************
 * @ingroup qatZstd
 *
 * @description
 *
 *****************************************************************************/
typedef struct ZSTD_QAT_Instance_S {
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
} ZSTD_QAT_Instance_T;

/**
 *****************************************************************************
 * @ingroup qatZstd
 *
 * @description
 *
 *****************************************************************************/
typedef struct ZSTD_QAT_ProcessData_S {
    int qzstdInitStatus;
    CpaInstanceHandle *dcInstHandle;
    ZSTD_QAT_Instance_T *qzstdInst;
    Cpa16U numInstances;
    pthread_mutex_t mutex;
    unsigned int cctxNum;
} ZSTD_QAT_ProcessData_T;

/**
 *****************************************************************************
 * @ingroup qatZstd
 *
 * @description
 *
 *****************************************************************************/
typedef struct ZSTD_QAT_InstanceList_S {
    ZSTD_QAT_Instance_T instance;
    CpaInstanceHandle dcInstHandle;
    struct ZSTD_QAT_InstanceList_S *next;
} ZSTD_QAT_InstanceList_T;

/**
 *****************************************************************************
 * @ingroup qatZstd
 *
 * @description
 *
 *****************************************************************************/
typedef struct ZSTD_QAT_Hardware_S {
    ZSTD_QAT_InstanceList_T devices[ZSTD_QAT_MAX_DEVICES];
    unsigned int devNum;
    unsigned int maxDevId;
} ZSTD_QAT_Hardware_T;

/**
 *****************************************************************************
 *      Global variable
 *****************************************************************************/
extern ZSTD_QAT_ProcessData_T gProcess;

/**
 *****************************************************************************
 *      Functions
 *****************************************************************************/
extern CpaStatus icp_adf_get_numDevices(Cpa32U *);

size_t qatMatchfinder(
    void* externalMatchState, ZSTD_Sequence* outSeqs, size_t outSeqsCapacity,
    const void* src, size_t srcSize,
    const void* dict, size_t dictSize,
    int compressionLevel,
    size_t windowSize
);

int ZSTD_QAT_startQatDevice(ZSTD_CCtx *cctx);
void ZSTD_QAT_stopQatDevice(void);
void *ZSTD_QAT_createMatchState(void);
void ZSTD_QAT_freeMatchState(void *matchState);

#endif /* ZSTD_QAT_H */
