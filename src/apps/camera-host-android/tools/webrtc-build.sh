rm -rf out

for arch in "armeabi-v7a arm" "arm64-v8a arm64" "x86 x86" "x86_64 x64"; do
    set -- $arch
    echo -= ${1} =-

    mkdir -p out/webrtc/lib/$1
    gn gen out/${1} --args='target_os="android" target_cpu="'${2}'" is_debug=false rtc_build_tools=false rtc_include_tests=false is_component_build=false rtc_build_examples=false rtc_use_x11=false rtc_use_gtk=false rtc_include_pulse_audio=false use_custom_libcxx=false use_custom_libcxx_for_host=false is_clang=true rtc_enable_protobuf=false rtc_use_h264=true clang_use_chrome_plugins=false'
    autoninja -C out/$1
    zip -j out/webrtc/lib/$1/libwebrtc.a.zip \
      out/$1/obj/libwebrtc.a \
#      out/$1/obj/rtc_base/librtc_base.a \
#      out/$1/obj/api/libjingle_peerconnection_api.a \
#      out/$1/obj/pc/libpeerconnection.a \
#      out/$1/obj/p2p/librtc_p2p.a \
#      out/$1/obj/pc/librtc_pc_base.a \
#      out/$1/obj/sdk/android/libpeerconnection_jni.a
done

find . -name '*.h' | cpio -pdm out/webrtc/include

tools_webrtc/android/build_aar.py --output=out/webrtc/libwebrtc.aar
zip -d out/webrtc/libwebrtc.aar "jni/*/*"

zip -r out/webrtc.zip out/webrtc
