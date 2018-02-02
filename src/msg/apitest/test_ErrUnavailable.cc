#include <cassert>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <fstream>
#include <string>
#include <poll.h>
#include <unistd.h>
#include "msg/apitest/msgtest.h"
#include "ais/include/saMsg.h"

static SaVersionT msg3_1 = { 'B', 3, 0 };

static const SaNameT queueName = {
  sizeof("safMq=TestQueue") - 1,
  "safMq=TestQueue"
};

static const SaNameT queueGroupName = {
  sizeof("safMqg=TestQueueGroup") - 1,
  "safMqg=TestQueueGroup"
};

static const SaMsgQueueCreationAttributesT creationAttributes = {
  0,
  { 5, 5, 5, 5 },
  SA_TIME_ONE_SECOND
};

static void queueOpenCallback(SaInvocationT invocation,
                              SaMsgQueueHandleT queueHandle,
                              SaAisErrorT error)
{
}

static std::string getClmNodeName(void)
{
  std::ifstream ifs("/etc/opensaf/node_name");
  std::string nodeName;

  ifs >> nodeName;

  return nodeName;
}

static void lockUnlockNode(bool lock)
{
  std::string command("immadm -o ");

  if (lock)
    command += '2';
  else
    command += '1';

  command += " safNode=";

  std::string nodeName(getClmNodeName());

  command += nodeName;
  command += ",safCluster=myClmCluster";

  int rc(system(command.c_str()));

  int status(WEXITSTATUS(rc));

  if (status != 0) {
    std::cerr << "unable to " << (lock ? "lock" : "unlock") << " local node"
              << std::endl;
    exit(EXIT_FAILURE);
  }

  sleep(1);
}

static void saErrUnavailable_01(void)
{
  lockUnlockNode(true);
  SaMsgHandleT msgHandle;
  SaAisErrorT rc = saMsgInitialize(&msgHandle, 0, &msg3_1);
  test_validate(rc, SA_AIS_ERR_UNAVAILABLE);
  lockUnlockNode(false);
}

static void saErrUnavailable_02(void)
{
  SaMsgHandleT msgHandle;
  SaAisErrorT rc = saMsgInitialize(&msgHandle, 0, &msg3_1);
  assert(rc == SA_AIS_OK);

  lockUnlockNode(true);
  SaSelectionObjectT selectionObject;
  rc = saMsgSelectionObjectGet(msgHandle, &selectionObject);
  test_validate(rc, SA_AIS_ERR_UNAVAILABLE);
  lockUnlockNode(false);

  rc = saMsgFinalize(msgHandle);
  assert(rc == SA_AIS_OK);
}

static void saErrUnavailable_03(void)
{
  SaMsgHandleT msgHandle;
  SaAisErrorT rc = saMsgInitialize(&msgHandle, 0, &msg3_1);
  assert(rc == SA_AIS_OK);

  lockUnlockNode(true);
  rc = saMsgDispatch(msgHandle, SA_DISPATCH_ONE);
  test_validate(rc, SA_AIS_ERR_UNAVAILABLE);
  lockUnlockNode(false);

  rc = saMsgFinalize(msgHandle);
  assert(rc == SA_AIS_OK);
}

static void saErrUnavailable_04(void)
{
  SaMsgHandleT msgHandle;
  SaAisErrorT rc = saMsgInitialize(&msgHandle, 0, &msg3_1);
  assert(rc == SA_AIS_OK);

  lockUnlockNode(true);
  rc = saMsgFinalize(msgHandle);
  test_validate(rc, SA_AIS_OK);
  lockUnlockNode(false);
}

static void saErrUnavailable_05(void)
{
  SaMsgHandleT msgHandle;
  SaAisErrorT rc = saMsgInitialize(&msgHandle, 0, &msg3_1);
  assert(rc == SA_AIS_OK);

  lockUnlockNode(true);
  SaMsgQueueHandleT queueHandle;
  rc = saMsgQueueOpen(msgHandle, &queueName, 0, 0, SA_TIME_END, &queueHandle);
  test_validate(rc, SA_AIS_ERR_UNAVAILABLE);
  lockUnlockNode(false);

  rc = saMsgFinalize(msgHandle);
  assert(rc == SA_AIS_OK);
}

static void saErrUnavailable_06(void)
{
  SaMsgHandleT msgHandle;
  SaMsgCallbacksT callbacks = {
    queueOpenCallback,
    0,
    0,
    0
  };

  SaAisErrorT rc = saMsgInitialize(&msgHandle, &callbacks, &msg3_1);
  assert(rc == SA_AIS_OK);

  lockUnlockNode(true);
  rc = saMsgQueueOpenAsync(msgHandle, 1, &queueName, 0, 0);
  test_validate(rc, SA_AIS_ERR_UNAVAILABLE);
  lockUnlockNode(false);

  rc = saMsgFinalize(msgHandle);
  assert(rc == SA_AIS_OK);
}

static void saErrUnavailable_07(void)
{
  SaMsgHandleT msgHandle;
  SaAisErrorT rc = saMsgInitialize(&msgHandle, 0, &msg3_1);
  assert(rc == SA_AIS_OK);

  SaMsgQueueHandleT queueHandle;
  rc = saMsgQueueOpen(msgHandle,
                      &queueName,
                      &creationAttributes,
                      SA_MSG_QUEUE_CREATE,
                      SA_TIME_END,
                      &queueHandle);
  assert(rc == SA_AIS_OK);

  lockUnlockNode(true);
  rc = saMsgQueueClose(queueHandle);
  test_validate(rc, SA_AIS_ERR_UNAVAILABLE);
  lockUnlockNode(false);

  rc = saMsgFinalize(msgHandle);
  assert(rc == SA_AIS_OK);
}

