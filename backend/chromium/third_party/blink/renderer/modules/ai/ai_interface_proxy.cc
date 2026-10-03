// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/modules/ai/ai_interface_proxy.h"

#include "base/task/single_thread_task_runner.h"
#include "third_party/blink/public/platform/browser_interface_broker_proxy.h"
#include "third_party/blink/public/platform/task_type.h"
#include "third_party/blink/renderer/platform/wtf/functional.h"

namespace blink {

const char AIInterfaceProxy::kSupplementName[] = "AIInterfaceProxy";

// TODO(crbug.com/406770758): Consider refactoring to have this class own the
// execution context as a member.
AIInterfaceProxy::AIInterfaceProxy(ExecutionContext* execution_context)
    : Supplement<ExecutionContext>(*execution_context),
      task_runner_(
          execution_context->GetTaskRunner(TaskType::kInternalDefault)) {}

AIInterfaceProxy::~AIInterfaceProxy() = default;

void AIInterfaceProxy::Trace(Visitor* visitor) const {
  Supplement<ExecutionContext>::Trace(visitor);
  visitor->Trace(ai_manager_remote_);
}

// static
scoped_refptr<base::SequencedTaskRunner> AIInterfaceProxy::GetTaskRunner(
    ExecutionContext* execution_context) {
  return AIInterfaceProxy::From(execution_context)->task_runner_;
}

// static
HeapMojoRemote<mojom::blink::AIManager>& AIInterfaceProxy::GetAIManagerRemote(
    ExecutionContext* execution_context) {
  return From(execution_context)->GetAIManagerRemoteImpl(execution_context);
}

// static
AIInterfaceProxy* AIInterfaceProxy::From(ExecutionContext* execution_context) {
  AIInterfaceProxy* translation_manager_proxy =
      Supplement<ExecutionContext>::From<AIInterfaceProxy>(*execution_context);
  if (!translation_manager_proxy) {
    translation_manager_proxy =
        MakeGarbageCollected<AIInterfaceProxy>(execution_context);
    ProvideTo(*execution_context, translation_manager_proxy);
  }
  return translation_manager_proxy;
}

HeapMojoRemote<mojom::blink::AIManager>&
AIInterfaceProxy::GetAIManagerRemoteImpl(ExecutionContext* execution_context) {
  if (!ai_manager_remote_.is_bound()) {
    execution_context->GetBrowserInterfaceBroker().GetInterface(
        ai_manager_remote_.BindNewPipeAndPassReceiver(task_runner_));
  }
  return ai_manager_remote_;
}

}  // namespace blink
