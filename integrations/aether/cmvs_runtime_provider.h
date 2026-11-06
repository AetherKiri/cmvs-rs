#pragma once
namespace aetherkiri::cmvs {
void RegisterRuntimeProvider();
#if defined(__ANDROID__)
// The host retains the JNI global Application reference for the process.
bool InitializeAndroidContext(void* java_vm, void* application_context);
#endif
}
