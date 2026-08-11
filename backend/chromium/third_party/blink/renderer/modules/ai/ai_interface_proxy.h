// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_BLINK_RENDERER_MODULES_AI_AI_INTERFACE_PROXY_H_
#define THIRD_PARTY_BLINK_RENDERER_MODULES_AI_AI_INTERFACE_PROXY_H_

#include "base/memory/scoped_refptr.h"
#include "base/task/sequenced_task_runner.h"
#include "third_party/blink/public/mojom/ai/ai_manager.mojom-blink.h"
#include "third_party/blink/renderer/core/execution_context/execution_context.h"
#include "third_party/blink/renderer/platform/heap/garbage_collected.h"
#include "third_party/blink/renderer/platform/mojo/heap_mojo_remote.h"
#include "third_party/blink/renderer/platform/supplementable.h"

namespace blink {

// Provides static getters to browser interfaces for the built-in AI APIs.
class AIInterfaceProxy final : public GarbageCollected<AIInterfaceProxy>,
                               public Supplement<ExecutionContext> {
 public:
  static const char kSupplementName[];

  explicit AIInterfaceProxy(ExecutionContext* execution_context);
  ~AIInterfaceProxy();

  // Not copyable or movable
  AIInterfaceProxy(const AIInterfaceProxy&) = delete;
  AIInterfaceProxy& operator=(const AIInterfaceProxy&) = delete;

  void Trace(Visitor* visitor) const override;

  static scoped_refptr<base::SequencedTaskRunner> GetTaskRunner(
      ExecutionContext* execution_context);

  static HeapMojoRemote<mojom::blink::AIManager>& GetAIManagerRemote(
      ExecutionContext* execution_context);

 private:
  static AIInterfaceProxy* From(ExecutionContext* execution_context);

  HeapMojoRemote<mojom::blink::AIManager>& GetAIManagerRemoteImpl(
      ExecutionContext* execution_context);

  scoped_refptr<base::SequencedTaskRunner> task_runner_;

  HeapMojoRemote<mojom::blink::AIManager> ai_manager_remote_{nullptr};
};

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_MODULES_AI_AI_INTERFACE_PROXY_H_
