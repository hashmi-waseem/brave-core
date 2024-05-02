/* Copyright (c) 2019 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

// This override is in place to make sure the first run page from chromium is
// not shown, as brave has its own first run page
#define BRAVE_STARTUPBROWSERCREATORIMPL_DETERMINEURLSANDLAUNCH \
  has_first_run_experience = false;

#include "src/chrome/browser/ui/startup/startup_browser_creator_impl.cc"
#undef BRAVE_STARTUPBROWSERCREATORIMPL_DETERMINEURLSANDLAUNCH
