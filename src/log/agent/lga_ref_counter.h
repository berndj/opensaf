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

#ifndef SRC_LOG_AGENT_LGA_REF_COUNTER_H_
#define SRC_LOG_AGENT_LGA_REF_COUNTER_H_

#include <stdint.h>
#include "base/mutex.h"

//>
// Using the reference counter class to synchronize access
// to its object owner - it provides help methods to let the owner object
// know how many thread are referring to itself, or it is being deleted.
// By using this RefCounter object, the owner can avoid data race.
//
// When the object owner is referring for reading/modifying, the caller
// should first check if the object is being deleted or not by
// using method `FetchAndIncreaseRefCounter()`. If there is a deletion going,
// means the return value is (-1), the caller should not touch this object.
//
// If the caller want to delete the owner object, the caller first check
// if the owner object is being used by other thread or not by calling method
// `FetchAndDecreaseRefCounter`. If there is thread referring to this object,
// the method will return non-zero (!=0). In that case, the caller
// should not delete the object.
//
// Example:
// class Owner {
// public:
//   RefCounter ref_counter_object_;
//}
//
// <! Case #1: Using Owner object !>
// if (ref_counter_object_.FetchAndIncreaseRefCounter() == -1) {
//    // Return try again and come back later
// }
//
// // It is ok referring to Owner object.
//
//
// <! Case #2: Delete Owner object !>
// if (ref_counter_object_.FetchAndDecreaseRefCounter() != 0) {
//   // The owner is using or deleting by other thread. Return try again.
// }
//
// // It is ok to delete the Owner object.
//<
class RefCounter {
 public:
  // Degree of reference counter. Introduce to force caller either using
  // kIncOne (1) or kDecOne(-1). Other value is not allowed.
  enum Degree {
    // Say, I want referring to object. Count me in.
    kIncOne = 1,
    // Say, I want to delete the object.
    kDecOne = -1
  };

  RefCounter();
  ~RefCounter() {};

  // Fetch and increase reference counter.
  // Increase one if @this object is not being deleted by other thread.
  // @updated will be set to true if there is an increase, false otherwise.
  // @caller shows who is the caller, the main purpose is for debugging.
  int32_t FetchAndIncreaseRefCounter(const char* caller, bool* updated);

  // Fetch and decrease reference counter.
  // Decrease one if @this object is not being used or deleted by other thread.
  // @updated will be set to true if there is an decrease, false otherwise.
  // @caller shows who is the caller, the main purpose is for debugging.
  int32_t FetchAndDecreaseRefCounter(const char* caller, bool* updated);

  // Restore the reference counter back if @updated is true.
  // Passed @value is either (1) [increase] or (-1) [decrease]
  // @caller shows who is the caller, the main purpose is for debugging.
  void RestoreRefCounter(const char* caller, Degree value, bool updated);

  // Return current ref_counter_ value
  int32_t ref_counter() const { return ref_counter_; }

 private:
  // Hold information how many thread are referring to this object
  // If the object is being deleted, the value is (-1).
  // If the object is not being deleted/used, the value is (0)
  // If there are N thread referring to this object, the counter will be N.
  int32_t ref_counter_;

  // The mutex to protect @ref_counter_.
  base::Mutex ref_counter_mutex_;

  DELETE_COPY_AND_MOVE_OPERATORS(RefCounter);
};

#endif  // SRC_LOG_AGENT_LGA_REF_COUNTER_H_
