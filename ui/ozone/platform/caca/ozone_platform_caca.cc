// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/ozone/platform/caca/ozone_platform_caca.h"

#include "ui/ozone/ozone_platform.h"
#include "ui/ozone/platform/caca/caca_connection.h"

namespace ui {

OzonePlatformCaca::OzonePlatformCaca()
    : connection_(),
      surface_factory_ozone_(&connection_),
      event_factory_ozone_(&connection_) {}

OzonePlatformCaca::~OzonePlatformCaca() {}

gfx::SurfaceFactoryOzone* OzonePlatformCaca::GetSurfaceFactoryOzone() {
  return &surface_factory_ozone_;
}

ui::EventFactoryOzone* OzonePlatformCaca::GetEventFactoryOzone() {
  return &event_factory_ozone_;
}

ui::InputMethodContextFactoryOzone*
OzonePlatformCaca::GetInputMethodContextFactoryOzone() {
  return &input_method_context_factory_ozone_;
}

ui::CursorFactoryOzone* OzonePlatformCaca::GetCursorFactoryOzone() {
  return &cursor_factory_ozone_;
}

OzonePlatform* CreateOzonePlatformCaca() { return new OzonePlatformCaca; }

}  // namespace ui
