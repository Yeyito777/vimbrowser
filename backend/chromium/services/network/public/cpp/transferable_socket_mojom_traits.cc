// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "services/network/public/cpp/transferable_socket_mojom_traits.h"

#include <algorithm>
#include <vector>

#include "base/dcheck_is_on.h"
#include "build/build_config.h"
#include "mojo/public/cpp/bindings/struct_traits.h"

namespace mojo {

mojo::PlatformHandle StructTraits<
    network::mojom::TransferableSocketDataView,
    network::TransferableSocket>::socket(network::TransferableSocket& value) {
#if DCHECK_IS_ON()
  DCHECK(!value.has_been_transferred_) << "Can only transfer once.";
#endif
  mojo::PlatformHandle output;
  std::swap(value.socket_, output);
  return output;
}

// static
bool StructTraits<network::mojom::TransferableSocketDataView,
                  network::TransferableSocket>::
    Read(network::mojom::TransferableSocketDataView in,
         network::TransferableSocket* out) {
  *out = network::TransferableSocket(in.TakeSocket());
#if DCHECK_IS_ON()
  out->has_been_transferred_ = true;
#endif
  return true;
}

}  // namespace mojo
