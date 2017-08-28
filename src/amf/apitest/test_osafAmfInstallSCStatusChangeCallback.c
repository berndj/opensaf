
#include "amftest.h"

static void sc_status_cbk(OsafAmfSCStatusT state)
{
        if (state == OSAF_AMF_SC_PRESENT) {
        } else if (state == OSAF_AMF_SC_ABSENT) {
        }
}

void osafAmfInstallSCStatusChangeCallback_01()
{
	SaAisErrorT rc;

	safassert(saAmfInitialize_4(&amfHandle, &amfCallbacks_4,
                                    &amfVersion_B41), SA_AIS_OK);
	rc = osafAmfInstallSCStatusChangeCallback(amfHandle, NULL);
	safassert(saAmfFinalize(amfHandle), SA_AIS_OK);
	test_validate(rc, SA_AIS_ERR_INVALID_PARAM);
}

void osafAmfInstallSCStatusChangeCallback_02()
{
	SaAisErrorT rc;

	rc = osafAmfInstallSCStatusChangeCallback(123, sc_status_cbk);
	test_validate(rc, SA_AIS_ERR_BAD_HANDLE);
}


void osafAmfInstallSCStatusChangeCallback_03()
{
	SaAisErrorT rc;

	safassert(saAmfInitialize_4(&amfHandle, &amfCallbacks_4,
                                    &amfVersion_B41), SA_AIS_OK);
	safassert(saAmfFinalize(amfHandle), SA_AIS_OK);
	rc = osafAmfInstallSCStatusChangeCallback(amfHandle, sc_status_cbk);
	test_validate(rc, SA_AIS_ERR_BAD_HANDLE);
}
void osafAmfInstallSCStatusChangeCallback_04()
{
	SaAisErrorT rc;

	safassert(saAmfInitialize_4(&amfHandle, &amfCallbacks_4,
                                    &amfVersion_B41), SA_AIS_OK);
	rc = osafAmfInstallSCStatusChangeCallback(amfHandle, sc_status_cbk);
	test_validate(rc, SA_AIS_OK);
}

__attribute__((constructor)) static void osafAmfInstallSCStatusChangeCallback_constructor(void)
{
        bool is_controller = true;
	int rc = 0;
	char command[30];
	memset(command, '\0', sizeof(command));
	strcpy(command, "amfclusterstatus -c -q");
	rc = system(command);
	if (rc != 0)
		is_controller = false;

        test_suite_add(1, "Test cases for osafAmfInstallSCStatusChangeCallback()");
        test_case_add(1, osafAmfInstallSCStatusChangeCallback_01,
                      "osafAmfInstallSCStatusChangeCallback with NULL cbk, SA_AIS_ERR_INVALID_PARAM");
        test_case_add(1, osafAmfInstallSCStatusChangeCallback_02,
                      "osafAmfInstallSCStatusChangeCallback with invalid handle, SA_AIS_ERR_BAD_HANDLE");
        test_case_add(1, osafAmfInstallSCStatusChangeCallback_03,
                      "osafAmfInstallSCStatusChangeCallback with already finalized handle, SA_AIS_ERR_BAD_HANDLE");
        test_case_add(1, osafAmfInstallSCStatusChangeCallback_04,
                      "osafAmfInstallSCStatusChangeCallback with valid params, SA_AIS_OK");
	if (is_controller == false) {
                /*TODO: Add some functional test here*/
		printf("Add some functional test here.");
	}
}

