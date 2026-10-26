/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 *
 * @modified    Tom Clay, 2026 - Adapted for ReXGlue runtime
 */

#include <rex/logging.h>
#include <rex/stream.h>
#include <rex/system/xevent.h>

namespace rex::system {

XEvent::XEvent(KernelState* kernel_state) : XObject(kernel_state, kObjectType) {}

XEvent::~XEvent() = default;

void XEvent::Initialize(bool manual_reset, bool initial_state) {
  assert_false(event_);

  manual_reset_ = manual_reset;
  this->CreateNative<X_KEVENT>();
  // dw5e-port: ported from xenia-canary dcf2994 (2026-09-13): keep the guest header truthful.
  // Don't touch wait_list: SetNativePointer stashes the handle there.
  auto* kevent = guest_object<X_KEVENT>();
  kevent->header.type = manual_reset ? 0x00 : 0x01;
  kevent->header.signal_state = initial_state ? 1 : 0;
  host_signaled_ = initial_state;

  if (manual_reset) {
    event_ = rex::thread::Event::CreateManualResetEvent(initial_state);
  } else {
    event_ = rex::thread::Event::CreateAutoResetEvent(initial_state);
  }
  assert_not_null(event_);
}

void XEvent::InitializeNative(void* native_ptr, X_DISPATCH_HEADER* header) {
  assert_false(event_);

  switch (header->type) {
    case 0x00:  // EventNotificationObject (manual reset)
      manual_reset_ = true;
      break;
    case 0x01:  // EventSynchronizationObject (auto reset)
      manual_reset_ = false;
      break;
    default:
      assert_always();
      return;
  }

  bool initial_state = header->signal_state ? true : false;
  host_signaled_ = initial_state;
  if (manual_reset_) {
    event_ = rex::thread::Event::CreateManualResetEvent(initial_state);
  } else {
    event_ = rex::thread::Event::CreateAutoResetEvent(initial_state);
  }
  assert_not_null(event_);
  // Track the guest header so Set/Reset/SyncFromGuest can reach it.
  SetNativePointer(memory()->HostToGuestVirtual(native_ptr), true);
}

void XEvent::Query(uint32_t* out_type, uint32_t* out_state) {
  if (out_type) {
    *out_type = manual_reset_ ? 0x00 : 0x01;
  }
  if (out_state) {
    // Query the live host event, not the stale guest header
    auto result = rex::thread::Wait(event_.get(), false, std::chrono::milliseconds(0));
    if (result == rex::thread::WaitResult::kSuccess) {
      *out_state = 1;
      // Re-signal since we consumed the signal by waiting
      event_->Set();
    } else {
      *out_state = 0;
    }
  }
}

int32_t XEvent::Set(uint32_t priority_increment, bool wait) {
  std::lock_guard<std::mutex> lock(state_lock_);
  guest_object<X_KEVENT>()->header.signal_state = 1;
  host_signaled_ = true;
  event_->Set();
  return 1;
}

int32_t XEvent::Pulse(uint32_t priority_increment, bool wait) {
  std::lock_guard<std::mutex> lock(state_lock_);
  auto* kevent = guest_object<X_KEVENT>();
  // KePulseEvent returns the pre-pulse signal state.
  const int32_t old_signal_state = kevent->header.signal_state;
  event_->Pulse();
  // Pulse leaves the event reset after releasing waiters.
  kevent->header.signal_state = 0;
  host_signaled_ = false;
  return old_signal_state;
}

int32_t XEvent::Reset() {
  std::lock_guard<std::mutex> lock(state_lock_);
  guest_object<X_KEVENT>()->header.signal_state = 0;
  host_signaled_ = false;
  event_->Reset();
  return 1;
}

void XEvent::WaitCallback() {
  // Auto-reset events clear on a successful wait; manual-reset events stay set.
  if (!manual_reset_) {
    std::lock_guard<std::mutex> lock(state_lock_);
    guest_object<X_KEVENT>()->header.signal_state = 0;
    host_signaled_ = false;
  }
}

void XEvent::SyncFromGuest() {
  if (!guest_object()) {
    return;
  }
  std::lock_guard<std::mutex> lock(state_lock_);
  bool guest_signaled = guest_object<X_KEVENT>()->header.signal_state ? true : false;
  if (guest_signaled == host_signaled_) {
    return;
  }
  host_signaled_ = guest_signaled;
  if (guest_signaled) {
    event_->Set();
  } else {
    event_->Reset();
  }
}

void XEvent::Clear() {
  event_->Reset();
}

bool XEvent::Save(stream::ByteStream* stream) {
  REXSYS_DEBUG("XEvent {:08X} ({})", handle(), manual_reset_ ? "manual" : "auto");
  SaveObject(stream);

  bool signaled = true;
  auto result = rex::thread::Wait(event_.get(), false, std::chrono::milliseconds(0));
  if (result == rex::thread::WaitResult::kSuccess) {
    signaled = true;
  } else if (result == rex::thread::WaitResult::kTimeout) {
    signaled = false;
  } else {
    assert_always();
  }

  if (signaled) {
    // Reset the event in-case it's an auto-reset.
    event_->Set();
  }

  stream->Write<bool>(signaled);
  stream->Write<bool>(manual_reset_);

  return true;
}

object_ref<XEvent> XEvent::Restore(KernelState* kernel_state, stream::ByteStream* stream) {
  auto evt = new XEvent(nullptr);
  evt->kernel_state_ = kernel_state;

  evt->RestoreObject(stream);
  bool signaled = stream->Read<bool>();
  evt->manual_reset_ = stream->Read<bool>();

  if (evt->manual_reset_) {
    evt->event_ = rex::thread::Event::CreateManualResetEvent(false);
  } else {
    evt->event_ = rex::thread::Event::CreateAutoResetEvent(false);
  }
  assert_not_null(evt->event_);

  if (signaled) {
    evt->event_->Set();
  }

  return object_ref<XEvent>(evt);
}

}  // namespace rex::system
