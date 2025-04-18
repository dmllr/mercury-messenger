# Keep my stuff
-keepnames class hela.eillance.** { *; }
-keepclassmembers class hela.eillance.** { *; }

# Keep Native Methods and their classes
-keep class hela.eillance.streamer.StreamerService{
    native <methods>;
}
-keep class hela.eillance.viewer.ViewerActivity{
    native <methods>;
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

# Keep Firebase classes
-keep class com.google.firebase.** { *; }
#-keep class com.google.android.gms.** { *; }
#-keep class com.google.common.** { *; }
-keep class com.firebase.ui.** { *; }

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
