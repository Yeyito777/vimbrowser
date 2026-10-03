// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_MESSAGE_CENTER_MESSAGE_CENTER_H_
#define UI_MESSAGE_CENTER_MESSAGE_CENTER_H_

#include "ui/message_center/message_center_export.h"

namespace message_center {

// Null process-global compatibility shell. Vimbrowser does not provide a
// notification center; Get() therefore always returns nullptr.
class MESSAGE_CENTER_EXPORT MessageCenter {
 public:
  static void Initialize();
  static MessageCenter* Get();
  static void Shutdown();

  MessageCenter(const MessageCenter&) = delete;
  MessageCenter& operator=(const MessageCenter&) = delete;
  virtual ~MessageCenter();

 protected:
  MessageCenter();
};

}  // namespace message_center

#endif  // UI_MESSAGE_CENTER_MESSAGE_CENTER_H_
