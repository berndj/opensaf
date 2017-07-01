/********************************************************************************************
#
# modification:
# 2008-11   Jony <jiangnan_hw@huawei.com>  Huawei Technologies CO., LTD.
#  1.  Provide test case tet_startup and tet_cleanup functions. The tet_startup
function #      is defined as tet_mqsv_startup(), as well as the tet_cleanup
function is defined #      as tet_mqsv_end(). #  2.  If "tetware-patch" is not
used, change the name of test case array from #      mqsv_onenode_testlist[] to
tet_testlist[]. #  3.  If "tetware-patch" is not used, delete the third
parameter of each item in this array. #      And add some functions to implement
the function of those parameters. #  4.  If "tetware-patch" is not used, delete
parts of tet_run_mqsv_app () function, #      which invoke the functions in the
patch. #  5.  If "tetware-patch" is required, one can add a macro definition
"TET_PATCH=1" in #      compilation process.
#
*********************************************************************************************/

#include "base/ncs_main_papi.h"
#include "tet_mqsv.h"
#include "osaf/apitest/utest.h"

void tet_create_mqd(void);
void tet_create_mqnd(void);
void tet_run_mqsv_app(void);

void tet_mqsv_startup(void);
void tet_mqsv_cleanup(void);

void (*tet_startup)() = tet_mqsv_startup;
void (*tet_cleanup)() = tet_mqsv_cleanup;

/******** TET_LIST Arrays *********/

#if (TET_PATCH == 1)
struct tet_testlist mqsv_onenode_testlist[] = {
#else
struct tet_testlist tet_testlist[] = {
#endif
    {mqsv_it_init_01, 1},
    {mqsv_it_init_02, 2},
    {mqsv_it_init_03, 3},
    {mqsv_it_init_04, 4},
    {mqsv_it_init_05, 5},
    {mqsv_it_init_06, 6},
    {mqsv_it_init_07, 7},
    {mqsv_it_init_08, 8},
    {mqsv_it_init_09, 9},
    {mqsv_it_init_10, 10},

    {mqsv_it_selobj_01, 11},
    {mqsv_it_selobj_02, 12},
    {mqsv_it_selobj_03, 13},
    {mqsv_it_selobj_04, 14},

    {mqsv_it_dispatch_01, 15},
    {mqsv_it_dispatch_02, 16},
    {mqsv_it_dispatch_03, 17},
    {mqsv_it_dispatch_04, 18},
    {mqsv_it_dispatch_05, 19},
    {mqsv_it_dispatch_06, 20},
    {mqsv_it_dispatch_07, 21},
    {mqsv_it_dispatch_08, 22},
    {mqsv_it_dispatch_09, 23},

    {mqsv_it_finalize_01, 24},
    {mqsv_it_finalize_02, 25},
    {mqsv_it_finalize_03, 26},
    {mqsv_it_finalize_04, 27},
    {mqsv_it_finalize_05, 28},
    {mqsv_it_finalize_06, 29},

    {mqsv_it_qopen_01, 30},
    {mqsv_it_qopen_02, 31},
    {mqsv_it_qopen_03, 32},
    {mqsv_it_qopen_04, 33},
    {mqsv_it_qopen_05, 34},
    {mqsv_it_qopen_06, 35},
    {mqsv_it_qopen_07, 36},
    {mqsv_it_qopen_08, 37},
    {mqsv_it_qopen_09, 38},
    {mqsv_it_qopen_10, 39},
    {mqsv_it_qopen_11, 40},
    {mqsv_it_qopen_12, 41},
    {mqsv_it_qopen_13, 42},
    {mqsv_it_qopen_14, 43},
    {mqsv_it_qopen_15, 44},
    {mqsv_it_qopen_16, 45},
    {mqsv_it_qopen_17, 46},
    {mqsv_it_qopen_18, 47},
    {mqsv_it_qopen_19, 48},
    {mqsv_it_qopen_20, 49},
    {mqsv_it_qopen_21, 50},
    {mqsv_it_qopen_22, 51},
    {mqsv_it_qopen_23, 52},
    {mqsv_it_qopen_24, 53},
    {mqsv_it_qopen_25, 54},
    {mqsv_it_qopen_26, 55},
    {mqsv_it_qopen_27, 56},
    {mqsv_it_qopen_28, 57},
    {mqsv_it_qopen_29, 58},
    {mqsv_it_qopen_30, 59},
    {mqsv_it_qopen_31, 60},
    {mqsv_it_qopen_32, 61},
    {mqsv_it_qopen_33, 62},
    {mqsv_it_qopen_34, 63},
    {mqsv_it_qopen_35, 64},
    {mqsv_it_qopen_36, 65},
    {mqsv_it_qopen_37, 66},
    {mqsv_it_qopen_38, 67},
    {mqsv_it_qopen_39, 68},
    {mqsv_it_qopen_40, 69},
    {mqsv_it_qopen_41, 70},
    {mqsv_it_qopen_42, 71},
    {mqsv_it_qopen_43, 72},
    {mqsv_it_qopen_44, 73},
    {mqsv_it_qopen_45, 74},
    {mqsv_it_qopen_46, 75},
    {mqsv_it_qopen_47, 76},
    {mqsv_it_qopen_48, 77},
    {mqsv_it_qopen_49, 78},
    {mqsv_it_qopen_50, 79},
    {mqsv_it_qopen_51, 80},

    {mqsv_it_close_01, 81},
    {mqsv_it_close_02, 82},
    {mqsv_it_close_03, 83},
    {mqsv_it_close_04, 84},
    {mqsv_it_close_05, 85},
    {mqsv_it_close_06, 86},
    {mqsv_it_close_07, 87},

    {mqsv_it_qstatus_01, 88},
    {mqsv_it_qstatus_02, 89},
    {mqsv_it_qstatus_03, 90},
    {mqsv_it_qstatus_04, 91},
    {mqsv_it_qstatus_05, 92},
    {mqsv_it_qstatus_06, 93},
    {mqsv_it_qstatus_07, 94},

    {mqsv_it_qunlink_01, 95},
    {mqsv_it_qunlink_02, 96},
    {mqsv_it_qunlink_03, 97},
    {mqsv_it_qunlink_04, 98},
    {mqsv_it_qunlink_05, 99},
    {mqsv_it_qunlink_06, 100},
    {mqsv_it_qunlink_07, 101},

    {mqsv_it_qgrp_create_01, 102},
    {mqsv_it_qgrp_create_02, 103},
    {mqsv_it_qgrp_create_03, 104},
    {mqsv_it_qgrp_create_04, 105},
    {mqsv_it_qgrp_create_05, 106},
    {mqsv_it_qgrp_create_06, 107},
    {mqsv_it_qgrp_create_07, 108},
    {mqsv_it_qgrp_create_08, 109},
    {mqsv_it_qgrp_create_09, 110},
    {mqsv_it_qgrp_create_10, 111},
    {mqsv_it_qgrp_create_11, 112},

    {mqsv_it_qgrp_insert_01, 113},
    {mqsv_it_qgrp_insert_02, 114},
    {mqsv_it_qgrp_insert_03, 115},
    {mqsv_it_qgrp_insert_04, 116},
    {mqsv_it_qgrp_insert_05, 117},
    {mqsv_it_qgrp_insert_06, 118},
    {mqsv_it_qgrp_insert_07, 119},
    {mqsv_it_qgrp_insert_08, 120},
    {mqsv_it_qgrp_insert_09, 121},

    {mqsv_it_qgrp_remove_01, 122},
    {mqsv_it_qgrp_remove_02, 123},
    {mqsv_it_qgrp_remove_03, 124},
    {mqsv_it_qgrp_remove_04, 125},
    {mqsv_it_qgrp_remove_05, 126},
    {mqsv_it_qgrp_remove_06, 127},
    {mqsv_it_qgrp_remove_07, 128},

    {mqsv_it_qgrp_delete_01, 129},
    {mqsv_it_qgrp_delete_02, 130},
    {mqsv_it_qgrp_delete_03, 131},
    {mqsv_it_qgrp_delete_04, 132},
    {mqsv_it_qgrp_delete_05, 133},
    {mqsv_it_qgrp_delete_06, 134},
    {mqsv_it_qgrp_delete_07, 135},

