#include "lcktest.h"
#include "tet_glsv.h"
#include "tet_gla_conf.h"
#include "base/ncs_main_papi.h"

int gl_glsv_inst_num;
unsigned gl_nodeId;
int gl_glsv_async;
int gl_tetlist_index;
int gl_wt_clbk_iter;
int gl_open_clbk_iter;
int gl_gr_clbk_iter;
int gl_ddlck_flag;
int gl_unlck_res = TET_PASS;
int gl_lck_res = TET_PASS;
int gl_lck_red_flg;
int gl_glsv_wait_time;
int gl_red_node;

int TET_GLSV_NODE1;
int TET_GLSV_NODE2;
int TET_GLSV_NODE3;
GLA_TEST_ENV gl_gla_env;

const char *saf_lck_status_string[] = {
    "SA_LCK_INVALID_STATUS",   "SA_LCK_LOCK_GRANTED",  "SA_LCK_LOCK_DEADLOCK",
    "SA_LCK_LOCK_NOT_QUEUED",  "SA_LCK_LOCK_ORPHANED", "SA_LCK_LOCK_NO_MORE",
    "SA_LCK_LOCK_DUPLICATE_EX"};

const char *saf_lck_mode_string[] = {
    "SA_LCK_INVALID_MODE", "SA_LCK_PR_LOCK_MODE", "SA_LCK_EX_LOCK_MODE"};

const char *saf_lck_flags_string[] = {"ZERO", "SA_LCK_LOCK_NO_QUEUE",
				      "SA_LCK_LOCK_ORPHAN"};

const char *glsv_saf_error_string[] = {
    "SA_AIS_NOT_VALID",
    "SA_AIS_OK",
    "SA_AIS_ERR_LIBRARY",
    "SA_AIS_ERR_VERSION",
    "SA_AIS_ERR_INIT",
    "SA_AIS_ERR_TIMEOUT",
    "SA_AIS_ERR_TRY_AGAIN",
    "SA_AIS_ERR_INVALID_PARAM",
    "SA_AIS_ERR_NO_MEMORY",
    "SA_AIS_ERR_BAD_HANDLE",
    "SA_AIS_ERR_BUSY",
    "SA_AIS_ERR_ACCESS",
    "SA_AIS_ERR_NOT_EXIST",
    "SA_AIS_ERR_NAME_TOO_LONG",
    "SA_AIS_ERR_EXIST",
    "SA_AIS_ERR_NO_SPACE",
    "SA_AIS_ERR_INTERRUPT",
    "SA_AIS_ERR_NAME_NOT_FOUND",
    "SA_AIS_ERR_NO_RESOURCES",
    "SA_AIS_ERR_NOT_SUPPORTED",
    "SA_AIS_ERR_BAD_OPERATION",
    "SA_AIS_ERR_FAILED_OPERATION",
    "SA_AIS_ERR_MESSAGE_ERROR",
    "SA_AIS_ERR_QUEUE_FULL",
    "SA_AIS_ERR_QUEUE_NOT_AVAILABLE",
    "SA_AIS_ERR_BAD_FLAGS",
    "SA_AIS_ERR_TOO_BIG",
    "SA_AIS_ERR_NO_SECTIONS",
};

/* *********** GLSV Callback Functions ************* */

void App_ResourceOpenCallback(SaInvocationT invocation,
			      SaLckResourceHandleT res_hdl, SaAisErrorT error)
{
	gl_open_clbk_iter++;
	gl_gla_env.open_clbk_invo = invocation;
	gl_gla_env.open_clbk_err = error;

	if (error == SA_AIS_OK) {
		gl_gla_env.open_clbk_res_hdl = res_hdl;
	}
}

void App_LockGrantCallback(SaInvocationT invocation,
			   SaLckLockStatusT lockStatus, SaAisErrorT error)
{
	gl_gr_clbk_iter++;
	gl_gla_env.gr_clbk_invo = invocation;
	gl_gla_env.gr_clbk_err = error;
	gl_gla_env.gr_clbk_status = lockStatus;

	if (lockStatus == SA_LCK_LOCK_DEADLOCK)
		gl_ddlck_flag = 100;
}

void App_LockGrantCallback_withunlock_lock(SaInvocationT invocation,
					   SaLckLockStatusT lockStatus,
					   SaAisErrorT error)
{
	gl_gr_clbk_iter++;
	gl_gla_env.gr_clbk_invo = invocation;
	gl_gla_env.gr_clbk_err = error;
	gl_gla_env.gr_clbk_status = lockStatus;

	gl_unlck_res = tet_test_lckResourceUnlockAsync(
	    LCK_RSC_UNLOCK_ASYNC_SUCCESS_T, TEST_CONFIG_MODE);
	sleep(3);
	if (gl_unlck_res == TET_PASS) {
		gl_lck_res = tet_test_lckResourceLockAsync(
		    LCK_RSC_LOCK_ASYNC_EXLCK_SUCCESS_T, TEST_CONFIG_MODE);
		sleep(3);
	}
}

void App_LockWaiterCallback(SaLckWaiterSignalT waiterSignal,
			    SaLckLockIdT lockId, SaLckLockModeT modeHeld,
			    SaLckLockModeT modeRequested)
{
	gl_wt_clbk_iter++;
	gl_gla_env.waiter_sig = waiterSignal;
	gl_gla_env.waiter_clbk_lck_id = lockId;
	gl_gla_env.waiter_clbk_mode_held = modeHeld;
	gl_gla_env.waiter_clbk_mode_req = modeRequested;
}

void App_ResourceUnlockCallback(SaInvocationT invocation, SaAisErrorT error)
{
	gl_gla_env.unlck_clbk_invo = invocation;
	gl_gla_env.unlck_clbk_err = error;
}

/* *********** Environment Initialization ************* */

void glsv_fill_lck_version(SaVersionT *version, SaUint8T rel_code,
			   SaUint8T mjr_ver, SaUint8T mnr_ver)
{
	version->releaseCode = rel_code;
	version->majorVersion = mjr_ver;
	version->minorVersion = mnr_ver;
}

void glsv_fill_lck_clbks(SaLckCallbacksT *clbk,
			 SaLckResourceOpenCallbackT opn_clbk,
			 SaLckLockGrantCallbackT gr_clbk,
			 SaLckLockWaiterCallbackT wt_clbk,
			 SaLckResourceUnlockCallbackT unlck_clbk)
{
	clbk->saLckResourceOpenCallback = opn_clbk;
	clbk->saLckLockGrantCallback = gr_clbk;
	clbk->saLckLockWaiterCallback = wt_clbk;
	clbk->saLckResourceUnlockCallback = unlck_clbk;
}

void glsv_fill_res_names(SaNameT *name, char *string, char *inst_num_char)
{
	memcpy(name->value, string, strlen(string));
	if (inst_num_char)
		memcpy(name->value + strlen(string), inst_num_char, strlen(inst_num_char));
	name->length = strlen(string) + (inst_num_char ? strlen(inst_num_char) : 0);
}

void init_glsv_test_env()
{
	char inst_num_char[10] = {0};
	char *inst_char = NULL;

	memset(&gl_gla_env, '\0', sizeof(GLA_TEST_ENV));

	if (gl_tetlist_index == GLSV_ONE_NODE_LIST) {
		sprintf(inst_num_char, "%d%u", gl_glsv_inst_num, gl_nodeId);
		inst_char = inst_num_char;
	}

	/* Invalid Parameters */

	gl_gla_env.inv_params.inv_lck_hdl = 12345;
	glsv_fill_lck_version(&gl_gla_env.inv_params.inv_version, 'C', 0, 1);
	glsv_fill_lck_version(&gl_gla_env.inv_params.inv_ver_bad_rel_code, '\0',
			      1, 0);
	glsv_fill_lck_version(&gl_gla_env.inv_params.inv_ver_not_supp, 'B', 4,
			      0);
	gl_gla_env.inv_params.inv_res_hdl = 54321;
	gl_gla_env.inv_params.inv_lck_id = 22232;

	glsv_fill_lck_clbks(&gl_gla_env.gen_clbks, App_ResourceOpenCallback,
			    App_LockGrantCallback, App_LockWaiterCallback,
			    App_ResourceUnlockCallback);
	glsv_fill_lck_clbks(&gl_gla_env.null_clbks, NULL, NULL, NULL, NULL);
	glsv_fill_lck_clbks(&gl_gla_env.null_wt_clbks, App_ResourceOpenCallback,
			    App_LockGrantCallback, NULL,
			    App_ResourceUnlockCallback);

	glsv_fill_lck_version(&gl_gla_env.version, 'B', 1, 1);

	glsv_fill_res_names(&gl_gla_env.res1,
			    "safLock=resource1,safApp=safLockService",
			    inst_char);
	glsv_fill_res_names(&gl_gla_env.res2,
			    "safLock=resource2,safApp=safLockService",
			    inst_char);
	glsv_fill_res_names(&gl_gla_env.res3,
			    "safLock=resource3,safApp=safLockService",
			    inst_char);
}

void glsv_print_testcase(char *string)
{
	m_TET_GLSV_PRINTF("%s", string);
	tet_printf("%s", string);
}

void glsv_result(int result)
{
	glsv_clean_output_params();
	glsv_clean_clbk_params();
}

/************* saLckInitialize Api Tests *************/

void glsv_it_init_01()
{
	int result;

	result =
	    tet_test_lckInitialize(LCK_INIT_SUCCESS_T, TEST_NONCONFIG_MODE);
	glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);

	glsv_result(result);
}

void glsv_it_init_02()
{
	int result;

	result = tet_test_lckInitialize(LCK_INIT_NULL_CBK_PARAM_T,
					TEST_NONCONFIG_MODE);
	glsv_init_cleanup(LCK_CLEAN_INIT_NULL_CBK_PARAM_T);

	glsv_result(result);
}

void glsv_it_init_03()
{
	int result;

	result = tet_test_lckInitialize(LCK_INIT_NULL_VERSION_T,
					TEST_NONCONFIG_MODE);
	glsv_result(result);
}

void glsv_it_init_04()
{
	int result;

	result =
	    tet_test_lckInitialize(LCK_INIT_NULL_HANDLE_T, TEST_NONCONFIG_MODE);
	glsv_result(result);
}

void glsv_it_init_05()
{
	int result;

	result = tet_test_lckInitialize(LCK_INIT_NULL_VERSION_CBKS_T,
					TEST_NONCONFIG_MODE);
	glsv_result(result);
}

void glsv_it_init_06()
{
	int result;

	result =
	    tet_test_lckInitialize(LCK_INIT_BAD_VERSION_T, TEST_NONCONFIG_MODE);
	glsv_restore_params(LCK_RESTORE_INIT_BAD_VERSION_T);
	glsv_result(result);
}

void glsv_it_init_07()
{
	int result;

	result = tet_test_lckInitialize(LCK_INIT_BAD_REL_CODE_T,
					TEST_NONCONFIG_MODE);
	glsv_restore_params(LCK_RESTORE_INIT_BAD_REL_CODE_T);
	glsv_result(result);
}

void glsv_it_init_08()
{
	int result;

	result = tet_test_lckInitialize(LCK_INIT_BAD_MAJOR_VER_T,
					TEST_NONCONFIG_MODE);
	glsv_restore_params(LCK_RESTORE_INIT_BAD_MAJOR_VER_T);
	glsv_result(result);
}

void glsv_it_init_09()
{
	int result;

	result =
	    tet_test_lckInitialize(LCK_INIT_BAD_VERSION_T, TEST_NONCONFIG_MODE);
	if (result == TET_PASS) {
		if (gl_gla_env.inv_params.inv_version.releaseCode == 'B' &&
		    gl_gla_env.inv_params.inv_version.majorVersion == 1 &&
		    gl_gla_env.inv_params.inv_version.minorVersion == 1) {
			glsv_restore_params(LCK_RESTORE_INIT_BAD_VERSION_T);
			glsv_result(TET_PASS);
			return;
		}
	}

	glsv_result(result);
}

void glsv_it_init_10()
{
	int result;

	result =
	    tet_test_lckInitialize(LCK_INIT_NULL_CBKS_T, TEST_NONCONFIG_MODE);
	glsv_init_cleanup(LCK_CLEAN_INIT_NULL_CBKS_T);
	glsv_result(result);
}

/*********** saLckSelectionObjectGet Api Tests ************/

void glsv_it_selobj_01()
{
	int result;

	result = tet_test_lckInitialize(LCK_INIT_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final;

	result = tet_test_lckSelectionObject(LCK_SEL_OBJ_SUCCESS_T,
					     TEST_NONCONFIG_MODE);

	glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);

final:
	glsv_result(result);
}

void glsv_it_selobj_02()
{
	int result;

	result = tet_test_lckInitialize(LCK_INIT_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final;

	result = tet_test_lckSelectionObject(LCK_SEL_OBJ_NULL_SEL_OBJ_T,
					     TEST_NONCONFIG_MODE);

	glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);

final:
	glsv_result(result);
}

void glsv_it_selobj_03()
{
	int result;

	result = tet_test_lckSelectionObject(LCK_SEL_OBJ_BAD_HANDLE_T,
					     TEST_NONCONFIG_MODE);
	glsv_result(result);
}

