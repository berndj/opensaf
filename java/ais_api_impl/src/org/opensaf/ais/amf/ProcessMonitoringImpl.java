/*      -*- OpenSAF  -*-
 *
 * (C) Copyright 2008 The OpenSAF Foundation
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. This file and program are licensed
 * under the GNU Lesser General Public License Version 2.1, February 1999.
 * The complete license can be accessed from the following location:
 * http://opensource.org/licenses/lgpl-license.php
 * See the Copying file included with the OpenSAF distribution for full
 * licensing terms.
 *
 * Author(s): Nokia Siemens Networks, OptXware Research & Development LLC.
 */

package org.opensaf.ais.amf;

import org.saforum.ais.AisBadHandleException;
import org.saforum.ais.AisInvalidParamException;
import org.saforum.ais.AisLibraryException;
import org.saforum.ais.AisNoMemoryException;
import org.saforum.ais.AisNoResourcesException;
import org.saforum.ais.AisNotExistException;
import org.saforum.ais.AisTimeoutException;
import org.saforum.ais.AisTryAgainException;
import org.saforum.ais.amf.AmfHandle;
import org.saforum.ais.amf.ProcessMonitoring;
import org.saforum.ais.amf.RecommendedRecovery;

public class ProcessMonitoringImpl implements ProcessMonitoring {

    /**
     *
     */
    private AmfHandle amfLibraryHandle;

    /**
     * TODO
     *
     * @param amfLibraryHandle TODO
     */
    ProcessMonitoringImpl( AmfHandle amfLibraryHandle ) {
        this.amfLibraryHandle = amfLibraryHandle;
    }


	public native void startProcessMonitoring(String componentName, long processId,
			int descendentsTreeDepth, int pmErrors,
			RecommendedRecovery recommendedRecovery)
			throws AisLibraryException, AisTimeoutException,
			AisTryAgainException, AisBadHandleException,
			AisInvalidParamException, AisNoMemoryException,
			AisNoResourcesException, AisNotExistException;

	public native void stopProcessMonitoring(String componentName,
			PmStopQualifier stopQualifier, long processId, int pmErrors)
			throws AisLibraryException, AisTimeoutException,
			AisTryAgainException, AisBadHandleException,
			AisInvalidParamException, AisNoMemoryException,
			AisNoResourcesException, AisNotExistException;

}