    {mqsv_it_qgrp_track_01, 136},
    {mqsv_it_qgrp_track_02, 137},
    {mqsv_it_qgrp_track_03, 138},
    {mqsv_it_qgrp_track_04, 139},
    {mqsv_it_qgrp_track_05, 140},
    {mqsv_it_qgrp_track_06, 141},
    {mqsv_it_qgrp_track_07, 142},
    {mqsv_it_qgrp_track_08, 143},
    {mqsv_it_qgrp_track_09, 144},
    {mqsv_it_qgrp_track_10, 145},
    {mqsv_it_qgrp_track_11, 146},
    {mqsv_it_qgrp_track_12, 147},
    {mqsv_it_qgrp_track_13, 148},
    {mqsv_it_qgrp_track_14, 149},
    {mqsv_it_qgrp_track_15, 150},
    {mqsv_it_qgrp_track_16, 151},
    {mqsv_it_qgrp_track_17, 152},
    {mqsv_it_qgrp_track_18, 153},
    {mqsv_it_qgrp_track_19, 154},
    {mqsv_it_qgrp_track_20, 155},
    {mqsv_it_qgrp_track_21, 156},
    {mqsv_it_qgrp_track_22, 157},
    {mqsv_it_qgrp_track_23, 158},
    {mqsv_it_qgrp_track_24, 159},
    {mqsv_it_qgrp_track_25, 160},
    {mqsv_it_qgrp_track_26, 161},
    {mqsv_it_qgrp_track_27, 162},
    {mqsv_it_qgrp_track_28, 163},
    {mqsv_it_qgrp_track_29, 164},
    {mqsv_it_qgrp_track_30, 165},
    {mqsv_it_qgrp_track_31, 166},
    {mqsv_it_qgrp_track_32, 167},
    {mqsv_it_qgrp_track_33, 168},

    {mqsv_it_qgrp_track_stop_01, 169},
    {mqsv_it_qgrp_track_stop_02, 170},
    {mqsv_it_qgrp_track_stop_03, 171},
    {mqsv_it_qgrp_track_stop_04, 172},
    {mqsv_it_qgrp_track_stop_05, 173},
    {mqsv_it_qgrp_track_stop_06, 174},
    {mqsv_it_qgrp_track_stop_07, 175},

    {mqsv_it_msg_send_01, 176},
    {mqsv_it_msg_send_02, 177},
    {mqsv_it_msg_send_03, 178},
    {mqsv_it_msg_send_04, 179},
    {mqsv_it_msg_send_05, 180},
    {mqsv_it_msg_send_06, 181},
    {mqsv_it_msg_send_07, 182},
    {mqsv_it_msg_send_08, 183},
    {mqsv_it_msg_send_09, 184},
    {mqsv_it_msg_send_10, 185},
    {mqsv_it_msg_send_11, 186},
    {mqsv_it_msg_send_12, 187},
    {mqsv_it_msg_send_13, 188},
    {mqsv_it_msg_send_14, 189},
    {mqsv_it_msg_send_15, 190},
    {mqsv_it_msg_send_16, 191},
    {mqsv_it_msg_send_17, 192},
    {mqsv_it_msg_send_18, 193},
    {mqsv_it_msg_send_19, 194},
    {mqsv_it_msg_send_20, 195},
    {mqsv_it_msg_send_21, 196},
    {mqsv_it_msg_send_22, 197},
    {mqsv_it_msg_send_23, 198},
    {mqsv_it_msg_send_24, 199},
    {mqsv_it_msg_send_25, 200},
    {mqsv_it_msg_send_26, 201},
    {mqsv_it_msg_send_27, 202},
    {mqsv_it_msg_send_28, 203},
    {mqsv_it_msg_send_29, 204},
    {mqsv_it_msg_send_30, 205},
    {mqsv_it_msg_send_31, 206},
    {mqsv_it_msg_send_32, 207},
    {mqsv_it_msg_send_33, 208},
    {mqsv_it_msg_send_34, 209},
    {mqsv_it_msg_send_35, 210},
    {mqsv_it_msg_send_36, 211},
    {mqsv_it_msg_send_37, 212},
    {mqsv_it_msg_send_38, 213},
    {mqsv_it_msg_send_39, 214},
    {mqsv_it_msg_send_40, 215},
    {mqsv_it_msg_send_41, 216},
    {mqsv_it_msg_send_42, 217},
    {mqsv_it_msg_send_43, 218},

    {mqsv_it_msg_get_01, 219},
    {mqsv_it_msg_get_02, 220},
    {mqsv_it_msg_get_03, 221},
    {mqsv_it_msg_get_04, 222},
    {mqsv_it_msg_get_05, 223},
    {mqsv_it_msg_get_06, 224},
    {mqsv_it_msg_get_07, 225},
    {mqsv_it_msg_get_08, 226},
    {mqsv_it_msg_get_09, 227},
    {mqsv_it_msg_get_10, 228},
    {mqsv_it_msg_get_11, 229},
    {mqsv_it_msg_get_12, 230},
    {mqsv_it_msg_get_13, 231},
    {mqsv_it_msg_get_14, 232},
    {mqsv_it_msg_get_15, 233},
    {mqsv_it_msg_get_16, 234},
    {mqsv_it_msg_get_17, 235},
    {mqsv_it_msg_get_18, 236},
    {mqsv_it_msg_get_19, 237},
    {mqsv_it_msg_get_20, 238},
    {mqsv_it_msg_get_21, 239},
    {mqsv_it_msg_get_22, 240},

    {mqsv_it_msg_cancel_01, 241},
    {mqsv_it_msg_cancel_02, 242},
    {mqsv_it_msg_cancel_03, 243},
    {mqsv_it_msg_cancel_04, 244},

    {mqsv_it_msg_sendrcv_01, 245},
    {mqsv_it_msg_sendrcv_02, 246},
    {mqsv_it_msg_sendrcv_03, 247},
    {mqsv_it_msg_sendrcv_04, 248},
    {mqsv_it_msg_sendrcv_05, 249},
    {mqsv_it_msg_sendrcv_06, 250},
    {mqsv_it_msg_sendrcv_07, 251},
    {mqsv_it_msg_sendrcv_08, 252},
    {mqsv_it_msg_sendrcv_09, 253},
    {mqsv_it_msg_sendrcv_10, 254},
    {mqsv_it_msg_sendrcv_11, 255},
    {mqsv_it_msg_sendrcv_12, 256},
    {mqsv_it_msg_sendrcv_13, 257},
    {mqsv_it_msg_sendrcv_14, 258},
    {mqsv_it_msg_sendrcv_15, 259},
    {mqsv_it_msg_sendrcv_16, 260},
    {mqsv_it_msg_sendrcv_17, 261},
    {mqsv_it_msg_sendrcv_18, 262},
    {mqsv_it_msg_sendrcv_19, 263},
    {mqsv_it_msg_sendrcv_20, 264},
    {mqsv_it_msg_sendrcv_21, 265},
    {mqsv_it_msg_sendrcv_22, 266},
    {mqsv_it_msg_sendrcv_23, 267},
    {mqsv_it_msg_sendrcv_24, 268},
    {mqsv_it_msg_sendrcv_25, 269},
    {mqsv_it_msg_sendrcv_26, 270},

    {mqsv_it_msg_reply_01, 271},
    {mqsv_it_msg_reply_02, 272},
    {mqsv_it_msg_reply_03, 273},
    {mqsv_it_msg_reply_04, 274},
    {mqsv_it_msg_reply_05, 275},
    {mqsv_it_msg_reply_06, 276},
    {mqsv_it_msg_reply_07, 277},
    {mqsv_it_msg_reply_08, 278},
    {mqsv_it_msg_reply_09, 279},
    {mqsv_it_msg_reply_10, 280},
    {mqsv_it_msg_reply_11, 281},
    {mqsv_it_msg_reply_12, 282},
    {mqsv_it_msg_reply_13, 283},
    {mqsv_it_msg_reply_14, 284},
    {mqsv_it_msg_reply_15, 285},
    {mqsv_it_msg_reply_16, 286},
    {mqsv_it_msg_reply_17, 287},
    {mqsv_it_msg_reply_18, 288},
    {mqsv_it_msg_reply_19, 289},
    {mqsv_it_msg_reply_20, 290},
    {mqsv_it_msg_reply_21, 291},
    {mqsv_it_msg_reply_22, 292},
    {mqsv_it_msg_reply_23, 293},
    {mqsv_it_msg_reply_24, 294},

