// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_BASE_ACCELERATORS_ACCELERATOR_MAP_H_
#define UI_BASE_ACCELERATORS_ACCELERATOR_MAP_H_

// TODO(oshima): Remove this.
#include <map>
#include <unordered_map>
#include <utility>
#include <vector>

#include "base/component_export.h"
#include "build/build_config.h"
#include "ui/base/accelerators/accelerator.h"


namespace std {
template <>
struct hash<ui::Accelerator> {
  size_t operator()(const ui::Accelerator& a) const { return a.hash(); }
};

}  // namespace std

namespace ui {

// This is a wrapper around an internal std::unordered_map of type
// |std::unordered_map<Accelerator, V>| where |V| is the mapped value.
//
// Accelerators in Chrome on all platforms are specified with the |key_code|,
// aka VKEY, however certain keys (eg. brackets, comma, period, plus, minus),
// are in different places based on the keyboard layout. In some cases the
// VKEYs don't exist, in some cases they now conflict with other shortcuts.
//
// Chrome OS uses a positional mapping for this subset of keys. Shortcuts
// based on these keys are determined by the position of the key on a US
// keyboard. This was already the case for all non-latin alphabet keyboards
// (Chinese, Japanese, Arabic, Russian, etc.).
//
// To achieve this on Chrome OS for the remaining layouts, an additional
// remapping may happen to the accelerator used for lookup based on the state
// of |use_positional_lookup_|. When false no remapping occurs. When true,
// the |code| aka DomCode (which is by definition fixed position), is used to
// find the US layout VKEY, and that VKEY is used to lookup in the map.
//
// Other non-positional keys, eg. alphanumeric, F-keys, and special keys are
// all not remapped. Alphanumeric keys always continue to follow the
// |code|/VKEY defined by the current layout as is existing behavior.
template <typename V>
class COMPONENT_EXPORT(UI_BASE) AcceleratorMap {
 public:
  AcceleratorMap() = default;
  ~AcceleratorMap() = default;

  using iterator = typename std::unordered_map<Accelerator, V>::iterator;
  using const_iterator =
      typename std::unordered_map<Accelerator, V>::const_iterator;

  // Lookup an accelerator and return a pointer to the value. If the
  // accelerator is not in the map, this returns nullptr.
  const V* Find(const Accelerator& accelerator) const {
    auto iter = FindImpl(accelerator);
    return iter == map_.end() ? nullptr : &iter->second;
  }

  V* Find(const Accelerator& accelerator) {
    // Call the const implementation to avoid duplicating code.
    return const_cast<V*>(
        const_cast<const AcceleratorMap*>(this)->Find(accelerator));
  }

  // Lookup an accelerator and return a reference to the value. If the
  // accelerator is not present this function will DCHECK.
  const V& Get(const Accelerator& accelerator) const {
    auto iter = FindImpl(accelerator);
    DCHECK(iter != map_.end());
    return iter->second;
  }

  V& Get(const Accelerator& accelerator) {
    // Call the const implementation to avoid duplicating code.
    return const_cast<V&>(
        const_cast<const AcceleratorMap*>(this)->Get(accelerator));
  }

  V& GetOrInsertDefault(const Accelerator& accelerator) {
    return map_[accelerator];
  }

  // Erase an accelerator from the map.
  bool Erase(const Accelerator& accelerator) {
    return map_.erase(accelerator) > 0;
  }

  void Clear() { map_.clear(); }

  // Inserts a new accelerator and value into the map. DCHECKs if the
  // accelerator was already in the map.
  void InsertNew(const std::pair<const Accelerator, V>& value) {
    auto result = map_.insert(value);
    DCHECK(result.second);
  }

  // Iterators for the internal map.
  iterator begin() { return map_.begin(); }
  iterator end() { return map_.end(); }

  // Returns the number of items in the map.
  size_t size() const { return map_.size(); }

  // Returns true if the map is empty.
  bool empty() const { return map_.empty(); }


 private:
  std::unordered_map<Accelerator, V> map_;


  const_iterator FindImpl(const Accelerator& accelerator) const {

    return map_.find(accelerator);
  }

  iterator FindImpl(const Accelerator& accelerator) {
    return const_cast<V*>(
        const_cast<const AcceleratorMap*>(this)->FindImpl(accelerator));
  }
};

}  // namespace ui

#endif  // UI_BASE_ACCELERATORS_ACCELERATOR_MAP_H_
