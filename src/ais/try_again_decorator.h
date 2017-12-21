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
 */

#ifndef AIS_TRY_AGAIN_DECORATOR_H_
#define AIS_TRY_AGAIN_DECORATOR_H_

#include <saAis.h>
#include <cstdint>
#include <functional>
#include <iostream>
#include "base/time.h"

namespace ais {

//>
// C++ decorator which encapsulates try again handling.
//
// E.g:
// 1) If user wants to call saClmInitialize() which has try again
// handling inside using this decorator, do this:
//
// #include "ais/try_again_decorator.h"
//
// auto saClmInitialize = ais::make_decorator(::saClmInitialize);
// if (saClmInitialize(handle, cbs, version) != SA_AIS_OK) {
//    // error handling
// }
//
// 2) If user wants using other retry control policy than default ones,
// then define your own policy like below. The best way is copy the default
// policy, then modify to your own values.
//
// #include "ais/try_again_decorator.h"
//
// class MyOwnTryAgain {
// public:
//   constexpr static bool is_ais_code_accepted(SaAisErrorT code) {
//     return (code != SA_AIS_ERR_TRY_AGAIN && code != SA_AIS_ERR_UNAVAILABLE);
//  }
//   constexpr static uint64_t kIntervalMs = 10;
//   constexpr static uint64_t kTimeoutMs  = 10 * 1000;
// };
//
// using MyOwnPolicy = ais::UseMyPolicy<MyOwnTryAgain>;
// auto saClmInitialize = MyOwnPolicy::make_decorator(::saClmInitialize);
// if (saClmInitialize(handle, cbs, version) != SA_AIS_OK) {
//    // error handling
// }
//
//<

class DefaultRetryPolicy {
 public:
  // Which error code you want to do the retry.
  constexpr static bool is_ais_code_accepted(SaAisErrorT code) {
    return (code != SA_AIS_ERR_TRY_AGAIN);
  }

  // Sleep time between retries (ms). Default is 100ms.
  constexpr static uint64_t kIntervalMs = 100;
  // Timeout for the retry (ms). Default is one minute.
  constexpr static uint64_t kTimeoutMs  = 60 * 1000;
};

template <class T, class Policy = DefaultRetryPolicy> class Decorator;
template <class T, class Policy, class... Args>
class Decorator<T(Args ...), Policy> {
 public:
  explicit Decorator(const std::function<T(Args ...)>& f) : f_{f} {}
  T operator()(Args ... args) {
    T ais_error = SA_AIS_OK;
    base::Timer wtime(Policy::kTimeoutMs);
    while (wtime.is_timeout() == false) {
      ais_error = f_(args...);
      if (Policy::is_ais_code_accepted(ais_error) == true) break;
      base::Sleep({0, Policy::kIntervalMs * 1000 * 1000ull});
    }
    return ais_error;
  }

 private:
  const std::function<T(Args ...)> f_;
};

template<class T, class... Args>
Decorator<T(Args...)> make_decorator(T (*f)(Args ...)) {
  return Decorator<T(Args...)>(std::function<T(Args...)>(f));
}

template <class Policy>
class UseMyPolicy {
 public:
  template<class T, class... Args>
  static Decorator<T(Args...), Policy> make_decorator(T (*f)(Args ...)) {
    return Decorator<T(Args...), Policy>(std::function<T(Args...)>(f));
  }
};
}  // namespace ais

#endif  //< AIS_TRY_AGAIN_DECORATOR_H_