    {mqsv_it_msgqs_01, 295},
    {mqsv_it_msgqs_02, 296},
    {mqsv_it_msgqs_03, 297},
    {mqsv_it_msgqs_04, 298},
    {mqsv_it_msgqs_05, 299},
    {mqsv_it_msgqs_06, 300},
    {mqsv_it_msgqs_07, 301},
    {mqsv_it_msgqs_08, 302},
    {mqsv_it_msgqs_09, 303},
    {mqsv_it_msgqs_10, 304},
    {mqsv_it_msgqs_11, 305},
    {mqsv_it_msgqs_12, 306},
    {mqsv_it_msgqs_13, 307},
    {mqsv_it_msgqs_14, 308},
    {mqsv_it_msgqs_15, 309},
    {mqsv_it_msgqs_16, 310},
    {mqsv_it_msgqs_17, 311},
    {mqsv_it_msgqs_18, 312},
    {mqsv_it_msgqs_19, 313},
    {mqsv_it_msgqs_20, 314},
    {mqsv_it_msgqs_21, 315},
    {mqsv_it_msgqs_22, 316},
    {mqsv_it_msgqs_23, 317},

    {mqsv_it_msgq_grps_01, 318},
    {mqsv_it_msgq_grps_02, 319},
    {mqsv_it_msgq_grps_03, 320},
    {mqsv_it_msgq_grps_04, 321},
    {mqsv_it_msgq_grps_05, 322},
    {mqsv_it_msgq_grps_06, 323},
    {mqsv_it_msgq_grps_07, 324},
    {mqsv_it_msgq_grps_08, 325},
    {mqsv_it_msgq_grps_09, 326},
    {mqsv_it_msgq_grps_10, 327},
    {mqsv_it_msgq_grps_11, 328},
    {mqsv_it_msgq_grps_12, 329},

