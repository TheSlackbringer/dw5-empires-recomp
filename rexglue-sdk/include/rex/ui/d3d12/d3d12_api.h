/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2018 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 *
 * @modified    Tom Clay, 2026 - Adapted for ReXGlue runtime
 */

#pragma once

// clang - format off
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <objbase.h>

#if __has_include(<DXProgrammableCapture.h>)
#include <DXProgrammableCapture.h>
#else
interface DECLSPEC_UUID("9f251514-9d4d-4902-9d60-18988ab7d4b5") DECLSPEC_NOVTABLE IDXGraphicsAnalysis : public IUnknown {
  virtual void STDMETHODCALLTYPE BeginCapture() = 0;
  virtual void STDMETHODCALLTYPE EndCapture() = 0;
};
#ifdef __CRT_UUID_DECL
__CRT_UUID_DECL(IDXGraphicsAnalysis, 0x9f251514, 0x9d4d, 0x4902, 0x9d, 0x60, 0x18, 0x98, 0x8a, 0xb7, 0xd4, 0xb5)
#endif
#endif
#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <d3dcompiler.h>
#include <dxgi1_5.h>
#include <dxgidebug.h>
// For Microsoft::WRL::ComPtr.
#include <wrl/client.h>
#include <dxcapi.h>
#include "DxbcConverter.h"

// clang-format on