static void saErrUnavailable_08(void)
{
  SaMsgHandleT msgHandle;
  SaAisErrorT rc = saMsgInitialize(&msgHandle, 0, &msg3_1);
  assert(rc == SA_AIS_OK);

  lockUnlockNode(true);
  SaMsgQueueStatusT queueStatus;
  rc = saMsgQueueStatusGet(msgHandle, &queueName, &queueStatus);
  test_validate(rc, SA_AIS_ERR_UNAVAILABLE);
  lockUnlockNode(false);

  rc = saMsgFinalize(msgHandle);
  assert(rc == SA_AIS_OK);
}

static void saErrUnavailable_09(void)
{
  SaMsgHandleT msgHandle;
  SaAisErrorT rc = saMsgInitialize(&msgHandle, 0, &msg3_1);
  assert(rc == SA_AIS_OK);

  SaMsgQueueHandleT queueHandle;
  rc = saMsgQueueOpen(msgHandle,
                      &queueName,
                      &creationAttributes,
                      SA_MSG_QUEUE_CREATE,
                      SA_TIME_END,
                      &queueHandle);
  assert(rc == SA_AIS_OK);

  lockUnlockNode(true);
  SaTimeT rTime = SA_TIME_END;
  rc = saMsgQueueRetentionTimeSet(queueHandle, &rTime);
  test_validate(rc, SA_AIS_ERR_UNAVAILABLE);
  lockUnlockNode(false);

  rc = saMsgFinalize(msgHandle);
  assert(rc == SA_AIS_OK);
}

static void saErrUnavailable_10(void)
{
  SaMsgHandleT msgHandle;
  SaAisErrorT rc = saMsgInitialize(&msgHandle, 0, &msg3_1);
  assert(rc == SA_AIS_OK);

  SaMsgQueueHandleT queueHandle;
  rc = saMsgQueueOpen(msgHandle,
                      &queueName,
                      &creationAttributes,
                      SA_MSG_QUEUE_CREATE,
                      SA_TIME_END,
                      &queueHandle);
  assert(rc == SA_AIS_OK);

  lockUnlockNode(true);
  rc = saMsgQueueUnlink(msgHandle, &queueName);
  test_validate(rc, SA_AIS_ERR_UNAVAILABLE);
  lockUnlockNode(false);

  rc = saMsgFinalize(msgHandle);
  assert(rc == SA_AIS_OK);
}

static void saErrUnavailable_11(void)
{
  SaMsgHandleT msgHandle;
  SaAisErrorT rc = saMsgInitialize(&msgHandle, 0, &msg3_1);
  assert(rc == SA_AIS_OK);

  lockUnlockNode(true);
  rc = saMsgQueueGroupCreate(msgHandle, &queueGroupName, SA_MSG_QUEUE_GROUP_ROUND_ROBIN);
  test_validate(rc, SA_AIS_ERR_UNAVAILABLE);
  lockUnlockNode(false);

  rc = saMsgFinalize(msgHandle);
  assert(rc == SA_AIS_OK);
}

static void saErrUnavailable_12(void)
{
  SaMsgHandleT msgHandle;
  SaAisErrorT rc = saMsgInitialize(&msgHandle, 0, &msg3_1);
  assert(rc == SA_AIS_OK);

  lockUnlockNode(true);
  rc = saMsgQueueGroupDelete(msgHandle, &queueGroupName);
  test_validate(rc, SA_AIS_ERR_UNAVAILABLE);
  lockUnlockNode(false);

  rc = saMsgFinalize(msgHandle);
  assert(rc == SA_AIS_OK);
}

static void saErrUnavailable_13(void)
{
  SaMsgHandleT msgHandle;
  SaAisErrorT rc = saMsgInitialize(&msgHandle, 0, &msg3_1);
  assert(rc == SA_AIS_OK);

  lockUnlockNode(true);
  rc = saMsgQueueGroupInsert(msgHandle, &queueGroupName, &queueName);
  test_validate(rc, SA_AIS_ERR_UNAVAILABLE);
  lockUnlockNode(false);

  rc = saMsgFinalize(msgHandle);
  assert(rc == SA_AIS_OK);
}

static void saErrUnavailable_14(void)
{
  SaMsgHandleT msgHandle;
  SaAisErrorT rc = saMsgInitialize(&msgHandle, 0, &msg3_1);
  assert(rc == SA_AIS_OK);

  lockUnlockNode(true);
  rc = saMsgQueueGroupRemove(msgHandle, &queueGroupName, &queueName);
  test_validate(rc, SA_AIS_ERR_UNAVAILABLE);
  lockUnlockNode(false);

  rc = saMsgFinalize(msgHandle);
  assert(rc == SA_AIS_OK);
}

static void saErrUnavailable_15(void)
{
  SaMsgHandleT msgHandle;
  SaAisErrorT rc = saMsgInitialize(&msgHandle, 0, &msg3_1);
  assert(rc == SA_AIS_OK);

  lockUnlockNode(true);
  rc = saMsgQueueGroupTrack(msgHandle, &queueGroupName, SA_TRACK_CHANGES, 0);
  test_validate(rc, SA_AIS_ERR_UNAVAILABLE);
  lockUnlockNode(false);

  rc = saMsgFinalize(msgHandle);
  assert(rc == SA_AIS_OK);
}

static void saErrUnavailable_16(void)
{
  SaMsgHandleT msgHandle;
  SaAisErrorT rc = saMsgInitialize(&msgHandle, 0, &msg3_1);
  assert(rc == SA_AIS_OK);

  lockUnlockNode(true);
  rc = saMsgQueueGroupTrackStop(msgHandle, &queueGroupName);
  test_validate(rc, SA_AIS_ERR_UNAVAILABLE);
  lockUnlockNode(false);

  rc = saMsgFinalize(msgHandle);
  assert(rc == SA_AIS_OK);
}

