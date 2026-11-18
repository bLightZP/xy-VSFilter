// MPC-BE IAllocatorPresenter ABI, through GetSubtitleDelay.
// Copyright 2022-2023 MPC-BE authors. SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

class DECLSPEC_UUID("9075FBA6-E312-434C-A8D1-2996794A3B0A") MpcSubtitleHelperFilter;

struct DECLSPEC_UUID("AD863F43-83F9-4B8E-962C-426F2BDBEAEF") IMpcSubtitlePresenter : IUnknown
{
    STDMETHOD(DisableSubPicInitialization)() PURE;
    STDMETHOD(EnablePreviewModeInitialization)() PURE;
    STDMETHOD(CreateRenderer)(IUnknown** renderer) PURE;
    STDMETHOD_(CLSID, GetAPCLSID)() PURE;
    STDMETHOD_(SIZE, GetVideoSize)() PURE;
    STDMETHOD_(SIZE, GetVideoSizeAR)() PURE;
    STDMETHOD_(void, SetPosition)(RECT window, RECT video) PURE;
    STDMETHOD(SetRotation)(int rotation) PURE;
    STDMETHOD_(int, GetRotation)() PURE;
    STDMETHOD(SetFlip)(bool flip) PURE;
    STDMETHOD_(bool, GetFlip)() PURE;
    STDMETHOD_(bool, Paint)(bool all) PURE;
    STDMETHOD_(void, SetTime)(REFERENCE_TIME now) PURE;
    STDMETHOD_(void, SetSubtitleDelay)(int milliseconds) PURE;
    STDMETHOD_(int, GetSubtitleDelay)() PURE;
};
