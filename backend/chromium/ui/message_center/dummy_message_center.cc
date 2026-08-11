// Copyright 2013 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/message_center/message_center.h"

// Null implementation used when the browser-facing notification system is not
// part of the product. Keeping this narrow interface avoids notification
// feature checks throughout consumers that still share notification data types.

namespace message_center {

// static
void MessageCenter::Initialize() {
}

// static
MessageCenter* MessageCenter::Get() {
  return nullptr;
}

// static
void MessageCenter::Shutdown() {
}

MessageCenter::MessageCenter() {
}

MessageCenter::~MessageCenter() {
}

}  // namespace message_center
