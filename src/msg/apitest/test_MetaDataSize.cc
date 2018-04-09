#include <cassert>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <queue>
#include <thread>
#include <sys/poll.h>
#include "msg/agent/mqa.h"
#include "msg/apitest/msgtest.h"
#include "msg/apitest/tet_mqsv.h"
#include <saMsg.h>
#include <saNtf.h>

static SaVersionT msg3_1 = {'B', 3, 0};

static void metaDataSize_01(void) {
  SaUint32T metaDataSize;
  SaAisErrorT rc(saMsgMetadataSizeGet(0xdeadbeef, &metaDataSize));
  aisrc_validate(rc, SA_AIS_ERR_BAD_HANDLE);
}

static void metaDataSize_02(void) {
  SaMsgHandleT msgHandle;
  SaAisErrorT rc = saMsgInitialize(&msgHandle, 0, &msg3_1);
  assert(rc == SA_AIS_OK);

  rc = saMsgFinalize(msgHandle);
  assert(rc == SA_AIS_OK);

  SaUint32T metaDataSize;
  rc = saMsgMetadataSizeGet(msgHandle, &metaDataSize);
  aisrc_validate(rc, SA_AIS_ERR_BAD_HANDLE);
}

static void metaDataSize_03(void) {
  SaMsgHandleT msgHandle;
  SaAisErrorT rc = saMsgInitialize(&msgHandle, 0, &msg3_1);
  assert(rc == SA_AIS_OK);

  rc = saMsgMetadataSizeGet(msgHandle, 0);
  aisrc_validate(rc, SA_AIS_ERR_INVALID_PARAM);

  rc = saMsgFinalize(msgHandle);
  assert(rc == SA_AIS_OK);
}

static void metaDataSize_04(void) {
  SaMsgHandleT msgHandle;
  SaVersionT msg1_1 = {'B', 1, 0};
  SaAisErrorT rc = saMsgInitialize(&msgHandle, 0, &msg1_1);
  assert(rc == SA_AIS_OK);

  SaUint32T metaDataSize;
  rc = saMsgMetadataSizeGet(msgHandle, &metaDataSize);
  aisrc_validate(rc, SA_AIS_ERR_VERSION);

  rc = saMsgFinalize(msgHandle);
  assert(rc == SA_AIS_OK);
}

static void metaDataSize_05(void) {
  SaMsgHandleT msgHandle;
  SaAisErrorT rc = saMsgInitialize(&msgHandle, 0, &msg3_1);
  assert(rc == SA_AIS_OK);

  SaUint32T metaDataSize;
  rc = saMsgMetadataSizeGet(msgHandle, &metaDataSize);
  if(rc == SA_AIS_OK){
      if (metaDataSize != sizeof(MQSV_MESSAGE) +
                         sizeof(NCS_OS_MQ_MSG_LL_HDR))
          rc = SA_AIS_ERR_MESSAGE_ERROR;
  }
  if (rc == SA_AIS_OK)
      rc = saMsgFinalize(msgHandle);
  aisrc_validate(rc, SA_AIS_OK);
}

__attribute__((constructor)) static void metaDataSize_constructor(void) {
  test_suite_add(28, "MetaData Size");
  test_case_add(28,
                metaDataSize_01,
                "saMsgMetadataSizeGet returns BAD_HANDLE when msgHandle is "
		"bad");
  test_case_add(28,
                metaDataSize_02,
                "saMsgMetadataSizeGet with finalized handle");
  test_case_add(28,
                metaDataSize_03,
                "saMsgMetadataSizeGet with null metaDataSize pointer");
  test_case_add(28,
                metaDataSize_04,
                "saMsgMetadataSizeGet with version != B.03.01");
  test_case_add(28,
                metaDataSize_05,
                "saMsgMetadataSizeGet success");
}