static void saErrUnavailable_17(void)
{
  SaMsgHandleT msgHandle;
  SaAisErrorT rc = saMsgInitialize(&msgHandle, 0, &msg3_1);
  assert(rc == SA_AIS_OK);

  lockUnlockNode(true);
  rc = saMsgQueueGroupNotificationFree(msgHandle, 0);
  test_validate(rc, SA_AIS_ERR_UNAVAILABLE);
  lockUnlockNode(false);

  rc = saMsgFinalize(msgHandle);
  assert(rc == SA_AIS_OK);
}

static void saErrUnavailable_18(void)
{
  SaMsgHandleT msgHandle;
  SaAisErrorT rc = saMsgInitialize(&msgHandle, 0, &msg3_1);
  assert(rc == SA_AIS_OK);

  lockUnlockNode(true);
  SaMsgMessageT message;
  rc = saMsgMessageSend(msgHandle, &queueName, &message, SA_TIME_END);
  test_validate(rc, SA_AIS_ERR_UNAVAILABLE);
  lockUnlockNode(false);

  rc = saMsgFinalize(msgHandle);
  assert(rc == SA_AIS_OK);
}

static void saErrUnavailable_19(void)
{
  SaMsgHandleT msgHandle;
  SaAisErrorT rc = saMsgInitialize(&msgHandle, 0, &msg3_1);
  assert(rc == SA_AIS_OK);

  lockUnlockNode(true);
  SaMsgMessageT message;
  rc = saMsgMessageSendAsync(msgHandle, 1, &queueName, &message, 0);
  test_validate(rc, SA_AIS_ERR_UNAVAILABLE);
  lockUnlockNode(false);

  rc = saMsgFinalize(msgHandle);
  assert(rc == SA_AIS_OK);
}

static void saErrUnavailable_20(void)
{
  SaMsgHandleT msgHandle;
  SaAisErrorT rc = saMsgInitialize(&msgHandle, 0, &msg3_1);
  assert(rc == SA_AIS_OK);

  SaMsgQueueHandleT queueHandle;
  rc = saMsgQueueOpen(msgHandle,
                      &queueName,
                      &creationAttributes,
                      SA_MSG_QUEUE_CREATE,
                      SA_TIME_END,
                      &queueHandle);
  assert(rc == SA_AIS_OK);

  lockUnlockNode(true);
  SaMsgMessageT message;
  SaMsgSenderIdT senderId;

  rc = saMsgMessageGet(queueHandle, &message, 0, &senderId, 0);
  test_validate(rc, SA_AIS_ERR_UNAVAILABLE);
  lockUnlockNode(false);

  rc = saMsgFinalize(msgHandle);
  assert(rc == SA_AIS_OK);
}

static void saErrUnavailable_21(void)
{
  SaMsgHandleT msgHandle;
  SaAisErrorT rc = saMsgInitialize(&msgHandle, 0, &msg3_1);
  assert(rc == SA_AIS_OK);

  lockUnlockNode(true);
  rc = saMsgMessageDataFree(msgHandle, 0);
  test_validate(rc, SA_AIS_ERR_UNAVAILABLE);
  lockUnlockNode(false);

  rc = saMsgFinalize(msgHandle);
  assert(rc == SA_AIS_OK);
}

static void saErrUnavailable_22(void)
{
  SaMsgHandleT msgHandle;
  SaAisErrorT rc = saMsgInitialize(&msgHandle, 0, &msg3_1);
  assert(rc == SA_AIS_OK);

  SaMsgQueueHandleT queueHandle;
  rc = saMsgQueueOpen(msgHandle,
                      &queueName,
                      &creationAttributes,
                      SA_MSG_QUEUE_CREATE,
                      SA_TIME_END,
                      &queueHandle);
  assert(rc == SA_AIS_OK);

  lockUnlockNode(true);
  rc = saMsgMessageCancel(queueHandle);
  test_validate(rc, SA_AIS_ERR_UNAVAILABLE);
  lockUnlockNode(false);

  rc = saMsgFinalize(msgHandle);
  assert(rc == SA_AIS_OK);
}

static void saErrUnavailable_23(void)
{
  SaMsgHandleT msgHandle;
  SaAisErrorT rc = saMsgInitialize(&msgHandle, 0, &msg3_1);
  assert(rc == SA_AIS_OK);

  lockUnlockNode(true);
  SaMsgMessageT message = { 0 }, receiveMessage;
  rc = saMsgMessageSendReceive(msgHandle, &queueName, &message, &receiveMessage, 0, SA_TIME_MAX);
  test_validate(rc, SA_AIS_ERR_UNAVAILABLE);
  lockUnlockNode(false);

  rc = saMsgFinalize(msgHandle);
  assert(rc == SA_AIS_OK);
}

static void saErrUnavailable_24(void)
{
  SaMsgHandleT msgHandle;
  SaAisErrorT rc = saMsgInitialize(&msgHandle, 0, &msg3_1);
  assert(rc == SA_AIS_OK);

  lockUnlockNode(true);
  SaMsgMessageT message;
  SaMsgSenderIdT senderId;
  rc = saMsgMessageReply(msgHandle, &message, &senderId, 0);
  test_validate(rc, SA_AIS_ERR_UNAVAILABLE);
  lockUnlockNode(false);

  rc = saMsgFinalize(msgHandle);
  assert(rc == SA_AIS_OK);
}

