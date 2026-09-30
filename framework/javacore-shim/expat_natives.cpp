// AOSP's SAX parser natives (org.apache.harmony.xml.ExpatParser and ExpatAttributes), built
// unmodified from libcore against expat-compat/ and a static expat. The staged runtime ships the
// Java classes but not these natives, so SAXParserFactory.newSAXParser() throws
// UnsatisfiedLinkError.
#include "org_apache_harmony_xml_ExpatParser.cpp"

// The two libcore helpers the file declares (JniConstants.h, JniException.h) but libjavacore defines.
jclass JniConstants::GetStringClass(JNIEnv* env) {
    static jclass cls = nullptr;
    if (cls == nullptr) {
        jclass local = env->FindClass("java/lang/String");
        cls = static_cast<jclass>(env->NewGlobalRef(local));
        env->DeleteLocalRef(local);
    }
    return cls;
}

void jniThrowOutOfMemoryError(JNIEnv* env, const char* message) {
    jniThrowException(env, "java/lang/OutOfMemoryError", message);
}

extern "C" int wl_register_expat(JNIEnv* env) {
    for (const char* name : {"org/apache/harmony/xml/ExpatParser", "org/apache/harmony/xml/ExpatAttributes"}) {
        jclass c = env->FindClass(name);
        if (c == nullptr) {
            env->ExceptionClear();
            return 0;
        }
        env->DeleteLocalRef(c);
    }
    register_org_apache_harmony_xml_ExpatParser(env);
    return NELEM(parserMethods) + NELEM(attributeMethods);
}
