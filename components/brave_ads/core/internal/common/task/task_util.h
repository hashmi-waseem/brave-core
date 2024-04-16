/* Copyright (c) 2024 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef BRAVE_COMPONENTS_BRAVE_ADS_CORE_INTERNAL_COMMON_TASK_TASK_UTIL_H_
#define BRAVE_COMPONENTS_BRAVE_ADS_CORE_INTERNAL_COMMON_TASK_TASK_UTIL_H_

#include "base/functional/callback_forward.h"

namespace base {
class TimeDelta;
}  // namespace base

namespace brave_ads {

// Posts a task to the current sequence to run after a delay.
// Use this function only when there is no way to use `base::OneShotTimer`,
// i.e. inside free functions.
void PostDelayTaskToCurrentSequence(base::OnceClosure task,
                                    base::TimeDelta delay);

}  // namespace brave_ads

#endif  // BRAVE_COMPONENTS_BRAVE_ADS_CORE_INTERNAL_COMMON_TASK_TASK_UTIL_H_