static void saErrUnavailable_25(void)
{
  SaMsgHandleT msgHandle;
  SaAisErrorT rc = saMsgInitialize(&msgHandle, 0, &msg3_1);
  assert(rc == SA_AIS_OK);

  lockUnlockNode(true);
  SaMsgMessageT message;
  SaMsgSenderIdT senderId;
  rc = saMsgMessageReplyAsync(msgHandle, 1, &message, &senderId, 0);
  test_validate(rc, SA_AIS_ERR_UNAVAILABLE);
  lockUnlockNode(false);

  rc = saMsgFinalize(msgHandle);
  assert(rc == SA_AIS_OK);
}

static void saErrUnavailable_26(void)
{
  SaMsgHandleT msgHandle;
  SaAisErrorT rc = saMsgInitialize(&msgHandle, 0, &msg3_1);
  assert(rc == SA_AIS_OK);

  SaMsgQueueHandleT queueHandle;
  rc = saMsgQueueOpen(msgHandle,
                      &queueName,
                      &creationAttributes,
                      SA_MSG_QUEUE_CREATE,
                      SA_TIME_END,
                      &queueHandle);
  assert(rc == SA_AIS_OK);

  lockUnlockNode(true);
  const SaMsgQueueThresholdsT thresholds = {
	{ 5, 5, 5, 5 },
	{ 5, 5, 5, 5 }
  };
  rc = saMsgQueueCapacityThresholdsSet(queueHandle, &thresholds);
  test_validate(rc, SA_AIS_ERR_UNAVAILABLE);
  lockUnlockNode(false);

  rc = saMsgFinalize(msgHandle);
  assert(rc == SA_AIS_OK);
}

static void saErrUnavailable_27(void)
{
  SaMsgHandleT msgHandle;
  SaAisErrorT rc = saMsgInitialize(&msgHandle, 0, &msg3_1);
  assert(rc == SA_AIS_OK);

  SaMsgQueueHandleT queueHandle;
  rc = saMsgQueueOpen(msgHandle,
                      &queueName,
                      &creationAttributes,
                      SA_MSG_QUEUE_CREATE,
                      SA_TIME_END,
                      &queueHandle);
  assert(rc == SA_AIS_OK);

  lockUnlockNode(true);
  SaMsgQueueThresholdsT thresholds;
  rc = saMsgQueueCapacityThresholdsGet(queueHandle, &thresholds);
  test_validate(rc, SA_AIS_ERR_UNAVAILABLE);
  lockUnlockNode(false);

  rc = saMsgFinalize(msgHandle);
  assert(rc == SA_AIS_OK);
}

static void saErrUnavailable_28(void)
{
  SaMsgHandleT msgHandle;
  SaAisErrorT rc = saMsgInitialize(&msgHandle, 0, &msg3_1);
  assert(rc == SA_AIS_OK);

  lockUnlockNode(true);
  SaUint32T metaDataSize(0);
  rc = saMsgMetadataSizeGet(msgHandle, &metaDataSize);
  test_validate(rc, SA_AIS_ERR_UNAVAILABLE);
  lockUnlockNode(false);

  rc = saMsgFinalize(msgHandle);
  assert(rc == SA_AIS_OK);
}

static void saErrUnavailable_29(void)
{
  SaMsgHandleT msgHandle;
  SaAisErrorT rc = saMsgInitialize(&msgHandle, 0, &msg3_1);
  assert(rc == SA_AIS_OK);

  lockUnlockNode(true);
  SaLimitValueT value;
  rc = saMsgLimitGet(msgHandle, SA_MSG_MAX_PRIORITY_AREA_SIZE_ID, &value);
  test_validate(rc, SA_AIS_ERR_UNAVAILABLE);
  lockUnlockNode(false);

  rc = saMsgFinalize(msgHandle);
  assert(rc == SA_AIS_OK);
}

static void saErrUnavailable_30(void)
{
  lockUnlockNode(true);
  SaMsgHandleT msgHandle;
  SaAisErrorT rc = saMsgInitialize(&msgHandle, 0, &msg3_1);
  assert(rc == SA_AIS_ERR_UNAVAILABLE);
  lockUnlockNode(false);
  rc = saMsgInitialize(&msgHandle, 0, &msg3_1);
  test_validate(rc, SA_AIS_OK);
  rc = saMsgFinalize(msgHandle);
  assert(rc == SA_AIS_OK);
}

static void saErrUnavailable_31(void)
{
  SaMsgHandleT msgHandle;
  SaAisErrorT rc = saMsgInitialize(&msgHandle, 0, &msg3_1);
  assert(rc == SA_AIS_OK);

  lockUnlockNode(true);
  lockUnlockNode(false);
  SaSelectionObjectT selectionObject;
  rc = saMsgSelectionObjectGet(msgHandle, &selectionObject);
  test_validate(rc, SA_AIS_ERR_UNAVAILABLE);

  rc = saMsgFinalize(msgHandle);
  assert(rc == SA_AIS_OK);
}

static void saErrUnavailable_32(void)
{
  SaMsgHandleT msgHandle;
  SaAisErrorT rc = saMsgInitialize(&msgHandle, 0, &msg3_1);
  assert(rc == SA_AIS_OK);

  lockUnlockNode(true);
  lockUnlockNode(false);
  rc = saMsgDispatch(msgHandle, SA_DISPATCH_ONE);
  test_validate(rc, SA_AIS_ERR_UNAVAILABLE);

  rc = saMsgFinalize(msgHandle);
  assert(rc == SA_AIS_OK);
}

static void saErrUnavailable_33(void)
{
  SaMsgHandleT msgHandle;
  SaAisErrorT rc = saMsgInitialize(&msgHandle, 0, &msg3_1);
  assert(rc == SA_AIS_OK);

  lockUnlockNode(true);
  lockUnlockNode(false);
  rc = saMsgFinalize(msgHandle);
  test_validate(rc, SA_AIS_OK);
}

