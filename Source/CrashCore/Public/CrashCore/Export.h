// Copyright BigCar Team. All Rights Reserved.
//
// Symbol export for the one configuration where CrashCore is a shared library: Unreal's modular (editor) targets.
// CrashCore.Build.cs then defines CRASHCORE_SHARED for CrashCore and its users, and CRASHCORE_BUILD while compiling
// CrashCore itself. Static builds (CMake, Unreal's monolithic game/client/server targets) leave CRASHCORE_EXPORT empty.
// Only out-of-line functions are annotated (never whole classes), so no compiler-generated members are exported.
#pragma once

#ifdef CRASHCORE_SHARED
#ifdef _WIN32
#ifdef CRASHCORE_BUILD
#define CRASHCORE_EXPORT __declspec(dllexport)
#else
#define CRASHCORE_EXPORT __declspec(dllimport)
#endif
#else
#define CRASHCORE_EXPORT __attribute__((visibility("default")))
#endif
#else
#define CRASHCORE_EXPORT
#endif
