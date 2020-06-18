#pragma once

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

extern "C" {
    void WINAPI winrtact_Initialize();
    void WINAPI winrtact_InitializeWithPath(PCWSTR manifestName);
};

HRESULT WINAPI RegFreeWinRTInitializeForTest();
HRESULT WINAPI RegFreeWinRTUninitializeForTest();