static void saErrUnavailable_34(void)
{
  SaMsgHandleT msgHandle;
  SaAisErrorT rc = saMsgInitialize(&msgHandle, 0, &msg3_1);
  assert(rc == SA_AIS_OK);

  lockUnlockNode(true);
  lockUnlockNode(false);
  SaMsgQueueHandleT queueHandle;
  rc = saMsgQueueOpen(msgHandle, &queueName, 0, 0, SA_TIME_END, &queueHandle);
  test_validate(rc, SA_AIS_ERR_UNAVAILABLE);

  rc = saMsgFinalize(msgHandle);
  assert(rc == SA_AIS_OK);
}

static void saErrUnavailable_35(void)
{
  SaMsgHandleT msgHandle;
  SaMsgCallbacksT callbacks = {
    queueOpenCallback,
    0,
    0,
    0
  };

  SaAisErrorT rc = saMsgInitialize(&msgHandle, &callbacks, &msg3_1);
  assert(rc == SA_AIS_OK);

  lockUnlockNode(true);
  lockUnlockNode(false);
  rc = saMsgQueueOpenAsync(msgHandle, 1, &queueName, 0, 0);
  test_validate(rc, SA_AIS_ERR_UNAVAILABLE);

  rc = saMsgFinalize(msgHandle);
  assert(rc == SA_AIS_OK);
}

static void saErrUnavailable_36(void)
{
  SaMsgHandleT msgHandle;
  SaAisErrorT rc = saMsgInitialize(&msgHandle, 0, &msg3_1);
  assert(rc == SA_AIS_OK);

  SaMsgQueueHandleT queueHandle;
  rc = saMsgQueueOpen(msgHandle,
                      &queueName,
                      &creationAttributes,
                      SA_MSG_QUEUE_CREATE,
                      SA_TIME_END,
                      &queueHandle);
  assert(rc == SA_AIS_OK);

  lockUnlockNode(true);
  lockUnlockNode(false);
  rc = saMsgQueueClose(queueHandle);
  test_validate(rc, SA_AIS_ERR_UNAVAILABLE);

  rc = saMsgFinalize(msgHandle);
  assert(rc == SA_AIS_OK);
}

static void saErrUnavailable_37(void)
{
  SaMsgHandleT msgHandle;
  SaAisErrorT rc = saMsgInitialize(&msgHandle, 0, &msg3_1);
  assert(rc == SA_AIS_OK);

  lockUnlockNode(true);
  lockUnlockNode(false);
  SaMsgQueueStatusT queueStatus;
  rc = saMsgQueueStatusGet(msgHandle, &queueName, &queueStatus);
  test_validate(rc, SA_AIS_ERR_UNAVAILABLE);

  rc = saMsgFinalize(msgHandle);
  assert(rc == SA_AIS_OK);
}

static void saErrUnavailable_38(void)
{
  SaMsgHandleT msgHandle;
  SaAisErrorT rc = saMsgInitialize(&msgHandle, 0, &msg3_1);
  assert(rc == SA_AIS_OK);

  SaMsgQueueHandleT queueHandle;
  rc = saMsgQueueOpen(msgHandle,
                      &queueName,
                      &creationAttributes,
                      SA_MSG_QUEUE_CREATE,
                      SA_TIME_END,
                      &queueHandle);
  assert(rc == SA_AIS_OK);

  lockUnlockNode(true);
  lockUnlockNode(false);
  SaTimeT rTime = SA_TIME_END;
  rc = saMsgQueueRetentionTimeSet(queueHandle, &rTime);
  test_validate(rc, SA_AIS_ERR_UNAVAILABLE);

  rc = saMsgFinalize(msgHandle);
  assert(rc == SA_AIS_OK);
}

static void saErrUnavailable_39(void)
{
  SaMsgHandleT msgHandle;
  SaAisErrorT rc = saMsgInitialize(&msgHandle, 0, &msg3_1);
  assert(rc == SA_AIS_OK);

  SaMsgQueueHandleT queueHandle;
  rc = saMsgQueueOpen(msgHandle,
                      &queueName,
                      &creationAttributes,
                      SA_MSG_QUEUE_CREATE,
                      SA_TIME_END,
                      &queueHandle);
  assert(rc == SA_AIS_OK);

  lockUnlockNode(true);
  lockUnlockNode(false);
  rc = saMsgQueueUnlink(msgHandle, &queueName);
  test_validate(rc, SA_AIS_ERR_UNAVAILABLE);

  rc = saMsgFinalize(msgHandle);
  assert(rc == SA_AIS_OK);
}

static void saErrUnavailable_40(void)
{
  SaMsgHandleT msgHandle;
  SaAisErrorT rc = saMsgInitialize(&msgHandle, 0, &msg3_1);
  assert(rc == SA_AIS_OK);

  lockUnlockNode(true);
  lockUnlockNode(false);
  rc = saMsgQueueGroupCreate(msgHandle, &queueGroupName, SA_MSG_QUEUE_GROUP_ROUND_ROBIN);
  test_validate(rc, SA_AIS_ERR_UNAVAILABLE);

  rc = saMsgFinalize(msgHandle);
  assert(rc == SA_AIS_OK);
}

static void saErrUnavailable_41(void)
{
  SaMsgHandleT msgHandle;
  SaAisErrorT rc = saMsgInitialize(&msgHandle, 0, &msg3_1);
  assert(rc == SA_AIS_OK);

  lockUnlockNode(true);
  lockUnlockNode(false);
  rc = saMsgQueueGroupDelete(msgHandle, &queueGroupName);
  test_validate(rc, SA_AIS_ERR_UNAVAILABLE);

  rc = saMsgFinalize(msgHandle);
  assert(rc == SA_AIS_OK);
}

