# Native methods are bound by their stable JNI names and payload constructors are
# instantiated from native code.
-keepclasseswithmembernames,includedescriptorclasses class com.nuvio.engine.** {
    native <methods>;
}
-keep class com.nuvio.engine.internal.NativeEventPayload { *; }
-keep class com.nuvio.engine.internal.NativeFilePayload { *; }
