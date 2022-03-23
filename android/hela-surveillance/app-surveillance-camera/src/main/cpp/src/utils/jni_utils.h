#pragma once


static JavaVM* gJavaVM;
static jweak gClassLoader;

void obtainClassLoader(JNIEnv* env) {
    // Find thread's context class loader.
    jclass javaLangThread = env->FindClass("java/lang/Thread");
    assert(javaLangThread != nullptr);

    jclass javaLangClassLoader = env->FindClass("java/lang/ClassLoader");
    assert(javaLangClassLoader != nullptr);

    jmethodID currentThread = env->GetStaticMethodID(javaLangThread, "currentThread", "()Ljava/lang/Thread;");
    assert(currentThread != nullptr);

    jmethodID getContextClassLoader = env->GetMethodID(javaLangThread, "getContextClassLoader", "()Ljava/lang/ClassLoader;");
    assert(getContextClassLoader != nullptr);

    jobject thread = env->CallStaticObjectMethod(javaLangThread, currentThread);
    assert(thread != nullptr);

    jobject classLoader = env->CallObjectMethod(thread, getContextClassLoader);

    if (classLoader != nullptr) {
        gClassLoader = env->NewWeakGlobalRef(classLoader);
    }
}

jclass loaderFindClass(JNIEnv* env, const char* className) {
    if (env->ExceptionCheck()) {
        fprintf(stderr, "ERROR: exception pending on entry to "
                        "FindClass()\n");
        return nullptr;
    }

    if (gClassLoader == nullptr) {
        return env->FindClass(className);
    }

    // JNI FindClass uses class names with slashes, but
    // ClassLoader.loadClass uses the dotted "binary name"
    // format. Convert formats.
    std::string convName = className;
    for (int i = 0; i < convName.size(); i++) {
        if (convName[i] == '/')
            convName[i] = '.';
    }

    jclass javaLangClassLoader = env->FindClass("java/lang/ClassLoader");
    assert(javaLangClassLoader != nullptr);
    jmethodID loadClass = env->GetMethodID(javaLangClassLoader, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;");
    assert(loadClass != nullptr);

    // Create an object for the class name string; alloc could fail.
    jstring strClassName = env->NewStringUTF(convName.c_str());
    if (env->ExceptionCheck()) {
        fprintf(stderr, "ERROR: unable to convert '%s' to string\n",
                convName.c_str());
        return nullptr;
    }

    // Try to find the named class.
    jclass cls = (jclass) env->CallObjectMethod(gClassLoader, loadClass, strClassName);

    if (env->ExceptionCheck()) {
        env->ExceptionDescribe();
        fprintf(stderr, "ERROR: unable to load class '%s' from %p\n", className, gClassLoader);
        return nullptr;
    }

    return cls;
}