static void saErrUnavailable_42(void)
{
  SaMsgHandleT msgHandle;
  SaAisErrorT rc = saMsgInitialize(&msgHandle, 0, &msg3_1);
  assert(rc == SA_AIS_OK);

  lockUnlockNode(true);
  lockUnlockNode(false);
  rc = saMsgQueueGroupInsert(msgHandle, &queueGroupName, &queueName);
  test_validate(rc, SA_AIS_ERR_UNAVAILABLE);

  rc = saMsgFinalize(msgHandle);
  assert(rc == SA_AIS_OK);
}

static void saErrUnavailable_43(void)
{
  SaMsgHandleT msgHandle;
  SaAisErrorT rc = saMsgInitialize(&msgHandle, 0, &msg3_1);
  assert(rc == SA_AIS_OK);

  lockUnlockNode(true);
  lockUnlockNode(false);
  rc = saMsgQueueGroupRemove(msgHandle, &queueGroupName, &queueName);
  test_validate(rc, SA_AIS_ERR_UNAVAILABLE);

  rc = saMsgFinalize(msgHandle);
  assert(rc == SA_AIS_OK);
}

static void saErrUnavailable_44(void)
{
  SaMsgHandleT msgHandle;
  SaAisErrorT rc = saMsgInitialize(&msgHandle, 0, &msg3_1);
  assert(rc == SA_AIS_OK);

  lockUnlockNode(true);
  lockUnlockNode(false);
  rc = saMsgQueueGroupTrack(msgHandle, &queueGroupName, SA_TRACK_CHANGES, 0);
  test_validate(rc, SA_AIS_ERR_UNAVAILABLE);

  rc = saMsgFinalize(msgHandle);
  assert(rc == SA_AIS_OK);
}

static void saErrUnavailable_45(void)
{
  SaMsgHandleT msgHandle;
  SaAisErrorT rc = saMsgInitialize(&msgHandle, 0, &msg3_1);
  assert(rc == SA_AIS_OK);

  lockUnlockNode(true);
  lockUnlockNode(false);
  rc = saMsgQueueGroupTrackStop(msgHandle, &queueGroupName);
  test_validate(rc, SA_AIS_ERR_UNAVAILABLE);

  rc = saMsgFinalize(msgHandle);
  assert(rc == SA_AIS_OK);
}

static void saErrUnavailable_46(void)
{
  SaMsgHandleT msgHandle;
  SaAisErrorT rc = saMsgInitialize(&msgHandle, 0, &msg3_1);
  assert(rc == SA_AIS_OK);

  lockUnlockNode(true);
  lockUnlockNode(false);
  rc = saMsgQueueGroupNotificationFree(msgHandle, 0);
  test_validate(rc, SA_AIS_ERR_UNAVAILABLE);

  rc = saMsgFinalize(msgHandle);
  assert(rc == SA_AIS_OK);
}

static void saErrUnavailable_47(void)
{
  SaMsgHandleT msgHandle;
  SaAisErrorT rc = saMsgInitialize(&msgHandle, 0, &msg3_1);
  assert(rc == SA_AIS_OK);

  lockUnlockNode(true);
  lockUnlockNode(false);
  SaMsgMessageT message;
  rc = saMsgMessageSend(msgHandle, &queueName, &message, SA_TIME_END);
  test_validate(rc, SA_AIS_ERR_UNAVAILABLE);

  rc = saMsgFinalize(msgHandle);
  assert(rc == SA_AIS_OK);
}

static void saErrUnavailable_48(void)
{
  SaMsgHandleT msgHandle;
  SaAisErrorT rc = saMsgInitialize(&msgHandle, 0, &msg3_1);
  assert(rc == SA_AIS_OK);

  lockUnlockNode(true);
  lockUnlockNode(false);
  SaMsgMessageT message;
  rc = saMsgMessageSendAsync(msgHandle, 1, &queueName, &message, 0);
  test_validate(rc, SA_AIS_ERR_UNAVAILABLE);

  rc = saMsgFinalize(msgHandle);
  assert(rc == SA_AIS_OK);
}

static void saErrUnavailable_49(void)
{
  SaMsgHandleT msgHandle;
  SaAisErrorT rc = saMsgInitialize(&msgHandle, 0, &msg3_1);
  assert(rc == SA_AIS_OK);

  SaMsgQueueHandleT queueHandle;
  rc = saMsgQueueOpen(msgHandle,
                      &queueName,
                      &creationAttributes,
                      SA_MSG_QUEUE_CREATE,
                      SA_TIME_END,
                      &queueHandle);
  assert(rc == SA_AIS_OK);

  lockUnlockNode(true);
  lockUnlockNode(false);
  SaMsgMessageT message;
  SaMsgSenderIdT senderId;

  rc = saMsgMessageGet(queueHandle, &message, 0, &senderId, 0);
  test_validate(rc, SA_AIS_ERR_UNAVAILABLE);

  rc = saMsgFinalize(msgHandle);
  assert(rc == SA_AIS_OK);
}

static void saErrUnavailable_50(void)
{
  SaMsgHandleT msgHandle;
  SaAisErrorT rc = saMsgInitialize(&msgHandle, 0, &msg3_1);
  assert(rc == SA_AIS_OK);

  lockUnlockNode(true);
  lockUnlockNode(false);
  rc = saMsgMessageDataFree(msgHandle, 0);
  test_validate(rc, SA_AIS_ERR_UNAVAILABLE);

  rc = saMsgFinalize(msgHandle);
  assert(rc == SA_AIS_OK);
}

