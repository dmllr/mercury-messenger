### NDK/JNI bindings
# Keep all native methods and their enclosing classes to ensure JNI linking works
-keepclasseswithmembernames class * {
    native <methods>;
}

# Keep data classes used by C++ via JNI
-keep class hela.eillance.ndkio.** { *; }

# Keep classes whose methods are invoked from C++
-keep class hela.eillance.events.NativeEventBus { *; }
-keep class hela.eillance.webrtc.WebRtcMediaTransport { *; }
-keepclassmembers class hela.eillance.MessengerService {
    public void setWebRtcMediaTransport(hela.eillance.webrtc.WebRtcMediaTransport);
    public void onNativeCoreCrash();
}

# Keep Kotlin Serialization necessary classes and annotations
-keep class kotlinx.serialization.** { *; }
-keep class * implements kotlinx.serialization.KSerializer { *; }
-keep class * implements kotlinx.serialization.SerializationStrategy { *; }
-keep class * implements kotlinx.serialization.DeserializationStrategy { *; }
# Keep classes annotated with @Serializable (and their members)
-keepnames class * { @kotlinx.serialization.Serializable *; }
-keep class **$$serializer { *; } # Keep generated serializers
-keepclassmembers class * {
    @kotlinx.serialization.SerialName <fields>;
    @kotlinx.serialization.Required <fields>;
    @kotlinx.serialization.Transient <fields>;
    <init>(kotlinx.serialization.internal.SerializationConstructorMarker);
}

# Keep Compose runtime necessities
-keepclassmembers class * { @androidx.compose.runtime.Composable <methods>; }
-keep class androidx.compose.runtime.** { *; }
-keep interface androidx.compose.runtime.** { *; }

# Keep any custom Parcelable implementations
-keep class * implements android.os.Parcelable {
  public static final android.os.Parcelable$Creator *;
}

# Keep callback classes used from layouts
-keepclassmembers class * {
    public void *(android.view.View);
}

# Keep enums used in method signatures
-keepclassmembers enum * {
    public static **[] values();
    public static ** valueOf(java.lang.String);
}

# Keep R classes
-keep class **.R$* { *; }
