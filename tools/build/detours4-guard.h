#pragma once
// SPDX-License-Identifier: GPL-3.0-or-later
// Every SDK translation unit must use the pinned MIT Detours header.
#include <Windows.h>
#include <detours.h>
#if !defined(DETOURS_VERSION) || DETOURS_VERSION != 0x4c0c1
#error SporeMP requires the pinned MIT Detours 4.0.1 header.
#endif