static void saErrUnavailable_51(void)
{
  SaMsgHandleT msgHandle;
  SaAisErrorT rc = saMsgInitialize(&msgHandle, 0, &msg3_1);
  assert(rc == SA_AIS_OK);

  SaMsgQueueHandleT queueHandle;
  rc = saMsgQueueOpen(msgHandle,
                      &queueName,
                      &creationAttributes,
                      SA_MSG_QUEUE_CREATE,
                      SA_TIME_END,
                      &queueHandle);
  assert(rc == SA_AIS_OK);

  lockUnlockNode(true);
  lockUnlockNode(false);
  rc = saMsgMessageCancel(queueHandle);
  test_validate(rc, SA_AIS_ERR_UNAVAILABLE);

  rc = saMsgFinalize(msgHandle);
  assert(rc == SA_AIS_OK);
}

static void saErrUnavailable_52(void)
{
  SaMsgHandleT msgHandle;
  SaAisErrorT rc = saMsgInitialize(&msgHandle, 0, &msg3_1);
  assert(rc == SA_AIS_OK);

  lockUnlockNode(true);
  lockUnlockNode(false);
  SaMsgMessageT message = { 0 }, receiveMessage;
  rc = saMsgMessageSendReceive(msgHandle, &queueName, &message, &receiveMessage, 0, SA_TIME_MAX);
  test_validate(rc, SA_AIS_ERR_UNAVAILABLE);

  rc = saMsgFinalize(msgHandle);
  assert(rc == SA_AIS_OK);
}

static void saErrUnavailable_53(void)
{
  SaMsgHandleT msgHandle;
  SaAisErrorT rc = saMsgInitialize(&msgHandle, 0, &msg3_1);
  assert(rc == SA_AIS_OK);

  lockUnlockNode(true);
  lockUnlockNode(false);
  SaMsgMessageT message;
  SaMsgSenderIdT senderId;
  rc = saMsgMessageReply(msgHandle, &message, &senderId, 0);
  test_validate(rc, SA_AIS_ERR_UNAVAILABLE);

  rc = saMsgFinalize(msgHandle);
  assert(rc == SA_AIS_OK);
}

static void saErrUnavailable_54(void)
{
  SaMsgHandleT msgHandle;
  SaAisErrorT rc = saMsgInitialize(&msgHandle, 0, &msg3_1);
  assert(rc == SA_AIS_OK);

  lockUnlockNode(true);
  lockUnlockNode(false);
  SaMsgMessageT message;
  SaMsgSenderIdT senderId;
  rc = saMsgMessageReplyAsync(msgHandle, 1, &message, &senderId, 0);
  test_validate(rc, SA_AIS_ERR_UNAVAILABLE);

  rc = saMsgFinalize(msgHandle);
  assert(rc == SA_AIS_OK);
}

static void saErrUnavailable_55(void)
{
  SaMsgHandleT msgHandle;
  SaAisErrorT rc = saMsgInitialize(&msgHandle, 0, &msg3_1);
  assert(rc == SA_AIS_OK);

  SaMsgQueueHandleT queueHandle;
  rc = saMsgQueueOpen(msgHandle,
                      &queueName,
                      &creationAttributes,
                      SA_MSG_QUEUE_CREATE,
                      SA_TIME_END,
                      &queueHandle);
  assert(rc == SA_AIS_OK);

  lockUnlockNode(true);
  lockUnlockNode(false);
  const SaMsgQueueThresholdsT thresholds = {
	{ 5, 5, 5, 5 },
	{ 5, 5, 5, 5 }
  };
  rc = saMsgQueueCapacityThresholdsSet(queueHandle, &thresholds);
  test_validate(rc, SA_AIS_ERR_UNAVAILABLE);

  rc = saMsgFinalize(msgHandle);
  assert(rc == SA_AIS_OK);
}

static void saErrUnavailable_56(void)
{
  SaMsgHandleT msgHandle;
  SaAisErrorT rc = saMsgInitialize(&msgHandle, 0, &msg3_1);
  assert(rc == SA_AIS_OK);

  SaMsgQueueHandleT queueHandle;
  rc = saMsgQueueOpen(msgHandle,
                      &queueName,
                      &creationAttributes,
                      SA_MSG_QUEUE_CREATE,
                      SA_TIME_END,
                      &queueHandle);
  assert(rc == SA_AIS_OK);

  lockUnlockNode(true);
  lockUnlockNode(false);
  SaMsgQueueThresholdsT thresholds;
  rc = saMsgQueueCapacityThresholdsGet(queueHandle, &thresholds);
  test_validate(rc, SA_AIS_ERR_UNAVAILABLE);

  rc = saMsgFinalize(msgHandle);
  assert(rc == SA_AIS_OK);
}

static void saErrUnavailable_57(void)
{
  SaMsgHandleT msgHandle;
  SaAisErrorT rc = saMsgInitialize(&msgHandle, 0, &msg3_1);
  assert(rc == SA_AIS_OK);

  lockUnlockNode(true);
  lockUnlockNode(false);
  SaUint32T metaDataSize(0);
  rc = saMsgMetadataSizeGet(msgHandle, &metaDataSize);
  test_validate(rc, SA_AIS_ERR_UNAVAILABLE);

  rc = saMsgFinalize(msgHandle);
  assert(rc == SA_AIS_OK);
}

static void saErrUnavailable_58(void)
{
  SaMsgHandleT msgHandle;
  SaAisErrorT rc = saMsgInitialize(&msgHandle, 0, &msg3_1);
  assert(rc == SA_AIS_OK);

  lockUnlockNode(true);
  lockUnlockNode(false);
  SaLimitValueT value;
  rc = saMsgLimitGet(msgHandle, SA_MSG_MAX_PRIORITY_AREA_SIZE_ID, &value);
  test_validate(rc, SA_AIS_ERR_UNAVAILABLE);

  rc = saMsgFinalize(msgHandle);
  assert(rc == SA_AIS_OK);
}


