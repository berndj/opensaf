/*      -*- OpenSAF  -*-
 *
 * (C) Copyright 2008 The OpenSAF Foundation
 * Copyright Ericsson AB 2018 - All Rights Reserved.
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
 * Author(s): Ericsson AB
 *
 */

#include "ais/include/saImmOi.h"

// Contains administrative handling of the lgs main object implementer (OI)
// Implements functions for initializing an OI in a background thread,
// finalizing the OI and stop an ongoing initialization
// The reason for using a background thread is to not block the log server.
// An initialize request may take up to 1 minute.
// When stopping the OI a running OI initializing thread is stopped and the OI
// is finalized.
//
// Examples of situations when the functions are used:
// Starting log server: OI shall be intialized. This should be done in
//                      background so that start of log server is not blocked.
//
// Switch/fail over:    The to become standby; must give up the OI. The chosen
//                      way of doing that is to finalize the OI. An alternative
//                      is to only clear the OI which is no longer used here.
//                      The to become active; must initialize an OI. An
//                      alternative is to keep the OI handle and just use the
//                      set API
//
// BAD HANDLE:          IMM may return bad handle when synchronous operations
//                      are called including the dispatch function. If this
//                      happen the OI shall be initialized again except in a
//                      switch/fail over situation where the server shall become
//                      standby
//
// Note1: The background thread puts a no-operation message in the mailbox
//        handled by the main poll loop when OI initialize is done. This is
//        needed to handle installing of the IMM fd in the poll loop.
//
// Note2: The previous global immOiHandle and immSelectionObject variables in
//        cb struct are removed. Values can be fetched using thread safe get
//        functions

#ifndef LGS_OI_ADMIN_H
#define LGS_OI_ADMIN_H

// Creates a new OI. Return false if fail.
// Note1: If there is an existing OI it will be deleted and a new is created.
//        This means calling IMM finalize, setting oi_handle = 0,
//        oi_selection_object = -1 and starting the OI creation thread. The
//        OI creation thread will update oi_handle and oi_selection_object with
//        new values when creation is done.
//        Nothing will be done if OI creation is already ongoing when this
//        function is called
// Note2: lgsOiCreateBackground() shall not be used when log server is starting.
//        During initialization configuration streams are created.
//        When creating a configuration stream cached Rt attributes has to be
//        written and this requires a valid OI handle
//        Instead use lgsOiCreateSyncronous()
//
// This function shall also be used to restore the OI in case of a BAD HANDLE
// return code when using the OI handle with an OI IMM operation.
// Not thread safe. Shall be used in the log server main thread only
void lgsOiCreateBackground();

// Note3: This function shall only be called once and only when the log server
//        is initializing
// This function is not thread safe
void lgsOiCreateSynchronous();

// Start an OI.
// Shall be used when this server shall become an OI.
// For example when becoming active
// Not thread safe. Shall be used in the log server main thread only
void lgsOiStart();

// Stop an existing OI
// To be used when this server no longer shall be an OI.
// For example becoming standby
// Not thread safe. Shall be used in the log server main thread only
void lgsOiStop();

// Get both OI handle and selection object. Guarantees that we get a pair
// that is created together. Return -1 if there is no selection object and
// the value of out_oi_handle will be 0
// Is thread safe
SaSelectionObjectT lgsGetOiFdsParams(SaImmOiHandleT* out_oi_handle);

// Returns the created OI selection object or -1 if there is none
// Is thread safe
SaSelectionObjectT lgsGetOiSelectionObject();

// Returns the created OI handle or 0 if there is no handle
// Is thread safe
SaImmOiHandleT lgsGetOiHandle();

#endif /* LGS_OI_ADMIN_H */

