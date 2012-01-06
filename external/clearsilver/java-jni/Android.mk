LOCAL_PATH:= $(call my-dir)


# clearsilver java library
# ============================================================
include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
	CS.java \
	CSFileLoader.java \
	JNI.java \
	HDF.java

LOCAL_MODULE:= clearsilver
LOCAL_MODULE_TAGS := optional
include $(BUILD_HOST_JAVA_LIBRARY)

our_java_lib := $(LOCAL_BUILT_MODULE)


# libclearsilver-jni.so
# ============================================================
include $(CLEAR_VARS)

LOCAL_MODULE_TAGS := optional

LOCAL_SRC_FILES:= \
	j_neo_util.c \
	j_neo_cs.c

LOCAL_C_INCLUDES := \
	$(LOCAL_PATH)/..

LOCAL_CFLAGS += -fPIC

#feq ($(HOST_JDK_IS_64BIT_VERSION),true)
LOCAL_CFLAGS += -m32
LOCAL_LDFLAGS += -m32
#ndif
# We use the host compilers because the Linux SDK build
# uses a 32-bit toolchain that can't handle -m64
LOCAL_CC := $(CC)
LOCAL_CXX := $(CXX)

LOCAL_NO_DEFAULT_COMPILER_FLAGS := true

ifeq ($(HOST_OS),darwin)
	LOCAL_C_INCLUDES += /System/Library/Frameworks/JavaVM.framework/Headers
	LOCAL_LDLIBS := -framework JavaVM
else
	LOCAL_C_INCLUDES += $(JNI_H_INCLUDE)
endif

LOCAL_MODULE:= libclearsilver-jni

LOCAL_MODULE_SUFFIX := $(HOST_JNILIB_SUFFIX)

LOCAL_SHARED_LIBRARIES := libneo_util libneo_cs libneo_cgi

include $(BUILD_HOST_SHARED_LIBRARY)

# Use -force with javah to make sure that the output file
# gets updated.  If javah decides not to update the file,
# make gets confused.

GEN := $(intermediates)/org_clearsilver_HDF.h
$(GEN): PRIVATE_OUR_JAVA_LIB := $(our_java_lib)
$(GEN): PRIVATE_CUSTOM_TOOL = javah -classpath $(PRIVATE_OUR_JAVA_LIB) -force -o $@ -jni org.clearsilver.HDF 
$(GEN): PRIVATE_MODULE := $(LOCAL_MODULE)
$(GEN): $(our_java_lib)
	$(transform-generated-source)
$(intermediates)/j_neo_util.o : $(GEN)

GEN := $(intermediates)/org_clearsilver_CS.h
$(GEN): PRIVATE_OUR_JAVA_LIB := $(our_java_lib)
$(GEN): PRIVATE_CUSTOM_TOOL = javah -classpath $(PRIVATE_OUR_JAVA_LIB) -force -o $@ -jni org.clearsilver.CS
$(GEN): PRIVATE_MODULE := $(LOCAL_MODULE)
$(GEN): $(our_java_lib)
	$(transform-generated-source)
$(intermediates)/j_neo_cs.o : $(GEN)