__attribute__((constructor)) static void saErrUnavailable_constructor(void)
{
  test_suite_add(26, "SA_AIS_ERR_UNAVAILABLE Test Suite");
  test_case_add(26, saErrUnavailable_01, "saMsgInitialize");
  test_case_add(26, saErrUnavailable_02, "saMsgSelectionObjectGet");
  test_case_add(26, saErrUnavailable_03, "saMsgDispatch");
  test_case_add(26, saErrUnavailable_04, "saMsgFinalize");
  test_case_add(26, saErrUnavailable_05, "saMsgQueueOpen");
  test_case_add(26, saErrUnavailable_06, "saMsgQueueOpenAsync");
  test_case_add(26, saErrUnavailable_07, "saMsgQueueClose");
  test_case_add(26, saErrUnavailable_08, "saMsgQueueStatusGet");
  test_case_add(26, saErrUnavailable_09, "saMsgQueueRetentionTimeSet");
  test_case_add(26, saErrUnavailable_10, "saMsgQueueUnlink");
  test_case_add(26, saErrUnavailable_11, "saMsgQueueGroupCreate");
  test_case_add(26, saErrUnavailable_12, "saMsgQueueGroupDelete");
  test_case_add(26, saErrUnavailable_13, "saMsgQueueGroupInsert");
  test_case_add(26, saErrUnavailable_14, "saMsgQueueGroupRemove");
  test_case_add(26, saErrUnavailable_15, "saMsgQueueGroupTrack");
  test_case_add(26, saErrUnavailable_16, "saMsgQueueGroupTrackStop");
  test_case_add(26, saErrUnavailable_17, "saMsgQueueGroupNotificationFree");
  test_case_add(26, saErrUnavailable_18, "saMsgMessageSend");
  test_case_add(26, saErrUnavailable_19, "saMsgMessageSendAsync");
  test_case_add(26, saErrUnavailable_20, "saMsgMessageGet");
  test_case_add(26, saErrUnavailable_21, "saMsgMessageDataFree");
  test_case_add(26, saErrUnavailable_22, "saMsgMessageCancel");
  test_case_add(26, saErrUnavailable_23, "saMsgMessageSendReceive");
  test_case_add(26, saErrUnavailable_24, "saMsgMessageReply");
  test_case_add(26, saErrUnavailable_25, "saMsgMessageReplyAsync");
  test_case_add(26, saErrUnavailable_26, "saMsgQueueCapacityThresholdsSet");
  test_case_add(26, saErrUnavailable_27, "saMsgQueueCapacityThresholdsGet");
  test_case_add(26, saErrUnavailable_28, "saMsgMetadataSizeGet");
  test_case_add(26, saErrUnavailable_29, "saMsgLimitGet");
  test_case_add(26, saErrUnavailable_30, "saMsgInitialize (stale)");
  test_case_add(26, saErrUnavailable_31, "saMsgSelectionObjectGet (stale)");
  test_case_add(26, saErrUnavailable_32, "saMsgDispatch (stale)");
  test_case_add(26, saErrUnavailable_33, "saMsgFinalize (stale)");
  test_case_add(26, saErrUnavailable_34, "saMsgQueueOpen (stale)");
  test_case_add(26, saErrUnavailable_35, "saMsgQueueOpenAsync (stale)");
  test_case_add(26, saErrUnavailable_36, "saMsgQueueClose (stale)");
  test_case_add(26, saErrUnavailable_37, "saMsgQueueStatusGet (stale)");
  test_case_add(26, saErrUnavailable_38, "saMsgQueueRetentionTimeSet (stale)");
  test_case_add(26, saErrUnavailable_39, "saMsgQueueUnlink (stale)");
  test_case_add(26, saErrUnavailable_40, "saMsgQueueGroupCreate (stale)");
  test_case_add(26, saErrUnavailable_41, "saMsgQueueGroupDelete (stale)");
  test_case_add(26, saErrUnavailable_42, "saMsgQueueGroupInsert (stale)");
  test_case_add(26, saErrUnavailable_43, "saMsgQueueGroupRemove (stale)");
  test_case_add(26, saErrUnavailable_44, "saMsgQueueGroupTrack (stale)");
  test_case_add(26, saErrUnavailable_45, "saMsgQueueGroupTrackStop (stale)");
  test_case_add(26, saErrUnavailable_46, "saMsgQueueGroupNotificationFree (stale)");
  test_case_add(26, saErrUnavailable_47, "saMsgMessageSend (stale)");
  test_case_add(26, saErrUnavailable_48, "saMsgMessageSendAsync (stale)");
  test_case_add(26, saErrUnavailable_49, "saMsgMessageGet (stale)");
  test_case_add(26, saErrUnavailable_50, "saMsgMessageDataFree (stale)");
  test_case_add(26, saErrUnavailable_51, "saMsgMessageCancel (stale)");
  test_case_add(26, saErrUnavailable_52, "saMsgMessageSendReceive (stale)");
  test_case_add(26, saErrUnavailable_53, "saMsgMessageReply (stale)");
  test_case_add(26, saErrUnavailable_54, "saMsgMessageReplyAsync (stale)");
  test_case_add(26, saErrUnavailable_55, "saMsgQueueCapacityThresholdsSet (stale)");
  test_case_add(26, saErrUnavailable_56, "saMsgQueueCapacityThresholdsGet (stale)");
  test_case_add(26, saErrUnavailable_57, "saMsgMetadataSizeGet (stale)");
  test_case_add(26, saErrUnavailable_58, "saMsgLimitGet (stale)");
}