    {mqsv_it_msg_delprop_01, 330},
    {mqsv_it_msg_delprop_02, 331},
    {mqsv_it_msg_delprop_03, 332},
    {mqsv_it_msg_delprop_04, 333},
    {mqsv_it_msg_delprop_05, 334},
    {mqsv_it_msg_delprop_06, 335},
    {mqsv_it_msg_delprop_07, 336},
    {mqsv_it_msg_delprop_08, 337},
    {mqsv_it_msg_delprop_09, 338},
    {mqsv_it_msg_delprop_10, 339},
    {mqsv_it_msg_delprop_11, 340},
    {mqsv_it_msg_delprop_12, 341},
    {mqsv_it_msg_delprop_13, 342},
    {NULL, -1}};

#if (TET_PATCH == 1)

void tet_mqsv_start_instance(TET_MQSV_INST *inst)
{
	int iter;
	int num_of_iter = 1;
	int test_case_num = -1;
	struct tet_testlist *mqsv_testlist = mqsv_onenode_testlist;

	if (inst->tetlist_index == MQSV_ONE_NODE_LIST)
		mqsv_testlist = mqsv_onenode_testlist;

	if (inst->num_of_iter)
		num_of_iter = inst->num_of_iter;

	if (inst->test_case_num)
		test_case_num = inst->test_case_num;

	for (iter = 0; iter < num_of_iter; iter++)
		tet_test_start(test_case_num, mqsv_testlist);
}
#endif

void tet_run_mqsv_app()
{
	TET_MQSV_INST inst;

	tet_mqsv_get_inputs(&inst);
	init_mqsv_test_env();
	tet_mqsv_fill_inputs(&inst);

#if (TET_PATCH == 1)
#ifndef TET_ALL

	tet_mqsv_start_instance(&inst);

	m_TET_MQSV_PRINTF("\n ##### End of Testlist #####\n");
	printf("\n MEM DUMP\n");

	ncs_tmr_whatsout();
	sleep(5);

	tet_test_cleanup();

#else

	tet_test_msgInitialize(MSG_INIT_SUCCESS_T, TEST_CONFIG_MODE);
	mqsv_createthread_all_loop();
	tet_test_msgFinalize(MSG_FINALIZE_SUCCESS_T, TEST_CONFIG_MODE);

	while (1) {
		m_TET_MQSV_PRINTF(
		    "\n\n*******  Starting MQSv testcases *******\n");
		tet_test_start(-1, mqsv_onenode_testlist);
		m_TET_MQSV_PRINTF(
		    "\n\n*******  Ending MQSv testcases *******\n");
	}
#endif

#endif
}

void tet_run_mqsv_dist_cases() {}

void tet_mqsv_startup(void)
{
	/* Using the default mode for startup */
	ncs_agents_startup();

#if (TET_D == 1)
	tet_create_mqd();
#endif

#if (TET_ND == 1)
	tet_create_mqnd();
#endif

	tet_run_mqsv_app();
}

void tet_mqsv_cleanup() { printf(" Ending the agony.. \n"); }

__attribute__((constructor)) static void mqsv_constructor(void)
{
	tet_mqsv_startup();

	test_suite_add(1, "saMsgInitialize Test Suite");
	test_case_add(1, mqsv_it_init_01, "with valid parameters");
	test_case_add(1, mqsv_it_init_02, "with NULL callback structure");
	test_case_add(1, mqsv_it_init_03, "with NULL version parameter");
	test_case_add(1, mqsv_it_init_04, "with NULL message handle");
	test_case_add(1, mqsv_it_init_05,
		      "with NULL callback and version parameters");
	test_case_add(1, mqsv_it_init_06,
		      "with release code > supported release code");
	test_case_add(1, mqsv_it_init_07,
		      "with invalid release code in version");
	test_case_add(1, mqsv_it_init_08,
		      "with major version > supported major version");
	test_case_add(
	    1, mqsv_it_init_09,
	    "returns supported version when called with invalid version");
	test_case_add(1, mqsv_it_init_10, "without registering any callback");

	test_suite_add(2, "saMsgSelectionObjectGet Test Suite");
	test_case_add(2, mqsv_it_selobj_01, "with valid parameters");
	test_case_add(2, mqsv_it_selobj_02,
		      "with NULL selection object parameter");
	test_case_add(2, mqsv_it_selobj_03,
		      "with uninitialized message handle");
	test_case_add(2, mqsv_it_selobj_04, "with finalized message handle");

	test_suite_add(3, "saMsgDispatch Test Suite");
	test_case_add(3, mqsv_it_dispatch_01,
		      "invokes pending callbacks - SA_DISPATCH_ONE");
	test_case_add(3, mqsv_it_dispatch_02,
		      "invokes pending callbacks - SA_DISPATCH_ALL");
	test_case_add(3, mqsv_it_dispatch_03,
		      "invokes pending callbacks - SA_DISPATCH_BLOCKING");
	test_case_add(3, mqsv_it_dispatch_04, "with invalid dispatch flags");
	test_case_add(3, mqsv_it_dispatch_05,
		      "with invalid message handle - SA_DISPATCH_ONE");
	test_case_add(3, mqsv_it_dispatch_06,
		      "with invalid message handle - SA_DISPATCH_ALL");
	test_case_add(3, mqsv_it_dispatch_07,
		      "with invalid message handle - SA_DISPATCH_BLOCKING");
	test_case_add(3, mqsv_it_dispatch_08,
		      "in case of no pending callbacks - SA_DISPATCH_ONE");
	test_case_add(3, mqsv_it_dispatch_09,
		      "in case of no pending callbacks - SA_DISPATCH_ALL");

	test_suite_add(4, "saMsgFinalize Test Suite");
	test_case_add(
	    4, mqsv_it_finalize_01,
	    "closes association between Message Service and app process");
	test_case_add(4, mqsv_it_finalize_02,
		      "with uninitialized message handle");
	test_case_add(4, mqsv_it_finalize_03, "with finalized message handle");
	test_case_add(
	    4, mqsv_it_finalize_04,
	    "Selection object becomes invalid after finalizing the message handle");
	test_case_add(
	    4, mqsv_it_finalize_05,
	    "Message queues that are opened are closed after finalizing the message handle");
	test_case_add(
	    4, mqsv_it_finalize_06,
	    "Group trackings are stopped when that message handle is finalized");

	test_suite_add(5, "saMsgQueueOpen and saMsgQueueOpenAsync Test Suite");
	test_case_add(5, mqsv_it_qopen_01,
		      "saMsgQueueOpen with NULL queue handle");
	test_case_add(5, mqsv_it_qopen_02,
		      "saMsgQueueOpen with NULL queue name");
	test_case_add(5, mqsv_it_qopen_03,
		      "saMsgQueueOpenAsync with NULL queue name");
	test_case_add(5, mqsv_it_qopen_04,
		      "saMsgQueueOpen with uninitialized message handle");
	test_case_add(5, mqsv_it_qopen_05,
		      "saMsgQueueOpen with finalized message handle ");
	test_case_add(5, mqsv_it_qopen_06,
		      "saMsgQueueOpenAsync with uninitialized message handle");
	test_case_add(5, mqsv_it_qopen_07,
		      "saMsgQueueOpenAsync with finalized message handle");
	test_case_add(
	    5, mqsv_it_qopen_08,
	    "saMsgQueueOpen with NULL attributes and SA_MSG_QUEUE_CREATE in open flags");
	test_case_add(
	    5, mqsv_it_qopen_09,
	    "saMsgQueueOpenAsync with NULL attributes and SA_MSG_QUEUE_CREATE in open flags");
	test_case_add(
	    5, mqsv_it_qopen_10,
	    "saMsgQueueOpen with non-NULL attributes and non-create open flags");
	test_case_add(
	    5, mqsv_it_qopen_11,
	    "saMsgQueueOpenAsync with non-NULL attributes and non-create open flags");
	test_case_add(5, mqsv_it_qopen_12,
		      "saMsgQueueOpen with invalid open flags");
	test_case_add(5, mqsv_it_qopen_13,
		      "saMsgQueueOpenAsync with invalid open flags");
	test_case_add(5, mqsv_it_qopen_14,
		      "saMsgQueueOpen with invalid creation flags");
	test_case_add(5, mqsv_it_qopen_15,
		      "saMsgQueueOpenAsync with invalid creation flags");
	test_case_add(5, mqsv_it_qopen_16,
		      "Create a non-persistent queue using saMsgQueueOpen");
	test_case_add(
	    5, mqsv_it_qopen_17,
	    "Create a non-persistent queue using saMsgQueueOpenAsync");
	test_case_add(
	    5, mqsv_it_qopen_18,
	    "Invocation in open callback is same as that given in saMsgQueueOpenAsync");
	test_case_add(5, mqsv_it_qopen_19,
		      "Create a persistent queue using saMsgQueueOpen");
	test_case_add(5, mqsv_it_qopen_20,
		      "Create a persistent queue using saMsgQueueOpenAsync");
	test_case_add(
	    5, mqsv_it_qopen_21,
	    "Create a queue with SA_MSG_QUEUE_EMPTY using saMsgQueueOpen");
	test_case_add(
	    5, mqsv_it_qopen_22,
	    "Create a queue with SA_MSG_QUEUE_EMPTY using saMsgQueueOpenAsync");
	test_case_add(
	    5, mqsv_it_qopen_23,
	    "Create a queue with SA_MSG_QUEUE_RECEIVE_CALLBACK using saMsgQueueOpen");
	test_case_add(
	    5, mqsv_it_qopen_24,
	    "Create a queue with SA_MSG_QUEUE_RECEIVE_CALLBACK using saMsgQueueOpenAsync");
	test_case_add(
	    5, mqsv_it_qopen_25,
	    "Create a queue with zero retention time using saMsgQueueOpen");
	test_case_add(
	    5, mqsv_it_qopen_26,
	    "Create a queue with zero retention time using saMsgQueueOpenAsync");
	test_case_add(5, mqsv_it_qopen_27,
		      "Create a queue with zero size using saMsgQueueOpen");
	test_case_add(
	    5, mqsv_it_qopen_28,
	    "Create a queue with zero size using saMsgQueueOpenAsync");
	test_case_add(5, mqsv_it_qopen_29,
		      "saMsgQueueOpen with zero timeout value");
	test_case_add(
	    5, mqsv_it_qopen_30,
	    "Open an existing queue with different attributes - saMsgQueueOpen");
	test_case_add(
	    5, mqsv_it_qopen_31,
	    "Open a closed queue with different attributes - saMsgQueueOpen");
	test_case_add(
	    5, mqsv_it_qopen_32,
	    "Open an existing queue with different attributes - saMsgQueueOpenAsync");
	test_case_add(
	    5, mqsv_it_qopen_33,
	    "Open a closed queue with different attributes - saMsgQueueOpenAsync");
	test_case_add(
	    5, mqsv_it_qopen_34,
	    "Open a queue that does not exist - NULL attr and zero open flag");
	test_case_add(
	    5, mqsv_it_qopen_35,
	    "Open an queue that does not exist - NULL attr and non-create open flag");
	test_case_add(
	    5, mqsv_it_qopen_36,
	    "Open a queue that does not exist - NULL attr and zero open flag");
	test_case_add(
	    5, mqsv_it_qopen_37,
	    "Open an queue that does not exist - NULL attr and non-create open flag");
	test_case_add(
	    5, mqsv_it_qopen_38,
	    "Open an existing open queue with same attributes - saMsgQueueOpen");
	test_case_add(
	    5, mqsv_it_qopen_39,
	    "Open an existing open queue with same attributes - saMsgQueueOpenAsync");
	test_case_add(
	    5, mqsv_it_qopen_40,
	    "Open an open queue with NULL attributes and non-create open flags");
	test_case_add(
	    5, mqsv_it_qopen_41,
	    "Open an open queue with NULL attributes and non-create open flags");
	test_case_add(
	    5, mqsv_it_qopen_42,
	    "saMsgQueueOpenAsync without registering the open callback");
	test_case_add(
	    5, mqsv_it_qopen_43,
	    "saMsgQueueOpen with recv clbk flag without registering received callback");
	test_case_add(
	    5, mqsv_it_qopen_44,
	    "saMsgQueueOpenAsync with recv clbk flag without registering received clbk");
	test_case_add(
	    5, mqsv_it_qopen_45,
	    "Queue handle obtained in open clbk is valid when error is SA_AIS_OK");
	test_case_add(5, mqsv_it_qopen_46,
		      "Open a closed queue using saMsgQueueOpen");
	test_case_add(
	    5, mqsv_it_qopen_47,
	    "Open a closed queue with empty flag using saMsgQueueOpen");
	test_case_add(
	    5, mqsv_it_qopen_48,
	    "Open a closed queue with recv clbk flag using saMsgQueueOpen");
	test_case_add(5, mqsv_it_qopen_49,
		      "Open a closed queue using saMsgQueueOpenAsync");
	test_case_add(
	    5, mqsv_it_qopen_50,
	    "Open a closed queue with empty flag using saMsgQueueOpenAsync");
	test_case_add(
	    5, mqsv_it_qopen_51,
	    "Open a closed queue with recv clbk flag using saMsgQueueOpenAsync");

	test_suite_add(6, "saMsgQueueClose Test Suite");
	test_case_add(6, mqsv_it_close_01,
		      "saMsgQueueClose with invalid queue handle");
	test_case_add(
	    6, mqsv_it_close_02,
	    "saMsgQueueClose with a queue handle associated with finalized msg handle");
	test_case_add(6, mqsv_it_close_03, "Close a message queue");
	test_case_add(6, mqsv_it_close_04,
		      "saMsgQueueClose with a closed queue handle");
	test_case_add(
	    6, mqsv_it_close_05,
	    "Closing a queue that is already unlinked will delete the queue");
	test_case_add(
	    6, mqsv_it_close_06,
	    "Closing a non-persistent queue with zero retention time will delete the queue");
	test_case_add(
	    6, mqsv_it_close_07,
	    "saMsgQueueClose cancels all pending callbacks on that queue handle");

	test_suite_add(7, "saMsgQueueStatusGet Test Suite");
	test_case_add(
	    7, mqsv_it_qstatus_01,
	    "saMsgQueueStatusGet with invalid message handle: Uninitialized message handle, Finalized message handle");
	test_case_add(7, mqsv_it_qstatus_02,
		      "saMsgQueueStatusGet with NULL queue name");
	test_case_add(7, mqsv_it_qstatus_03,
		      "saMsgQueueStatusGet with NULL status parameter");
	test_case_add(7, mqsv_it_qstatus_04,
		      "Get the status of a message queue");
	test_case_add(7, mqsv_it_qstatus_05,
		      "Get the status of a message queue that does not exist");
	test_case_add(7, mqsv_it_qstatus_06,
		      "Get the status of a message queue that closed");
	test_case_add(
	    7, mqsv_it_qstatus_07,
	    "Get the status of a message queue when a message is in the queue");

	test_suite_add(8, "saMsgQueueUnlink Test Suite");
	test_case_add(
	    8, mqsv_it_qunlink_01,
	    "saMsgQueueUnlink with invalid message handle: Uninitialized message handle, Finalized message handle");
	test_case_add(8, mqsv_it_qunlink_02,
		      "saMsgQueueUnlink with NULL queue name");
	test_case_add(8, mqsv_it_qunlink_03,
		      "Unlink a message queue that does not exist");
	test_case_add(8, mqsv_it_qunlink_04, "Unlink a message queue");
	test_case_add(8, mqsv_it_qunlink_05,
		      "Unlink a message queue that is not open by any process");
	test_case_add(
	    8, mqsv_it_qunlink_06,
	    "Unlinking a queue that is open by any process will not delete the queue");
	test_case_add(
	    8, mqsv_it_qunlink_07,
	    "After saMsgQueueUnlink, all apis that use this queue name return errors");

	test_suite_add(9, "saMsgQueueGroupCreate Test Suite");
	test_case_add(
	    9, mqsv_it_qgrp_create_01,
	    "saMsgQueueGroupCreate with invalid message handle: Uninitialized message handle, Finalized message handle");
	test_case_add(9, mqsv_it_qgrp_create_02,
		      "saMsgQueueGroupCreate with null group name");
	test_case_add(9, mqsv_it_qgrp_create_03,
		      "saMsgQueueGroupCreate with bad queue group policy");
	test_case_add(9, mqsv_it_qgrp_create_04,
		      "Create a message queue group");
	test_case_add(
	    9, mqsv_it_qgrp_create_05,
	    "saMsgQueueGroupCreate with queue group that already exists");
	test_case_add(
	    9, mqsv_it_qgrp_create_06,
	    "saMsgQueueGroupCreate with existing group name with different group policy");
	test_case_add(
	    9, mqsv_it_qgrp_create_07,
	    "saMsgQueueGroupCreate with existing group name with different group policy");
	test_case_add(
	    9, mqsv_it_qgrp_create_08,
	    "saMsgQueueGroupCreate with existing group name with different group policy");
	test_case_add(
	    9, mqsv_it_qgrp_create_09,
	    "saMsgQueueGroupCreate with a group policy not supported");
	test_case_add(
	    9, mqsv_it_qgrp_create_10,
	    "saMsgQueueGroupCreate with a group policy not supported");
	test_case_add(
	    9, mqsv_it_qgrp_create_11,
	    "saMsgQueueGroupCreate with a group policy not supported");

	test_suite_add(10, "saMsgQueueGroupInsert Test Suite");
	test_case_add(
	    10, mqsv_it_qgrp_insert_01,
	    "saMsgQueueGroupInsert with uninitilaized message handle: Uninitialized message handle, Finalized message handle");
	test_case_add(10, mqsv_it_qgrp_insert_02,
		      "saMsgQueueGroupInsert with NULL queue group name");
	test_case_add(10, mqsv_it_qgrp_insert_03,
		      "saMsgQueueGroupInsert with NULL queue name");
	test_case_add(10, mqsv_it_qgrp_insert_04,
		      "Insert a queue into a group");
	test_case_add(10, mqsv_it_qgrp_insert_05,
		      "Insert a non-existing queue into a group");
	test_case_add(10, mqsv_it_qgrp_insert_06,
		      "Insert a queue into a non-existing group");
	test_case_add(10, mqsv_it_qgrp_insert_07,
		      "Insert a queue into a group more than once");
	test_case_add(10, mqsv_it_qgrp_insert_08,
		      "Insert a queue into a non-empty group");
	test_case_add(
	    10, mqsv_it_qgrp_insert_09,
	    "Insert the same queue queue into a two different queue groups");

	test_suite_add(11, "saMsgQueueGroupRemove Test Suite");
	test_case_add(
	    11, mqsv_it_qgrp_remove_01,
	    "saMsgQueueGroupRemove with invalid message handle: Uninitialized message handle, Finalized message handle");
	test_case_add(11, mqsv_it_qgrp_remove_02,
		      "saMsgQueueGroupRemove with NULL queue name");
	test_case_add(11, mqsv_it_qgrp_remove_03,
		      "saMsgQueueGroupRemove with NULL group name");
	test_case_add(11, mqsv_it_qgrp_remove_04,
		      "Remove a queue from a queue group");
	test_case_add(11, mqsv_it_qgrp_remove_05,
		      "Remove a queue from a non-existing queue group");
	test_case_add(11, mqsv_it_qgrp_remove_06,
		      "Remove a non-existing queue from a queue group");
	test_case_add(
	    11, mqsv_it_qgrp_remove_07,
	    "Remove a queue that is not a member of that queue group");

	test_suite_add(12, "saMsgQueueGroupDelete Test Suite");
	test_case_add(
	    12, mqsv_it_qgrp_delete_01,
	    "saMsgQueueGroupDelete with invalid message handle: Uninitialized message handle, Finalized message handle");
	test_case_add(12, mqsv_it_qgrp_delete_02,
		      "saMsgQueueGroupDelete with NULL queue group name");
	test_case_add(12, mqsv_it_qgrp_delete_03,
		      "Delete a message queue group");
	test_case_add(12, mqsv_it_qgrp_delete_04,
		      "Delete a message queue group that is not existing");
	test_case_add(12, mqsv_it_qgrp_delete_05,
		      "Delete a queue group with member queues");
	test_case_add(12, mqsv_it_qgrp_delete_06, "Delete a empty queue group");
	test_case_add(12, mqsv_it_qgrp_delete_07,
		      "Send a message to a group that is already deleted");

	test_suite_add(13, "saMsgQueueGroupTrack Test Suite");
	test_case_add(
	    13, mqsv_it_qgrp_track_01,
	    "saMsgQueueGroupTrack with invalid message handle: Uninitialized message handle, Finalized message handle");
	test_case_add(13, mqsv_it_qgrp_track_02,
		      "saMsgQueueGroupTrack with NULL queue group name");
	test_case_add(
	    13, mqsv_it_qgrp_track_03,
	    "saMsgQueueGroupTrack with a group name that does not exist");
	test_case_add(13, mqsv_it_qgrp_track_04,
		      "saMsgQueueGroupTrack with a invalid track flags");
	test_case_add(13, mqsv_it_qgrp_track_05,
		      "saMsgQueueGroupTrack with a wrong track flags");
	test_case_add(
	    13, mqsv_it_qgrp_track_06,
	    "saMsgQueueGroupTrack with invalid notification buffer - SA_TRACK_CURRENT");
	test_case_add(
	    13, mqsv_it_qgrp_track_07,
	    "saMsgQueueGroupTrack without registering track callback - SA_TRACK_CURRENT");
	test_case_add(
	    13, mqsv_it_qgrp_track_08,
	    "saMsgQueueGroupTrack without track callback - Track Current (non-NULL buffer)");
	test_case_add(
	    13, mqsv_it_qgrp_track_09,
	    "saMsgQueueGroupTrack without registering track callback - SA_TRACK_CHANGES");
	test_case_add(
	    13, mqsv_it_qgrp_track_10,
	    "saMsgQueueGroupTrack without registering track callback - SA_TRACK_CHANGES_ONLY");
	test_case_add(
	    13, mqsv_it_qgrp_track_11,
	    "Track with SA_TRACK_CURRENT - non-Null notif-buffer and NULL notification");
	test_case_add(
	    13, mqsv_it_qgrp_track_12,
	    "Track with SA_TRACK_CURRENT - non-Null noif-buffer and non-NULL notification");
	test_case_add(
	    13, mqsv_it_qgrp_track_13,
	    "saMsgQueueGroupTrack with insufficient notification buffer");
	test_case_add(
	    13, mqsv_it_qgrp_track_14,
	    "saMsgQueueGroupTrack updates numberOfItems when return SA_AIS_ERR_NO_SPACE");
	test_case_add(
	    13, mqsv_it_qgrp_track_15,
	    "saMsgQueueGroupTrack with NULL notification buffer - SA_TRACK_CURRENT");
	test_case_add(
	    13, mqsv_it_qgrp_track_16,
	    "saMsgQueueGroupTrack with SA_TRACK_CHANGES and invalid buffer");
	test_case_add(
	    13, mqsv_it_qgrp_track_17,
	    "saMsgQueueGroupTrack with SA_TRACK_CHANGES_ONLY and invalid buffer");
	test_case_add(
	    13, mqsv_it_qgrp_track_18,
	    "Track with SA_TRACK_CURRENT | SA_TRACK_CHANGES and invalid buffer");
	test_case_add(
	    13, mqsv_it_qgrp_track_19,
	    "Track with SA_TRACK_CURRENT | SA_TRACK_CHANGES_ONLY and invalid buffer");
	test_case_add(
	    13, mqsv_it_qgrp_track_20,
	    "Track with SA_TRACK_CHANGES and insert a queue into the group");
	test_case_add(
	    13, mqsv_it_qgrp_track_21,
	    "Track with SA_TRACK_CHANGES_ONLY and insert a queue into the group");
	test_case_add(
	    13, mqsv_it_qgrp_track_22,
	    "Track with SA_TRACK_CHANGES and remove a queue from the group");
	test_case_add(
	    13, mqsv_it_qgrp_track_23,
	    "Track with SA_TRACK_CHANGES_ONLY and remove a queue from the group");
	test_case_add(
	    13, mqsv_it_qgrp_track_24,
	    "Track with SA_TRACK_CURRENT | SA_TRACK_CHANGES with NULL notif-buffer");
	test_case_add(
	    13, mqsv_it_qgrp_track_25,
	    "Track with SA_TRACK_CURRENT | SA_TRACK_CHANGES_ONLY with NULL notif-buffer");
	test_case_add(
	    13, mqsv_it_qgrp_track_26,
	    "Track with SA_TRACK_CURRENT | SA_TRACK_CHANGES with non-NULL notif-buffer");
	test_case_add(
	    13, mqsv_it_qgrp_track_27,
	    "Track with SA_TRACK_CURRENT | SA_TRACK_CHANGES_ONLY with non-NULL notif-buffer");
	test_case_add(
	    13, mqsv_it_qgrp_track_28,
	    "Track a group with TRACK_CHANGES_ONLY while being tracked with TRACK_CHANGES");
	test_case_add(
	    13, mqsv_it_qgrp_track_29,
	    "Track a group with TRACK_CHANGES while being tracked with TRACK_CHANGES_ONLY");
	test_case_add(
	    13, mqsv_it_qgrp_track_30,
	    "Track info gives the policy of the queue group - SA_TRACK_CURRENT");
	test_case_add(
	    13, mqsv_it_qgrp_track_31,
	    "Track info gives the policy of the queue group - SA_TRACK_CHANGES");
	test_case_add(
	    13, mqsv_it_qgrp_track_32,
	    "Deleting a queue group while being tracked with SA_TRACK_CHANGES");
	test_case_add(
	    13, mqsv_it_qgrp_track_33,
	    "Deleting a queue group while being tracked with SA_TRACK_CHANGES_ONLY");

	test_suite_add(14, "saMsgQueueGroupTrack Test Suite");
	test_case_add(
	    14, mqsv_it_qgrp_track_01,
	    "saMsgQueueGroupTrackStop with invalid message handle: Uninitialized message handle, inalized message handle");
	test_case_add(14, mqsv_it_qgrp_track_02,
		      "saMsgQueueGroupTrackStop with NULL group name");
	test_case_add(
	    14, mqsv_it_qgrp_track_03,
	    "saMsgQueueGroupTrackStop with group name that does not exist");
	test_case_add(
	    14, mqsv_it_qgrp_track_04,
	    "Stop tracking a group that is tracked with SA_TRACK_CHANGES");
	test_case_add(
	    14, mqsv_it_qgrp_track_05,
	    "Stop tracking a group that is tracked with SA_TRACK_CHANGES_ONLY");
	test_case_add(
	    14, mqsv_it_qgrp_track_06,
	    "Stop tracking a group that is tracked with SA_TRACK_CURRENT");
	test_case_add(14, mqsv_it_qgrp_track_07,
		      "saMsgQueueGroupTrackStop with an untracked group");

	test_suite_add(15,
		       "saMsgMessageSend and saMsgMessageSendAsync Test Suite");
	test_case_add(
	    15, mqsv_it_msg_send_01,
	    "saMsgMessageSend with invalid message handle: Uninitialized message handle, Finalized message handle");
	test_case_add(
	    15, mqsv_it_msg_send_02,
	    "saMsgMessageSendAsync with invalid message handle: Uninitialized message handle, Finalized message handle");
	test_case_add(15, mqsv_it_msg_send_03,
		      "saMsgMessageSend with NULL destination");
	test_case_add(15, mqsv_it_msg_send_04,
		      "saMsgMessageSendAsync with NULL destination");
	test_case_add(15, mqsv_it_msg_send_05,
		      "saMsgMessageSend with NULL message parameter");
	test_case_add(15, mqsv_it_msg_send_06,
		      "saMsgMessageSendAsync with NULL message parameter");
	test_case_add(15, mqsv_it_msg_send_07,
		      "saMsgMessageSend with zero timeout");
	test_case_add(15, mqsv_it_msg_send_08,
		      "Send to queue that does not exist");
	test_case_add(15, mqsv_it_msg_send_09,
		      "SendAsync to queue that does not exist");
	test_case_add(15, mqsv_it_msg_send_10,
		      "Send to queue group that does not exist");
	test_case_add(15, mqsv_it_msg_send_11,
		      "SendAsync to queue group that does not exist");
	test_case_add(15, mqsv_it_msg_send_12,
		      "Send a message to a message queue - saMsgMessageSend");
	test_case_add(
	    15, mqsv_it_msg_send_13,
	    "Send a message to a message queue - saMsgMessageSendAsync");
	test_case_add(
	    15, mqsv_it_msg_send_14,
	    "Send a message to a message queue group - saMsgMessageSend");
	test_case_add(
	    15, mqsv_it_msg_send_15,
	    "Send a message to a message queue group - saMsgMessageSendAsync");
	test_case_add(
	    15, mqsv_it_msg_send_16,
	    "Send a message to a member queue of a group - saMsgMessageSend");
	test_case_add(
	    15, mqsv_it_msg_send_17,
	    "Send a message to a member queue of a group - saMsgMessageSendAsync");
	test_case_add(
	    15, mqsv_it_msg_send_18,
	    "Delivered clbk in called when saMsgMessageSendAsync is called with ackFlag = 1");
	test_case_add(
	    15, mqsv_it_msg_send_19,
	    "Invocation in delivered clbk is same as that given in saMsgMessageSendAsync");
	test_case_add(
	    15, mqsv_it_msg_send_20,
	    "Process is not intimated of delivery when message is sent with ackFlags = 0");
	test_case_add(
	    15, mqsv_it_msg_send_21,
	    "SendAsync without registering with delivered callback (ackFlags = 1)");
	test_case_add(
	    15, mqsv_it_msg_send_22,
	    "SendAsync without registering with delivered callback (ackFlags = 0)");
	test_case_add(15, mqsv_it_msg_send_23,
		      "saMsgMessageSendAsync with invalid ackFlags");
	test_case_add(15, mqsv_it_msg_send_24, "Send to a queue that is full");
	test_case_add(15, mqsv_it_msg_send_25,
		      "SendAsync to a queue that is full");
	test_case_add(15, mqsv_it_msg_send_26, "Send to an empty queue group");
	test_case_add(15, mqsv_it_msg_send_27,
		      "SendAsync to an empty queue group");
	test_case_add(15, mqsv_it_msg_send_28,
		      "Send a message with invalid priority");
	test_case_add(
	    15, mqsv_it_msg_send_29,
	    "saMsgMessageSendAsync with a message with invalid priority");
	test_case_add(15, mqsv_it_msg_send_30, "Send to a zero size queue");
	test_case_add(15, mqsv_it_msg_send_31,
		      "SendAsync to a zero size queue");
	test_case_add(15, mqsv_it_msg_send_32, "Send to an unavailable queue");
	test_case_add(15, mqsv_it_msg_send_33,
		      "SendAsync to an unavailable queue");
	test_case_add(15, mqsv_it_msg_send_34,
		      "Send a message with NULL sender name to a queue");
	test_case_add(
	    15, mqsv_it_msg_send_35,
	    "saMsgMessageSendAsync a message with NULL sender name to a queue");
	test_case_add(15, mqsv_it_msg_send_36,
		      "Send a big message to a small size queue");
	test_case_add(
	    15, mqsv_it_msg_send_37,
	    "saMsgMessageSendAsync with a  big message to a small size queue");
	test_case_add(15, mqsv_it_msg_send_38,
		      "Send to a member queue of a group that is full");
	test_case_add(15, mqsv_it_msg_send_39,
		      "SendAsync to a member queue of a group that is full");
	test_case_add(
	    15, mqsv_it_msg_send_40,
	    "Send a message to a message queue with zero size - saMsgMessageSend");
	test_case_add(
	    15, mqsv_it_msg_send_41,
	    "Send a message to a message queue with zero size - saMsgMessageSendAsync");
	test_case_add(
	    15, mqsv_it_msg_send_42,
	    "Send to a multicast group with members queues that are full");
	test_case_add(
	    15, mqsv_it_msg_send_43,
	    "Send Async to a multicast group with members queues that are full");

	test_suite_add(16, "saMsgMessageGet Test Suite");
	test_case_add(
	    16, mqsv_it_msg_get_01,
	    "saMsgMessageGet with invalid queue handle: Invalid queue handle, losed queue handle");
	test_case_add(
	    16, mqsv_it_msg_get_02,
	    "saMsgMessageGet with queue handle associated with finalized message handle");
	test_case_add(16, mqsv_it_msg_get_03,
		      "saMsgMessageGet with NULL receive message");
	test_case_add(16, mqsv_it_msg_get_04,
		      "saMsgMessageGet with NULL sender id");
	test_case_add(
	    16, mqsv_it_msg_get_05,
	    "saMsgMessageGet with zero timeout with no message in the queue");
	test_case_add(
	    16, mqsv_it_msg_get_06,
	    "saMsgMessageGet with zero timeout when there is a message in the queue");
	test_case_add(16, mqsv_it_msg_get_07,
		      "Receive a message from the queue");
	test_case_add(16, mqsv_it_msg_get_08,
		      "Receive a message from the queue with NULL send time");
	test_case_add(16, mqsv_it_msg_get_09,
		      "Message is removed from the queue once it is received");
	test_case_add(
	    16, mqsv_it_msg_get_10,
	    "Messages are received in their priority order (higher to lower)");
	test_case_add(
	    16, mqsv_it_msg_get_11,
	    "Messages of same priority are receive in the order in which they are sent");
	test_case_add(16, mqsv_it_msg_get_12,
		      "saMsgMessageGet with NULL data in message parameter");
	test_case_add(16, mqsv_it_msg_get_13,
		      "saMsgMessageGet with NULL data in message parameter");
	test_case_add(
	    16, mqsv_it_msg_get_14,
	    "saMsgMessageGet with data buffer too small to hold the received message");
	test_case_add(
	    16, mqsv_it_msg_get_15,
	    "saMsgMessageGet with data buffer too small to hold the received message");
	test_case_add(
	    16, mqsv_it_msg_get_16,
	    "saMsgMessageGet updates the size of message when return SA_AIS_ERR_NO_SPACE");
	test_case_add(
	    16, mqsv_it_msg_get_17,
	    "saMsgMessageGet updates the size of message when return SA_AIS_OK");
	test_case_add(
	    16, mqsv_it_msg_get_18,
	    "When saMsgMessageSend or SendAsync is used to send, sender_id = 0");
	test_case_add(
	    16, mqsv_it_msg_get_19,
	    "Receive a message from the queue with non-NULL send time parameter");
	test_case_add(16, mqsv_it_msg_get_20,
		      "Receive a message from the queue with NULL senderName");
	test_case_add(
	    16, mqsv_it_msg_get_21,
	    "Receive a message from the queue when no senderName is sent");
	test_case_add(
	    16, mqsv_it_msg_get_22,
	    "when saMsgMessageGet returns SA_AIS_ERR_NO_SPACE the message is not consumed");

	test_suite_add(17, "saMsgMessageCancel Test Suite");
	test_case_add(
	    17, mqsv_it_msg_cancel_01,
	    "saMsgMessageCancel with invalid queue handle: Invalid queue handle, Closed queue handle");
	test_case_add(
	    17, mqsv_it_msg_cancel_02,
	    "saMsgMessageCancel with queue handle associated with finalized message handle");
	test_case_add(
	    17, mqsv_it_msg_cancel_03,
	    "saMsgMessageCancel when there is no blocking call to saMsgMessageGet");
	test_case_add(
	    17, mqsv_it_msg_cancel_04,
	    "saMsgMessageCancel cancels the blocking call to saMsgMessageGet");

	test_suite_add(18, "saMsgMessageSendReceive Test Suite");
	test_case_add(
	    18, mqsv_it_msg_sendrcv_01,
	    "saMsgMessageSendReceive with invalid message handle: Uninitialized message handle, Finalized message handle");
	test_case_add(18, mqsv_it_msg_sendrcv_02,
		      "saMsgMessageSendReceive with NULL destination");
	test_case_add(18, mqsv_it_msg_sendrcv_03,
		      "saMsgMessageSendReceive with NULL sendMessage");
	test_case_add(18, mqsv_it_msg_sendrcv_04,
		      "saMsgMessageSendReceive with NULL reply buffer");
	test_case_add(18, mqsv_it_msg_sendrcv_05,
		      "saMsgMessageSendReceive with NULL send time");
	test_case_add(
	    18, mqsv_it_msg_sendrcv_06,
	    "saMsgMessageSendReceive to send and receive a message from a queue");
	test_case_add(
	    18, mqsv_it_msg_sendrcv_07,
	    "saMsgMessageSendReceive to send and receive a message from a group");
	test_case_add(
	    18, mqsv_it_msg_sendrcv_08,
	    "saMsgMessageSendReceive with non-NULL send time parameter");
	test_case_add(
	    18, mqsv_it_msg_sendrcv_09,
	    "When message is sent using saMsgMessageSendReceive, sender_id != 0");
	test_case_add(
	    18, mqsv_it_msg_sendrcv_10,
	    "saMsgMessageSendReceive to a destination queue that is full");
	test_case_add(
	    18, mqsv_it_msg_sendrcv_11,
	    "saMsgMessageSendReceive to a destination queue of zero size");
	test_case_add(18, mqsv_it_msg_sendrcv_12,
		      "saMsgMessageSendReceive to a destination without reply");
	test_case_add(
	    18, mqsv_it_msg_sendrcv_13,
	    "saMsgMessageSendReceive to a destination queue that does not exist");
	test_case_add(
	    18, mqsv_it_msg_sendrcv_14,
	    "saMsgMessageSendReceive to a destination queue group that does not exist");
	test_case_add(18, mqsv_it_msg_sendrcv_15,
		      "saMsgMessageSendReceive with insufficient reply buffer");
	test_case_add(18, mqsv_it_msg_sendrcv_16,
		      "saMsgMessageSendReceive to an unavailable queue");
	test_case_add(18, mqsv_it_msg_sendrcv_17,
		      "saMsgMessageSendReceive to an empty queue group");
	test_case_add(
	    18, mqsv_it_msg_sendrcv_18,
	    "saMsgMessageSendReceive with non-NULL data in reply buffer");
	test_case_add(
	    18, mqsv_it_msg_sendrcv_19,
	    "saMsgMessageSendReceive with invalid priority in sendMessage");
	test_case_add(
	    18, mqsv_it_msg_sendrcv_20,
	    "saMsgMessageSendReceive to a member queue of a queue group");
	test_case_add(
	    18, mqsv_it_msg_sendrcv_21,
	    "saMsgMessageSendReceive to a member queue of a queue group that is full");
	test_case_add(
	    18, mqsv_it_msg_sendrcv_22,
	    "saMsgMessageSendReceive to send a message with NULL sender name");
	test_case_add(
	    18, mqsv_it_msg_sendrcv_23,
	    "saMsgMessageSendReceive with NULL sender name in the reply buffer");
	test_case_add(
	    18, mqsv_it_msg_sendrcv_24,
	    "SendReceive with sender name in the reply buffer and reply with NULL sender name");
	test_case_add(
	    18, mqsv_it_msg_sendrcv_25,
	    "saMsgMessageSendReceive to send a message with zero size to a queue");
	test_case_add(18, mqsv_it_msg_sendrcv_26,
		      "saMsgMessageSendReceive to a multicast queue group");

	test_suite_add(
	    19, "saMsgMessageReply and saMsgMessageReplyAsync Test Suite");
	test_case_add(
	    19, mqsv_it_msg_reply_01,
	    "saMsgMessageReply with invalid message handle: Uninitialized message handle, Finalized message handle");
	test_case_add(
	    19, mqsv_it_msg_reply_02,
	    "saMsgMessageReplyAsync with invalid message handle: Uninitialized message handle, Finalized message handle");
	test_case_add(19, mqsv_it_msg_reply_03,
		      "saMsgMessageReply with NULL replyMessage");
	test_case_add(19, mqsv_it_msg_reply_04,
		      "saMsgMessageReplyAsync with NULL replyMessage");
	test_case_add(19, mqsv_it_msg_reply_05,
		      "saMsgMessageReply with NULL pointer to sender_id");
	test_case_add(19, mqsv_it_msg_reply_06,
		      "saMsgMessageReplyAsync with NULL pointer to sender_id");
	test_case_add(19, mqsv_it_msg_reply_07,
		      "saMsgMessageReply with invalid sender_id");
	test_case_add(19, mqsv_it_msg_reply_08,
		      "saMsgMessageReplyAsync with invalid sender_id");
	test_case_add(
	    19, mqsv_it_msg_reply_09,
	    "saMsgMessageReply with a reply message size greater than the reply buffer size");
	test_case_add(
	    19, mqsv_it_msg_reply_10,
	    "saMsgMessageReplyAsync with a message size greater than the reply buffer size");
	test_case_add(
	    19, mqsv_it_msg_reply_11,
	    "saMsgMessageReplyAsync without registering delivered callback (ackFlags = 0)");
	test_case_add(
	    19, mqsv_it_msg_reply_12,
	    "saMsgMessageReplyAsync without registering delivered callback (ackFlags = 1)");
	test_case_add(19, mqsv_it_msg_reply_13,
		      "saMsgMessageReplyAsync with invalid ackFlags");
	test_case_add(19, mqsv_it_msg_reply_14,
		      "Reply a message using saMsgMessageReply");
	test_case_add(
	    19, mqsv_it_msg_reply_15,
	    "Reply a message using saMsgMessageReplyAsync with acknowledgement");
	test_case_add(
	    19, mqsv_it_msg_reply_16,
	    "Reply a message using saMsgMessageReplyAsync without acknowledgement");
	test_case_add(
	    19, mqsv_it_msg_reply_17,
	    "Process cannot reply to a message more than once - saMsgMessageReply");
	test_case_add(
	    19, mqsv_it_msg_reply_18,
	    "Process cannot reply to a message more than once - saMsgMessageReplyAsync");
	test_case_add(19, mqsv_it_msg_reply_19,
		      "saMsgMessageReply with NULL sender name");
	test_case_add(19, mqsv_it_msg_reply_20,
		      "saMsgMessageReplyAsync with NULL sender name");
	test_case_add(
	    19, mqsv_it_msg_reply_21,
	    "Reply to a message that is not sent by saMsgMessageSendReceive");
	test_case_add(
	    19, mqsv_it_msg_reply_22,
	    "ReplyAsync to a message that is not sent by saMsgMessageSendReceive");
	test_case_add(19, mqsv_it_msg_reply_23,
		      "Reply a message with zero message size");
	test_case_add(
	    19, mqsv_it_msg_reply_24,
	    "Reply a message using saMsgMessageReplyAsync with zero size message");

	test_suite_add(20, "Message Queues Test Suite");
	test_case_add(
	    20, mqsv_it_msgqs_01,
	    "Messages can be written to a message queue - saMsgMessageSend");
	test_case_add(
	    20, mqsv_it_msgqs_02,
	    "Messages can be written to a message queue - saMsgMessageSendAsync");
	test_case_add(20, mqsv_it_msgqs_03,
		      "Messages can be read from a message queue");
	test_case_add(
	    20, mqsv_it_msgqs_04,
	    "Non-Persistent queue will be removed if it is closed for retention time");
	test_case_add(
	    20, mqsv_it_msgqs_05,
	    "Non-Persistent queue will be removed if it is closed for retention time");
	test_case_add(
	    20, mqsv_it_msgqs_06,
	    "Persistent queue will not be removed if it is closed for retention time");
	test_case_add(
	    20, mqsv_it_msgqs_07,
	    "Persistent queue will not be removed if it is closed for retention time");
	test_case_add(
	    20, mqsv_it_msgqs_08,
	    "Opening a closed Non-Persistent queue before completion of retention time");
	test_case_add(
	    20, mqsv_it_msgqs_09,
	    "Opening a closed Non-Persistent queue before completion of retention time");
	test_case_add(
	    20, mqsv_it_msgqs_10,
	    "Message Service preserves messages that are not consumed - Non-Persistent");
	test_case_add(
	    20, mqsv_it_msgqs_11,
	    "Message Service preserves messages that are not consumed - Persistent");
	test_case_add(
	    20, mqsv_it_msgqs_12,
	    "Process should open the queue before retrieving messages from it");
	test_case_add(20, mqsv_it_msgqs_13,
		      "Open a message queue empty - saMsgQueueOpen");
	test_case_add(20, mqsv_it_msgqs_14,
		      "Open a message queue empty - saMsgQueueOpenAsync");
	test_case_add(20, mqsv_it_msgqs_15,
		      "Send a message to a closed queue - saMsgMessageSend");
	test_case_add(
	    20, mqsv_it_msgqs_16,
	    "Send a message to a closed queue - saMsgMessageSendAsync");
	test_case_add(
	    20, mqsv_it_msgqs_17,
	    "Open a queue, Send a message, Close it, Open the queue and get the message");
	test_case_add(
	    20, mqsv_it_msgqs_18,
	    "Receive callback is invoked when a message is in a queue - saMsgQueueOpen");
	test_case_add(
	    20, mqsv_it_msgqs_19,
	    "Receive callback is invoked when a message is in a queue - saMsgQueueOpenAsync");
	test_case_add(
	    20, mqsv_it_msgqs_20,
	    "Receive callback is called when a non-empty closed queue is opened with rcv clbk flag");
	test_case_add(
	    20, mqsv_it_msgqs_21,
	    "Receive callback is not invoked when queue is not opened with "
	    "SA_MSG_QUEUE_RECEIVE_CALLBACK open flag");
	test_case_add(
	    20, mqsv_it_msgqs_22,
	    "Receive a message from unlinked message queue - Persistent");
	test_case_add(
	    20, mqsv_it_msgqs_23,
	    "Receive a message from unlinked message queue - Non-Persistent");

	test_suite_add(21, "Message Queue Groups Test Suite");
	test_case_add(21, mqsv_it_msgq_grps_01,
		      "Message queues can be inserted into a queue group");
	test_case_add(
	    21, mqsv_it_msgq_grps_02,
	    "Messages sent to a unicast group are sent to only one member queue");
	test_case_add(21, mqsv_it_msgq_grps_03,
		      "Messages sent to a group follow the group policy");
	test_case_add(
	    21, mqsv_it_msgq_grps_04,
	    "Queue groups can be created as unicast or multicast type");
	test_case_add(
	    21, mqsv_it_msgq_grps_05,
	    "Unlinking a member queue will remove the queue from queue group");
	test_case_add(
	    21, mqsv_it_msgq_grps_06,
	    "Track callback is invoked when non-persistent member queue"
	    " is kept closed for retention time");
	test_case_add(
	    21, mqsv_it_msgq_grps_07,
	    "Track callback is invoked when non-persistent member queue"
	    " is kept closed for retention time");
	test_case_add(
	    21, mqsv_it_msgq_grps_08,
	    "When a queue is deleted, it will be deleted from all groups it is member of");
	test_case_add(
	    21, mqsv_it_msgq_grps_09,
	    "When a queue is deleted, track callback is invoked for all the "
	    "groups it is member of");
	test_case_add(
	    21, mqsv_it_msgq_grps_10,
	    "Track callback is not invoked when a member queue is closed (TRACK CHANGES)");
	test_case_add(
	    21, mqsv_it_msgq_grps_11,
	    "Track callback is not invoked when a member queue is closed (TRACK CHANGES ONLY)");
	test_case_add(
	    21, mqsv_it_msgq_grps_12,
	    "A scenario for group tracking with two groups and two queues");

	test_suite_add(22, "Message Delivery Properties Test Suite");
	test_case_add(
	    22, mqsv_it_msg_delprop_01,
	    "Messages are received in their priority order (higher to lower)");
	test_case_add(
	    22, mqsv_it_msg_delprop_02,
	    "Messages of same priority are receive in the FIFO order");
	test_case_add(
	    22, mqsv_it_msg_delprop_03,
	    "Messages that are received from the queue are removed from the queue");
	test_case_add(22, mqsv_it_msg_delprop_04,
		      "SendAsync to a queue that is full");
	test_case_add(
	    22, mqsv_it_msg_delprop_05,
	    "Processes can opt for acknowledgement for message delivery");
	test_case_add(22, mqsv_it_msg_delprop_06,
		      "Messages never expire in message queue");
	test_case_add(22, mqsv_it_msg_delprop_07,
		      "Receive messages from received callback");
	test_case_add(22, mqsv_it_msg_delprop_08,
		      "Receive messages from received callback");
	test_case_add(
	    22, mqsv_it_msg_delprop_09,
	    "Received callback is invoked when the queue is closed and opened");
	test_case_add(22, mqsv_it_msg_delprop_10,
		      "Sending and Receiving messages in a loop");
	test_case_add(
	    22, mqsv_it_msg_delprop_11,
	    "Delivered callback is invoked when send async is successful");
	test_case_add(
	    22, mqsv_it_msg_delprop_12,
	    "If saMsgMessageSendReceive becomes timeout before the reply "
	    "is sent, Reply returns SA_AIS_ERR_NOT_EXIST");
	test_case_add(
	    22, mqsv_it_msg_delprop_13,
	    "If saMsgMessageSendReceive becomes timeout before the reply "
	    "is sent, ReplyAsync returns SA_AIS_ERR_NOT_EXIST");
}
