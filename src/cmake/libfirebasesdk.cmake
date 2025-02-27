set(FIREBASE_USE_BORINGSSL On)
import_remote("https://github.com/firebase/firebase-cpp-sdk.git" TAG "v12.6.0" DST "${CMAKE_SOURCE_DIR}/_imported" ALIAS firebasesdk)
