#include <cassert>
#include <cstdio>
#include <cstring>
#include <poll.h>
#include "msg/apitest/msgtest.h"
#include <saMsg.h>

static SaVersionT msg1_1 = {'B', 1, 0};

static void saMsgVersion_01(void) {
  SaMsgHandleT msgHandle;
  SaAisErrorT rc = saMsgInitialize(&msgHandle, 0, &msg1_1);
  assert(rc == SA_AIS_OK);

  const SaNameT queueGroupName = {sizeof("safMqg=messageQGroup") - 1,
                                  "safMqg=messageQGroup"};

  rc = saMsgQueueGroupCreate(msgHandle, &queueGroupName,
                             SA_MSG_QUEUE_GROUP_ROUND_ROBIN);
  assert(rc == SA_AIS_OK);

  SaMsgQueueGroupNotificationT notification;
  SaMsgQueueGroupNotificationBufferT notificationBuffer = {
      3, &notification, SA_MSG_QUEUE_GROUP_ROUND_ROBIN};

  rc = saMsgQueueGroupTrack(msgHandle, &queueGroupName, SA_TRACK_CURRENT,
                            &notificationBuffer);
  aisrc_validate(rc, SA_AIS_ERR_INIT);

  rc = saMsgQueueGroupDelete(msgHandle, &queueGroupName);
  assert(rc == SA_AIS_OK);

  rc = saMsgFinalize(msgHandle);
  assert(rc == SA_AIS_OK);
}

__attribute__((constructor)) static void saMsgVersion_constructor(void) {
  test_suite_add(23, "SaVersionT Test Suite");
  test_case_add(23, saMsgVersion_01,
                "B.01.01 saMsgQueueGroupTrack returns SA_AIS_ERR_INIT when "
                "buffer is non-NULL");
}