void glsv_it_selobj_04()
{
	int result;

	result = tet_test_lckInitialize(LCK_INIT_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final;

	result = tet_test_lckFinalize(LCK_FINALIZE_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final;

	result = tet_test_lckSelectionObject(LCK_SEL_OBJ_FINALIZED_HDL_T,
					     TEST_NONCONFIG_MODE);

final:
	glsv_result(result);
}

void glsv_it_selobj_05()
{
	int result;
	SaSelectionObjectT sel_obj;

	result = tet_test_lckInitialize(LCK_INIT_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final;

	result = tet_test_lckSelectionObject(LCK_SEL_OBJ_SUCCESS_T,
					     TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	sel_obj = gl_gla_env.sel_obj;

	result = tet_test_lckSelectionObject(LCK_SEL_OBJ_SUCCESS_T,
					     TEST_NONCONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	if (sel_obj == gl_gla_env.sel_obj)
		result = TET_PASS;
	else
		result = TET_FAIL;

final1:
	glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);

final:
	glsv_result(result);
}

/*********** saLckOptionCheck Api Tests ************/

void glsv_it_option_chk_01()
{
	int result, result1, result2;

	result1 = tet_test_lckOptionCheck(LCK_OPT_CHCK_BAD_HDL_T,
					  TEST_NONCONFIG_MODE);

	result = tet_test_lckInitialize(LCK_INIT_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final;

	result = tet_test_lckFinalize(LCK_FINALIZE_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS) {
		glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);
		goto final;
	}

	result2 = tet_test_lckOptionCheck(LCK_OPT_CHCK_FINALIZED_HDL_T,
					  TEST_NONCONFIG_MODE);

	if (result1 == TET_PASS && result2 == TET_PASS)
		result = TET_PASS;
	else
		result = TET_FAIL;

final:
	glsv_result(result);
}

void glsv_it_option_chk_02()
{
	int result;

	result = tet_test_lckInitialize(LCK_INIT_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final;

	result = tet_test_lckOptionCheck(LCK_OPT_CHCK_INVALID_PARAM,
					 TEST_NONCONFIG_MODE);

	glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);

final:
	glsv_result(result);
}

void glsv_it_option_chk_03()
{
	int result;

	result = tet_test_lckInitialize(LCK_INIT_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final;

	result = tet_test_lckOptionCheck(LCK_OPT_CHCK_SUCCESS_T,
					 TEST_NONCONFIG_MODE);

	glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);

final:
	glsv_result(result);
}

/*********** saLckDispatch Api Tests ************/

void glsv_it_dispatch_01()
{
	int result;

	result = tet_test_lckInitialize(LCK_INIT_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final;

	result = tet_test_lckResourceOpenAsync(
	    LCK_RESOURCE_OPEN_ASYNC_HDL1_NAME1_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	m_GLSV_WAIT;

	result = tet_test_lckDispatch(LCK_DISPATCH_DISPATCH_ONE_SUCCESS_T,
				      TEST_NONCONFIG_MODE);
	if (result == TET_PASS && gl_gla_env.open_clbk_invo == 208 &&
	    gl_gla_env.open_clbk_err == SA_AIS_OK)
		result = TET_PASS;
	else
		result = TET_FAIL;

final1:
	glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);

final:
	glsv_result(result);
}

void glsv_it_dispatch_02()
{
	int result;

	result = tet_test_lckInitialize(LCK_INIT_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final;

	result = tet_test_lckResourceOpen(
	    LCK_RESOURCE_OPEN_HDL1_NAME1_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	result = tet_test_lckResourceLockAsync(
	    LCK_RSC_LOCK_ASYNC_PRLCK_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	sleep(2);

	result = tet_test_lckResourceUnlockAsync(
	    LCK_RSC_UNLOCK_ASYNC_SUCCESS_ID2_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	m_GLSV_WAIT;

	result = tet_test_lckDispatch(LCK_DISPATCH_DISPATCH_ALL_SUCCESS_T,
				      TEST_NONCONFIG_MODE);
	if (result == TET_PASS && gl_gla_env.gr_clbk_invo == 706 &&
	    gl_gla_env.unlck_clbk_invo == 905)
		result = TET_PASS;
	else
		result = TET_FAIL;

final1:
	glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);

final:
	glsv_result(result);
}

void glsv_it_dispatch_03()
{
	int result;

	result = tet_test_lckInitialize(LCK_INIT_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final;

	glsv_createthread(&gl_gla_env.lck_hdl1);

	result = tet_test_lckResourceOpenAsync(
	    LCK_RESOURCE_OPEN_ASYNC_HDL1_NAME1_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	m_GLSV_WAIT;

	if (gl_gla_env.open_clbk_invo != 208 ||
	    gl_gla_env.open_clbk_err != SA_AIS_OK) {
		result = TET_FAIL;
		goto final1;
	}

	gl_gla_env.res_hdl1 = gl_gla_env.open_clbk_res_hdl;

	result = tet_test_lckResourceLockAsync(
	    LCK_RSC_LOCK_ASYNC_PRLCK_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	m_GLSV_WAIT;

	if (gl_gla_env.gr_clbk_invo == 706 &&
	    gl_gla_env.gr_clbk_err == SA_AIS_OK &&
	    gl_gla_env.gr_clbk_status == SA_LCK_LOCK_GRANTED)
		result = TET_PASS;
	else
		result = TET_FAIL;

final1:
	glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);

final:
	glsv_result(result);
}

void glsv_it_dispatch_04()
{
	int result;

	result = tet_test_lckInitialize(LCK_INIT_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final;

	result =
	    tet_test_lckDispatch(LCK_DISPATCH_BAD_FLAGS_T, TEST_NONCONFIG_MODE);

	glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);

final:
	glsv_result(result);
}

void glsv_it_dispatch_05()
{
	int result, result1, result2;

	result1 = tet_test_lckDispatch(LCK_DISPATCH_ONE_BAD_HANDLE_T,
				       TEST_NONCONFIG_MODE);

	result = tet_test_lckInitialize(LCK_INIT_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final;

	result = tet_test_lckFinalize(LCK_FINALIZE_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS) {
		glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);
		goto final;
	}

	result2 = tet_test_lckDispatch(LCK_DISPATCH_ONE_FINALIZED_HDL_T,
				       TEST_NONCONFIG_MODE);

	if (result1 == TET_PASS && result2 == TET_PASS)
		result = TET_PASS;
	else
		result = TET_FAIL;

final:
	glsv_result(result);
}

void glsv_it_dispatch_06()
{
	int result, result1, result2;

	result1 = tet_test_lckDispatch(LCK_DISPATCH_ALL_BAD_HANDLE_T,
				       TEST_NONCONFIG_MODE);

	result = tet_test_lckInitialize(LCK_INIT_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final;

	result = tet_test_lckFinalize(LCK_FINALIZE_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS) {
		glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);
		goto final;
	}

	result2 = tet_test_lckDispatch(LCK_DISPATCH_ALL_FINALIZED_HDL_T,
				       TEST_NONCONFIG_MODE);

	if (result1 == TET_PASS && result2 == TET_PASS)
		result = TET_PASS;
	else
		result = TET_FAIL;

final:
	glsv_result(result);
}

void glsv_it_dispatch_07()
{
	int result, result1, result2;

	result1 = tet_test_lckDispatch(LCK_DISPATCH_BLOCKING_BAD_HANDLE_T,
				       TEST_NONCONFIG_MODE);

	result = tet_test_lckInitialize(LCK_INIT_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final;

	result = tet_test_lckFinalize(LCK_FINALIZE_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS) {
		glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);
		goto final;
	}

	result2 = tet_test_lckDispatch(LCK_DISPATCH_BLOCKING_FINALIZED_HDL_T,
				       TEST_NONCONFIG_MODE);

	if (result1 == TET_PASS && result2 == TET_PASS)
		result = TET_PASS;
	else
		result = TET_FAIL;

final:
	glsv_result(result);
}

void glsv_it_dispatch_08()
{
	int result;

	result = tet_test_lckInitialize(LCK_INIT_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final;

	result = tet_test_lckDispatch(LCK_DISPATCH_DISPATCH_ONE_SUCCESS_T,
				      TEST_NONCONFIG_MODE);

	glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);

final:
	glsv_result(result);
}

void glsv_it_dispatch_09()
{
	int result;

	result = tet_test_lckInitialize(LCK_INIT_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final;

	result = tet_test_lckDispatch(LCK_DISPATCH_DISPATCH_ALL_SUCCESS_T,
				      TEST_NONCONFIG_MODE);

	glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);

final:
	glsv_result(result);
}

/*********** saLckFinalize Api Tests ************/

void glsv_it_finalize_01()
{
	int result;

	result = tet_test_lckInitialize(LCK_INIT_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final;

	result = tet_test_lckFinalize(LCK_FINALIZE_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS) {
		glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);
		goto final;
	}

	result = tet_test_lckDispatch(LCK_DISPATCH_ALL_FINALIZED_HDL_T,
				      TEST_NONCONFIG_MODE);

final:
	glsv_result(result);
}

void glsv_it_finalize_02()
{
	int result;

	result =
	    tet_test_lckFinalize(LCK_FINALIZE_BAD_HDL_T, TEST_NONCONFIG_MODE);
	glsv_result(result);
}

void glsv_it_finalize_03()
{
	int result;

	result = tet_test_lckInitialize(LCK_INIT_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final;

	result = tet_test_lckFinalize(LCK_FINALIZE_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS) {
		glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);
		goto final;
	}

	result = tet_test_lckFinalize(LCK_FINALIZE_FINALIZED_HDL_T,
				      TEST_NONCONFIG_MODE);

final:
	glsv_result(result);
}

void glsv_it_finalize_04()
{
	int result;
	fd_set read_fd;
	struct timeval tv;

	result = tet_test_lckInitialize(LCK_INIT_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final;

	result = tet_test_lckSelectionObject(LCK_SEL_OBJ_SUCCESS_T,
					     TEST_CONFIG_MODE);
	if (result != TET_PASS) {
		glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);
		goto final;
	}

	result = tet_test_lckFinalize(LCK_FINALIZE_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS) {
		glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);
		goto final;
	}

	tv.tv_sec = 5;
	tv.tv_usec = 0;

	FD_ZERO(&read_fd);
	FD_SET(gl_gla_env.sel_obj, &read_fd);
	result = select(gl_gla_env.sel_obj + 1, &read_fd, NULL, NULL, &tv);
	if (result == -1)
		result = TET_PASS;
	else
		result = TET_FAIL;

final:
	glsv_result(result);
}

void glsv_it_finalize_05()
{
	int result;

	result = tet_test_lckInitialize(LCK_INIT_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final;

	result = tet_test_lckResourceOpen(
	    LCK_RESOURCE_OPEN_HDL1_NAME1_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS) {
		glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);
		goto final;
	}

	result = tet_test_lckFinalize(LCK_FINALIZE_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS) {
		glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);
		goto final;
	}

	result = tet_test_lckResourceClose(LCK_RESOURCE_CLOSE_BAD_HANDLE2_T,
					   TEST_NONCONFIG_MODE);

final:
	glsv_result(result);
}

void glsv_it_finalize_06()
{
	int result;

	result = tet_test_lckInitialize(LCK_INIT_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final;

	result = tet_test_lckResourceOpen(
	    LCK_RESOURCE_OPEN_HDL1_NAME1_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS) {
		glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);
		goto final;
	}

	result = tet_test_lckResourceLock(LCK_RSC_LOCK_EX_LOCK_SUCCESS_T,
					  TEST_CONFIG_MODE);
	if (result != TET_PASS ||
	    gl_gla_env.lck_status != SA_LCK_LOCK_GRANTED) {
		result = TET_UNRESOLVED;
		glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);
		goto final;
	}

	result = tet_test_lckFinalize(LCK_FINALIZE_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS) {
		glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);
		goto final;
	}

	result = tet_test_lckResourceUnlock(LCK_RSC_FINALIZED_UNLOCKED_LOCKID_T,
					    TEST_NONCONFIG_MODE);

final:
	glsv_result(result);
}

/*********** saLckResourceOpen Api Tests ************/

void glsv_it_res_open_01()
{
	int result, result1, result2;

	result1 = tet_test_lckResourceOpen(LCK_RESOURCE_OPEN_BAD_HANDLE_T,
					   TEST_NONCONFIG_MODE);

	result = tet_test_lckInitialize(LCK_INIT_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final;

	result = tet_test_lckFinalize(LCK_FINALIZE_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS) {
		glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);
		goto final;
	}

	result2 = tet_test_lckResourceOpen(LCK_RESOURCE_OPEN_FINALIZED_HANDLE_T,
					   TEST_NONCONFIG_MODE);

	if (result1 == TET_PASS && result2 == TET_PASS)
		result = TET_PASS;
	else
		result = TET_FAIL;

final:
	glsv_result(result);
}

void glsv_it_res_open_02()
{
	int result;

	result = tet_test_lckInitialize(LCK_INIT_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final;

	result = tet_test_lckResourceOpen(LCK_RESOURCE_OPEN_NULL_RSC_NAME_T,
					  TEST_NONCONFIG_MODE);

	glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);

final:
	glsv_result(result);
}

void glsv_it_res_open_03()
{
	int result;

	result = tet_test_lckInitialize(LCK_INIT_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final;

	result = tet_test_lckResourceOpen(LCK_RESOURCE_OPEN_NULL_RSC_HDL_T,
					  TEST_NONCONFIG_MODE);

	glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);

final:
	glsv_result(result);
}

void glsv_it_res_open_04()
{
	int result;

	result = tet_test_lckInitialize(LCK_INIT_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final;

	result = tet_test_lckResourceOpen(LCK_RESOURCE_OPEN_BAD_FLAGS_T,
					  TEST_NONCONFIG_MODE);

	glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);

final:
	glsv_result(result);
}

void glsv_it_res_open_05()
{
	int result;

	result = tet_test_lckInitialize(LCK_INIT_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final;

	result = tet_test_lckResourceOpen(LCK_RESOURCE_OPEN_TIMEOUT_T,
					  TEST_NONCONFIG_MODE);

	glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);

final:
	glsv_result(result);
}

void glsv_it_res_open_06()
{
	int result;

	result = tet_test_lckInitialize(LCK_INIT_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final;

	result = tet_test_lckResourceOpen(LCK_RESOURCE_OPEN_RSC_NOT_EXIST_T,
					  TEST_NONCONFIG_MODE);

	glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);

final:
	glsv_result(result);
}

void glsv_it_res_open_07()
{
	int result;

	result = tet_test_lckInitialize(LCK_INIT_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final;

	result = tet_test_lckResourceOpen(
	    LCK_RESOURCE_OPEN_HDL1_NAME2_SUCCESS_T, TEST_NONCONFIG_MODE);

	glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);

final:
	glsv_result(result);
}

void glsv_it_res_open_08()
{
	int result;

	result = tet_test_lckInitialize(LCK_INIT_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final;

	result = tet_test_lckResourceOpen(
	    LCK_RESOURCE_OPEN_HDL1_NAME2_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	result = tet_test_lckResourceOpen(
	    LCK_RESOURCE_OPEN_RSC_EXIST_HDL1_NAME2_SUCCESS_T,
	    TEST_NONCONFIG_MODE);

final1:
	glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);

final:
	glsv_result(result);
}

void glsv_it_res_open_09()
{
	int result;

	result = tet_test_lckInitialize(LCK_INIT_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final;

	result = tet_test_lckResourceOpen(
	    LCK_RESOURCE_OPEN_HDL1_NAME1_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	result = tet_test_lckResourceOpen(
	    LCK_RESOURCE_OPEN_NAME1_EXIST_SUCCESS_T, TEST_NONCONFIG_MODE);

final1:
	glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);

final:
	glsv_result(result);
}

void glsv_it_res_open_10()
{
	int result;

	result = tet_test_lckInitialize(LCK_INIT_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final;

	result = tet_test_lckResourceOpen(
	    LCK_RESOURCE_OPEN_HDL1_NAME1_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	result = tet_test_lckResourceClose(
	    LCK_RESOURCE_CLOSE_RSC_HDL1_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	result = tet_test_lckResourceOpen(
	    LCK_RESOURCE_OPEN_NAME1_EXIST_SUCCESS_T, TEST_NONCONFIG_MODE);

final1:
	glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);

final:
	glsv_result(result);
}

/*********** saLckResourceOpenAsync Api Tests ************/

void glsv_it_res_open_async_01()
{
	int result, result1, result2;

	result1 = tet_test_lckResourceOpenAsync(
	    LCK_RESOURCE_OPEN_ASYNC_BAD_HANDLE_T, TEST_NONCONFIG_MODE);

	result = tet_test_lckInitialize(LCK_INIT_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final;

	result = tet_test_lckFinalize(LCK_FINALIZE_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS) {
		glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);
		goto final;
	}

	result2 = tet_test_lckResourceOpenAsync(
	    LCK_RESOURCE_OPEN_ASYNC_FINALIZED_HDL_T, TEST_NONCONFIG_MODE);

	if (result1 == TET_PASS && result2 == TET_PASS)
		result = TET_PASS;
	else
		result = TET_FAIL;

final:
	glsv_result(result);
}

void glsv_it_res_open_async_02()
{
	int result;

	result = tet_test_lckInitialize(LCK_INIT_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final;

	result = tet_test_lckResourceOpenAsync(
	    LCK_RESOURCE_OPEN_ASYNC_NULL_RSC_NAME_T, TEST_NONCONFIG_MODE);

	glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);

final:
	glsv_result(result);
}

void glsv_it_res_open_async_03()
{
	int result;

	result = tet_test_lckInitialize(LCK_INIT_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final;

	result = tet_test_lckResourceOpenAsync(
	    LCK_RESOURCE_OPEN_ASYNC_BAD_FLAGS_T, TEST_NONCONFIG_MODE);

	glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);

final:
	glsv_result(result);
}

void glsv_it_res_open_async_04()
{
	int result;

	result = tet_test_lckInitialize(LCK_INIT_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final;

	result = tet_test_lckResourceOpenAsync(
	    LCK_RESOURCE_OPEN_ASYNC_RSC_NOT_EXIST_T, TEST_NONCONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	m_GLSV_WAIT;

	result = tet_test_lckDispatch(LCK_DISPATCH_DISPATCH_ONE_SUCCESS_T,
				      TEST_NONCONFIG_MODE);
	if (result == TET_PASS && gl_gla_env.open_clbk_invo == 206 &&
	    gl_gla_env.open_clbk_err == SA_AIS_ERR_NOT_EXIST)
		result = TET_PASS;
	else
		result = TET_FAIL;

final1:
	glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);

final:
	glsv_result(result);
}

void glsv_it_res_open_async_05()
{
	int result;

	result = tet_test_lckInitialize(LCK_INIT_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final;

	result = tet_test_lckResourceOpenAsync(
	    LCK_RESOURCE_OPEN_ASYNC_HDL1_NAME1_SUCCESS_T, TEST_NONCONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	m_GLSV_WAIT;

	result = tet_test_lckDispatch(LCK_DISPATCH_DISPATCH_ONE_SUCCESS_T,
				      TEST_NONCONFIG_MODE);
	if (result == TET_PASS && gl_gla_env.open_clbk_invo == 208 &&
	    gl_gla_env.open_clbk_err == SA_AIS_OK)
		result = TET_PASS;
	else
		result = TET_FAIL;

final1:
	glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);

final:
	glsv_result(result);
}

void glsv_it_res_open_async_06()
{
	int result;

	result = tet_test_lckInitialize(LCK_INIT_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final;

	result = tet_test_lckResourceOpenAsync(
	    LCK_RESOURCE_OPEN_ASYNC_HDL1_NAME1_SUCCESS_T, TEST_NONCONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	m_GLSV_WAIT;

	result = tet_test_lckDispatch(LCK_DISPATCH_DISPATCH_ONE_SUCCESS_T,
				      TEST_NONCONFIG_MODE);
	if (result == TET_PASS && gl_gla_env.open_clbk_invo == 208 &&
	    gl_gla_env.open_clbk_err == SA_AIS_OK)
		result = TET_PASS;
	else
		result = TET_FAIL;

final1:
	glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);

final:
	glsv_result(result);
}

void glsv_it_res_open_async_07()
{
	int result;

	result = tet_test_lckInitialize(LCK_INIT_NULL_CBKS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final;

	result = tet_test_lckResourceOpenAsync(
	    LCK_RESOURCE_OPEN_ASYNC_NULL_OPEN_CBK_T, TEST_NONCONFIG_MODE);

	glsv_init_cleanup(LCK_CLEAN_INIT_NULL_CBKS_T);

final:
	glsv_result(result);
}

void glsv_it_res_open_async_08()
{
	int result;

	result = tet_test_lckInitialize(LCK_INIT_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final;

	result = tet_test_lckResourceOpenAsync(
	    LCK_RESOURCE_OPEN_ASYNC_HDL1_NAME1_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	m_GLSV_WAIT;

	result = tet_test_lckDispatch(LCK_DISPATCH_DISPATCH_ONE_SUCCESS_T,
				      TEST_CONFIG_MODE);
	if (result != TET_PASS || gl_gla_env.open_clbk_invo != 208 ||
	    gl_gla_env.open_clbk_err != SA_AIS_OK) {
		result = TET_UNRESOLVED;
		goto final1;
	}

	result = tet_test_lckResourceOpenAsync(
	    LCK_RESOURCE_OPEN_ASYNC_HDL1_NAME1_EXIST_SUCCESS_T,
	    TEST_NONCONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	m_GLSV_WAIT;

	result = tet_test_lckDispatch(LCK_DISPATCH_DISPATCH_ONE_SUCCESS_T,
				      TEST_NONCONFIG_MODE);

	if (result != TET_PASS || gl_gla_env.open_clbk_invo != 212 ||
	    gl_gla_env.open_clbk_err != SA_AIS_OK)
		result = TET_FAIL;
	else
		result = TET_PASS;

final1:
	glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);

final:
	glsv_result(result);
}

void glsv_it_res_open_async_09()
{
	int result;

	result = tet_test_lckInitialize(LCK_INIT_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final;

	result = tet_test_lckResourceOpenAsync(
	    LCK_RESOURCE_OPEN_ASYNC_HDL1_NAME1_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	m_GLSV_WAIT;

	result = tet_test_lckDispatch(LCK_DISPATCH_DISPATCH_ONE_SUCCESS_T,
				      TEST_CONFIG_MODE);
	if (result != TET_PASS || gl_gla_env.open_clbk_invo != 208 ||
	    gl_gla_env.open_clbk_err != SA_AIS_OK) {
		result = TET_UNRESOLVED;
		goto final1;
	}

	result = tet_test_lckResourceOpenAsync(
	    LCK_RESOURCE_OPEN_ASYNC_NAME1_EXIST_T, TEST_NONCONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	m_GLSV_WAIT;

	result = tet_test_lckDispatch(LCK_DISPATCH_DISPATCH_ONE_SUCCESS_T,
				      TEST_NONCONFIG_MODE);

	if (result != TET_PASS || gl_gla_env.open_clbk_invo != 214 ||
	    gl_gla_env.open_clbk_err != SA_AIS_OK)
		result = TET_FAIL;
	else
		result = TET_PASS;

final1:
	glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);

final:
	glsv_result(result);
}

void glsv_it_res_open_async_10()
{
	int result;

	result = tet_test_lckInitialize(LCK_INIT_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final;

	result = tet_test_lckResourceOpen(
	    LCK_RESOURCE_OPEN_HDL1_NAME1_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	result = tet_test_lckResourceClose(
	    LCK_RESOURCE_CLOSE_RSC_HDL1_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	result = tet_test_lckResourceOpenAsync(
	    LCK_RESOURCE_OPEN_ASYNC_HDL1_NAME1_SUCCESS_T, TEST_NONCONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	m_GLSV_WAIT;

	result = tet_test_lckDispatch(LCK_DISPATCH_DISPATCH_ONE_SUCCESS_T,
				      TEST_NONCONFIG_MODE);

	if (result != TET_PASS || gl_gla_env.open_clbk_invo != 208 ||
	    gl_gla_env.open_clbk_err != SA_AIS_OK)
		result = TET_FAIL;
	else
		result = TET_PASS;

final1:
	glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);

final:
	glsv_result(result);
}

/*********** saLckResourceClose Api Tests ************/

void glsv_it_res_close_01()
{
	int result;

	result = tet_test_lckInitialize(LCK_INIT_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final;

	result = tet_test_lckResourceOpen(
	    LCK_RESOURCE_OPEN_HDL1_NAME2_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	result = tet_test_lckResourceClose(
	    LCK_RESOURCE_CLOSE_RSC_HDL2_SUCCESS_T, TEST_NONCONFIG_MODE);

final1:
	glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);

final:
	glsv_result(result);
}

void glsv_it_res_close_02()
{
	int result;

	result = tet_test_lckResourceClose(LCK_RESOURCE_CLOSE_BAD_RSC_HDL_T,
					   TEST_NONCONFIG_MODE);

	glsv_result(result);
}

void glsv_it_res_close_03()
{
	int result;

	result = tet_test_lckInitialize(LCK_INIT_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final;

	result = tet_test_lckResourceOpen(
	    LCK_RESOURCE_OPEN_HDL1_NAME2_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS) {
		glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);
		goto final;
	}

	result = tet_test_lckFinalize(LCK_FINALIZE_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS) {
		glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);
		goto final;
	}

	result = tet_test_lckResourceClose(LCK_RESOURCE_CLOSE_BAD_HANDLE2_T,
					   TEST_NONCONFIG_MODE);

final:
	glsv_result(result);
}

void glsv_it_res_close_04()
{
	int result;

	result = tet_test_lckInitialize(LCK_INIT_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final;

	result = tet_test_lckResourceOpen(
	    LCK_RESOURCE_OPEN_HDL1_NAME1_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	result = tet_test_lckResourceClose(
	    LCK_RESOURCE_CLOSE_RSC_HDL1_SUCCESS_T, TEST_NONCONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	result = tet_test_lckResourceLock(LCK_RSC_LOCK_CLOSED_RSC_HDL_T,
					  TEST_NONCONFIG_MODE);

final1:
	glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);

final:
	glsv_result(result);
}

void glsv_it_res_close_05()
{
	int result;

	result = tet_test_lckInitialize(LCK_INIT_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final;

	result = tet_test_lckResourceOpen(
	    LCK_RESOURCE_OPEN_HDL1_NAME1_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	result = tet_test_lckResourceLock(LCK_RSC_LOCK_EX_LOCK_SUCCESS_T,
					  TEST_CONFIG_MODE);
	if (result != TET_PASS ||
	    gl_gla_env.lck_status != SA_LCK_LOCK_GRANTED) {
		result = TET_UNRESOLVED;
		goto final1;
	}

	result = tet_test_lckResourceClose(
	    LCK_RESOURCE_CLOSE_RSC_HDL1_SUCCESS_T, TEST_NONCONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	result = tet_test_lckResourceUnlock(LCK_RSC_UNLOCK_UNLOCKED_LOCKID_T,
					    TEST_NONCONFIG_MODE);

final1:
	glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);

final:
	glsv_result(result);
}

void glsv_it_res_close_06()
{
	int result;

	result = tet_test_lckInitialize(LCK_INIT_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final;

	result = tet_test_lckResourceOpen(
	    LCK_RESOURCE_OPEN_HDL1_NAME1_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	result = tet_test_lckResourceLock(LCK_RSC_LOCK_PR_LOCK_SUCCESS_T,
					  TEST_CONFIG_MODE);
	if (result != TET_PASS ||
	    gl_gla_env.lck_status != SA_LCK_LOCK_GRANTED) {
		result = TET_UNRESOLVED;
		goto final1;
	}

	result = tet_test_lckResourceLockAsync(
	    LCK_RSC_LOCK_ASYNC_EXLCK_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	result = tet_test_lckResourceClose(
	    LCK_RESOURCE_CLOSE_RSC_HDL1_SUCCESS_T, TEST_NONCONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	result = tet_test_lckResourceUnlock(LCK_RSC_UNLOCK_UNLOCKED_LOCKID_T,
					    TEST_NONCONFIG_MODE);

final1:
	glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);

final:
	glsv_result(result);
}

void glsv_it_res_close_07()
{
	int result;
	SaLckResourceHandleT lcl_res_hdl;

	result = tet_test_lckInitialize(LCK_INIT_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final;

	result =
	    tet_test_lckInitialize(LCK_INIT_SUCCESS_HDL2_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	result = tet_test_lckResourceOpen(
	    LCK_RESOURCE_OPEN_HDL1_NAME1_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final2;

	lcl_res_hdl = gl_gla_env.res_hdl1;

	result = tet_test_lckResourceOpen(
	    LCK_RESOURCE_OPEN_HDL2_NAME1_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final2;

	result = tet_test_lckResourceLock(LCK_RSC_LOCK_PR_LOCK_SUCCESS_T,
					  TEST_CONFIG_MODE);
	if (result != TET_PASS ||
	    gl_gla_env.lck_status != SA_LCK_LOCK_GRANTED) {
		result = TET_UNRESOLVED;
		goto final2;
	}

	gl_gla_env.res_hdl1 = lcl_res_hdl;

	result = tet_test_lckResourceClose(
	    LCK_RESOURCE_CLOSE_RSC_HDL1_SUCCESS_T, TEST_NONCONFIG_MODE);
	if (result != TET_PASS)
		goto final2;

	result = tet_test_lckResourceUnlock(LCK_RSC_UNLOCK_LCKID2_SUCCESS_T,
					    TEST_NONCONFIG_MODE);

final2:
	glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_HDL2_T);

final1:
	glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);

final:
	glsv_result(result);
}

void glsv_it_res_close_08()
{
	int result;

	result = tet_test_lckInitialize(LCK_INIT_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final;

	result = tet_test_lckResourceOpen(
	    LCK_RESOURCE_OPEN_HDL1_NAME1_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	result = tet_test_lckResourceClose(
	    LCK_RESOURCE_CLOSE_RSC_HDL1_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	result = tet_test_lckResourceClose(LCK_RESOURCE_CLOSE_BAD_HANDLE_T,
					   TEST_NONCONFIG_MODE);

final1:
	glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);

final:
	glsv_result(result);
}

void glsv_it_res_close_09()
{
	int result;
	SaLckResourceHandleT lcl_res_hdl;

	result = tet_test_lckInitialize(LCK_INIT_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final;

	result =
	    tet_test_lckInitialize(LCK_INIT_SUCCESS_HDL2_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

   /*
   [LCK_RESOURCE_OPEN_HDL1_NAME1_SUCCESS_T] =
      {&gl_gla_env.lck_hdl1, &gl_gla_env.res1, RES_OPEN_TIMEOUT,
       &gl_gla_env.res_hdl1, SA_LCK_RESOURCE_CREATE, SA_AIS_OK},
       */

	result = tet_test_lckResourceOpen(
	    LCK_RESOURCE_OPEN_HDL1_NAME1_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final2;

	lcl_res_hdl = gl_gla_env.res_hdl1;

  /*
  [LCK_RESOURCE_OPEN_HDL2_NAME1_SUCCESS_T] =
      {&gl_gla_env.lck_hdl2, &gl_gla_env.res1, RES_OPEN_TIMEOUT,
       &gl_gla_env.res_hdl1, SA_LCK_RESOURCE_CREATE, SA_AIS_OK},
       */

	result = tet_test_lckResourceOpen(
	    LCK_RESOURCE_OPEN_HDL2_NAME1_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final2;

  /*
  [LCK_RESOURCE_CLOSE_RSC_HDL1_SUCCESS_T] = {&gl_gla_env.res_hdl1,
               SA_AIS_OK},
  [LCK_RESOURCE_CLOSE_RSC_HDL2_SUCCESS_T] = {&gl_gla_env.res_hdl2,
               SA_AIS_OK},
               */

	result = tet_test_lckResourceClose(
	    LCK_RESOURCE_CLOSE_RSC_HDL1_SUCCESS_T, TEST_NONCONFIG_MODE);
	if (result != TET_PASS)
		goto final2;

	gl_gla_env.res_hdl1 = lcl_res_hdl;

	result = tet_test_lckResourceClose(
	    LCK_RESOURCE_CLOSE_RSC_HDL1_SUCCESS_T, TEST_NONCONFIG_MODE);
	if (result != TET_PASS)
		goto final2;

  /*
  [LCK_RESOURCE_OPEN_RSC_NOT_EXIST_T] = {&gl_gla_env.lck_hdl1,
                 &gl_gla_env.res1,
                 RES_OPEN_TIMEOUT,
                 &gl_gla_env.res_hdl1, 0,
                 SA_AIS_ERR_NOT_EXIST},
                 */

	result = tet_test_lckResourceOpen(LCK_RESOURCE_OPEN_RSC_NOT_EXIST_T,
					  TEST_NONCONFIG_MODE);

final2:
	glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_HDL2_T);

final1:
	glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);

final:
	glsv_result(result);
}

void glsv_it_res_close_10()
{
	int result;

	result = tet_test_lckInitialize(LCK_INIT_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final;

	result = tet_test_lckResourceOpen(
	    LCK_RESOURCE_OPEN_HDL1_NAME1_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	result = tet_test_lckResourceLockAsync(
	    LCK_RSC_LOCK_ASYNC_EXLCK_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	sleep(2);

	result = tet_test_lckResourceClose(
	    LCK_RESOURCE_CLOSE_RSC_HDL1_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	m_GLSV_WAIT;

	result = tet_test_lckDispatch(LCK_DISPATCH_DISPATCH_ALL_SUCCESS_T,
				      TEST_NONCONFIG_MODE);
	if (result == TET_PASS && gl_gla_env.gr_clbk_invo == 0)
		result = TET_PASS;
	else
		result = TET_FAIL;

final1:
	glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);

final:
	glsv_result(result);
}

void glsv_it_res_close_11()
{
	int result;
	SaLckResourceHandleT lcl_res_hdl1, lcl_res_hdl2;

	result = tet_test_lckInitialize(LCK_INIT_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final;

	result =
	    tet_test_lckInitialize(LCK_INIT_SUCCESS_HDL2_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	result = tet_test_lckResourceOpen(
	    LCK_RESOURCE_OPEN_HDL1_NAME1_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final2;

	lcl_res_hdl1 = gl_gla_env.res_hdl1;

	result = tet_test_lckResourceOpen(
	    LCK_RESOURCE_OPEN_HDL2_NAME1_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final2;

	lcl_res_hdl2 = gl_gla_env.res_hdl1;

	gl_gla_env.res_hdl1 = lcl_res_hdl1;

	result = tet_test_lckResourceLock(LCK_RSC_LOCK_EX_LOCK_SUCCESS_T,
					  TEST_CONFIG_MODE);
	if (result != TET_PASS ||
	    gl_gla_env.lck_status != SA_LCK_LOCK_GRANTED) {
		result = TET_FAIL;
		goto final2;
	}

	gl_gla_env.res_hdl1 = lcl_res_hdl2;

	result = tet_test_lckResourceLock(LCK_RSC_LOCK_PR_ERR_TIMEOUT_T,
					  TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	gl_gla_env.res_hdl1 = lcl_res_hdl1;

	result = tet_test_lckResourceClose(
	    LCK_RESOURCE_CLOSE_RSC_HDL1_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	m_GLSV_WAIT;

	result = tet_test_lckDispatch(LCK_DISPATCH_DISPATCH_ALL_SUCCESS_T,
				      TEST_NONCONFIG_MODE);
	result = tet_test_lckDispatch(LCK_DISPATCH_DISPATCH_ALL_SUCCESS2_T,
				      TEST_NONCONFIG_MODE);
	if (result == TET_PASS && gl_gla_env.waiter_sig == 0 &&
	    gl_gla_env.waiter_clbk_lck_id == 0)
		result = TET_PASS;
	else
		result = TET_FAIL;

final2:
	glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_HDL2_T);

final1:
	glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);

final:
	glsv_result(result);
}

/*********** saLckResourceLock Api Tests ************/

void glsv_it_res_lck_01()
{
	int result;

	result = tet_test_lckResourceLock(LCK_RSC_LOCK_BAD_RSC_HDL_T,
					  TEST_CONFIG_MODE);
	glsv_result(result);
}

void glsv_it_res_lck_02()
{
	int result;

	result = tet_test_lckInitialize(LCK_INIT_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final;

	result = tet_test_lckFinalize(LCK_FINALIZE_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS) {
		glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);
		goto final;
	}

	result = tet_test_lckResourceLock(LCK_RSC_LOCK_FINALIZED_HDL_T,
					  TEST_NONCONFIG_MODE);

final:
	glsv_result(result);
}

void glsv_it_res_lck_03()
{
	int result;

	result = tet_test_lckInitialize(LCK_INIT_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final;

	result = tet_test_lckResourceOpen(
	    LCK_RESOURCE_OPEN_HDL1_NAME1_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	result = tet_test_lckResourceClose(
	    LCK_RESOURCE_CLOSE_RSC_HDL1_SUCCESS_T, TEST_NONCONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	result = tet_test_lckResourceLock(LCK_RSC_LOCK_CLOSED_RSC_HDL_T,
					  TEST_NONCONFIG_MODE);

final1:
	glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);

final:
	glsv_result(result);
}

void glsv_it_res_lck_04()
{
	int result;

	result = tet_test_lckInitialize(LCK_INIT_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final;

	result = tet_test_lckResourceOpen(
	    LCK_RESOURCE_OPEN_HDL1_NAME1_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	result = tet_test_lckResourceLock(LCK_RSC_LOCK_NULL_LCKID_T,
					  TEST_NONCONFIG_MODE);

final1:
	glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);

final:
	glsv_result(result);
}

void glsv_it_res_lck_05()
{
	int result;

	result = tet_test_lckInitialize(LCK_INIT_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final;

	result = tet_test_lckResourceOpen(
	    LCK_RESOURCE_OPEN_HDL1_NAME1_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	result = tet_test_lckResourceLock(LCK_RSC_LOCK_NULL_LCK_STATUS_T,
					  TEST_NONCONFIG_MODE);

final1:
	glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);

final:
	glsv_result(result);
}

void glsv_it_res_lck_06()
{
	int result;

	result = tet_test_lckInitialize(LCK_INIT_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final;

	result = tet_test_lckResourceOpen(
	    LCK_RESOURCE_OPEN_HDL1_NAME1_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	result = tet_test_lckResourceLock(LCK_RSC_LOCK_INVALID_LOCK_MODE_T,
					  TEST_NONCONFIG_MODE);

final1:
	glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);

final:
	glsv_result(result);
}

void glsv_it_res_lck_07()
{
	int result;

	result = tet_test_lckInitialize(LCK_INIT_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final;

	result = tet_test_lckResourceOpen(
	    LCK_RESOURCE_OPEN_HDL1_NAME1_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	result = tet_test_lckResourceLock(LCK_RSC_LOCK_BAD_FLGS_T,
					  TEST_NONCONFIG_MODE);

final1:
	glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);

final:
	glsv_result(result);
}

void glsv_it_res_lck_08()
{
	int result;

	result = tet_test_lckInitialize(LCK_INIT_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final;

	result = tet_test_lckResourceOpen(
	    LCK_RESOURCE_OPEN_HDL1_NAME1_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	result = tet_test_lckResourceLock(LCK_RSC_LOCK_ZERO_TIMEOUT_T,
					  TEST_NONCONFIG_MODE);

final1:
	glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);

final:
	glsv_result(result);
}

void glsv_it_res_lck_09()
{
	int result;

	result = tet_test_lckInitialize(LCK_INIT_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final;

	result = tet_test_lckResourceOpen(
	    LCK_RESOURCE_OPEN_HDL1_NAME1_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	result = tet_test_lckResourceLock(LCK_RSC_LOCK_PR_LOCK_SUCCESS_T,
					  TEST_NONCONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	if (gl_gla_env.lck_status != SA_LCK_LOCK_GRANTED)
		result = TET_FAIL;

final1:
	glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);

final:
	glsv_result(result);
}

void glsv_it_res_lck_10()
{
	int result;

	result = tet_test_lckInitialize(LCK_INIT_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final;

	result = tet_test_lckResourceOpen(
	    LCK_RESOURCE_OPEN_HDL1_NAME1_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	result = tet_test_lckResourceLock(LCK_RSC_LOCK_NO_QUEUE_PRLCK_T,
					  TEST_NONCONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	if (gl_gla_env.lck_status != SA_LCK_LOCK_GRANTED)
		result = TET_FAIL;

final1:
	glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);

final:
	glsv_result(result);
}

void glsv_it_res_lck_11()
{
	int result;

	result = tet_test_lckInitialize(LCK_INIT_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final;

	result = tet_test_lckResourceOpen(
	    LCK_RESOURCE_OPEN_HDL1_NAME1_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	result = tet_test_lckResourceLock(LCK_RSC_LOCK_ORPHAN_PRLCK_T,
					  TEST_NONCONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	if (gl_gla_env.lck_status != SA_LCK_LOCK_GRANTED)
		result = TET_FAIL;

	glsv_orphan_cleanup(LCK_CLEAN_RSC_LOCK_ORPHAN_PRLCK_T);

final1:
	glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);

final:
	glsv_result(result);
}

void glsv_it_res_lck_12()
{
	int result;

	result = tet_test_lckInitialize(LCK_INIT_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final;

	result = tet_test_lckResourceOpen(
	    LCK_RESOURCE_OPEN_HDL1_NAME1_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	result = tet_test_lckResourceLock(LCK_RSC_LOCK_EX_LOCK_SUCCESS_T,
					  TEST_NONCONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	if (gl_gla_env.lck_status != SA_LCK_LOCK_GRANTED)
		result = TET_FAIL;

final1:
	glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);

final:
	glsv_result(result);
}

void glsv_it_res_lck_13()
{
	int result;

	result = tet_test_lckInitialize(LCK_INIT_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final;

	result = tet_test_lckResourceOpen(
	    LCK_RESOURCE_OPEN_HDL1_NAME1_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	result = tet_test_lckResourceLock(LCK_RSC_LOCK_NO_QUEUE_EXLCK_T,
					  TEST_NONCONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	if (gl_gla_env.lck_status != SA_LCK_LOCK_GRANTED)
		result = TET_FAIL;

final1:
	glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);

final:
	glsv_result(result);
}

void glsv_it_res_lck_14()
{
	int result;

	result = tet_test_lckInitialize(LCK_INIT_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final;

	result = tet_test_lckResourceOpen(
	    LCK_RESOURCE_OPEN_HDL1_NAME1_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	result = tet_test_lckResourceLock(LCK_RSC_LOCK_ORPHAN_EXLCK_T,
					  TEST_NONCONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	if (gl_gla_env.lck_status != SA_LCK_LOCK_GRANTED)
		result = TET_FAIL;

	glsv_orphan_cleanup(LCK_CLEAN_RSC_LOCK_ORPHAN_EXLCK_T);

final1:
	glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);

final:
	glsv_result(result);
}

void glsv_it_res_lck_15()
{
	int result;

	result = tet_test_lckInitialize(LCK_INIT_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final;

	result =
	    tet_test_lckInitialize(LCK_INIT_SUCCESS_HDL2_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	result = tet_test_lckResourceOpen(
	    LCK_RESOURCE_OPEN_HDL1_NAME1_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final2;

	result = tet_test_lckResourceLock(LCK_RSC_LOCK_EX_LOCK_SUCCESS_T,
					  TEST_CONFIG_MODE);
	if (result != TET_PASS ||
	    gl_gla_env.lck_status != SA_LCK_LOCK_GRANTED) {
		result = TET_UNRESOLVED;
		goto final2;
	}

	result = tet_test_lckResourceOpen(
	    LCK_RESOURCE_OPEN_RSC_EXIST_HDL2_NAME1_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final2;

	result = tet_test_lckResourceLock(LCK_RSC_LOCK_PR_ERR_TIMEOUT_T,
					  TEST_NONCONFIG_MODE);

final2:
	glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_HDL2_T);

final1:
	glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);

final:
	glsv_result(result);
}

void glsv_it_res_lck_16()
{
	int result;

	result = tet_test_lckInitialize(LCK_INIT_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final;

	result = tet_test_lckResourceOpen(
	    LCK_RESOURCE_OPEN_HDL1_NAME1_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	result = tet_test_lckResourceLock(LCK_RSC_LOCK_EX_LOCK_SUCCESS_T,
					  TEST_CONFIG_MODE);
	if (result != TET_PASS ||
	    gl_gla_env.lck_status != SA_LCK_LOCK_GRANTED) {
		result = TET_UNRESOLVED;
		goto final1;
	}

	result = tet_test_lckResourceLock(LCK_RSC_LOCK_PR_LOCK_DEADLOCK_T,
					  TEST_NONCONFIG_MODE);

	if (result != TET_PASS || gl_gla_env.lck_status != SA_LCK_LOCK_DEADLOCK)
		result = TET_FAIL;

final1:
	glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);

final:
	glsv_result(result);
}

void glsv_it_res_lck_17()
{
	int result;

	result = tet_test_lckInitialize(LCK_INIT_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final;

	result = tet_test_lckResourceOpen(
	    LCK_RESOURCE_OPEN_HDL1_NAME1_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	result = tet_test_lckResourceLock(LCK_RSC_LOCK_EX_LOCK_SUCCESS_T,
					  TEST_CONFIG_MODE);
	if (result != TET_PASS ||
	    gl_gla_env.lck_status != SA_LCK_LOCK_GRANTED) {
		result = TET_UNRESOLVED;
		goto final1;
	}

	result = tet_test_lckResourceLock(LCK_RSC_LOCK_DUPLICATE_EXLCK_T,
					  TEST_NONCONFIG_MODE);

	if (result != TET_PASS ||
	    gl_gla_env.lck_status != SA_LCK_LOCK_DUPLICATE_EX)
		result = TET_FAIL;

final1:
	glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);

final:
	glsv_result(result);
}

void glsv_it_res_lck_18()
{
	int result;

	result = tet_test_lckInitialize(LCK_INIT_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final;

	result =
	    tet_test_lckInitialize(LCK_INIT_SUCCESS_HDL2_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	result = tet_test_lckResourceOpen(
	    LCK_RESOURCE_OPEN_HDL2_NAME1_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final2;

	result = tet_test_lckResourceLock(LCK_RSC_LOCK_EX_LOCK_SUCCESS_T,
					  TEST_CONFIG_MODE);
	if (result != TET_PASS ||
	    gl_gla_env.lck_status != SA_LCK_LOCK_GRANTED) {
		result = TET_UNRESOLVED;
		goto final2;
	}

	result = tet_test_lckResourceOpen(
	    LCK_RESOURCE_OPEN_HDL1_NAME1_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final2;

	result = tet_test_lckResourceLock(LCK_RSC_LOCK_PR_LOCK_NOT_QUEUED_T,
					  TEST_NONCONFIG_MODE);

	if (result != TET_PASS ||
	    gl_gla_env.lck_status != SA_LCK_LOCK_NOT_QUEUED)
		result = TET_FAIL;

final2:
	glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_HDL2_T);

final1:
	glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);

final:
	glsv_result(result);
}

/*********** saLckResourceLockAsync Api Tests ************/

void glsv_it_res_lck_async_01()
{
	int result;

	result = tet_test_lckResourceLockAsync(LCK_RSC_LOCK_ASYNC_BAD_RSC_HDL_T,
					       TEST_NONCONFIG_MODE);
	glsv_result(result);
}

void glsv_it_res_lck_async_02()
{
	int result;

	result = tet_test_lckInitialize(LCK_INIT_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final;

	result = tet_test_lckResourceOpen(
	    LCK_RESOURCE_OPEN_HDL1_NAME1_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS) {
		glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);
		goto final;
	}

	result = tet_test_lckFinalize(LCK_FINALIZE_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS) {
		glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);
		goto final;
	}

	result = tet_test_lckResourceLockAsync(
	    LCK_RSC_LOCK_ASYNC_FINALIZED_HDL_T, TEST_NONCONFIG_MODE);

final:
	glsv_result(result);
}

void glsv_it_res_lck_async_03()
{
	int result;

	result = tet_test_lckInitialize(LCK_INIT_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final;

	result = tet_test_lckResourceOpen(
	    LCK_RESOURCE_OPEN_HDL1_NAME1_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	result = tet_test_lckResourceClose(
	    LCK_RESOURCE_CLOSE_RSC_HDL1_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	result = tet_test_lckResourceLockAsync(
	    LCK_RSC_LOCK_ASYNC_CLOSED_RSC_HDL_T, TEST_NONCONFIG_MODE);

final1:
	glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);

final:
	glsv_result(result);
}

void glsv_it_res_lck_async_04()
{
	int result;

	result = tet_test_lckInitialize(LCK_INIT_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final;

	result = tet_test_lckResourceOpen(
	    LCK_RESOURCE_OPEN_HDL1_NAME1_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	result = tet_test_lckResourceLockAsync(LCK_RSC_LOCK_ASYNC_NULL_LCKID_T,
					       TEST_NONCONFIG_MODE);

final1:
	glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);

final:
	glsv_result(result);
}

void glsv_it_res_lck_async_05()
{
	int result;

	result = tet_test_lckInitialize(LCK_INIT_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final;

	result = tet_test_lckResourceOpen(
	    LCK_RESOURCE_OPEN_HDL1_NAME1_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	result = tet_test_lckResourceLockAsync(
	    LCK_RSC_LOCK_ASYNC_INVALID_LOCK_MODE_T, TEST_NONCONFIG_MODE);

final1:
	glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);

final:
	glsv_result(result);
}

void glsv_it_res_lck_async_06()
{
	int result;

	result = tet_test_lckInitialize(LCK_INIT_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final;

	result = tet_test_lckResourceOpen(
	    LCK_RESOURCE_OPEN_HDL1_NAME1_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	result = tet_test_lckResourceLockAsync(LCK_RSC_LOCK_ASYNC_BAD_FLGS_T,
					       TEST_NONCONFIG_MODE);

final1:
	glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);

final:
	glsv_result(result);
}

void glsv_it_res_lck_async_07()
{
	int result;

	result =
	    tet_test_lckInitialize(LCK_INIT_NULL_CBKS2_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final;

	result = tet_test_lckResourceOpen(
	    LCK_RESOURCE_OPEN_HDL1_NAME1_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	result = tet_test_lckResourceLockAsync(LCK_RSC_LOCK_ASYNC_ERR_INIT_T,
					       TEST_NONCONFIG_MODE);

final1:
	glsv_init_cleanup(LCK_CLEAN_INIT_NULL_CBKS2_T);

final:
	glsv_result(result);
}

void glsv_it_res_lck_async_08()
{
	int result;

	result = tet_test_lckInitialize(LCK_INIT_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final;

	result = tet_test_lckResourceOpen(
	    LCK_RESOURCE_OPEN_HDL1_NAME1_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	result = tet_test_lckResourceLockAsync(
	    LCK_RSC_LOCK_ASYNC_PRLCK_SUCCESS_T, TEST_NONCONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	m_GLSV_WAIT;

	result = tet_test_lckDispatch(LCK_DISPATCH_DISPATCH_ONE_SUCCESS_T,
				      TEST_NONCONFIG_MODE);

	if (result == TET_PASS && gl_gla_env.gr_clbk_invo == 706 &&
	    gl_gla_env.gr_clbk_err == SA_AIS_OK &&
	    gl_gla_env.gr_clbk_status == SA_LCK_LOCK_GRANTED)
		result = TET_PASS;
	else
		result = TET_FAIL;

final1:
	glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);

final:
	glsv_result(result);
}

void glsv_it_res_lck_async_09()
{
	int result;

	result = tet_test_lckInitialize(LCK_INIT_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final;

	result = tet_test_lckResourceOpen(
	    LCK_RESOURCE_OPEN_HDL1_NAME1_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	result = tet_test_lckResourceLockAsync(
	    LCK_RSC_LOCK_ASYNC_NO_QUEUE_PRLCK_T, TEST_NONCONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	m_GLSV_WAIT;

	result = tet_test_lckDispatch(LCK_DISPATCH_DISPATCH_ONE_SUCCESS_T,
				      TEST_NONCONFIG_MODE);

	if (result == TET_PASS && gl_gla_env.gr_clbk_invo == 710 &&
	    gl_gla_env.gr_clbk_err == SA_AIS_OK &&
	    gl_gla_env.gr_clbk_status == SA_LCK_LOCK_GRANTED)
		result = TET_PASS;
	else
		result = TET_FAIL;

final1:
	glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);

final:
	glsv_result(result);
}

void glsv_it_res_lck_async_10()
{
	int result;

	result = tet_test_lckInitialize(LCK_INIT_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final;

	result = tet_test_lckResourceOpen(
	    LCK_RESOURCE_OPEN_HDL1_NAME1_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	result = tet_test_lckResourceLockAsync(
	    LCK_RSC_LOCK_ASYNC_NO_QUEUE_PRLCK_T, TEST_NONCONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	m_GLSV_WAIT;

	result = tet_test_lckDispatch(LCK_DISPATCH_DISPATCH_ONE_SUCCESS_T,
				      TEST_NONCONFIG_MODE);

	if (result == TET_PASS && gl_gla_env.gr_clbk_invo == 710 &&
	    gl_gla_env.gr_clbk_err == SA_AIS_OK &&
	    gl_gla_env.gr_clbk_status == SA_LCK_LOCK_GRANTED)
		result = TET_PASS;
	else
		result = TET_FAIL;

final1:
	glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);

final:
	glsv_result(result);
}

void glsv_it_res_lck_async_11()
{
	int result;

	result = tet_test_lckInitialize(LCK_INIT_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final;

	result = tet_test_lckResourceOpen(
	    LCK_RESOURCE_OPEN_HDL1_NAME1_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	result = tet_test_lckResourceLockAsync(
	    LCK_RSC_LOCK_ASYNC_ORPHAN_PRLCK_T, TEST_NONCONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	m_GLSV_WAIT;

	result = tet_test_lckDispatch(LCK_DISPATCH_DISPATCH_ONE_SUCCESS_T,
				      TEST_NONCONFIG_MODE);

	if (result == TET_PASS && gl_gla_env.gr_clbk_invo == 712 &&
	    gl_gla_env.gr_clbk_err == SA_AIS_OK &&
	    gl_gla_env.gr_clbk_status == SA_LCK_LOCK_GRANTED)
		result = TET_PASS;
	else
		result = TET_FAIL;

	glsv_orphan_cleanup(LCK_CLEAN_RSC_LOCK_ORPHAN_PRLCK_T);

final1:
	glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);

final:
	glsv_result(result);
}

void glsv_it_res_lck_async_12()
{
	int result;

	result = tet_test_lckInitialize(LCK_INIT_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final;

	result = tet_test_lckResourceOpen(
	    LCK_RESOURCE_OPEN_HDL1_NAME1_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	result = tet_test_lckResourceLockAsync(
	    LCK_RSC_LOCK_ASYNC_EXLCK_SUCCESS_T, TEST_NONCONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	m_GLSV_WAIT;

	result = tet_test_lckDispatch(LCK_DISPATCH_DISPATCH_ONE_SUCCESS_T,
				      TEST_NONCONFIG_MODE);

	if (result == TET_PASS && gl_gla_env.gr_clbk_invo == 707 &&
	    gl_gla_env.gr_clbk_err == SA_AIS_OK &&
	    gl_gla_env.gr_clbk_status == SA_LCK_LOCK_GRANTED)
		result = TET_PASS;
	else
		result = TET_FAIL;

final1:
	glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);

final:
	glsv_result(result);
}

void glsv_it_res_lck_async_13()
{
	int result;

	result = tet_test_lckInitialize(LCK_INIT_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final;

	result = tet_test_lckResourceOpen(
	    LCK_RESOURCE_OPEN_HDL1_NAME1_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	result = tet_test_lckResourceLockAsync(
	    LCK_RSC_LOCK_ASYNC_NO_QUEUE_EXLCK_T, TEST_NONCONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	m_GLSV_WAIT;

	result = tet_test_lckDispatch(LCK_DISPATCH_DISPATCH_ONE_SUCCESS_T,
				      TEST_NONCONFIG_MODE);

	if (result == TET_PASS && gl_gla_env.gr_clbk_invo == 711 &&
	    gl_gla_env.gr_clbk_err == SA_AIS_OK &&
	    gl_gla_env.gr_clbk_status == SA_LCK_LOCK_GRANTED)
		result = TET_PASS;
	else
		result = TET_FAIL;

final1:
	glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);

final:
	glsv_result(result);
}

void glsv_it_res_lck_async_14()
{
	int result;

	result = tet_test_lckInitialize(LCK_INIT_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final;

	result = tet_test_lckResourceOpen(
	    LCK_RESOURCE_OPEN_HDL1_NAME1_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	result = tet_test_lckResourceLockAsync(
	    LCK_RSC_LOCK_ASYNC_ORPHAN_EXLCK_T, TEST_NONCONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	m_GLSV_WAIT;

	result = tet_test_lckDispatch(LCK_DISPATCH_DISPATCH_ONE_SUCCESS_T,
				      TEST_NONCONFIG_MODE);

	if (result == TET_PASS && gl_gla_env.gr_clbk_invo == 713 &&
	    gl_gla_env.gr_clbk_err == SA_AIS_OK &&
	    gl_gla_env.gr_clbk_status == SA_LCK_LOCK_GRANTED)
		result = TET_PASS;
	else
		result = TET_FAIL;

	glsv_orphan_cleanup(LCK_CLEAN_RSC_LOCK_ORPHAN_EXLCK_T);

final1:
	glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);

final:
	glsv_result(result);
}

void glsv_it_res_lck_async_15()
{
	int result;

	result = tet_test_lckInitialize(LCK_INIT_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final;

	result =
	    tet_test_lckInitialize(LCK_INIT_SUCCESS_HDL2_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	result = tet_test_lckResourceOpen(
	    LCK_RESOURCE_OPEN_HDL2_NAME1_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final2;

	result = tet_test_lckResourceLock(LCK_RSC_LOCK_EX_LOCK_SUCCESS_T,
					  TEST_CONFIG_MODE);
	if (result != TET_PASS ||
	    gl_gla_env.lck_status != SA_LCK_LOCK_GRANTED) {
		result = TET_UNRESOLVED;
		goto final2;
	}

	result = tet_test_lckResourceOpen(
	    LCK_RESOURCE_OPEN_RSC_EXIST_HDL1_NAME1_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final2;

	result = tet_test_lckResourceLockAsync(
	    LCK_RSC_LOCK_ASYNC_PRLCK_SUCCESS_T, TEST_NONCONFIG_MODE);
	if (result != TET_PASS)
		goto final2;

	sleep(15);

	result = tet_test_lckDispatch(LCK_DISPATCH_DISPATCH_ONE_SUCCESS_T,
				      TEST_NONCONFIG_MODE);

	if (result == TET_PASS && gl_gla_env.gr_clbk_invo == 706 &&
	    gl_gla_env.gr_clbk_err == SA_AIS_ERR_TIMEOUT)
		result = TET_PASS;
	else
		result = TET_FAIL;

final2:
	glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_HDL2_T);

final1:
	glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);

final:
	glsv_result(result);
}

void glsv_it_res_lck_async_16()
{
	int result;

	result = tet_test_lckInitialize(LCK_INIT_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final;

	result = tet_test_lckResourceOpen(
	    LCK_RESOURCE_OPEN_HDL1_NAME1_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	result = tet_test_lckResourceLock(LCK_RSC_LOCK_EX_LOCK_SUCCESS_T,
					  TEST_CONFIG_MODE);
	if (result != TET_PASS ||
	    gl_gla_env.lck_status != SA_LCK_LOCK_GRANTED) {
		result = TET_UNRESOLVED;
		goto final1;
	}

	result = tet_test_lckResourceLockAsync(
	    LCK_RSC_LOCK_ASYNC_PRLCK_SUCCESS_T, TEST_NONCONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	sleep(17);
	m_GLSV_WAIT;

	result = tet_test_lckDispatch(LCK_DISPATCH_DISPATCH_ALL_SUCCESS_T,
				      TEST_NONCONFIG_MODE);

	if (result == TET_PASS && gl_gla_env.waiter_sig == 5600 &&
	    gl_gla_env.waiter_clbk_lck_id == gl_gla_env.ex_lck_id &&
	    gl_gla_env.waiter_clbk_mode_held == SA_LCK_EX_LOCK_MODE &&
	    gl_gla_env.waiter_clbk_mode_req == SA_LCK_PR_LOCK_MODE &&
	    gl_gla_env.gr_clbk_invo == 706 &&
	    gl_gla_env.gr_clbk_err == SA_AIS_OK &&
	    gl_gla_env.gr_clbk_status == SA_LCK_LOCK_DEADLOCK)
		result = TET_PASS;
	else
		result = TET_FAIL;

final1:
	glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);

final:
	glsv_result(result);
}

void glsv_it_res_lck_async_17()
{
	int result;

	result = tet_test_lckInitialize(LCK_INIT_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final;

	result = tet_test_lckResourceOpen(
	    LCK_RESOURCE_OPEN_HDL1_NAME1_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	result = tet_test_lckResourceLock(LCK_RSC_LOCK_EX_LOCK_SUCCESS_T,
					  TEST_CONFIG_MODE);
	if (result != TET_PASS ||
	    gl_gla_env.lck_status != SA_LCK_LOCK_GRANTED) {
		result = TET_UNRESOLVED;
		goto final1;
	}

	result = tet_test_lckResourceLockAsync(
	    LCK_RSC_LOCK_ASYNC_EXLCK_SUCCESS_T, TEST_NONCONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	m_GLSV_WAIT;

	result = tet_test_lckDispatch(LCK_DISPATCH_DISPATCH_ONE_SUCCESS_T,
				      TEST_NONCONFIG_MODE);

	if (result == TET_PASS && gl_gla_env.gr_clbk_invo == 707 &&
	    gl_gla_env.gr_clbk_err == SA_AIS_OK &&
	    gl_gla_env.gr_clbk_status == SA_LCK_LOCK_DUPLICATE_EX)
		result = TET_PASS;
	else
		result = TET_FAIL;

final1:
	glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);

final:
	glsv_result(result);
}

void glsv_it_res_lck_async_18()
{
	int result;

	result = tet_test_lckInitialize(LCK_INIT_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final;

	result =
	    tet_test_lckInitialize(LCK_INIT_SUCCESS_HDL2_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	result = tet_test_lckResourceOpen(
	    LCK_RESOURCE_OPEN_HDL2_NAME1_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final2;

	result = tet_test_lckResourceLock(LCK_RSC_LOCK_EX_LOCK_SUCCESS_T,
					  TEST_CONFIG_MODE);
	if (result != TET_PASS ||
	    gl_gla_env.lck_status != SA_LCK_LOCK_GRANTED) {
		result = TET_UNRESOLVED;
		goto final2;
	}

	result = tet_test_lckResourceOpen(
	    LCK_RESOURCE_OPEN_HDL1_NAME1_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final2;

	result = tet_test_lckResourceLockAsync(
	    LCK_RSC_LOCK_ASYNC_NO_QUEUE_PRLCK_T, TEST_NONCONFIG_MODE);
	if (result != TET_PASS)
		goto final2;

	m_GLSV_WAIT;

	result = tet_test_lckDispatch(LCK_DISPATCH_DISPATCH_ONE_SUCCESS_T,
				      TEST_NONCONFIG_MODE);

	if (result == TET_PASS && gl_gla_env.gr_clbk_invo == 710 &&
	    gl_gla_env.gr_clbk_err == SA_AIS_OK &&
	    gl_gla_env.gr_clbk_status == SA_LCK_LOCK_NOT_QUEUED)
		result = TET_PASS;
	else
		result = TET_FAIL;

final2:
	glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_HDL2_T);

final1:
	glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);

final:
	glsv_result(result);
}

void glsv_it_res_lck_async_19()
{
	int result;

	result = tet_test_lckInitialize(LCK_INIT_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final;

	result = tet_test_lckResourceOpen(
	    LCK_RESOURCE_OPEN_HDL1_NAME1_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	result = tet_test_lckResourceLockAsync(
	    LCK_RSC_LOCK_ASYNC_EXLCK_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	sleep(2);

	result = tet_test_lckResourceUnlock(LCK_RSC_UNLOCK_LCKID1_SUCCESS_T,
					    TEST_NONCONFIG_MODE);

final1:
	glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);

final:
	glsv_result(result);
}

void glsv_it_res_lck_async_20()
{
	int result;

	result = tet_test_lckInitialize(LCK_INIT_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final;

	result = tet_test_lckResourceOpen(
	    LCK_RESOURCE_OPEN_HDL1_NAME1_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	result = tet_test_lckResourceLockAsync(
	    LCK_RSC_LOCK_ASYNC_EXLCK_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	result = tet_test_lckResourceUnlockAsync(LCK_RSC_UNLOCK_ASYNC_SUCCESS_T,
						 TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	m_GLSV_WAIT;

	result = tet_test_lckDispatch(LCK_DISPATCH_DISPATCH_ALL_SUCCESS_T,
				      TEST_CONFIG_MODE);
	if (result == TET_PASS && gl_gla_env.unlck_clbk_invo == 904 &&
	    gl_gla_env.unlck_clbk_err == SA_AIS_OK)
		result = TET_PASS;
	else
		result = TET_FAIL;

final1:
	glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);

final:
	glsv_result(result);
}

/*********** saLckResourceUnlock Api Tests ************/

void glsv_it_res_unlck_01()
{
	int result;

	result = tet_test_lckResourceUnlock(LCK_RSC_UNLOCK_BAD_LOCKID_T,
					    TEST_NONCONFIG_MODE);
	glsv_result(result);
}

void glsv_it_res_unlck_02()
{
	int result;

	result = tet_test_lckInitialize(LCK_INIT_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final;

	result = tet_test_lckResourceOpen(
	    LCK_RESOURCE_OPEN_HDL1_NAME1_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	result = tet_test_lckResourceLock(LCK_RSC_LOCK_EX_LOCK_SUCCESS_T,
					  TEST_CONFIG_MODE);
	if (result != TET_PASS || gl_gla_env.lck_status != SA_LCK_LOCK_GRANTED)
		goto final1;

	result = tet_test_lckResourceClose(
	    LCK_RESOURCE_CLOSE_RSC_HDL1_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	result = tet_test_lckResourceUnlock(LCK_RSC_UNLOCK_UNLOCKED_LOCKID_T,
					    TEST_NONCONFIG_MODE);

final1:
	glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);

final:
	glsv_result(result);
}

void glsv_it_res_unlck_03()
{
	int result;

	result = tet_test_lckInitialize(LCK_INIT_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final;

	result = tet_test_lckResourceOpen(
	    LCK_RESOURCE_OPEN_HDL1_NAME1_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS) {
		glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);
		goto final;
	}

	result = tet_test_lckResourceLock(LCK_RSC_LOCK_EX_LOCK_SUCCESS_T,
					  TEST_CONFIG_MODE);
	if (result != TET_PASS ||
	    gl_gla_env.lck_status != SA_LCK_LOCK_GRANTED) {
		glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);
		goto final;
	}

	result = tet_test_lckFinalize(LCK_FINALIZE_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS) {
		glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);
		goto final;
	}

	result = tet_test_lckResourceUnlock(LCK_RSC_FINALIZED_UNLOCKED_LOCKID_T,
					    TEST_NONCONFIG_MODE);

final:
	glsv_result(result);
}

void glsv_it_res_unlck_04()
{
	int result;

	result = tet_test_lckInitialize(LCK_INIT_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final;

	result = tet_test_lckResourceOpen(
	    LCK_RESOURCE_OPEN_HDL1_NAME1_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	result = tet_test_lckResourceLock(LCK_RSC_LOCK_EX_LOCK_SUCCESS_T,
					  TEST_CONFIG_MODE);
	if (result != TET_PASS || gl_gla_env.lck_status != SA_LCK_LOCK_GRANTED)
		goto final1;

	result = tet_test_lckResourceUnlock(LCK_RSC_UNLOCK_ERR_TIMEOUT_T,
					    TEST_NONCONFIG_MODE);

final1:
	glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);

final:
	glsv_result(result);
}

void glsv_it_res_unlck_05()
{
	int result;

	result = tet_test_lckInitialize(LCK_INIT_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final;

	result = tet_test_lckResourceOpen(
	    LCK_RESOURCE_OPEN_HDL1_NAME1_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	result = tet_test_lckResourceLock(LCK_RSC_LOCK_PR_LOCK_SUCCESS_T,
					  TEST_CONFIG_MODE);
	if (result != TET_PASS || gl_gla_env.lck_status != SA_LCK_LOCK_GRANTED)
		goto final1;
	result = tet_test_lckResourceUnlock(LCK_RSC_UNLOCK_LCKID2_SUCCESS_T,
					    TEST_NONCONFIG_MODE);

final1:
	glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);

final:
	glsv_result(result);
}

void glsv_it_res_unlck_06()
{
	int result;

	result = tet_test_lckInitialize(LCK_INIT_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final;

	result =
	    tet_test_lckInitialize(LCK_INIT_SUCCESS_HDL2_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	result = tet_test_lckResourceOpen(
	    LCK_RESOURCE_OPEN_HDL1_NAME1_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final2;

	result = tet_test_lckResourceLock(LCK_RSC_LOCK_PR_LOCK_SUCCESS_T,
					  TEST_CONFIG_MODE);
	if (result != TET_PASS || gl_gla_env.lck_status != SA_LCK_LOCK_GRANTED)
		goto final2;

	result = tet_test_lckResourceOpen(
	    LCK_RESOURCE_OPEN_HDL2_NAME1_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final2;

	result = tet_test_lckResourceLockAsync(
	    LCK_RSC_LOCK_ASYNC_EXLCK_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final2;

	result = tet_test_lckResourceUnlock(LCK_RSC_UNLOCK_LCKID1_SUCCESS_T,
					    TEST_NONCONFIG_MODE);
	if (result != TET_PASS)
		goto final2;

	m_GLSV_WAIT;

	result = tet_test_lckDispatch(LCK_DISPATCH_DISPATCH_ALL_SUCCESS2_T,
				      TEST_NONCONFIG_MODE);
	if (result == TET_PASS && gl_gla_env.gr_clbk_invo == 707 &&
	    gl_gla_env.gr_clbk_err == SA_AIS_ERR_NOT_EXIST &&
	    gl_gla_env.gr_clbk_status == 0)
		result = TET_PASS;
	else
		result = TET_FAIL;

final2:
	glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_HDL2_T);

final1:
	glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);

final:
	glsv_result(result);
}

void glsv_it_res_unlck_07()
{
	int result;

	result = tet_test_lckInitialize(LCK_INIT_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final;

	result = tet_test_lckResourceOpen(
	    LCK_RESOURCE_OPEN_HDL1_NAME1_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	result = tet_test_lckResourceLock(LCK_RSC_LOCK_EX_LOCK_SUCCESS_T,
					  TEST_CONFIG_MODE);
	if (result != TET_PASS || gl_gla_env.lck_status != SA_LCK_LOCK_GRANTED)
		goto final1;

	result = tet_test_lckResourceUnlock(LCK_RSC_UNLOCK_LCKID1_SUCCESS_T,
					    TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	result = tet_test_lckResourceUnlock(LCK_RSC_UNLOCK_UNLOCKED_LOCKID_T,
					    TEST_NONCONFIG_MODE);

final1:
	glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);

final:
	glsv_result(result);
}

void glsv_it_res_unlck_08()
{
	int result;

	result = tet_test_lckInitialize(LCK_INIT_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final;

	result = tet_test_lckResourceOpen(
	    LCK_RESOURCE_OPEN_HDL1_NAME1_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	result = tet_test_lckResourceLockAsync(
	    LCK_RSC_LOCK_ASYNC_EXLCK_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	sleep(2);

	result = tet_test_lckResourceUnlock(LCK_RSC_UNLOCK_LCKID1_SUCCESS_T,
					    TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	m_GLSV_WAIT;

	result = tet_test_lckDispatch(LCK_DISPATCH_DISPATCH_ALL_SUCCESS_T,
				      TEST_NONCONFIG_MODE);
	if (result == TET_PASS && gl_gla_env.gr_clbk_invo == 707 &&
	    gl_gla_env.gr_clbk_err == SA_AIS_ERR_NOT_EXIST &&
	    gl_gla_env.gr_clbk_status == 0)
		result = TET_PASS;
	else
		result = TET_FAIL;

final1:
	glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);

final:
	glsv_result(result);
}

/*********** saLckResourceUnlockAsync Api Tests ************/

void glsv_it_res_unlck_async_01()
{
	int result;

	result = tet_test_lckResourceUnlockAsync(
	    LCK_RSC_UNLOCK_ASYNC_BAD_LOCKID_T, TEST_NONCONFIG_MODE);
	glsv_result(result);
}

void glsv_it_res_unlck_async_02()
{
	int result;

	result = tet_test_lckInitialize(LCK_INIT_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final;

	result = tet_test_lckResourceOpen(
	    LCK_RESOURCE_OPEN_HDL1_NAME1_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	result = tet_test_lckResourceLock(LCK_RSC_LOCK_EX_LOCK_SUCCESS_T,
					  TEST_CONFIG_MODE);
	if (result != TET_PASS || gl_gla_env.lck_status != SA_LCK_LOCK_GRANTED)
		goto final1;

	result = tet_test_lckResourceClose(
	    LCK_RESOURCE_CLOSE_RSC_HDL1_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	result = tet_test_lckResourceUnlockAsync(
	    LCK_RSC_UNLOCK_ASYNC_UNLOCKED_LOCKID_T, TEST_NONCONFIG_MODE);

final1:
	glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);

final:
	glsv_result(result);
}

void glsv_it_res_unlck_async_03()
{
	int result;

	result = tet_test_lckInitialize(LCK_INIT_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final;

	result = tet_test_lckResourceOpen(
	    LCK_RESOURCE_OPEN_HDL1_NAME1_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS) {
		glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);
		goto final;
	}

	result = tet_test_lckResourceLock(LCK_RSC_LOCK_EX_LOCK_SUCCESS_T,
					  TEST_CONFIG_MODE);
	if (result != TET_PASS ||
	    gl_gla_env.lck_status != SA_LCK_LOCK_GRANTED) {
		glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);
		goto final;
	}

	result = tet_test_lckFinalize(LCK_FINALIZE_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS) {
		glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);
		goto final;
	}

	result = tet_test_lckResourceUnlockAsync(
	    LCK_RSC_FINALIZED_ASYNC_UNLOCKED_LOCKID_T, TEST_NONCONFIG_MODE);

final:
	glsv_result(result);
}

void glsv_it_res_unlck_async_04()
{
	int result;

	result =
	    tet_test_lckInitialize(LCK_INIT_NULL_CBKS2_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final;

	result = tet_test_lckResourceOpen(
	    LCK_RESOURCE_OPEN_HDL1_NAME1_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	result = tet_test_lckResourceLock(LCK_RSC_LOCK_EX_LOCK_SUCCESS_T,
					  TEST_CONFIG_MODE);
	if (result != TET_PASS || gl_gla_env.lck_status != SA_LCK_LOCK_GRANTED)
		goto final1;

	result = tet_test_lckResourceUnlockAsync(
	    LCK_RSC_UNLOCK_ASYNC_ERR_INIT_T, TEST_NONCONFIG_MODE);

final1:
	glsv_init_cleanup(LCK_CLEAN_INIT_NULL_CBKS2_T);

final:
	glsv_result(result);
}

void glsv_it_res_unlck_async_05()
{
	int result;

	result = tet_test_lckInitialize(LCK_INIT_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final;

	result = tet_test_lckResourceOpen(
	    LCK_RESOURCE_OPEN_HDL1_NAME1_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	result = tet_test_lckResourceLock(LCK_RSC_LOCK_EX_LOCK_SUCCESS_T,
					  TEST_CONFIG_MODE);
	if (result != TET_PASS || gl_gla_env.lck_status != SA_LCK_LOCK_GRANTED)
		goto final1;

	result = tet_test_lckResourceUnlockAsync(LCK_RSC_UNLOCK_ASYNC_SUCCESS_T,
						 TEST_NONCONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	m_GLSV_WAIT;

	result = tet_test_lckDispatch(LCK_DISPATCH_DISPATCH_ONE_SUCCESS_T,
				      TEST_NONCONFIG_MODE);

	if (result == TET_PASS && gl_gla_env.unlck_clbk_invo == 904 &&
	    gl_gla_env.unlck_clbk_err == SA_AIS_OK)
		result = TET_PASS;
	else
		result = TET_FAIL;

final1:
	glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);

final:
	glsv_result(result);
}

void glsv_it_res_unlck_async_06()
{
	int result;

	result = tet_test_lckInitialize(LCK_INIT_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final;

	result =
	    tet_test_lckInitialize(LCK_INIT_SUCCESS_HDL2_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	result = tet_test_lckResourceOpen(
	    LCK_RESOURCE_OPEN_HDL1_NAME1_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final2;

	result = tet_test_lckResourceLock(LCK_RSC_LOCK_PR_LOCK_SUCCESS_T,
					  TEST_CONFIG_MODE);
	if (result != TET_PASS || gl_gla_env.lck_status != SA_LCK_LOCK_GRANTED)
		goto final2;

	result = tet_test_lckResourceOpen(
	    LCK_RESOURCE_OPEN_HDL2_NAME1_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final2;

	result = tet_test_lckResourceLockAsync(
	    LCK_RSC_LOCK_ASYNC_EXLCK_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final2;

	result = tet_test_lckResourceUnlockAsync(LCK_RSC_UNLOCK_ASYNC_SUCCESS_T,
						 TEST_NONCONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	m_GLSV_WAIT;

	result = tet_test_lckDispatch(LCK_DISPATCH_DISPATCH_ALL_SUCCESS2_T,
				      TEST_NONCONFIG_MODE);

	if (result == TET_PASS && gl_gla_env.unlck_clbk_invo == 904 &&
	    gl_gla_env.unlck_clbk_err == SA_AIS_OK &&
	    gl_gla_env.gr_clbk_invo == 707 &&
	    gl_gla_env.gr_clbk_err == SA_AIS_ERR_NOT_EXIST &&
	    gl_gla_env.gr_clbk_status == 0)
		result = TET_PASS;
	else
		result = TET_FAIL;

final2:
	glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_HDL2_T);

final1:
	glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);

final:
	glsv_result(result);
}

void glsv_it_res_unlck_async_07()
{
	int result;

	result = tet_test_lckInitialize(LCK_INIT_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final;

	result = tet_test_lckResourceOpen(
	    LCK_RESOURCE_OPEN_HDL1_NAME1_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	result = tet_test_lckResourceLock(LCK_RSC_LOCK_EX_LOCK_SUCCESS_T,
					  TEST_CONFIG_MODE);
	if (result != TET_PASS || gl_gla_env.lck_status != SA_LCK_LOCK_GRANTED)
		goto final1;

	result = tet_test_lckResourceUnlock(LCK_RSC_UNLOCK_LCKID1_SUCCESS_T,
					    TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	result = tet_test_lckResourceUnlockAsync(
	    LCK_RSC_UNLOCK_ASYNC_UNLOCKED_LOCKID_T, TEST_NONCONFIG_MODE);

final1:
	glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);

final:
	glsv_result(result);
}

void glsv_it_res_unlck_async_08()
{
	int result;

	result = tet_test_lckInitialize(LCK_INIT_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final;

	result = tet_test_lckResourceOpen(
	    LCK_RESOURCE_OPEN_HDL1_NAME1_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	result = tet_test_lckResourceLockAsync(
	    LCK_RSC_LOCK_ASYNC_EXLCK_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	sleep(2);

	result = tet_test_lckResourceUnlockAsync(LCK_RSC_UNLOCK_ASYNC_SUCCESS_T,
						 TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	m_GLSV_WAIT;

	result = tet_test_lckDispatch(LCK_DISPATCH_DISPATCH_ALL_SUCCESS_T,
				      TEST_NONCONFIG_MODE);
	if (result == TET_PASS && gl_gla_env.unlck_clbk_invo == 904 &&
	    gl_gla_env.unlck_clbk_err == SA_AIS_OK &&
	    gl_gla_env.gr_clbk_invo == 707 &&
	    gl_gla_env.gr_clbk_err == SA_AIS_OK)
		result = TET_PASS;
	else
		result = TET_FAIL;

final1:
	glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);

final:
	glsv_result(result);
}

void glsv_it_res_unlck_async_09()
{
	int result;

	result = tet_test_lckInitialize(LCK_INIT_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final;

	result = tet_test_lckResourceOpen(
	    LCK_RESOURCE_OPEN_HDL1_NAME1_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	result = tet_test_lckResourceLock(LCK_RSC_LOCK_EX_LOCK_SUCCESS_T,
					  TEST_CONFIG_MODE);
	if (result != TET_PASS || gl_gla_env.lck_status != SA_LCK_LOCK_GRANTED)
		goto final1;

	result = tet_test_lckResourceUnlockAsync(LCK_RSC_UNLOCK_ASYNC_SUCCESS_T,
						 TEST_NONCONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	sleep(2);

	result = tet_test_lckResourceUnlock(
	    LCK_RSC_UNLOCK_ASYNC_UNLOCKED_LOCKID_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	m_GLSV_WAIT;

	result = tet_test_lckDispatch(LCK_DISPATCH_DISPATCH_ALL_SUCCESS_T,
				      TEST_NONCONFIG_MODE);
	if (result == TET_PASS && gl_gla_env.unlck_clbk_invo == 904 &&
	    gl_gla_env.unlck_clbk_err == SA_AIS_OK)
		result = TET_PASS;
	else
		result = TET_FAIL;

final1:
	glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);

final:
	glsv_result(result);
}

void glsv_it_res_unlck_async_10()
{
	int result;

	result = tet_test_lckInitialize(LCK_INIT_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final;

	result = tet_test_lckResourceOpen(
	    LCK_RESOURCE_OPEN_HDL1_NAME1_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	result = tet_test_lckResourceLock(LCK_RSC_LOCK_EX_LOCK_SUCCESS_T,
					  TEST_CONFIG_MODE);
	if (result != TET_PASS || gl_gla_env.lck_status != SA_LCK_LOCK_GRANTED)
		goto final1;

	result = tet_test_lckResourceUnlockAsync(LCK_RSC_UNLOCK_ASYNC_SUCCESS_T,
						 TEST_NONCONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	sleep(2);

	result = tet_test_lckResourceClose(
	    LCK_RESOURCE_CLOSE_RSC_HDL1_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	m_GLSV_WAIT;

	result = tet_test_lckDispatch(LCK_DISPATCH_DISPATCH_ALL_SUCCESS_T,
				      TEST_NONCONFIG_MODE);
	if (result == TET_PASS && gl_gla_env.unlck_clbk_invo == 904 &&
	    gl_gla_env.unlck_clbk_err == SA_AIS_ERR_NOT_EXIST)
		result = TET_PASS;
	else
		result = TET_FAIL;

final1:
	glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);

final:
	glsv_result(result);
}

/*********** saLckLockPurge Api Tests ************/

void glsv_it_lck_purge_01()
{
	int result;

	result = tet_test_lckLockPurge(LCK_LOCK_PURGE_BAD_HDL_T,
				       TEST_NONCONFIG_MODE);
	glsv_result(result);
}

void glsv_it_lck_purge_02()
{
	int result;

	result = tet_test_lckInitialize(LCK_INIT_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final;

	result = tet_test_lckResourceOpen(
	    LCK_RESOURCE_OPEN_HDL1_NAME1_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	result = tet_test_lckResourceClose(
	    LCK_RESOURCE_CLOSE_RSC_HDL1_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	result = tet_test_lckLockPurge(LCK_LOCK_PURGE_CLOSED_HDL_T,
				       TEST_NONCONFIG_MODE);

final1:
	glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);

final:
	glsv_result(result);
}

void glsv_it_lck_purge_03()
{
	int result;

	result = tet_test_lckInitialize(LCK_INIT_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final;

	result = tet_test_lckResourceOpen(
	    LCK_RESOURCE_OPEN_HDL1_NAME1_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS) {
		glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);
		goto final;
	}

	result = tet_test_lckFinalize(LCK_FINALIZE_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS) {
		glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);
		goto final;
	}

	result = tet_test_lckLockPurge(LCK_LOCK_PURGE_FINALIZED_HDL_T,
				       TEST_NONCONFIG_MODE);

final:
	glsv_result(result);
}

void glsv_it_lck_purge_04()
{
	int result;

	result = tet_test_lckInitialize(LCK_INIT_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final;

	result = tet_test_lckResourceOpen(
	    LCK_RESOURCE_OPEN_HDL1_NAME1_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	result = tet_test_lckLockPurge(LCK_LOCK_PURGE_NO_ORPHAN_T,
				       TEST_NONCONFIG_MODE);

final1:
	glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);

final:
	glsv_result(result);
}

void glsv_it_lck_purge_05()
{
	int result;

	result = tet_test_lckInitialize(LCK_INIT_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final;

	result = tet_test_lckResourceOpen(
	    LCK_RESOURCE_OPEN_HDL1_NAME1_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS) {
		glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);
		goto final;
	}

	result = tet_test_lckResourceLock(LCK_RSC_LOCK_ORPHAN_PRLCK_T,
					  TEST_CONFIG_MODE);
	if (result != TET_PASS ||
	    gl_gla_env.lck_status != SA_LCK_LOCK_GRANTED) {
		glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);
		goto final;
	}

	result = tet_test_lckFinalize(LCK_FINALIZE_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS) {
		glsv_orphan_cleanup(LCK_CLEAN_RSC_LOCK_ORPHAN_PRLCK_T);
		glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);
		goto final;
	}

	result =
	    tet_test_lckInitialize(LCK_INIT_SUCCESS_HDL2_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final;

	result = tet_test_lckResourceOpen(
	    LCK_RESOURCE_OPEN_HDL2_NAME1_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	result = tet_test_lckResourceLock(LCK_RSC_LOCK_EX_LOCK_ORPHANED_T,
					  TEST_CONFIG_MODE);
	if (result != TET_PASS ||
	    gl_gla_env.lck_status != SA_LCK_LOCK_ORPHANED) {
		result = TET_UNRESOLVED;
		goto final1;
	}

	result = tet_test_lckLockPurge(LCK_LOCK_PURGE_SUCCESS_T,
				       TEST_NONCONFIG_MODE);

	gl_gla_env.lck_status = 0;

	result = tet_test_lckResourceLock(LCK_RSC_LOCK_EX_LOCK_SUCCESS_T,
					  TEST_NONCONFIG_MODE);
	if (result == TET_PASS || gl_gla_env.lck_status == SA_LCK_LOCK_GRANTED)
		result = TET_PASS;
	else
		result = TET_FAIL;

final1:
	glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_HDL2_T);

final:
	glsv_result(result);
}

/*********** SINGLE NODE Functionality Tests **************/

/* Creation and Deletion of Resources */

void glsv_it_res_cr_del_01()
{
	int result;

	result = tet_test_lckInitialize(LCK_INIT_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final;

	result = tet_test_lckResourceOpen(
	    LCK_RESOURCE_OPEN_HDL1_NAME1_SUCCESS_T, TEST_NONCONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	result = tet_test_lckResourceOpen(
	    LCK_RESOURCE_OPEN_HDL1_NAME2_SUCCESS_T, TEST_NONCONFIG_MODE);

final1:
	glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);

final:
	glsv_result(result);
}

void glsv_it_res_cr_del_02()
{
	int result;

	result = tet_test_lckInitialize(LCK_INIT_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final;

	result = tet_test_lckResourceOpenAsync(
	    LCK_RESOURCE_OPEN_ASYNC_HDL1_NAME1_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	m_GLSV_WAIT;

	result = tet_test_lckDispatch(LCK_DISPATCH_DISPATCH_ONE_SUCCESS_T,
				      TEST_CONFIG_MODE);
	if (result != TET_PASS || gl_gla_env.open_clbk_invo != 208 ||
	    gl_gla_env.open_clbk_err != SA_AIS_OK) {
		result = TET_FAIL;
		goto final1;
	}

	glsv_clean_clbk_params();

	result = tet_test_lckResourceOpenAsync(
	    LCK_RESOURCE_OPEN_ASYNC_HDL1_NAME2_SUCCESS_T, TEST_NONCONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	m_GLSV_WAIT;

	result = tet_test_lckDispatch(LCK_DISPATCH_DISPATCH_ONE_SUCCESS_T,
				      TEST_NONCONFIG_MODE);

	if (result != TET_PASS || gl_gla_env.open_clbk_invo != 209 ||
	    gl_gla_env.open_clbk_err != SA_AIS_OK)
		result = TET_FAIL;
	else
		result = TET_PASS;

final1:
	glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);

final:
	glsv_result(result);
}

void glsv_it_res_cr_del_03()
{
	int result;
	SaLckResourceHandleT lcl_res_hdl1;

	result = tet_test_lckInitialize(LCK_INIT_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final;

	result = tet_test_lckResourceOpenAsync(
	    LCK_RESOURCE_OPEN_ASYNC_HDL1_NAME1_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	m_GLSV_WAIT;

	result = tet_test_lckDispatch(LCK_DISPATCH_DISPATCH_ONE_SUCCESS_T,
				      TEST_CONFIG_MODE);
	if (result != TET_PASS || gl_gla_env.open_clbk_invo != 208 ||
	    gl_gla_env.open_clbk_err != SA_AIS_OK) {
		result = TET_FAIL;
		goto final1;
	}

	result = tet_test_lckResourceOpen(
	    LCK_RESOURCE_OPEN_NAME1_EXIST_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	lcl_res_hdl1 = gl_gla_env.res_hdl1;

	result = tet_test_lckResourceOpen(
	    LCK_RESOURCE_OPEN_RSC_EXIST_HDL1_NAME1_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	result = tet_test_lckResourceClose(
	    LCK_RESOURCE_CLOSE_RSC_HDL1_SUCCESS_T, TEST_NONCONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	gl_gla_env.res_hdl1 = gl_gla_env.open_clbk_res_hdl;

	result = tet_test_lckResourceClose(
	    LCK_RESOURCE_CLOSE_RSC_HDL1_SUCCESS_T, TEST_NONCONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	gl_gla_env.res_hdl1 = lcl_res_hdl1;

	result = tet_test_lckResourceClose(
	    LCK_RESOURCE_CLOSE_RSC_HDL1_SUCCESS_T, TEST_NONCONFIG_MODE);

final1:
	glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);

final:
	glsv_result(result);
}

void glsv_it_res_cr_del_04()
{
	int result;

	result = tet_test_lckInitialize(LCK_INIT_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final;

	result = tet_test_lckResourceOpenAsync(
	    LCK_RESOURCE_OPEN_ASYNC_RSC_NOT_EXIST_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	m_GLSV_WAIT;

	result = tet_test_lckDispatch(LCK_DISPATCH_DISPATCH_ONE_SUCCESS_T,
				      TEST_CONFIG_MODE);
	if (result != TET_PASS || gl_gla_env.open_clbk_invo != 206 ||
	    gl_gla_env.open_clbk_err != SA_AIS_ERR_NOT_EXIST) {
		result = TET_FAIL;
		goto final1;
	}

	gl_gla_env.res_hdl1 = gl_gla_env.open_clbk_res_hdl;

	result = tet_test_lckResourceLock(LCK_RSC_LOCK_BAD_HDL_T,
					  TEST_NONCONFIG_MODE);

final1:
	glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);

final:
	glsv_result(result);
}

void glsv_it_res_cr_del_05()
{
	int result;
	int i = 0;

	gl_open_clbk_iter = 0;

	result = tet_test_lckInitialize(LCK_INIT_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final;

	glsv_createthread(&gl_gla_env.lck_hdl1);

	for (i = 0; i < 10; i++) {
		result = tet_test_lckResourceOpenAsync(
		    LCK_RESOURCE_OPEN_ASYNC_HDL1_NAME1_SUCCESS_T,
		    TEST_CONFIG_MODE);
		if (result != TET_PASS)
			break;

		m_GLSV_WAIT;
	}

	if (result != TET_PASS)
		goto final1;

	if (gl_open_clbk_iter == 10)
		result = TET_PASS;
	else
		result = TET_FAIL;

final1:
	glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);

final:
	glsv_result(result);
}

static void glsv_it_res_cr_del_06_sync_async(int async)
{
	int result;
	SaLckResourceHandleT lcl_res_hdl1, lcl_res_hdl2;

	gl_glsv_async = async;

	result = tet_test_lckInitialize(LCK_INIT_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final;

	result = tet_test_lckResourceOpen(
	    LCK_RESOURCE_OPEN_HDL1_NAME1_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	lcl_res_hdl1 = gl_gla_env.res_hdl1;

	result = tet_test_lckResourceOpenAsync(
	    LCK_RESOURCE_OPEN_ASYNC_HDL1_NAME1_EXIST_SUCCESS_T,
	    TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	m_GLSV_WAIT;

	result = tet_test_lckDispatch(LCK_DISPATCH_DISPATCH_ONE_SUCCESS_T,
				      TEST_CONFIG_MODE);
	if (result != TET_PASS || gl_gla_env.open_clbk_invo != 212 ||
	    gl_gla_env.open_clbk_err != SA_AIS_OK) {
		result = TET_UNRESOLVED;
		goto final1;
	}

	lcl_res_hdl2 = gl_gla_env.open_clbk_res_hdl;

	if (gl_glsv_async == 1) {
		gl_gla_env.res_hdl1 = lcl_res_hdl2;

		result = tet_test_lckResourceLockAsync(
		    LCK_RSC_LOCK_ASYNC_EXLCK_SUCCESS_T, TEST_CONFIG_MODE);
		if (result != TET_PASS)
			goto final1;

		m_GLSV_WAIT;

		result = tet_test_lckDispatch(
		    LCK_DISPATCH_DISPATCH_ONE_SUCCESS_T, TEST_CONFIG_MODE);
		if (result != TET_PASS || gl_gla_env.gr_clbk_invo != 707 ||
		    gl_gla_env.gr_clbk_err != SA_AIS_OK ||
		    gl_gla_env.gr_clbk_status != SA_LCK_LOCK_GRANTED) {
			result = TET_UNRESOLVED;
			goto final1;
		}
	} else {
		gl_gla_env.res_hdl1 = lcl_res_hdl2;

		result = tet_test_lckResourceLock(
		    LCK_RSC_LOCK_EX_LOCK_SUCCESS_T, TEST_CONFIG_MODE);
		if (result != TET_PASS ||
		    gl_gla_env.lck_status != SA_LCK_LOCK_GRANTED) {
			result = TET_FAIL;
			goto final1;
		}
	}

	gl_gla_env.res_hdl1 = lcl_res_hdl1;

	result = tet_test_lckResourceClose(
	    LCK_RESOURCE_CLOSE_RSC_HDL1_SUCCESS_T, TEST_NONCONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	result = tet_test_lckResourceUnlock(LCK_RSC_UNLOCK_LCKID1_SUCCESS_T,
					    TEST_NONCONFIG_MODE);

final1:
	glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);

final:
	glsv_result(result);
}

void glsv_it_res_cr_del_06(void)
{
  glsv_it_res_cr_del_06_sync_async(0);
}

void glsv_it_res_cr_del_07(void)
{
  glsv_it_res_cr_del_06_sync_async(1);
}

/* Lock Modes and Lock Waiter Callback */

static void glsv_it_lck_modes_wt_clbk_01_sync_async(int async)
{
	int result;

	gl_glsv_async = async;

	result = tet_test_lckInitialize(LCK_INIT_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final;

	result = tet_test_lckResourceOpen(
	    LCK_RESOURCE_OPEN_HDL1_NAME1_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	if (gl_glsv_async == 1) {
		glsv_createthread(&gl_gla_env.lck_hdl1);

		result = tet_test_lckResourceLockAsync(
		    LCK_RSC_LOCK_ASYNC_PRLCK_SUCCESS_T, TEST_NONCONFIG_MODE);
		if (result != TET_PASS)
			goto final1;

		m_GLSV_WAIT;

		if (gl_gla_env.gr_clbk_invo != 706 ||
		    gl_gla_env.gr_clbk_err != SA_AIS_OK ||
		    gl_gla_env.gr_clbk_status != SA_LCK_LOCK_GRANTED) {
			result = TET_FAIL;
			goto final1;
		}

		glsv_clean_clbk_params();

		result = tet_test_lckResourceLockAsync(
		    LCK_RSC_LOCK_ASYNC_PRLCK_SUCCESS_T, TEST_NONCONFIG_MODE);
		if (result != TET_PASS)
			goto final1;

		m_GLSV_WAIT;

		if (gl_gla_env.gr_clbk_invo != 706 ||
		    gl_gla_env.gr_clbk_err != SA_AIS_OK ||
		    gl_gla_env.gr_clbk_status != SA_LCK_LOCK_GRANTED) {
			result = TET_FAIL;
			goto final1;
		}

		glsv_clean_clbk_params();

		result = tet_test_lckResourceLockAsync(
		    LCK_RSC_LOCK_ASYNC_PRLCK_SUCCESS_T, TEST_NONCONFIG_MODE);
		if (result != TET_PASS)
			goto final1;

		m_GLSV_WAIT;

		if (gl_gla_env.gr_clbk_invo != 706 ||
		    gl_gla_env.gr_clbk_err != SA_AIS_OK ||
		    gl_gla_env.gr_clbk_status != SA_LCK_LOCK_GRANTED)
			result = TET_FAIL;
	} else {
		result = tet_test_lckResourceLock(
		    LCK_RSC_LOCK_PR_LOCK_SUCCESS_T, TEST_NONCONFIG_MODE);
		if (result != TET_PASS ||
		    gl_gla_env.lck_status != SA_LCK_LOCK_GRANTED) {
			result = TET_FAIL;
			goto final1;
		}

		gl_gla_env.lck_status = 0;
		result = tet_test_lckResourceLock(
		    LCK_RSC_LOCK_PR_LOCK_SUCCESS_T, TEST_NONCONFIG_MODE);
		if (result != TET_PASS ||
		    gl_gla_env.lck_status != SA_LCK_LOCK_GRANTED) {
			result = TET_FAIL;
			goto final1;
		}

		gl_gla_env.lck_status = 0;
		result = tet_test_lckResourceLock(
		    LCK_RSC_LOCK_PR_LOCK_SUCCESS_T, TEST_NONCONFIG_MODE);
		if (result != TET_PASS ||
		    gl_gla_env.lck_status != SA_LCK_LOCK_GRANTED)
			result = TET_FAIL;
	}

final1:
	glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);

final:
	glsv_result(result);
}

void glsv_it_lck_modes_wt_clbk_01(void)
{
  glsv_it_lck_modes_wt_clbk_01_sync_async(true);
}

void glsv_it_lck_modes_wt_clbk_02(void)
{
  glsv_it_lck_modes_wt_clbk_01_sync_async(false);
}

static void glsv_it_lck_modes_wt_clbk_03_sync_async(int async)
{
	int result;
	SaLckResourceHandleT lcl_res_hdl1, lcl_res_hdl2;

	gl_glsv_async = async;

	result = tet_test_lckInitialize(LCK_INIT_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final;

	result =
	    tet_test_lckInitialize(LCK_INIT_SUCCESS_HDL2_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	result = tet_test_lckResourceOpen(
	    LCK_RESOURCE_OPEN_HDL1_NAME1_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final2;

	lcl_res_hdl1 = gl_gla_env.res_hdl1;

	result = tet_test_lckResourceOpen(
	    LCK_RESOURCE_OPEN_HDL2_NAME1_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final2;

	lcl_res_hdl2 = gl_gla_env.res_hdl1;

	if (gl_glsv_async == 1) {
		gl_gla_env.res_hdl1 = lcl_res_hdl1;

		result = tet_test_lckResourceLockAsync(
		    LCK_RSC_LOCK_ASYNC_EXLCK_SUCCESS_T, TEST_NONCONFIG_MODE);
		if (result != TET_PASS)
			goto final2;

		m_GLSV_WAIT;

		result = tet_test_lckDispatch(
		    LCK_DISPATCH_DISPATCH_ONE_SUCCESS_T, TEST_NONCONFIG_MODE);
		if (result != TET_PASS || gl_gla_env.gr_clbk_invo != 707 ||
		    gl_gla_env.gr_clbk_err != SA_AIS_OK ||
		    gl_gla_env.gr_clbk_status != SA_LCK_LOCK_GRANTED) {
			result = TET_FAIL;
			goto final2;
		}

		glsv_clean_clbk_params();

		gl_gla_env.res_hdl1 = lcl_res_hdl2;

		result = tet_test_lckResourceLockAsync(
		    LCK_RSC_LOCK_ASYNC_EXLCK_SUCCESS_T, TEST_NONCONFIG_MODE);
		if (result != TET_PASS)
			goto final2;

		sleep(15);
		m_GLSV_WAIT;

		result = tet_test_lckDispatch(
		    LCK_DISPATCH_DISPATCH_ONE_SUCCESS2_T, TEST_NONCONFIG_MODE);
		if (gl_gla_env.gr_clbk_invo != 707 ||
		    gl_gla_env.gr_clbk_err != SA_AIS_ERR_TIMEOUT)
			result = TET_FAIL;
	} else {
		gl_gla_env.res_hdl1 = lcl_res_hdl1;

		result = tet_test_lckResourceLock(
		    LCK_RSC_LOCK_EX_LOCK_SUCCESS_T, TEST_NONCONFIG_MODE);
		if (result != TET_PASS ||
		    gl_gla_env.lck_status != SA_LCK_LOCK_GRANTED) {
			result = TET_FAIL;
			goto final2;
		}

		gl_gla_env.res_hdl1 = lcl_res_hdl2;
		gl_gla_env.lck_status = 0;

		result = tet_test_lckResourceLock(LCK_RSC_LOCK_EX_ERR_TIMEOUT_T,
						  TEST_NONCONFIG_MODE);
	}

final2:
	glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_HDL2_T);

final1:
	glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);

final:
	glsv_result(result);
}

void glsv_it_lck_modes_wt_clbk_03(void)
{
  glsv_it_lck_modes_wt_clbk_03_sync_async(true);
}

void glsv_it_lck_modes_wt_clbk_04(void)
{
  glsv_it_lck_modes_wt_clbk_03_sync_async(false);
}

static void glsv_it_lck_modes_wt_clbk_05_sync_async(int async)
{
	int result;
	SaLckResourceHandleT lcl_res_hdl1, lcl_res_hdl2;

	gl_glsv_async = async;

	result = tet_test_lckInitialize(LCK_INIT_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final;

	result = tet_test_lckResourceOpen(
	    LCK_RESOURCE_OPEN_HDL1_NAME1_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	lcl_res_hdl1 = gl_gla_env.res_hdl1;

	result = tet_test_lckResourceOpen(
	    LCK_RESOURCE_OPEN_RSC_EXIST_HDL1_NAME1_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	lcl_res_hdl2 = gl_gla_env.res_hdl1;

	if (gl_glsv_async == 1) {
		gl_gla_env.res_hdl1 = lcl_res_hdl1;

		result = tet_test_lckResourceLockAsync(
		    LCK_RSC_LOCK_ASYNC_EXLCK_SUCCESS_T, TEST_NONCONFIG_MODE);
		if (result != TET_PASS)
			goto final1;

		m_GLSV_WAIT;

		result = tet_test_lckDispatch(
		    LCK_DISPATCH_DISPATCH_ONE_SUCCESS_T, TEST_NONCONFIG_MODE);
		if (result != TET_PASS || gl_gla_env.gr_clbk_invo != 707 ||
		    gl_gla_env.gr_clbk_err != SA_AIS_OK ||
		    gl_gla_env.gr_clbk_status != SA_LCK_LOCK_GRANTED) {
			result = TET_FAIL;
			goto final1;
		}

		glsv_clean_clbk_params();

		gl_gla_env.res_hdl1 = lcl_res_hdl2;

		result = tet_test_lckResourceLockAsync(
		    LCK_RSC_LOCK_ASYNC_EXLCK_SUCCESS_T, TEST_NONCONFIG_MODE);
		if (result != TET_PASS)
			goto final1;

		m_GLSV_WAIT;

		result = tet_test_lckDispatch(
		    LCK_DISPATCH_DISPATCH_ALL_SUCCESS_T, TEST_NONCONFIG_MODE);
		if (gl_gla_env.gr_clbk_invo != 707 ||
		    gl_gla_env.gr_clbk_err != SA_AIS_OK ||
		    gl_gla_env.gr_clbk_status != SA_LCK_LOCK_DEADLOCK)
			result = TET_FAIL;
	} else {
		gl_gla_env.res_hdl1 = lcl_res_hdl1;

		result = tet_test_lckResourceLock(
		    LCK_RSC_LOCK_EX_LOCK_SUCCESS_T, TEST_NONCONFIG_MODE);
		if (result != TET_PASS ||
		    gl_gla_env.lck_status != SA_LCK_LOCK_GRANTED) {
			result = TET_FAIL;
			goto final1;
		}

		gl_gla_env.res_hdl1 = lcl_res_hdl2;
		gl_gla_env.lck_status = 0;

		result = tet_test_lckResourceLock(
		    LCK_RSC_LOCK_EX_LOCK_DEADLOCK_T, TEST_NONCONFIG_MODE);
	}

final1:
	glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);

final:
	glsv_result(result);
}

void glsv_it_lck_modes_wt_clbk_05(void)
{
  glsv_it_lck_modes_wt_clbk_05_sync_async(true);
}

void glsv_it_lck_modes_wt_clbk_06(void)
{
  glsv_it_lck_modes_wt_clbk_05_sync_async(false);
}

static void glsv_it_lck_modes_wt_clbk_07_sync_async(int async)
{
	int result;
	SaLckResourceHandleT lcl_res_hdl1, lcl_res_hdl2;

	gl_glsv_async = async;

	result = tet_test_lckInitialize(LCK_INIT_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final;

	glsv_createthread(&gl_gla_env.lck_hdl1);

	result =
	    tet_test_lckInitialize(LCK_INIT_SUCCESS_HDL2_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	result = tet_test_lckResourceOpen(
	    LCK_RESOURCE_OPEN_HDL1_NAME1_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final2;

	lcl_res_hdl1 = gl_gla_env.res_hdl1;

	result = tet_test_lckResourceOpen(
	    LCK_RESOURCE_OPEN_HDL2_NAME1_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final2;

	lcl_res_hdl2 = gl_gla_env.res_hdl1;

	if (gl_glsv_async == 1) {
		gl_gla_env.res_hdl1 = lcl_res_hdl1;

		result = tet_test_lckResourceLockAsync(
		    LCK_RSC_LOCK_ASYNC_EXLCK_SUCCESS_T, TEST_CONFIG_MODE);
		if (result != TET_PASS)
			goto final2;

		m_GLSV_WAIT;

		if (gl_gla_env.gr_clbk_invo != 707 ||
		    gl_gla_env.gr_clbk_err != SA_AIS_OK ||
		    gl_gla_env.gr_clbk_status != SA_LCK_LOCK_GRANTED) {
			result = TET_FAIL;
			goto final2;
		}

		glsv_clean_clbk_params();

		gl_gla_env.res_hdl1 = lcl_res_hdl2;
		result = tet_test_lckResourceLockAsync(
		    LCK_RSC_LOCK_ASYNC_PRLCK_SUCCESS_T, TEST_CONFIG_MODE);
		if (result != TET_PASS)
			goto final2;

		m_GLSV_WAIT;

		if (gl_gla_env.waiter_sig == 5600 &&
		    gl_gla_env.waiter_clbk_lck_id == gl_gla_env.ex_lck_id &&
		    gl_gla_env.waiter_clbk_mode_req == SA_LCK_PR_LOCK_MODE &&
		    gl_gla_env.waiter_clbk_mode_held == SA_LCK_EX_LOCK_MODE)
			result = TET_PASS;
		else
			result = TET_FAIL;
	} else {
		gl_gla_env.res_hdl1 = lcl_res_hdl1;

		result = tet_test_lckResourceLock(
		    LCK_RSC_LOCK_EX_LOCK_SUCCESS_T, TEST_CONFIG_MODE);
		if (result != TET_PASS ||
		    gl_gla_env.lck_status != SA_LCK_LOCK_GRANTED) {
			result = TET_FAIL;
			goto final2;
		}

		gl_gla_env.res_hdl1 = lcl_res_hdl2;

		result = tet_test_lckResourceLock(LCK_RSC_LOCK_PR_ERR_TIMEOUT_T,
						  TEST_CONFIG_MODE);

		if (gl_gla_env.waiter_sig == 1700 &&
		    gl_gla_env.waiter_clbk_lck_id == gl_gla_env.ex_lck_id &&
		    gl_gla_env.waiter_clbk_mode_req == SA_LCK_PR_LOCK_MODE &&
		    gl_gla_env.waiter_clbk_mode_held == SA_LCK_EX_LOCK_MODE)
			result = TET_PASS;
		else
			result = TET_FAIL;
	}

final2:
	glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_HDL2_T);

final1:
	glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);

final:
	glsv_result(result);
}

void glsv_it_lck_modes_wt_clbk_07(void)
{
  glsv_it_lck_modes_wt_clbk_07_sync_async(true);
}

void glsv_it_lck_modes_wt_clbk_08(void)
{
  glsv_it_lck_modes_wt_clbk_07_sync_async(false);
}

void glsv_it_lck_modes_wt_clbk_09()
{
	int result;
	SaLckResourceHandleT lcl_res_hdl;

	result = tet_test_lckInitialize(LCK_INIT_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final;

	glsv_createthread(&gl_gla_env.lck_hdl1);

	result =
	    tet_test_lckInitialize(LCK_INIT_SUCCESS_HDL2_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	result = tet_test_lckResourceOpen(
	    LCK_RESOURCE_OPEN_HDL2_NAME1_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final2;

	lcl_res_hdl = gl_gla_env.res_hdl1;

	result = tet_test_lckResourceOpen(
	    LCK_RESOURCE_OPEN_HDL1_NAME1_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final2;

	result = tet_test_lckResourceLock(LCK_RSC_LOCK_EX_LOCK_SUCCESS_T,
					  TEST_CONFIG_MODE);
	if (result != TET_PASS ||
	    gl_gla_env.lck_status != SA_LCK_LOCK_GRANTED) {
		result = TET_FAIL;
		goto final2;
	}

	gl_gla_env.res_hdl1 = lcl_res_hdl;

	result = tet_test_lckResourceLock(LCK_RSC_LOCK_PR_ERR_TIMEOUT_T,
					  TEST_CONFIG_MODE);

	if (gl_gla_env.waiter_sig != 1700 ||
	    gl_gla_env.waiter_clbk_lck_id != gl_gla_env.ex_lck_id ||
	    gl_gla_env.waiter_clbk_mode_req != SA_LCK_PR_LOCK_MODE ||
	    gl_gla_env.waiter_clbk_mode_held != SA_LCK_EX_LOCK_MODE)
		result = TET_FAIL;
	else
		result = TET_PASS;

final2:
	glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_HDL2_T);

final1:
	glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);

final:
	glsv_result(result);
}

void glsv_it_lck_modes_wt_clbk_10()
{
	int result;
	int i = 0;
	SaLckResourceHandleT lcl_res_hdl;

	gl_wt_clbk_iter = 0;

	result = tet_test_lckInitialize(LCK_INIT_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final;

	glsv_createthread(&gl_gla_env.lck_hdl1);

	result =
	    tet_test_lckInitialize(LCK_INIT_SUCCESS_HDL2_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	result = tet_test_lckResourceOpen(
	    LCK_RESOURCE_OPEN_HDL2_NAME1_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final2;

	lcl_res_hdl = gl_gla_env.res_hdl1;

	result = tet_test_lckResourceOpen(
	    LCK_RESOURCE_OPEN_HDL1_NAME1_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final2;

	while (i++ < 5) {
		result = tet_test_lckResourceLock(
		    LCK_RSC_LOCK_PR_LOCK_SUCCESS_T, TEST_CONFIG_MODE);
		if (result != TET_PASS ||
		    gl_gla_env.lck_status != SA_LCK_LOCK_GRANTED) {
			result = TET_UNRESOLVED;
			break;
		}

		sleep(2);
	}

	if (result != TET_PASS)
		goto final2;

	gl_gla_env.res_hdl1 = lcl_res_hdl;

	result = tet_test_lckResourceLock(LCK_RSC_LOCK_EX_ERR_TIMEOUT_T,
					  TEST_CONFIG_MODE);

	if (gl_wt_clbk_iter == 5)
		result = TET_PASS;
	else
		result = TET_FAIL;

final2:
	glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_HDL2_T);

final1:
	glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);

final:
	glsv_result(result);
}

void glsv_it_lck_modes_wt_clbk_11()
{
	int result;
	int i = 0;
	SaLckResourceHandleT lcl_res_hdl;

	gl_wt_clbk_iter = 0;

	result = tet_test_lckInitialize(LCK_INIT_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final;

	glsv_createthread(&gl_gla_env.lck_hdl1);

	result =
	    tet_test_lckInitialize(LCK_INIT_SUCCESS_HDL2_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	result = tet_test_lckResourceOpen(
	    LCK_RESOURCE_OPEN_HDL2_NAME1_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final2;

	lcl_res_hdl = gl_gla_env.res_hdl1;

	result = tet_test_lckResourceOpen(
	    LCK_RESOURCE_OPEN_HDL1_NAME1_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final2;

	result = tet_test_lckResourceLock(LCK_RSC_LOCK_EX_LOCK_SUCCESS_T,
					  TEST_CONFIG_MODE);
	if (result != TET_PASS ||
	    gl_gla_env.lck_status != SA_LCK_LOCK_GRANTED) {
		result = TET_FAIL;
		goto final2;
	}

	gl_gla_env.res_hdl1 = lcl_res_hdl;

	while (i++ < 5) {
		result = tet_test_lckResourceLockAsync(
		    LCK_RSC_LOCK_ASYNC_PRLCK_SUCCESS_T, TEST_CONFIG_MODE);
		if (result != TET_PASS)
			break;

		sleep(2);
	}

	if (result != TET_PASS)
		goto final2;

	if (gl_wt_clbk_iter == 5)
		result = TET_PASS;
	else
		result = TET_FAIL;

final2:
	glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_HDL2_T);

final1:
	glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);

final:
	glsv_result(result);
}

void glsv_it_lck_modes_wt_clbk_12()
{
	int result;
	int i = 0;

	gl_gr_clbk_iter = 0;

	result = tet_test_lckInitialize(LCK_INIT_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final;

	glsv_createthread(&gl_gla_env.lck_hdl1);

	result = tet_test_lckResourceOpen(
	    LCK_RESOURCE_OPEN_HDL1_NAME1_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	for (i = 0; i < 10; i++) {
		result = tet_test_lckResourceLockAsync(
		    LCK_RSC_LOCK_ASYNC_PRLCK_SUCCESS_T, TEST_CONFIG_MODE);
		if (result != TET_PASS)
			break;

		m_GLSV_WAIT;
	}

	if (result != TET_PASS)
		goto final1;

	if (gl_gr_clbk_iter == 10)
		result = TET_PASS;
	else
		result = TET_FAIL;

final1:
	glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);

final:
	glsv_result(result);
}

void glsv_it_lck_modes_wt_clbk_13()
{
	int result;
	SaLckResourceHandleT lcl_res_hdl;

	result = tet_test_lckInitialize(LCK_INIT_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final;

	result = tet_test_lckResourceOpen(
	    LCK_RESOURCE_OPEN_HDL1_NAME1_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	lcl_res_hdl = gl_gla_env.res_hdl1;

	result = tet_test_lckResourceOpen(
	    LCK_RESOURCE_OPEN_HDL1_NAME1_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	result = tet_test_lckResourceLock(LCK_RSC_LOCK_EX_LOCK_SUCCESS_T,
					  TEST_CONFIG_MODE);
	if (result != TET_PASS ||
	    gl_gla_env.lck_status != SA_LCK_LOCK_GRANTED) {
		result = TET_UNRESOLVED;
		goto final1;
	}

	gl_gla_env.res_hdl1 = lcl_res_hdl;

	result = tet_test_lckResourceLock(LCK_RSC_LOCK_EX_LOCK_DEADLOCK_T,
					  TEST_NONCONFIG_MODE);

final1:
	glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);

final:
	glsv_result(result);
}

/* Lock Types, Deadlocks and Orphan Locks */

void glsv_it_ddlcks_orplks_01()
{
	int result;

	result = tet_test_lckInitialize(LCK_INIT_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final;

	result = tet_test_lckResourceOpen(
	    LCK_RESOURCE_OPEN_HDL1_NAME1_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS) {
		glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);
		goto final;
	}

	result = tet_test_lckResourceLock(LCK_RSC_LOCK_ORPHAN_PRLCK_T,
					  TEST_CONFIG_MODE);
	if (result != TET_PASS ||
	    gl_gla_env.lck_status != SA_LCK_LOCK_GRANTED) {
		result = TET_UNRESOLVED;
		glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);
		goto final;
	}

	result = tet_test_lckFinalize(LCK_FINALIZE_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS) {
		glsv_orphan_cleanup(LCK_CLEAN_RSC_LOCK_ORPHAN_PRLCK_T);
		glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);
		goto final;
	}

	result = tet_test_lckInitialize(LCK_INIT_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	result = tet_test_lckResourceOpen(
	    LCK_RESOURCE_OPEN_RSC_EXIST_HDL1_NAME1_SUCCESS_T,
	    TEST_NONCONFIG_MODE);

final1:
	glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);
	glsv_orphan_cleanup(LCK_PURGE_RSC1_T);

final:
	glsv_result(result);
}

static void glsv_it_ddlcks_orplks_02_sync_async(int async)
{
	int result;

	gl_glsv_async = async;

	result = tet_test_lckInitialize(LCK_INIT_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final;

	result = tet_test_lckResourceOpen(
	    LCK_RESOURCE_OPEN_HDL1_NAME1_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS) {
		glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);
		goto final;
	}

	result = tet_test_lckResourceLock(LCK_RSC_LOCK_ORPHAN_PRLCK_T,
					  TEST_CONFIG_MODE);
	if (result != TET_PASS ||
	    gl_gla_env.lck_status != SA_LCK_LOCK_GRANTED) {
		result = TET_UNRESOLVED;
		glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);
		goto final;
	}

	result = tet_test_lckFinalize(LCK_FINALIZE_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS) {
		glsv_orphan_cleanup(LCK_CLEAN_RSC_LOCK_ORPHAN_PRLCK_T);
		glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);
		goto final;
	}

	result = tet_test_lckInitialize(LCK_INIT_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	result = tet_test_lckResourceOpen(
	    LCK_RESOURCE_OPEN_RSC_EXIST_HDL1_NAME1_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	if (gl_glsv_async == 1) {
		result = tet_test_lckResourceLockAsync(
		    LCK_RSC_LOCK_ASYNC_ORPHAN_EXLCK_T, TEST_NONCONFIG_MODE);
		if (result != TET_PASS)
			goto final1;

		m_GLSV_WAIT;

		result = tet_test_lckDispatch(
		    LCK_DISPATCH_DISPATCH_ONE_SUCCESS_T, TEST_NONCONFIG_MODE);
		if (result == TET_PASS && gl_gla_env.gr_clbk_invo == 713 &&
		    gl_gla_env.gr_clbk_err == SA_AIS_OK &&
		    gl_gla_env.gr_clbk_status == SA_LCK_LOCK_ORPHANED)
			result = TET_PASS;
		else
			result = TET_FAIL;
	} else {
		result = tet_test_lckResourceLock(
		    LCK_RSC_LOCK_EX_LOCK_ORPHANED_T, TEST_NONCONFIG_MODE);
		if (result == TET_PASS &&
		    gl_gla_env.lck_status == SA_LCK_LOCK_ORPHANED)
			result = TET_PASS;
		else
			result = TET_FAIL;
	}

final1:
	glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);
	glsv_orphan_cleanup(LCK_PURGE_RSC1_T);

final:
	glsv_result(result);
}

void glsv_it_ddlcks_orplks_02(void)
{
  glsv_it_ddlcks_orplks_02_sync_async(true);
}

void glsv_it_ddlcks_orplks_03(void)
{
  glsv_it_ddlcks_orplks_02_sync_async(false);
}

static void glsv_it_ddlcks_orplks_04_sync_async(int async)
{
	int result;

	gl_glsv_async = async;

	result = tet_test_lckInitialize(LCK_INIT_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final;

	result = tet_test_lckResourceOpen(
	    LCK_RESOURCE_OPEN_HDL1_NAME1_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS) {
		glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);
		goto final;
	}

	result = tet_test_lckResourceLock(LCK_RSC_LOCK_ORPHAN_PRLCK_T,
					  TEST_CONFIG_MODE);
	if (result != TET_PASS ||
	    gl_gla_env.lck_status != SA_LCK_LOCK_GRANTED) {
		result = TET_UNRESOLVED;
		glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);
		goto final;
	}

	result = tet_test_lckFinalize(LCK_FINALIZE_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS) {
		glsv_orphan_cleanup(LCK_CLEAN_RSC_LOCK_ORPHAN_PRLCK_T);
		glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);
		goto final;
	}

	result = tet_test_lckInitialize(LCK_INIT_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	result = tet_test_lckResourceOpen(
	    LCK_RESOURCE_OPEN_RSC_EXIST_HDL1_NAME1_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	if (gl_glsv_async == 1) {
		result = tet_test_lckResourceLockAsync(
		    LCK_RSC_LOCK_ASYNC_EXLCK_SUCCESS_T, TEST_NONCONFIG_MODE);
		if (result != TET_PASS)
			goto final1;

		sleep(15);
		m_GLSV_WAIT;

		result = tet_test_lckDispatch(
		    LCK_DISPATCH_DISPATCH_ONE_SUCCESS_T, TEST_NONCONFIG_MODE);
		if (result == TET_PASS && gl_gla_env.gr_clbk_invo == 707 &&
		    gl_gla_env.gr_clbk_err == SA_AIS_ERR_TIMEOUT)
			result = TET_PASS;
		else
			result = TET_FAIL;
	} else {
		result = tet_test_lckResourceLock(LCK_RSC_LOCK_EX_ERR_TIMEOUT_T,
						  TEST_NONCONFIG_MODE);
	}

final1:
	glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);
	glsv_orphan_cleanup(LCK_PURGE_RSC1_T);

final:
	glsv_result(result);
}

void glsv_it_ddlcks_orplks_04(void)
{
  glsv_it_ddlcks_orplks_04_sync_async(true);
}

void glsv_it_ddlcks_orplks_05(void)
{
  glsv_it_ddlcks_orplks_04_sync_async(false);
}

static void glsv_it_ddlcks_orplks_06_sync_async(int async)
{
	int result;
	SaLckLockIdT pr_lck_id1, pr_lck_id2;

	gl_glsv_async = async;

	result = tet_test_lckInitialize(LCK_INIT_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final;

	result =
	    tet_test_lckInitialize(LCK_INIT_SUCCESS_HDL2_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	result = tet_test_lckResourceOpenAsync(
	    LCK_RESOURCE_OPEN_ASYNC_HDL1_NAME1_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final2;

	m_GLSV_WAIT;

	result = tet_test_lckDispatch(LCK_DISPATCH_DISPATCH_ONE_SUCCESS_T,
				      TEST_CONFIG_MODE);
	if (result != TET_PASS || gl_gla_env.open_clbk_invo != 208 ||
	    gl_gla_env.open_clbk_err != SA_AIS_OK) {
		result = TET_UNRESOLVED;
		goto final2;
	}

	gl_gla_env.res_hdl1 = gl_gla_env.open_clbk_res_hdl;

	result = tet_test_lckResourceOpen(
	    LCK_RESOURCE_OPEN_HDL2_NAME2_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final2;

	if (gl_glsv_async == 1) {
		result = tet_test_lckResourceLockAsync(
		    LCK_RSC_LOCK_ASYNC_PRLCK_SUCCESS_T, TEST_CONFIG_MODE);
		if (result != TET_PASS)
			goto final2;

		m_GLSV_WAIT;

		result = tet_test_lckDispatch(
		    LCK_DISPATCH_DISPATCH_ONE_SUCCESS_T, TEST_CONFIG_MODE);
		if (result != TET_PASS || gl_gla_env.gr_clbk_invo != 706 ||
		    gl_gla_env.gr_clbk_err != SA_AIS_OK ||
		    gl_gla_env.gr_clbk_status != SA_LCK_LOCK_GRANTED) {
			result = TET_UNRESOLVED;
			goto final2;
		}

		pr_lck_id1 = gl_gla_env.pr_lck_id;

		glsv_clean_clbk_params();

		result = tet_test_lckResourceLockAsync(
		    LCK_RSC_LOCK_ASYNC_PRLCK_RSC2_SUCCESS_T, TEST_CONFIG_MODE);
		if (result != TET_PASS)
			goto final2;

		m_GLSV_WAIT;

		result = tet_test_lckDispatch(
		    LCK_DISPATCH_DISPATCH_ONE_SUCCESS2_T, TEST_CONFIG_MODE);
		if (result != TET_PASS || gl_gla_env.gr_clbk_invo != 708 ||
		    gl_gla_env.gr_clbk_err != SA_AIS_OK ||
		    gl_gla_env.gr_clbk_status != SA_LCK_LOCK_GRANTED) {
			result = TET_UNRESOLVED;
			goto final2;
		}

		pr_lck_id2 = gl_gla_env.pr_lck_id;
	} else {
		result = tet_test_lckResourceLock(
		    LCK_RSC_LOCK_PR_LOCK_SUCCESS_T, TEST_CONFIG_MODE);
		if (result != TET_PASS ||
		    gl_gla_env.lck_status != SA_LCK_LOCK_GRANTED) {
			result = TET_UNRESOLVED;
			goto final2;
		}

		pr_lck_id1 = gl_gla_env.pr_lck_id;
		gl_gla_env.lck_status = 0;

		result = tet_test_lckResourceLock(
		    LCK_RSC_LOCK_PR_LOCK_RSC2_SUCCESS_T, TEST_CONFIG_MODE);
		if (result != TET_PASS ||
		    gl_gla_env.lck_status != SA_LCK_LOCK_GRANTED) {
			result = TET_UNRESOLVED;
			goto final2;
		}

		pr_lck_id2 = gl_gla_env.pr_lck_id;
	}

	glsv_clean_clbk_params();

	result = tet_test_lckResourceOpen(
	    LCK_RESOURCE_OPEN_RSC_EXIST_HDL1_NAME2_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final2;

	result = tet_test_lckResourceOpen(
	    LCK_RESOURCE_OPEN_RSC_EXIST_HDL2_NAME1_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final2;

	result = tet_test_lckResourceLockAsync(
	    LCK_RSC_LOCK_ASYNC_EXLCK_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final2;

	if (gl_glsv_async == 1) {
		result = tet_test_lckResourceLockAsync(
		    LCK_RSC_LOCK_ASYNC_EXLCK_RSC2_SUCCESS_T, TEST_CONFIG_MODE);
		if (result != TET_PASS)
			goto final2;

		m_GLSV_WAIT;

		result = tet_test_lckDispatch(
		    LCK_DISPATCH_DISPATCH_ALL_SUCCESS_T, TEST_CONFIG_MODE);
		if (result != TET_PASS || gl_gla_env.gr_clbk_invo != 709 ||
		    gl_gla_env.gr_clbk_err != SA_AIS_OK ||
		    gl_gla_env.gr_clbk_status != SA_LCK_LOCK_DEADLOCK ||
		    gl_gla_env.waiter_sig != 5700 ||
		    gl_gla_env.waiter_clbk_lck_id != pr_lck_id1 ||
		    gl_gla_env.waiter_clbk_mode_req != SA_LCK_EX_LOCK_MODE ||
		    gl_gla_env.waiter_clbk_mode_held != SA_LCK_PR_LOCK_MODE) {
			result = TET_FAIL;
			goto final2;
		}

		result = tet_test_lckDispatch(
		    LCK_DISPATCH_DISPATCH_ALL_SUCCESS2_T, TEST_CONFIG_MODE);
		if (result != TET_PASS || gl_gla_env.waiter_sig != 5900 ||
		    gl_gla_env.waiter_clbk_lck_id != pr_lck_id2 ||
		    gl_gla_env.waiter_clbk_mode_req != SA_LCK_EX_LOCK_MODE ||
		    gl_gla_env.waiter_clbk_mode_held != SA_LCK_PR_LOCK_MODE) {
			result = TET_FAIL;
			goto final2;
		}

		sleep(15);

		result = tet_test_lckDispatch(
		    LCK_DISPATCH_DISPATCH_ALL_SUCCESS2_T, TEST_CONFIG_MODE);
		if (result != TET_PASS || gl_gla_env.gr_clbk_invo != 707 ||
		    gl_gla_env.gr_clbk_err != SA_AIS_ERR_TIMEOUT)
			result = TET_FAIL;
		else
			result = TET_PASS;
	} else {
		gl_gla_env.lck_status = 0;

		result = tet_test_lckResourceLock(
		    LCK_RSC_LOCK_EX_LOCK_RSC2_SUCCESS_T, TEST_CONFIG_MODE);
		if (result != TET_PASS ||
		    gl_gla_env.lck_status != SA_LCK_LOCK_DEADLOCK) {
			result = TET_FAIL;
			goto final2;
		}

		m_GLSV_WAIT;

		result = tet_test_lckDispatch(
		    LCK_DISPATCH_DISPATCH_ONE_SUCCESS2_T, TEST_CONFIG_MODE);
		if (result != TET_PASS || gl_gla_env.waiter_sig != 2100 ||
		    gl_gla_env.waiter_clbk_lck_id != pr_lck_id2 ||
		    gl_gla_env.waiter_clbk_mode_req != SA_LCK_EX_LOCK_MODE ||
		    gl_gla_env.waiter_clbk_mode_held != SA_LCK_PR_LOCK_MODE) {
			result = TET_FAIL;
			goto final2;
		}

		result = tet_test_lckDispatch(
		    LCK_DISPATCH_DISPATCH_ALL_SUCCESS_T, TEST_CONFIG_MODE);
		if (result != TET_PASS || gl_gla_env.waiter_sig != 5700 ||
		    gl_gla_env.waiter_clbk_lck_id != pr_lck_id1 ||
		    gl_gla_env.waiter_clbk_mode_req != SA_LCK_EX_LOCK_MODE ||
		    gl_gla_env.waiter_clbk_mode_held != SA_LCK_PR_LOCK_MODE) {
			result = TET_FAIL;
			goto final2;
		}

		sleep(15);

		result = tet_test_lckDispatch(
		    LCK_DISPATCH_DISPATCH_ALL_SUCCESS2_T, TEST_CONFIG_MODE);
		if (result != TET_PASS || gl_gla_env.gr_clbk_invo != 707 ||
		    gl_gla_env.gr_clbk_err != SA_AIS_ERR_TIMEOUT)
			result = TET_FAIL;
		else
			result = TET_PASS;
	}

final2:
	glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_HDL2_T);

final1:
	glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);

final:
	glsv_result(result);
}

void glsv_it_ddlcks_orplks_06(void)
{
  glsv_it_ddlcks_orplks_06_sync_async(true);
}

void glsv_it_ddlcks_orplks_07(void)
{
  glsv_it_ddlcks_orplks_06_sync_async(false);
}

void glsv_it_ddlcks_orplks_08()
{
	int result;

	result = tet_test_lckInitialize(LCK_INIT_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final;

	result = tet_test_lckResourceOpen(
	    LCK_RESOURCE_OPEN_HDL1_NAME1_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	result = tet_test_lckResourceOpenAsync(
	    LCK_RESOURCE_OPEN_ASYNC_HDL1_NAME1_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	m_GLSV_WAIT;

	result = tet_test_lckDispatch(LCK_DISPATCH_DISPATCH_ONE_SUCCESS_T,
				      TEST_CONFIG_MODE);
	if (result != TET_PASS || gl_gla_env.open_clbk_invo != 208 ||
	    gl_gla_env.open_clbk_err != SA_AIS_OK) {
		result = TET_UNRESOLVED;
		goto final1;
	}

	result = tet_test_lckResourceLock(LCK_RSC_LOCK_EX_LOCK_SUCCESS_T,
					  TEST_CONFIG_MODE);
	if (result != TET_PASS ||
	    gl_gla_env.lck_status != SA_LCK_LOCK_GRANTED) {
		result = TET_UNRESOLVED;
		goto final1;
	}

	gl_gla_env.lck_status = 0;
	gl_gla_env.res_hdl1 = gl_gla_env.open_clbk_res_hdl;

	result = tet_test_lckResourceLock(LCK_RSC_LOCK_PR_LOCK_DEADLOCK_T,
					  TEST_NONCONFIG_MODE);
	if (result == TET_PASS && gl_gla_env.lck_status == SA_LCK_LOCK_DEADLOCK)
		result = TET_PASS;
	else
		result = TET_FAIL;

final1:
	glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);

final:
	glsv_result(result);
}

void glsv_it_ddlcks_orplks_09()
{
	int result;

	result = tet_test_lckInitialize(LCK_INIT_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final;

	glsv_createthread(&gl_gla_env.lck_hdl1);

	result = tet_test_lckResourceOpen(
	    LCK_RESOURCE_OPEN_HDL1_NAME1_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	result = tet_test_lckResourceLock(LCK_RSC_LOCK_EX_LOCK_SUCCESS_T,
					  TEST_CONFIG_MODE);
	if (result != TET_PASS ||
	    gl_gla_env.lck_status != SA_LCK_LOCK_GRANTED) {
		result = TET_UNRESOLVED;
		goto final1;
	}

	result = tet_test_lckResourceLockAsync(
	    LCK_RSC_LOCK_ASYNC_ORPHAN_PRLCK_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	result = tet_test_lckResourceClose(
	    LCK_RESOURCE_CLOSE_RSC_HDL1_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	result = tet_test_lckResourceOpen(
	    LCK_RESOURCE_OPEN_NAME1_EXIST_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	glsv_clean_clbk_params();

	m_GLSV_WAIT;

	result = tet_test_lckResourceLockAsync(
	    LCK_RSC_LOCK_ASYNC_ORPHAN_EXLCK_T, TEST_NONCONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	m_GLSV_WAIT;

	if (gl_gla_env.gr_clbk_invo == 713 &&
	    gl_gla_env.gr_clbk_err == SA_AIS_OK &&
	    gl_gla_env.gr_clbk_status != SA_LCK_LOCK_ORPHANED)
		result = TET_PASS;
	else
		result = TET_FAIL;

	glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);
	glsv_orphan_cleanup(LCK_PURGE_RSC1_T);
	goto final;

final1:
	glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);

final:
	glsv_result(result);
}

static void glsv_it_ddlcks_orplks_10_sync_async(int async)
{
	int result;

	gl_glsv_async = async;

	result = tet_test_lckInitialize(LCK_INIT_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final;

	result = tet_test_lckResourceOpen(
	    LCK_RESOURCE_OPEN_HDL1_NAME1_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS) {
		glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);
		goto final;
	}

	result = tet_test_lckResourceLock(LCK_RSC_LOCK_ORPHAN_PRLCK_T,
					  TEST_CONFIG_MODE);
	if (result != TET_PASS ||
	    gl_gla_env.lck_status != SA_LCK_LOCK_GRANTED) {
		result = TET_UNRESOLVED;
		glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);
		goto final;
	}

	result = tet_test_lckFinalize(LCK_FINALIZE_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS) {
		glsv_orphan_cleanup(LCK_CLEAN_RSC_LOCK_ORPHAN_PRLCK_T);
		glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);
		goto final;
	}

	result = tet_test_lckInitialize(LCK_INIT_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	result = tet_test_lckResourceOpen(
	    LCK_RESOURCE_OPEN_RSC_EXIST_HDL1_NAME1_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	if (gl_glsv_async == 1) {
		result = tet_test_lckResourceLockAsync(
		    LCK_RSC_LOCK_ASYNC_NO_QUEUE_EXLCK_T, TEST_NONCONFIG_MODE);
		if (result != TET_PASS)
			goto final1;

		m_GLSV_WAIT;

		result = tet_test_lckDispatch(
		    LCK_DISPATCH_DISPATCH_ONE_SUCCESS_T, TEST_NONCONFIG_MODE);
		if (result == TET_PASS && gl_gla_env.gr_clbk_invo == 711 &&
		    gl_gla_env.gr_clbk_err == SA_AIS_OK &&
		    gl_gla_env.gr_clbk_status == SA_LCK_LOCK_NOT_QUEUED)
			result = TET_PASS;
		else
			result = TET_FAIL;
	} else {
		result = tet_test_lckResourceLock(LCK_RSC_LOCK_NO_QUEUE_EXLCK_T,
						  TEST_NONCONFIG_MODE);
		if (result == TET_PASS &&
		    gl_gla_env.lck_status == SA_LCK_LOCK_NOT_QUEUED)
			result = TET_PASS;
		else
			result = TET_FAIL;
	}

final1:
	glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);
	glsv_orphan_cleanup(LCK_PURGE_RSC1_T);

final:
	glsv_result(result);
}

void glsv_it_ddlcks_orplks_10(void)
{
  glsv_it_ddlcks_orplks_10_sync_async(true);
}

void glsv_it_ddlcks_orplks_11(void)
{
  glsv_it_ddlcks_orplks_10_sync_async(false);
}

/* Lock Stripping and Purging */

void glsv_it_lck_strip_purge_01(void)
{
	int result;

	result = tet_test_lckInitialize(LCK_INIT_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final;

	result = tet_test_lckResourceOpen(
	    LCK_RESOURCE_OPEN_HDL1_NAME1_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	result = tet_test_lckResourceOpenAsync(
	    LCK_RESOURCE_OPEN_ASYNC_HDL1_NAME1_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	m_GLSV_WAIT;

	result = tet_test_lckDispatch(LCK_DISPATCH_DISPATCH_ONE_SUCCESS_T,
				      TEST_CONFIG_MODE);
	if (result != TET_PASS || gl_gla_env.open_clbk_invo != 208 ||
	    gl_gla_env.open_clbk_err != SA_AIS_OK) {
		result = TET_UNRESOLVED;
		goto final1;
	}

	result = tet_test_lckResourceLock(LCK_RSC_LOCK_ORPHAN_PRLCK_T,
					  TEST_CONFIG_MODE);
	if (result != TET_PASS ||
	    gl_gla_env.lck_status != SA_LCK_LOCK_GRANTED) {
		result = TET_UNRESOLVED;
		goto final1;
	}

	gl_gla_env.res_hdl1 = gl_gla_env.open_clbk_res_hdl;
	gl_gla_env.lck_status = 0;

	result = tet_test_lckResourceLock(LCK_RSC_LOCK_ORPHAN_PRLCK_T,
					  TEST_CONFIG_MODE);
	if (result != TET_PASS ||
	    gl_gla_env.lck_status != SA_LCK_LOCK_GRANTED) {
		result = TET_UNRESOLVED;
		glsv_orphan_cleanup(LCK_CLEAN_RSC_LOCK_ORPHAN_PRLCK_T);
		glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);
		goto final;
	}

	result = tet_test_lckFinalize(LCK_FINALIZE_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS) {
		glsv_orphan_cleanup(LCK_CLEAN_RSC_LOCK_ORPHAN_PRLCK_T);
		glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);
		goto final;
	}

	result = tet_test_lckInitialize(LCK_INIT_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	result = tet_test_lckResourceOpen(
	    LCK_RESOURCE_OPEN_HDL1_NAME1_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	gl_gla_env.lck_status = 0;
	result = tet_test_lckResourceLock(LCK_RSC_LOCK_EX_LOCK_ORPHANED_T,
					  TEST_CONFIG_MODE);
	if (result != TET_PASS ||
	    gl_gla_env.lck_status != SA_LCK_LOCK_ORPHANED) {
		result = TET_UNRESOLVED;
		goto final1;
	}

	result =
	    tet_test_lckLockPurge(LCK_LOCK_PURGE_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	gl_gla_env.lck_status = 0;
	result = tet_test_lckResourceLock(LCK_RSC_LOCK_EX_LOCK_SUCCESS_T,
					  TEST_NONCONFIG_MODE);
	if (result == TET_PASS && gl_gla_env.lck_status == SA_LCK_LOCK_GRANTED)
		result = TET_PASS;
	else
		result = TET_FAIL;

final1:
	glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);
	glsv_orphan_cleanup(LCK_PURGE_RSC1_T);

final:
	glsv_result(result);
}

void glsv_it_lck_strip_purge_02()
{
	int result;

	result = tet_test_lckInitialize(LCK_INIT_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final;

	result = tet_test_lckResourceOpen(
	    LCK_RESOURCE_OPEN_HDL1_NAME1_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS) {
		glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);
		goto final;
	}

	result = tet_test_lckResourceLock(LCK_RSC_LOCK_ORPHAN_PRLCK_T,
					  TEST_CONFIG_MODE);
	if (result != TET_PASS ||
	    gl_gla_env.lck_status != SA_LCK_LOCK_GRANTED) {
		result = TET_UNRESOLVED;
		glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);
		goto final;
	}

	result = tet_test_lckResourceClose(
	    LCK_RESOURCE_CLOSE_RSC_HDL1_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS) {
		glsv_orphan_cleanup(LCK_CLEAN_RSC_LOCK_ORPHAN_PRLCK_T);
		glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);
		goto final;
	}

	result = tet_test_lckResourceOpen(
	    LCK_RESOURCE_OPEN_RSC_EXIST_HDL1_NAME1_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	gl_gla_env.lck_status = 0;
	result = tet_test_lckResourceLock(LCK_RSC_LOCK_EX_LOCK_ORPHANED_T,
					  TEST_CONFIG_MODE);
	if (result != TET_PASS ||
	    gl_gla_env.lck_status != SA_LCK_LOCK_ORPHANED) {
		result = TET_UNRESOLVED;
		goto final1;
	}

	result =
	    tet_test_lckLockPurge(LCK_LOCK_PURGE_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	gl_gla_env.lck_status = 0;
	result = tet_test_lckResourceLock(LCK_RSC_LOCK_EX_LOCK_SUCCESS_T,
					  TEST_NONCONFIG_MODE);
	if (result == TET_PASS && gl_gla_env.lck_status == SA_LCK_LOCK_GRANTED)
		result = TET_PASS;
	else
		result = TET_FAIL;

final1:
	glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);
	glsv_orphan_cleanup(LCK_PURGE_RSC1_T);

final:
	glsv_result(result);
}

void glsv_it_lck_strip_purge_03()
{
	int result;

	result = tet_test_lckInitialize(LCK_INIT_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final;

	result = tet_test_lckResourceOpen(
	    LCK_RESOURCE_OPEN_HDL1_NAME1_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS) {
		glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);
		goto final;
	}

	result = tet_test_lckResourceLock(LCK_RSC_LOCK_ORPHAN_PRLCK_T,
					  TEST_CONFIG_MODE);
	if (result != TET_PASS ||
	    gl_gla_env.lck_status != SA_LCK_LOCK_GRANTED) {
		result = TET_UNRESOLVED;
		glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);
		goto final;
	}

	result = tet_test_lckResourceClose(
	    LCK_RESOURCE_CLOSE_RSC_HDL1_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS) {
		glsv_orphan_cleanup(LCK_CLEAN_RSC_LOCK_ORPHAN_PRLCK_T);
		glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);
		goto final;
	}

	result = tet_test_lckResourceOpen(
	    LCK_RESOURCE_OPEN_RSC_EXIST_HDL1_NAME1_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	gl_gla_env.lck_status = 0;
	result = tet_test_lckResourceLock(LCK_RSC_LOCK_PR_LOCK_SUCCESS_T,
					  TEST_CONFIG_MODE);
	if (result != TET_PASS ||
	    gl_gla_env.lck_status != SA_LCK_LOCK_GRANTED) {
		result = TET_UNRESOLVED;
		goto final1;
	}

	gl_gla_env.lck_status = 0;
	result = tet_test_lckResourceLock(LCK_RSC_LOCK_EX_LOCK_ORPHANED_T,
					  TEST_CONFIG_MODE);
	if (result != TET_PASS ||
	    gl_gla_env.lck_status != SA_LCK_LOCK_ORPHANED) {
		result = TET_UNRESOLVED;
		goto final1;
	}

	result =
	    tet_test_lckLockPurge(LCK_LOCK_PURGE_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	result = tet_test_lckResourceLockAsync(
	    LCK_RSC_LOCK_ASYNC_ORPHAN_EXLCK_T, TEST_NONCONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	sleep(15);
	m_GLSV_WAIT;

	result = tet_test_lckDispatch(LCK_DISPATCH_DISPATCH_ALL_SUCCESS_T,
				      TEST_NONCONFIG_MODE);
	if (result == TET_PASS && gl_gla_env.gr_clbk_invo == 713 &&
	    gl_gla_env.gr_clbk_err == SA_AIS_OK &&
	    gl_gla_env.gr_clbk_status == SA_LCK_LOCK_DEADLOCK)
		result = TET_PASS;
	else
		result = TET_FAIL;

final1:
	glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);
	glsv_orphan_cleanup(LCK_PURGE_RSC1_T);

final:
	glsv_result(result);
}

/* ERR_TRY_AGAIN Testcases */

void glsv_it_err_try_again_01()
{
	int result;

	glsv_print_testcase(
	    " \n\n ***** API TEST for SA_AIS_ERR_TRY_AGAIN (case 1) *****\n");

	printf(" KILL GLD/GLND AND PRESS ENTER TO CONTINUE\n");
	getchar();

	result = tet_test_lckInitialize(LCK_INIT_ERR_TRY_AGAIN_T,
					TEST_NONCONFIG_MODE);

	glsv_result(result);
}

void glsv_it_err_try_again_02()
{
	int result;

	glsv_print_testcase(
	    " \n\n ***** API TEST for SA_AIS_ERR_TRY_AGAIN (case 2) *****\n");

	result = tet_test_lckInitialize(LCK_INIT_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final;

	printf(" KILL GLD/GLND AND PRESS ENTER TO CONTINUE\n");
	getchar();

	tet_test_lckSelectionObject(LCK_SEL_OBJ_ERR_TRY_AGAIN_T,
				    TEST_NONCONFIG_MODE);

	tet_test_lckOptionCheck(LCK_OPT_CHCK_SUCCESS_T, TEST_NONCONFIG_MODE);

	tet_test_lckDispatch(LCK_DISPATCH_ERR_TRY_AGAIN_T, TEST_NONCONFIG_MODE);

	tet_test_lckFinalize(LCK_FINALIZE_ERR_TRY_AGAIN_T, TEST_NONCONFIG_MODE);

	tet_test_lckResourceOpen(LCK_RESOURCE_OPEN_ERR_TRY_AGAIN_T,
				 TEST_NONCONFIG_MODE);

	tet_test_lckResourceOpenAsync(LCK_RESOURCE_OPEN_ASYNC_ERR_TRY_AGAIN_T,
				      TEST_NONCONFIG_MODE);

	glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);

final:
	glsv_result(result);
}

void glsv_it_err_try_again_03()
{
	int result;

	glsv_print_testcase(
	    " \n\n ***** API TEST for SA_AIS_ERR_TRY_AGAIN (case 3) *****\n");

	result = tet_test_lckInitialize(LCK_INIT_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final;

	result = tet_test_lckResourceOpen(
	    LCK_RESOURCE_OPEN_HDL1_NAME1_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	printf(" KILL GLD/GLND AND PRESS ENTER TO CONTINUE\n");
	getchar();

	tet_test_lckResourceClose(LCK_RESOURCE_CLOSE_ERR_TRY_AGAIN_T,
				  TEST_NONCONFIG_MODE);

	tet_test_lckResourceLock(LCK_RSC_LOCK_ERR_TRY_AGAIN_T,
				 TEST_NONCONFIG_MODE);

	tet_test_lckResourceLockAsync(LCK_RSC_LOCK_ASYNC_ERR_TRY_AGAIN_T,
				      TEST_NONCONFIG_MODE);

	tet_test_lckLockPurge(LCK_LOCK_PURGE_ERR_TRY_AGAIN_T,
			      TEST_NONCONFIG_MODE);

final1:
	glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);

final:
	glsv_result(result);
}

void glsv_it_err_try_again_04()
{
	int result;

	glsv_print_testcase(
	    " \n\n ***** API TEST for SA_AIS_ERR_TRY_AGAIN (case 4) *****\n");

	result = tet_test_lckInitialize(LCK_INIT_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final;

	result = tet_test_lckResourceOpen(
	    LCK_RESOURCE_OPEN_HDL1_NAME1_SUCCESS_T, TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	result = tet_test_lckResourceLock(LCK_RSC_LOCK_EX_LOCK_SUCCESS_T,
					  TEST_CONFIG_MODE);
	if (result != TET_PASS)
		goto final1;

	printf(" KILL GLD/GLND AND PRESS ENTER TO CONTINUE\n");
	getchar();

	tet_test_lckResourceUnlock(LCK_RSC_UNLOCK_ERR_TRY_AGAIN_T,
				   TEST_NONCONFIG_MODE);

	tet_test_lckResourceUnlockAsync(LCK_RSC_UNLOCK_ASYNC_ERR_TRY_AGAIN_T,
					TEST_NONCONFIG_MODE);

final1:
	glsv_init_cleanup(LCK_CLEAN_INIT_SUCCESS_T);

final:
	glsv_result(result);
}

/* *************** GLSV Inputs **************** */

void tet_glsv_get_inputs(TET_GLSV_INST *inst)
{
	char *tmp_ptr = NULL;

	memset(inst, '\0', sizeof(TET_GLSV_INST));

	tmp_ptr = (char *)getenv("TET_GLSV_INST_NUM");
	if (tmp_ptr) {
		inst->inst_num = atoi(tmp_ptr);
		tmp_ptr = NULL;
		gl_glsv_inst_num = inst->inst_num;
	}

	tmp_ptr = (char *)getenv("TET_GLSV_TET_LIST_INDEX");
	if (tmp_ptr) {
		inst->tetlist_index = atoi(tmp_ptr);
		tmp_ptr = NULL;
		gl_tetlist_index = inst->tetlist_index;
	}

	tmp_ptr = (char *)getenv("TET_GLSV_TEST_CASE_NUM");
	if (tmp_ptr) {
		inst->test_case_num = atoi(tmp_ptr);
		tmp_ptr = NULL;
	}

	tmp_ptr = (char *)getenv("TET_GLSV_NUM_ITER");
	if (tmp_ptr) {
		inst->num_of_iter = atoi(tmp_ptr);
		tmp_ptr = NULL;
	}

	inst->node_id = m_NCS_GET_NODE_ID;
	gl_nodeId = inst->node_id;

	TET_GLSV_NODE1 = m_NCS_GET_NODE_ID;

	tmp_ptr = (char *)getenv("GLA_NODE_ID_2");
	if (tmp_ptr) {
		TET_GLSV_NODE2 = atoi(tmp_ptr);
		tmp_ptr = NULL;
	}

	tmp_ptr = (char *)getenv("GLA_NODE_ID_3");
	if (tmp_ptr) {
		TET_GLSV_NODE3 = atoi(tmp_ptr);
		tmp_ptr = NULL;
	}

	tmp_ptr = (char *)getenv("TET_GLSV_RES_NAME1");
	if (tmp_ptr) {
		memcpy(inst->res_name1.value, tmp_ptr, strlen(tmp_ptr));
		inst->res_name1.length = strlen(tmp_ptr);
		tmp_ptr = NULL;
	}

	tmp_ptr = (char *)getenv("TET_GLSV_RES_NAME2");
	if (tmp_ptr) {
		memcpy(inst->res_name2.value, tmp_ptr, strlen(tmp_ptr));
		inst->res_name2.length = strlen(tmp_ptr);
		tmp_ptr = NULL;
	}

	tmp_ptr = (char *)getenv("TET_GLSV_RES_NAME3");
	if (tmp_ptr) {
		memcpy(inst->res_name3.value, tmp_ptr, strlen(tmp_ptr));
		inst->res_name3.length = strlen(tmp_ptr);
		tmp_ptr = NULL;
	}

	tmp_ptr = (char *)getenv("TET_GLSV_RED_FLAG");
	if (tmp_ptr) {
		gl_lck_red_flg = atoi(tmp_ptr);
		tmp_ptr = NULL;
	}

	tmp_ptr = (char *)getenv("TET_GLSV_WAIT_TIME");
	if (tmp_ptr) {
		gl_glsv_wait_time = atoi(tmp_ptr);
		tmp_ptr = NULL;
	}
}

void tet_glsv_fill_inputs(TET_GLSV_INST *inst)
{
	if (inst->res_name1.length) {
		memset(&gl_gla_env.res1, '\0', sizeof(SaNameT));
		memcpy(gl_gla_env.res1.value,
           inst->res_name1.value,
           inst->res_name1.length);
		gl_gla_env.res1.length = inst->res_name1.length;
	}

	if (inst->res_name2.length) {
		memset(&gl_gla_env.res2, '\0', sizeof(SaNameT));
		memcpy(gl_gla_env.res2.value,
           inst->res_name2.value,
           inst->res_name2.length);
		gl_gla_env.res2.length = inst->res_name2.length;
	}

	if (inst->res_name3.length) {
		memset(&gl_gla_env.res3, '\0', sizeof(SaNameT));
		memcpy(gl_gla_env.res3.value,
           inst->res_name3.value,
           inst->res_name3.length);
		gl_gla_env.res3.length = inst->res_name3.length;
	}
}
