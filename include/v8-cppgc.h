// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef INCLUDE_V8_CPPGC_H_
#define INCLUDE_V8_CPPGC_H_

#include <memory>
#include <vector>

#include "cppgc/custom-space.h"
#include "cppgc/internal/write-barrier.h"
#include "cppgc/visitor.h"
#include "v8-internal.h"  // NOLINT(build/include_directory)
#include "v8.h"           // NOLINT(build/include_directory)

namespace cppgc {
class AllocationHandle;
class HeapHandle;
}  // namespace cppgc

namespace v8 {

namespace internal {
class CppHeap;
}  // namespace internal

struct V8_EXPORT CppHeapCreateParams {
  std::vector<std::unique_ptr<cppgc::CustomSpaceBase>> custom_spaces;
};

/**
 * A heap for allocating managed C++ objects.
 */
class V8_EXPORT CppHeap {
 public:
  virtual ~CppHeap() = default;

  /**
   * \returns the opaque handle for allocating objects using
   * `MakeGarbageCollected()`.
   */
  cppgc::AllocationHandle& GetAllocationHandle();

  /**
   * \returns the opaque heap handle which may be used to refer to this heap in
   *   other APIs. Valid as long as the underlying `CppHeap` is alive.
   */
  cppgc::HeapHandle& GetHeapHandle();

  /**
   * Terminate clears all roots and performs multiple garbage collections to
   * reclaim potentially newly created objects in destructors.
   *
   * After this call, object allocation is prohibited.
   */
  void Terminate();

 private:
  CppHeap() = default;

  friend class internal::CppHeap;
};

class JSVisitor : public cppgc::Visitor {
 public:
  explicit JSVisitor(cppgc::Visitor::Key key) : cppgc::Visitor(key) {}

  void Trace(const TracedReferenceBase& ref) {
    if (ref.IsEmptyThreadSafe()) return;
    Visit(ref);
  }

 protected:
  using cppgc::Visitor::Visit;

  virtual void Visit(const TracedReferenceBase& ref) {}
};

/**
 * **DO NOT USE: Use the appropriate managed types.**
 *
 * Consistency helpers that aid in maintaining a consistent internal state of
 * the garbage collector.
 */
class V8_EXPORT JSHeapConsistency final {
 public:
  using WriteBarrierParams = cppgc::internal::WriteBarrier::Params;
  using WriteBarrierType = cppgc::internal::WriteBarrier::Type;

  /**
   * Gets the required write barrier type for a specific write.
   *
   * Note: Handling for C++ to JS references.
   *
   * \param ref The reference being written to.
   * \param params Parameters that may be used for actual write barrier calls.
   *   Only filled if return value indicates that a write barrier is needed. The
   *   contents of the `params` are an implementation detail.
   * \returns whether a write barrier is needed and which barrier to invoke.
   */
  static V8_INLINE WriteBarrierType GetWriteBarrierType(
      const TracedReferenceBase& ref, WriteBarrierParams& params) {
    if (ref.IsEmpty()) return WriteBarrierType::kNone;
    return cppgc::internal::WriteBarrier::GetWriteBarrierType(&ref, params);
  }

  /**
   * Gets the required write barrier type for a specific write.
   *
   * Note: Handling for JS to C++ references.
   *
   * \param wrapper The wrapper that has been written into.
   * \param wrapper_index The wrapper index in `wrapper` that has been written
   *   into.
   * \param wrappable The value that was written.
   * \param params Parameters that may be used for actual write barrier calls.
   *   Only filled if return value indicates that a write barrier is needed. The
   *   contents of the `params` are an implementation detail.
   * \returns whether a write barrier is needed and which barrier to invoke.
   */
  static V8_INLINE WriteBarrierType
  GetWriteBarrierType(v8::Local<v8::Object>& wrapper, int wrapper_index,
                      const void* wrappable, WriteBarrierParams& params) {
#if V8_ENABLE_CHECKS
    CheckWrapper(wrapper, wrapper_index, wrappable);
#endif  // V8_ENABLE_CHECKS
    return cppgc::internal::WriteBarrier::
        GetWriteBarrierTypeForExternallyReferencedObject(wrappable, params);
  }

  /**
   * Conservative Dijkstra-style write barrier that processes an object if it
   * has not yet been processed.
   *
   * \param params The parameters retrieved from `GetWriteBarrierType()`.
   * \param ref The reference being written to.
   */
  static V8_INLINE void DijkstraMarkingBarrier(const WriteBarrierParams& params,
                                               cppgc::HeapHandle& heap_handle,
                                               const TracedReferenceBase& ref) {
    cppgc::internal::WriteBarrier::CheckParams(WriteBarrierType::kMarking,
                                               params);
    DijkstraMarkingBarrierSlow(heap_handle, ref);
  }

  /**
   * Conservative Dijkstra-style write barrier that processes an object if it
   * has not yet been processed.
   *
   * \param params The parameters retrieved from `GetWriteBarrierType()`.
   * \param object The pointer to the object. May be an interior pointer to a
   *   an interface of the actual object.
   */
  static V8_INLINE void DijkstraMarkingBarrier(const WriteBarrierParams& params,
                                               cppgc::HeapHandle& heap_handle,
                                               const void* object) {
    cppgc::internal::WriteBarrier::DijkstraMarkingBarrier(params, object);
  }

  /**
   * Generational barrier for maintaining consistency when running with multiple
   * generations.
   *
   * \param params The parameters retrieved from `GetWriteBarrierType()`.
   * \param ref The reference being written to.
   */
  static V8_INLINE void GenerationalBarrier(const WriteBarrierParams& params,
                                            const TracedReferenceBase& ref) {}

 private:
  JSHeapConsistency() = delete;

  static void CheckWrapper(v8::Local<v8::Object>&, int, const void*);

  static void DijkstraMarkingBarrierSlow(cppgc::HeapHandle&,
                                         const TracedReferenceBase& ref);
};

}  // namespace v8

namespace cppgc {

template <typename T>
struct TraceTrait<v8::TracedReference<T>> {
  static void Trace(Visitor* visitor, const v8::TracedReference<T>* self) {
    static_cast<v8::JSVisitor*>(visitor)->Trace(*self);
  }
};

}  // namespace cppgc

#endif  // INCLUDE_V8_CPPGC_H_
