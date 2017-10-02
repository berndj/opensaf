/*      -*- OpenSAF  -*-
 *
 * Copyright Ericsson AB 2017 - All Rights Reserved.
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

#include "log/agent/lga_ref_counter.h"
#include <assert.h>
#include "base/logtrace.h"

//------------------------------------------------------------------------------
// RefCounter
//------------------------------------------------------------------------------
RefCounter::RefCounter() : ref_counter_{0}, ref_counter_mutex_{} {}

// Return (-1) if @this object is being deleted.
// Otherwise, increase the reference counter.
int32_t RefCounter::FetchAndIncreaseRefCounter(const char* caller,
                                               bool* updated) {
  TRACE_ENTER();
  assert(caller != nullptr && updated != nullptr);
  base::Lock scopeLock(ref_counter_mutex_);
  int32_t backup = ref_counter_;
  *updated = false;
  TRACE("%s(%s): counter(%d) = %d", __func__, caller, *updated, ref_counter_);
  // The @this object is being deleted.
  if (ref_counter_ == -1) return backup;
  ref_counter_ += 1;
  *updated = true;
  return backup;
}

// Return (0) if @this object is allowed to deleted
// Otherwise, @this object is being used or being deleted by
// other thread.
int32_t RefCounter::FetchAndDecreaseRefCounter(const char* caller,
                                               bool* updated) {
  TRACE_ENTER();
  assert(caller != nullptr && updated != nullptr);
  base::Lock scopeLock(ref_counter_mutex_);
  int32_t backup = ref_counter_;
  *updated = false;
  TRACE("%s(%s): counter(%d) = %d", __func__, caller, *updated, ref_counter_);
  // @this object is being used or being deleted.
  if (ref_counter_ != 0) return backup;
  ref_counter_ = -1;
  *updated = true;
  return backup;
}

void RefCounter::RestoreRefCounter(const char* caller,
                                   Degree value, bool updated) {
  TRACE_ENTER();
  assert(caller != nullptr && "No caller");
  if (updated == false) return;
  base::Lock scopeLock(ref_counter_mutex_);
  int degree = (value == Degree::kIncOne ? 1 : -1);
  ref_counter_ -= degree;
  TRACE("%s(%s): counter(%d) = %d", __func__, caller, degree, ref_counter_);
  // Don't expect the @ref_counter_ is less than (-1)
  assert(ref_counter_ >= -1);
}
