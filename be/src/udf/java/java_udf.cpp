// Copyright 2021-present StarRocks, Inc. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "udf/java/java_udf.h"

#include <iterator>
#include <memory>
#include <sstream>
#include <string>

#include "column/binary_column.h"
#include "column/column.h"
#include "common/status.h"
#include "exprs/function_context.h"
#include "fmt/core.h"
#include "jni.h"
#include "types/logical_type.h"
#include "udf/java/java_native_method.h"
#include "udf/java/type_traits.h"
#include "udf/java/utils.h"
#include "util/defer_op.h"

// find a jclass and return a global jclass ref
#define JNI_FIND_CLASS(clazz_name)                                                                 \
    [](const char* name) {                                                                         \
        auto clazz = _env->FindClass(name);                                                        \
        CHECK(clazz != nullptr) << "not found class" << name << " plz check JDK and jni-packages"; \
        auto g_clazz = _env->NewGlobalRef(clazz);                                                  \
        _env->DeleteLocalRef(clazz);                                                               \
        return (jclass)g_clazz;                                                                    \
    }(clazz_name)

#define ADD_NUMBERIC_CLASS(prim_clazz, clazz, sign)                                                           \
    {                                                                                                         \
        _class_##prim_clazz = JNI_FIND_CLASS("java/lang/" #clazz);                                            \
        CHECK(_class_##prim_clazz);                                                                           \
        _value_of_##prim_clazz =                                                                              \
                _env->GetStaticMethodID(_class_##prim_clazz, "valueOf", "(" #sign ")Ljava/lang/" #clazz ";"); \
        CHECK(_value_of_##prim_clazz) << "Not Found"                                                          \
                                      << "(" #sign ")Ljava/lang/" #clazz ";";                                 \
        _val_##prim_clazz = _env->GetMethodID(_class_##prim_clazz, #prim_clazz "Value", "()" #sign);          \
        CHECK(_val_##prim_clazz);                                                                             \
    }

#define DEFINE_NEW_BOX(prim_clazz, cxx_type, CLAZZ, CallType)                                    \
    jobject JVMFunctionHelper::new##CLAZZ(cxx_type value) {                                      \
        return _env->CallStaticObjectMethod(_class_##prim_clazz, _value_of_##prim_clazz, value); \
    }                                                                                            \
    cxx_type JVMFunctionHelper::val##cxx_type(jobject obj) {                                     \
        return _env->Call##CallType##Method(obj, _val_##prim_clazz);                             \
    }

namespace starrocks {

constexpr const char* CLASS_UDF_HELPER_NAME = "com.starrocks.udf.UDFHelper";
constexpr const char* CLASS_NATIVE_METHOD_HELPER_NAME = "com.starrocks.utils.NativeMethodHelper";

constexpr const char* BATCH_UPDATE_SIGNATURE =
        "(Ljava/lang/Object;Ljava/lang/reflect/Method;Lcom/starrocks/udf/FunctionStates;[I[Ljava/lang/Object;)V";
constexpr const char* BATCH_CALL_SIGNATURE =
        "(Ljava/lang/Object;Ljava/lang/reflect/Method;I[Ljava/lang/Object;)[Ljava/lang/Object;";
constexpr const char* BATCH_CALL_NO_ARGS_SIGNATURE =
        "(Ljava/lang/Object;Ljava/lang/reflect/Method;I)[Ljava/lang/Object;";
constexpr const char* BATCH_UPDATE_STATE_SIGNATURE =
        "(Ljava/lang/Object;Ljava/lang/reflect/Method;[Ljava/lang/Object;)V";
constexpr const char* BATCH_UPDATE_IF_NOT_NULL_SIGNATURE =
        "(Ljava/lang/Object;Ljava/lang/reflect/Method;Lcom/starrocks/udf/FunctionStates;[I[Ljava/lang/Object;)V";
constexpr const char* INT_BATCH_CALL_SIGNATURE = "([Ljava/lang/Object;Ljava/lang/reflect/Method;I)[I";

constexpr const char* CREATE_BOXED_PRIMI_SIGNATURE = "(ILjava/nio/ByteBuffer;Ljava/nio/ByteBuffer;)[Ljava/lang/Object;";
constexpr const char* CREATE_BOXED_STRING_SIGNATURE =
        "(ILjava/nio/ByteBuffer;Ljava/nio/ByteBuffer;Ljava/nio/ByteBuffer;)[Ljava/lang/Object;";
constexpr const char* CREATE_BOXED_LIST_SIGNATURE =
        "(ILjava/nio/ByteBuffer;Ljava/nio/ByteBuffer;[Ljava/lang/Object;)[Ljava/lang/Object;";
constexpr const char* CREATE_BOXED_MAP_SIGNATURE =
        "(ILjava/nio/ByteBuffer;Ljava/nio/ByteBuffer;[Ljava/lang/Object;[Ljava/lang/Object;)[Ljava/lang/Object;";

#define INIT_STATIC_METHOD(target, clazz, name, signature)    \
    target = _env->GetStaticMethodID(clazz, name, signature); \
    CHECK(target != nullptr) << "not found method:" << name << " plz check your jni-packages";

#define INIT_HELPER_METHOD(target, name, signature) INIT_STATIC_METHOD(target, _udf_helper_class, name, signature);

#define SET_METHOD_ID(target, clazz, name, signature)   \
    target = _env->GetMethodID(clazz, name, signature); \
    DCHECK(target != nullptr);

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wwrite-strings"
static JNINativeMethod java_native_methods[] = {
        {"resizeStringData", "(JI)J", (void*)&JavaNativeMethods::resizeStringData},
        {"resize", "(JI)V", (void*)&JavaNativeMethods::resize},
        {"getColumnLogicalType", "(J)I", (void*)&JavaNativeMethods::getColumnLogicalType},
        {"getAddrs", "(J)[J", (void*)&JavaNativeMethods::getAddrs},
        {"memoryTrackerMalloc", "(J)J", (void*)&JavaNativeMethods::memory_malloc},
        {"memoryTrackerFree", "(J)V", (void*)&JavaNativeMethods::memory_free},
};
#pragma GCC diagnostic pop

StatusOr<jobject> MapMeta::newLocalInstance(jobject keys, jobject values) const {
    JNIEnv* env = getJNIEnv();
    return env->NewObject(immutable_map_class->clazz(), immutable_map_constructor, keys, values);
}

JVMFunctionHelper& JVMFunctionHelper::getInstance() {
    if (_env == nullptr) {
        _env = getJNIEnv();
        CHECK(_env != nullptr) << "couldn't got a JNIEnv";
    }
    static JVMFunctionHelper helper;
    return helper;
}

std::pair<JNIEnv*, JVMFunctionHelper&> JVMFunctionHelper::getInstanceWithEnv() {
    auto& instance = getInstance();
    return {instance.getEnv(), instance};
}

void JVMFunctionHelper::_init() {
    _object_class = JNI_FIND_CLASS("java/lang/Object");
    _object_array_class = JNI_FIND_CLASS("[Ljava/lang/Object;");
    _string_class = JNI_FIND_CLASS("java/lang/String");
    _jarrays_class = JNI_FIND_CLASS("java/util/Arrays");
    _exception_util_class = JNI_FIND_CLASS("org/apache/commons/lang3/exception/ExceptionUtils");

    CHECK(_object_class);
    CHECK(_string_class);
    CHECK(_jarrays_class);
    CHECK(_exception_util_class);

    ADD_NUMBERIC_CLASS(boolean, Boolean, Z);
    ADD_NUMBERIC_CLASS(byte, Byte, B);
    ADD_NUMBERIC_CLASS(short, Short, S);
    ADD_NUMBERIC_CLASS(int, Integer, I);
    ADD_NUMBERIC_CLASS(long, Long, J);
    ADD_NUMBERIC_CLASS(float, Float, F);
    ADD_NUMBERIC_CLASS(double, Double, D);

    jclass charsets = JNI_FIND_CLASS("java/nio/charset/StandardCharsets");
    DCHECK(charsets != nullptr);
    auto fieldId = _env->GetStaticFieldID(charsets, "UTF_8", "Ljava/nio/charset/Charset;");
    DCHECK(fieldId != nullptr);

    auto charset = _env->GetStaticObjectField(charsets, fieldId);
    _utf8_charsets = _env->NewGlobalRef(charset);
    _env->DeleteLocalRef(charset);

    DCHECK(_utf8_charsets != nullptr);
    _string_construct_with_bytes = _env->GetMethodID(_string_class, "<init>", "([BLjava/nio/charset/Charset;)V");
    DCHECK(_string_construct_with_bytes != nullptr);

    // init JNI native methods
    std::string native_method_name = JVMFunctionHelper::to_jni_class_name(CLASS_NATIVE_METHOD_HELPER_NAME);
    jclass native_method_class = JNI_FIND_CLASS(native_method_name.c_str());
    DCHECK(native_method_class != nullptr);
    int res = _env->RegisterNatives(native_method_class, java_native_methods,
                                    sizeof(java_native_methods) / sizeof(java_native_methods[0]));

    // init UDF Helper
    std::string name = JVMFunctionHelper::to_jni_class_name(CLASS_UDF_HELPER_NAME);
    _udf_helper_class = JNI_FIND_CLASS(name.c_str());
    DCHECK(_udf_helper_class != nullptr);
    DCHECK_EQ(res, 0);

    INIT_HELPER_METHOD(_create_boxed_array, "createBoxedArray", "(IIZ[Ljava/nio/ByteBuffer;)[Ljava/lang/Object;");
    INIT_HELPER_METHOD(_batch_update, "batchUpdate", BATCH_UPDATE_SIGNATURE);
    INIT_HELPER_METHOD(_batch_call, "batchCall", BATCH_CALL_SIGNATURE);
    INIT_HELPER_METHOD(_batch_call_no_args, "batchCall", BATCH_CALL_NO_ARGS_SIGNATURE);
    INIT_HELPER_METHOD(_batch_update_state, "batchUpdateState", BATCH_UPDATE_STATE_SIGNATURE);
    INIT_HELPER_METHOD(_batch_update_if_not_null, "batchUpdateIfNotNull", BATCH_UPDATE_IF_NOT_NULL_SIGNATURE);
    INIT_HELPER_METHOD(_batch_create_bytebuf, "batchCreateDirectBuffer", "(J[II)[Ljava/lang/Object;");
    INIT_HELPER_METHOD(_int_batch_call, "batchCall", INT_BATCH_CALL_SIGNATURE);
    INIT_HELPER_METHOD(_get_boxed_result, "getResultFromBoxedArray", "(IILjava/lang/Object;J)V");
    INIT_HELPER_METHOD(_extract_keys_from_map, "extractKeyList", "(Ljava/util/Map;)Ljava/util/List;");
    INIT_HELPER_METHOD(_extract_values_from_map, "extractValueList", "(Ljava/util/Map;)Ljava/util/List;");
    // init UDF Helper convert method
    jmethodID tmp = nullptr;
    INIT_HELPER_METHOD(tmp, "createBoxedBoolArray", CREATE_BOXED_PRIMI_SIGNATURE);
    _method_map.emplace(JNIPrimTypeId<uint8_t>::id, tmp);
    INIT_HELPER_METHOD(tmp, "createBoxedByteArray", CREATE_BOXED_PRIMI_SIGNATURE);
    _method_map.emplace(JNIPrimTypeId<int8_t>::id, tmp);
    INIT_HELPER_METHOD(tmp, "createBoxedShortArray", CREATE_BOXED_PRIMI_SIGNATURE);
    _method_map.emplace(JNIPrimTypeId<int16_t>::id, tmp);
    INIT_HELPER_METHOD(tmp, "createBoxedIntegerArray", CREATE_BOXED_PRIMI_SIGNATURE);
    _method_map.emplace(JNIPrimTypeId<int32_t>::id, tmp);
    INIT_HELPER_METHOD(tmp, "createBoxedLongArray", CREATE_BOXED_PRIMI_SIGNATURE);
    _method_map.emplace(JNIPrimTypeId<int64_t>::id, tmp);
    INIT_HELPER_METHOD(tmp, "createBoxedFloatArray", CREATE_BOXED_PRIMI_SIGNATURE);
    _method_map.emplace(JNIPrimTypeId<float>::id, tmp);
    INIT_HELPER_METHOD(tmp, "createBoxedDoubleArray", CREATE_BOXED_PRIMI_SIGNATURE);
    _method_map.emplace(JNIPrimTypeId<double>::id, tmp);
    INIT_HELPER_METHOD(tmp, "createBoxedStringArray", CREATE_BOXED_STRING_SIGNATURE);
    _method_map.emplace(JNIPrimTypeId<Slice>::id, tmp);
    INIT_HELPER_METHOD(tmp, "createBoxedListArray", CREATE_BOXED_LIST_SIGNATURE);
    _method_map.emplace(TYPE_ARRAY_METHOD_ID, tmp);
    INIT_HELPER_METHOD(tmp, "createBoxedMapArray", CREATE_BOXED_MAP_SIGNATURE);
    _method_map.emplace(TYPE_MAP_METHOD_ID, tmp);

    // init bytebuffer
    _direct_buffer_class = JNI_FIND_CLASS("java/nio/ByteBuffer");
    SET_METHOD_ID(_direct_buffer_clear, _direct_buffer_class, "clear", "()Ljava/nio/Buffer;");

    // init list meta
    auto list_clazz = JNI_FIND_CLASS("java/util/List");
    DCHECK(list_clazz);
    _list_meta.list_class = new JVMClass(std::move(list_clazz));
    auto array_list_clazz = JNI_FIND_CLASS("java/util/ArrayList");
    DCHECK(array_list_clazz);
    _list_meta.array_list_class = new JVMClass(std::move(array_list_clazz));

    SET_METHOD_ID(_list_meta.list_get, list_clazz, "get", "(I)Ljava/lang/Object;");
    SET_METHOD_ID(_list_meta.list_size, list_clazz, "size", "()I");
    SET_METHOD_ID(_list_meta.list_add, list_clazz, "add", "(Ljava/lang/Object;)Z");

    // init immutable map meta
    auto immutable_map_clazz = JNI_FIND_CLASS("com/starrocks/udf/ImmutableMap");
    DCHECK(immutable_map_clazz != nullptr);
    _map_meta.immutable_map_class = new JVMClass(std::move(immutable_map_clazz));
    auto map_clazz = JNI_FIND_CLASS("java/util/Map");
    DCHECK(map_clazz);
    _map_meta.map_class = new JVMClass(map_clazz);
    _map_meta.immutable_map_constructor =
            _env->GetMethodID(_map_meta.immutable_map_class->clazz(), "<init>", "(Ljava/util/List;Ljava/util/List;)V");

    name = JVMFunctionHelper::to_jni_class_name(UDAFStateList::clazz_name);
    jclass loaded_clazz = JNI_FIND_CLASS(name.c_str());
    _function_states_clazz = new JVMClass(std::move(loaded_clazz));
}

jobjectArray JVMFunctionHelper::_build_object_array(jclass clazz, jobject* arr, int sz) {
    jobjectArray res_arr = _env->NewObjectArray(sz, _object_array_class, nullptr);
    for (int i = 0; i < sz; ++i) {
        _env->SetObjectArrayElement(res_arr, i, arr[i]);
    }
    return res_arr;
}

JVMClass& JVMFunctionHelper::function_state_clazz() {
    return *_function_states_clazz;
}

#define CHECK_FUNCTION_EXCEPTION(_env, name)                   \
    if (auto e = _env->ExceptionOccurred()) {                  \
        LOCAL_REF_GUARD(e);                                    \
        _env->ExceptionClear();                                \
        LOG(WARNING) << "Exception happened when call " #name; \
        return "";                                             \
    }

#define RETURN_ERROR_IF_EXCEPTION(env, errmsg)                                   \
    if (jthrowable jthr = env->ExceptionOccurred()) {                            \
        LOCAL_REF_GUARD(jthr);                                                   \
        std::string msg = fmt::format(errmsg, helper.dumpExceptionString(jthr)); \
        LOG(WARNING) << msg;                                                     \
        env->ExceptionClear();                                                   \
        return Status::RuntimeError(msg);                                        \
    }

Status JVMFunctionHelper::_check_exception_status() {
    auto& helper = *this;
    RETURN_ERROR_IF_EXCEPTION(_env, "exception happened when invoke method: {}");
    return Status::OK();
}

std::string JVMFunctionHelper::array_to_string(jobject object) {
    _env->ExceptionClear();
    std::string value;
    jmethodID arrayToStringMethod =
            _env->GetStaticMethodID(_jarrays_class, "toString", "([Ljava/lang/Object;)Ljava/lang/String;");
    DCHECK(arrayToStringMethod != nullptr);
    jobject jstr = _env->CallStaticObjectMethod(_jarrays_class, arrayToStringMethod, object);
    LOCAL_REF_GUARD(jstr);
    CHECK_FUNCTION_EXCEPTION(_env, "array_to_string")
    value = to_cxx_string((jstring)jstr);
    return value;
}

std::string JVMFunctionHelper::to_string(jobject obj) {
    _env->ExceptionClear();
    std::string value;
    auto method = getToStringMethod(_object_class);
    auto res = _env->CallObjectMethod(obj, method);
    LOCAL_REF_GUARD(res);
    CHECK_FUNCTION_EXCEPTION(_env, "to_string")
    value = to_cxx_string((jstring)res);
    return value;
}

std::string JVMFunctionHelper::to_cxx_string(jstring str) {
    std::string res;
    const char* charflow = _env->GetStringUTFChars((jstring)str, nullptr);
    res = charflow;
    _env->ReleaseStringUTFChars((jstring)str, charflow);
    return res;
}

std::string JVMFunctionHelper::dumpExceptionString(jthrowable throwable) {
    // toString
    auto get_stack_trace = _env->GetStaticMethodID(_exception_util_class, "getStackTrace",
                                                   "(Ljava/lang/Throwable;)Ljava/lang/String;");
    CHECK(get_stack_trace != nullptr) << "Not Found JNI method getStackTrace";
    jobject stack_traces = _env->CallStaticObjectMethod(_exception_util_class, get_stack_trace, (jobject)throwable);
    LOCAL_REF_GUARD(stack_traces);
    return to_cxx_string((jstring)stack_traces);
}

jmethodID JVMFunctionHelper::getToStringMethod(jclass clazz) {
    return _env->GetMethodID(clazz, "toString", "()Ljava/lang/String;");
}

jstring JVMFunctionHelper::to_jstring(const std::string& str) {
    return _env->NewStringUTF(str.c_str());
}

jmethodID JVMFunctionHelper::getMethod(jclass clazz, const std::string& method, const std::string& sig) {
    return _env->GetMethodID(clazz, method.c_str(), sig.c_str());
}

jmethodID JVMFunctionHelper::getStaticMethod(jclass clazz, const std::string& method, const std::string& sig) {
    return _env->GetStaticMethodID(clazz, method.c_str(), sig.c_str());
}

jobject JVMFunctionHelper::create_array(int sz) {
    return _env->NewObjectArray(sz, _object_class, nullptr);
}

jobject JVMFunctionHelper::create_boxed_array(int type, int num_rows, bool nullable, DirectByteBuffer* buffs, int sz) {
    jobjectArray input_arr = _env->NewObjectArray(sz, _direct_buffer_class, nullptr);
    LOCAL_REF_GUARD(input_arr);
    for (int i = 0; i < sz; ++i) {
        _env->SetObjectArrayElement(input_arr, i, buffs[i].handle());
    }
    jobject res =
            _env->CallStaticObjectMethod(_udf_helper_class, _create_boxed_array, type, num_rows, nullable, input_arr);
    if (_env->ExceptionCheck()) {
        LOG(WARNING) << "fail to create array " << this->dumpExceptionString(_env->ExceptionOccurred());
        _env->ExceptionClear();
    }
    return res;
}

jobject JVMFunctionHelper::create_object_array(jobject o, int num_rows) {
    jobjectArray res_arr = _env->NewObjectArray(num_rows, _object_array_class, o);
    return res_arr;
}

jobject JVMFunctionHelper::batch_create_bytebuf(unsigned char* ptr, const uint32_t* offset, int begin, int end) {
    int size = end - begin;
    auto offsets = _env->NewIntArray(size + 1);
    _env->SetIntArrayRegion(offsets, 0, size + 1, (const int32_t*)offset);
    LOCAL_REF_GUARD(offsets);
    return _env->CallStaticObjectMethod(_udf_helper_class, _batch_create_bytebuf, ptr, offsets, size);
}

void JVMFunctionHelper::batch_update_single(AggBatchCallStub* stub, int state, jobject* input, int cols, int rows) {
    auto obj = convert_handle_to_jobject(stub->ctx(), state);
    LOCAL_REF_GUARD(obj);
    stub->batch_update_single(rows, obj, input, cols);
}

void JVMFunctionHelper::batch_update(FunctionContext* ctx, jobject udaf, jobject update, jobject states, jobject* input,
                                     int cols) {
    jobjectArray input_arr = _build_object_array(_object_array_class, input, cols);
    LOCAL_REF_GUARD(input_arr);
    _env->CallStaticVoidMethod(_udf_helper_class, _batch_update, udaf, update, ctx->udaf_ctxs()->states->handle(),
                               states, input_arr);
    CHECK_UDF_CALL_EXCEPTION(_env, ctx);
}

void JVMFunctionHelper::batch_update_state(FunctionContext* ctx, jobject udaf, jobject update, jobject* input,
                                           int cols) {
    jobjectArray input_arr = _build_object_array(_object_array_class, input, cols);
    LOCAL_REF_GUARD(input_arr);
    _env->CallStaticVoidMethod(_udf_helper_class, _batch_update_state, udaf, update, input_arr);
    CHECK_UDF_CALL_EXCEPTION(_env, ctx);
}

void JVMFunctionHelper::batch_update_if_not_null(FunctionContext* ctx, jobject udaf, jobject update, jobject states,
                                                 jobject* input, int cols) {
    jobjectArray input_arr = _build_object_array(_object_array_class, input, cols);
    LOCAL_REF_GUARD(input_arr);
    _env->CallStaticVoidMethod(_udf_helper_class, _batch_update_if_not_null, udaf, update,
                               ctx->udaf_ctxs()->states->handle(), states, input_arr);
    CHECK_UDF_CALL_EXCEPTION(_env, ctx);
}

StatusOr<jobject> JVMFunctionHelper::batch_call(BatchEvaluateStub* stub, jobject* input, int cols, int rows) {
    return stub->batch_evaluate(rows, input, cols);
}

jobject JVMFunctionHelper::batch_call(FunctionContext* ctx, jobject caller, jobject method, jobject* input, int cols,
                                      int rows) {
    jobjectArray input_arr = _build_object_array(_object_array_class, input, cols);
    LOCAL_REF_GUARD(input_arr);
    auto res = _env->CallStaticObjectMethod(_udf_helper_class, _batch_call, caller, method, rows, input_arr);
    CHECK_UDF_CALL_EXCEPTION(_env, ctx);
    return res;
}

jobject JVMFunctionHelper::batch_call(FunctionContext* ctx, jobject caller, jobject method, int rows) {
    auto res = _env->CallStaticObjectMethod(_udf_helper_class, _batch_call_no_args, caller, method, rows);
    CHECK_UDF_CALL_EXCEPTION(_env, ctx);
    return res;
}

jobject JVMFunctionHelper::int_batch_call(FunctionContext* ctx, jobject callers, jobject method, int rows) {
    auto res = _env->CallStaticObjectMethod(_udf_helper_class, _int_batch_call, callers, method, rows);
    CHECK_UDF_CALL_EXCEPTION(_env, ctx);
    return res;
}

void JVMFunctionHelper::get_result_from_boxed_array(FunctionContext* ctx, int type, Column* col, jobject jcolumn,
                                                    int rows) {
    col->resize(rows);
    _env->CallStaticVoidMethod(_udf_helper_class, _get_boxed_result, type, rows, jcolumn,
                               reinterpret_cast<int64_t>(col));
    CHECK_UDF_CALL_EXCEPTION(_env, ctx);
}

Status JVMFunctionHelper::get_result_from_boxed_array(int type, Column* col, jobject jcolumn, int rows) {
    col->resize(rows);
    _env->CallStaticVoidMethod(_udf_helper_class, _get_boxed_result, type, rows, jcolumn,
                               reinterpret_cast<int64_t>(col));
    RETURN_ERROR_IF_JNI_EXCEPTION(_env);
    return Status::OK();
}

// convert UDAF ctx to jobject
jobject JVMFunctionHelper::convert_handle_to_jobject(FunctionContext* ctx, int state) {
    auto* states = ctx->udaf_ctxs()->states.get();
    return states->get_state(ctx, _env, state);
}

jobject JVMFunctionHelper::convert_handles_to_jobjects(FunctionContext* ctx, jobject state_ids) {
    auto* states = ctx->udaf_ctxs()->states.get();
    return states->get_state(ctx, _env, state_ids);
}

StatusOr<jobject> JVMFunctionHelper::extract_key_list(jobject map) {
    auto res = _env->CallStaticObjectMethod(_udf_helper_class, _extract_keys_from_map, map);
    RETURN_ERROR_IF_JNI_EXCEPTION(_env);
    return res;
}

StatusOr<jobject> JVMFunctionHelper::extract_val_list(jobject map) {
    auto res = _env->CallStaticObjectMethod(_udf_helper_class, _extract_values_from_map, map);
    RETURN_ERROR_IF_JNI_EXCEPTION(_env);
    return res;
}

DEFINE_NEW_BOX(boolean, uint8_t, Boolean, Boolean);
DEFINE_NEW_BOX(byte, int8_t, Byte, Byte);
DEFINE_NEW_BOX(short, int16_t, Short, Short);
DEFINE_NEW_BOX(int, int32_t, Integer, Int);
DEFINE_NEW_BOX(long, int64_t, Long, Long);
DEFINE_NEW_BOX(float, float, Float, Float);
DEFINE_NEW_BOX(double, double, Double, Double);

jobject JVMFunctionHelper::newString(const char* data, size_t size) {
    auto bytesArr = _env->NewByteArray(size);
    LOCAL_REF_GUARD(bytesArr);
    _env->SetByteArrayRegion(bytesArr, 0, size, reinterpret_cast<const jbyte*>(data));
    jobject nstr = _env->NewObject(_string_class, _string_construct_with_bytes, bytesArr, _utf8_charsets);
    return nstr;
}

Slice JVMFunctionHelper::sliceVal(jstring jstr, std::string* buffer) {
    const size_t utf_length = _env->GetStringUTFLength(jstr);
    buffer->resize(utf_length);
    const size_t string_length = _env->GetStringLength(jstr);
    _env->GetStringUTFRegion(jstr, 0, string_length, buffer->data());
    return {buffer->data(), buffer->length()};
}

std::string JVMFunctionHelper::to_jni_class_name(const std::string& name) {
    std::string jni_class_name;
    auto inserter = std::inserter(jni_class_name, jni_class_name.end());
    std::transform(name.begin(), name.end(), inserter, [](auto ele) {
        if (ele == '.') {
            return '/';
        }
        return ele;
    });
    return jni_class_name;
}

void JVMFunctionHelper::clear(DirectByteBuffer* buffer, FunctionContext* ctx) {
    auto res = _env->CallNonvirtualObjectMethod(buffer->handle(), _direct_buffer_class, _direct_buffer_clear);
    LOCAL_REF_GUARD(res);
    CHECK_UDF_CALL_EXCEPTION(_env, ctx);
}

DirectByteBuffer::DirectByteBuffer(void* ptr, int capacity) {
    auto& helper = JVMFunctionHelper::getInstance();
    auto* env = helper.getEnv();
    auto lref = env->NewDirectByteBuffer(ptr, capacity);
    LOCAL_REF_GUARD(lref);
    _handle = env->NewGlobalRef(lref);
    _capacity = capacity;
    _data = ptr;
}

DirectByteBuffer::~DirectByteBuffer() {
    if (_handle != nullptr) {
        auto ret = call_hdfs_scan_function_in_pthread([this]() {
            auto& helper = JVMFunctionHelper::getInstance();
            JNIEnv* env = helper.getEnv();
            env->DeleteGlobalRef(_handle);
            _handle = nullptr;
            return Status::OK();
        });
        (void)ret->get_future().get();
    }
}

JavaGlobalRef::~JavaGlobalRef() {
    clear();
}

void JavaGlobalRef::clear() {
    if (_handle) {
        auto ret = call_hdfs_scan_function_in_pthread([this]() {
            JVMFunctionHelper::getInstance().getEnv()->DeleteGlobalRef(_handle);
            _handle = nullptr;
            return Status::OK();
        });
        (void)ret->get_future().get();
    }
}

StatusOr<JavaGlobalRef> JVMClass::newInstance() const {
    JNIEnv* env = getJNIEnv();
    // get default constructor
    jmethodID constructor = env->GetMethodID((jclass)_clazz.handle(), "<init>", "()V");
    if (constructor == nullptr) {
        return Status::InternalError("couldn't found default constructor for Java Object");
    }
    auto local_ref = env->NewObject((jclass)_clazz.handle(), constructor);
    LOCAL_REF_GUARD(local_ref);
    return env->NewGlobalRef(local_ref);
}

StatusOr<jobject> JVMClass::newLocalInstance() const {
    JNIEnv* env = getJNIEnv();
    // get default constructor
    jmethodID constructor = env->GetMethodID((jclass)_clazz.handle(), "<init>", "()V");
    if (constructor == nullptr) {
        return Status::InternalError("couldn't found default constructor for Java Object");
    }
    return env->NewObject((jclass)_clazz.handle(), constructor);
}

UDAFStateList::UDAFStateList(JavaGlobalRef&& handle, JavaGlobalRef&& get, JavaGlobalRef&& batch_get,
                             JavaGlobalRef&& add, JavaGlobalRef&& remove, JavaGlobalRef&& clear)
        : _handle(std::move(handle)),
          _get_method(std::move(get)),
          _batch_get_method(std::move(batch_get)),
          _add_method(std::move(add)),
          _remove_method(std::move(remove)),
          _clear_method(std::move(clear)) {
    auto* env = JVMFunctionHelper::getInstance().getEnv();
    _get_method_id = env->FromReflectedMethod(_get_method.handle());
    _batch_get_method_id = env->FromReflectedMethod(_batch_get_method.handle());
    _add_method_id = env->FromReflectedMethod(_add_method.handle());
    _remove_method_id = env->FromReflectedMethod(_remove_method.handle());
    _clear_method_id = env->FromReflectedMethod(_clear_method.handle());
}

jobject UDAFStateList::get_state(FunctionContext* ctx, JNIEnv* env, int state_handle) {
    auto obj = env->CallObjectMethod(_handle.handle(), _get_method_id, state_handle);
    CHECK_UDF_CALL_EXCEPTION(env, ctx);
    return obj;
}

jobject UDAFStateList::get_state(FunctionContext* ctx, JNIEnv* env, jobject state_ids) {
    auto obj = env->CallObjectMethod(_handle.handle(), _batch_get_method_id, state_ids);
    CHECK_UDF_CALL_EXCEPTION(env, ctx);
    return obj;
}

int UDAFStateList::add_state(FunctionContext* ctx, JNIEnv* env, jobject state) {
    auto res = env->CallIntMethod(_handle.handle(), _add_method_id, state);
    CHECK_UDF_CALL_EXCEPTION(env, ctx);
    return res;
}

void UDAFStateList::remove(FunctionContext* ctx, JNIEnv* env, int state_handle) {
    env->CallVoidMethod(_handle.handle(), _remove_method_id, state_handle);
    CHECK_UDF_CALL_EXCEPTION(env, ctx);
}

void UDAFStateList::clear(FunctionContext* ctx, JNIEnv* env) {
    env->CallVoidMethod(_handle.handle(), _clear_method_id);
    CHECK_UDF_CALL_EXCEPTION(env, ctx);
}

ClassLoader::~ClassLoader() {
    _handle.clear();
    _clazz.clear();
}

constexpr const char* CLASS_LOADER_NAME = "com.starrocks.udf.UDFClassLoader";
constexpr const char* CLASS_ANALYZER_NAME = "com.starrocks.udf.UDFClassAnalyzer";

Status ClassLoader::init() {
    JNIEnv* env = JVMFunctionHelper::getInstance().getEnv();
    if (env == nullptr) {
        return Status::InternalError("Init JNIEnv fail");
    }
    std::string name = JVMFunctionHelper::to_jni_class_name(CLASS_LOADER_NAME);
    jclass clazz = env->FindClass(name.c_str());
    LOCAL_REF_GUARD(clazz);
    _clazz = env->NewGlobalRef(clazz);

    if (clazz == nullptr) {
        return Status::InternalError(fmt::format("ClassLoader Not Found: {}", CLASS_LOADER_NAME));
    }

    jmethodID udf_loader_contructor = env->GetMethodID(clazz, "<init>", "(Ljava/lang/String;)V");
    if (udf_loader_contructor == nullptr) {
        return Status::InternalError("ClassLoader constructor Not Found");
    }

    // create class loader instance
    jstring jstr = env->NewStringUTF(_path.c_str());
    LOCAL_REF_GUARD(jstr);

    auto handle = env->NewObject(clazz, udf_loader_contructor, jstr);
    LOCAL_REF_GUARD(handle);
    _handle = env->NewGlobalRef(handle);

    if (jthrowable jthr = env->ExceptionOccurred()) {
        LOCAL_REF_GUARD(jthr);
        std::string err_msg = fmt::format("Error: couldn't create class loader {},{} ", CLASS_LOADER_NAME,
                                          JVMFunctionHelper::getInstance().dumpExceptionString(jthr));
        LOG(WARNING) << err_msg;
        return Status::InternalError(err_msg);
    }

    // init method id
    _get_class = env->GetMethodID((jclass)_clazz.handle(), "findClass", "(Ljava/lang/String;)Ljava/lang/Class;");
    _get_call_stub =
            env->GetMethodID((jclass)_clazz.handle(), "generateCallStubV",
                             "(Ljava/lang/String;Ljava/lang/Class;Ljava/lang/reflect/Method;I)Ljava/lang/Class;");

    // init method
    if (_get_class == nullptr || _get_call_stub == nullptr) {
        return Status::InternalError("couldn't get method for classloader");
    }

    return Status::OK();
}

StatusOr<JVMClass> ClassLoader::getClass(const std::string& className) {
    auto& helper = JVMFunctionHelper::getInstance();
    JNIEnv* env = helper.getEnv();
    CHECK(env != nullptr) << "couldn't got a JNIEnv";

    // class Name java.lang.Object -> java/lang/Object
    std::string jni_class_name = JVMFunctionHelper::to_jni_class_name(className);
    // invoke class loader
    jstring jstr_name = helper.to_jstring(jni_class_name);
    LOCAL_REF_GUARD(jstr_name);

    auto loaded_clazz = env->CallObjectMethod(_handle.handle(), _get_class, jstr_name);
    LOCAL_REF_GUARD(loaded_clazz);

    // check exception
    RETURN_ERROR_IF_EXCEPTION(env, "exception happened when get class: {}");
    // no exception happened, class exists
    DCHECK(loaded_clazz != nullptr);

    auto res = env->NewGlobalRef(loaded_clazz);
    return res;
}

StatusOr<JVMClass> ClassLoader::genCallStub(const std::string& stubClassName, jclass clazz, jobject method, int type) {
    auto& helper = JVMFunctionHelper::getInstance();
    JNIEnv* env = helper.getEnv();

    std::string jni_class_name = JVMFunctionHelper::to_jni_class_name(stubClassName);
    jstring jstr_name = helper.to_jstring(jni_class_name);
    LOCAL_REF_GUARD(jstr_name);

    // generate call stub
    auto loaded_clazz = env->CallObjectMethod(_handle.handle(), _get_call_stub, jstr_name, clazz, method, type);
    LOCAL_REF_GUARD(loaded_clazz);
    RETURN_ERROR_IF_EXCEPTION(env, "exception happened when gen call stub: {}");

    auto res = env->NewGlobalRef(loaded_clazz);
    return res;
}

jmethodID JavaMethodDescriptor::get_method_id() const {
    return JVMFunctionHelper::getInstance().getEnv()->FromReflectedMethod(method.handle());
}

Status ClassAnalyzer::has_method(jclass clazz, const std::string& method, bool* has) {
    DCHECK(clazz != nullptr);
    DCHECK(has != nullptr);

    auto& helper = JVMFunctionHelper::getInstance();
    JNIEnv* env = getJNIEnv();

    std::string anlyzer_clazz_name = JVMFunctionHelper::to_jni_class_name(CLASS_ANALYZER_NAME);
    jclass class_analyzer = env->FindClass(anlyzer_clazz_name.c_str());
    LOCAL_REF_GUARD(class_analyzer);

    if (class_analyzer == nullptr) {
        return Status::InternalError(fmt::format("ClassAnalyzer Not Found: {}", CLASS_ANALYZER_NAME));
    }

    jmethodID hasMethod =
            env->GetStaticMethodID(class_analyzer, "hasMemberMethod", "(Ljava/lang/String;Ljava/lang/Class;)Z");
    if (hasMethod == nullptr) {
        return Status::InternalError("couldn't found hasMethod method");
    }

    jstring method_name = helper.to_jstring(method.c_str());
    LOCAL_REF_GUARD(method_name);

    *has = env->CallStaticBooleanMethod(class_analyzer, hasMethod, method_name, (jobject)clazz);

    if (jthrowable jthr = env->ExceptionOccurred(); jthr) {
        LOCAL_REF_GUARD(jthr);

        std::string err = helper.dumpExceptionString(jthr);
        env->ExceptionClear();
        return Status::InternalError(fmt::format("call hasMemberMethod failed: {} err:{}", method, err));
    }

    return Status::OK();
}

Status ClassAnalyzer::get_signature(jclass clazz, const std::string& method, std::string* sign) {
    DCHECK(clazz != nullptr);
    DCHECK(sign != nullptr);
    auto& helper = JVMFunctionHelper::getInstance();
    JNIEnv* env = helper.getEnv();
    std::string anlyzer_clazz_name = JVMFunctionHelper::to_jni_class_name(CLASS_ANALYZER_NAME);
    jclass class_analyzer = env->FindClass(anlyzer_clazz_name.c_str());
    LOCAL_REF_GUARD(class_analyzer);

    if (class_analyzer == nullptr) {
        return Status::InternalError(fmt::format("ClassAnalyzer Not Found: {}", CLASS_ANALYZER_NAME));
    }
    jmethodID getSign = env->GetStaticMethodID(class_analyzer, "getSignature",
                                               "(Ljava/lang/String;Ljava/lang/Class;)Ljava/lang/String;");
    if (getSign == nullptr) {
        return Status::InternalError("couldn't found getSignature method");
    }

    jstring method_name = helper.to_jstring(method.c_str());
    LOCAL_REF_GUARD(method_name);

    jobject result_sign = env->CallStaticObjectMethod(class_analyzer, getSign, method_name, (jobject)clazz);
    LOCAL_REF_GUARD(result_sign);

    if (jthrowable thr = env->ExceptionOccurred(); thr != nullptr) {
        LOCAL_REF_GUARD(thr);
        std::string err = helper.dumpExceptionString(thr);
        env->ExceptionClear();
        return Status::InternalError(fmt::format("could't found method {} err:{}", method, err));
    }
    if (result_sign == nullptr) {
        return Status::InternalError(fmt::format("couldn't found method:{}", method));
    }
    *sign = helper.to_string(result_sign);
    return Status::OK();
}

StatusOr<jobject> ClassAnalyzer::get_method_object(jclass clazz, const std::string& method) {
    auto& helper = JVMFunctionHelper::getInstance();
    JNIEnv* env = helper.getEnv();

    std::string anlyzer_clazz_name = JVMFunctionHelper::to_jni_class_name(CLASS_ANALYZER_NAME);
    jclass class_analyzer = env->FindClass(anlyzer_clazz_name.c_str());
    LOCAL_REF_GUARD(class_analyzer);
    DCHECK(class_analyzer);

    jmethodID getMethodObject = env->GetStaticMethodID(
            class_analyzer, "getMethodObject", "(Ljava/lang/String;Ljava/lang/Class;)Ljava/lang/reflect/Method;");
    DCHECK(getMethodObject);
    jstring method_name = helper.to_jstring(method.c_str());
    LOCAL_REF_GUARD(method_name);

    jobject method_object = env->CallStaticObjectMethod(class_analyzer, getMethodObject, method_name, (jobject)clazz);
    LOCAL_REF_GUARD(method_object);
    env->ExceptionClear();
    if (method_object == nullptr) {
        return Status::InternalError(fmt::format("couldn't found method:{}", method));
    }
    auto res = env->NewGlobalRef(method_object);
    return res;
}

Status ClassAnalyzer::get_method_desc(const std::string& sign, std::vector<MethodTypeDescriptor>* desc) {
    RETURN_IF_ERROR(get_udaf_method_desc(sign, desc));
    // return type may be a void type
    for (int i = 1; i < desc->size(); ++i) {
        if (desc->at(i).type == TYPE_UNKNOWN) {
            return Status::InternalError(fmt::format("unknown type sign:{}", sign));
        }
    }
    return Status::OK();
}

// clang-format off
#define ADD_BOXED_METHOD_TYPE_DESC(STR, TYPE) \
    } else if (type == STR) {                 \
      desc->emplace_back(MethodTypeDescriptor{TYPE, true});

#define ADD_PRIM_METHOD_TYPE_DESC(STR, TYPE) \
    } else if (sign[i] == STR) {             \
      desc->emplace_back(MethodTypeDescriptor{TYPE, false});
// clang-format on

Status ClassAnalyzer::get_udaf_method_desc(const std::string& sign, std::vector<MethodTypeDescriptor>* desc) {
    for (int i = 0; i < sign.size(); ++i) {
        if (sign[i] == '(' || sign[i] == ')') {
            continue;
        }
        if (sign[i] == '[') {
            while (sign[i] != ';') {
                i++;
            }
            desc->emplace_back(MethodTypeDescriptor{TYPE_UNKNOWN, true});
        }
        if (sign[i] == 'L') {
            int st = i + 1;
            while (sign[i] != ';') {
                i++;
            }
            std::string type = sign.substr(st, i - st);
            if (false) {
                // clang-format off
            ADD_BOXED_METHOD_TYPE_DESC("java/lang/Boolean", TYPE_BOOLEAN)
            ADD_BOXED_METHOD_TYPE_DESC("java/lang/Byte", TYPE_TINYINT)
            ADD_BOXED_METHOD_TYPE_DESC("java/lang/Short", TYPE_SMALLINT)
            ADD_BOXED_METHOD_TYPE_DESC("java/lang/Integer", TYPE_INT)
            ADD_BOXED_METHOD_TYPE_DESC("java/lang/Long", TYPE_BIGINT)
            ADD_BOXED_METHOD_TYPE_DESC("java/lang/Float", TYPE_FLOAT)
            ADD_BOXED_METHOD_TYPE_DESC("java/lang/Double", TYPE_DOUBLE)
                // clang-format on
            } else if (type == "java/lang/String") {
                desc->emplace_back(MethodTypeDescriptor{TYPE_VARCHAR, true});
            } else if (type == "java/util/List") {
                desc->emplace_back(MethodTypeDescriptor{TYPE_ARRAY, true});
            } else if (type == "java/util/Map") {
                desc->emplace_back(MethodTypeDescriptor{TYPE_MAP, true});
            }
            continue;
        }
        if (false) {
            // clang-format off
        ADD_PRIM_METHOD_TYPE_DESC('Z', TYPE_BOOLEAN)
        ADD_PRIM_METHOD_TYPE_DESC('B', TYPE_TINYINT)
        ADD_PRIM_METHOD_TYPE_DESC('S', TYPE_SMALLINT)
        ADD_PRIM_METHOD_TYPE_DESC('I', TYPE_INT)
        ADD_PRIM_METHOD_TYPE_DESC('J', TYPE_BIGINT)
        ADD_PRIM_METHOD_TYPE_DESC('F', TYPE_FLOAT)
        ADD_PRIM_METHOD_TYPE_DESC('D', TYPE_DOUBLE)
            // clang-format on
        } else if (sign[i] == 'V') {
            desc->emplace_back(MethodTypeDescriptor{TYPE_UNKNOWN, false});
        } else {
            desc->emplace_back(MethodTypeDescriptor{TYPE_UNKNOWN, false});
        }
    }

    if (desc->size() > 1) {
        desc->insert(desc->begin(), (*desc)[desc->size() - 1]);
        desc->erase(desc->begin() + desc->size() - 1);
    }
    return Status::OK();
}

JavaUDFContext::~JavaUDFContext() = default;

int UDAFFunction::create() {
    auto [env, helper] = JVMFunctionHelper::getInstanceWithEnv();
    jmethodID create = _ctx->create->get_method_id();
    auto obj = env->CallObjectMethod(_udaf_handle, create);
    LOCAL_REF_GUARD(obj);
    CHECK_UDF_CALL_EXCEPTION(env, _function_context);
    return _ctx->states->add_state(_function_context, env, obj);
}

void UDAFFunction::destroy(int state) {
    auto [env, helper] = JVMFunctionHelper::getInstanceWithEnv();
    auto obj = helper.convert_handle_to_jobject(_function_context, state);
    LOCAL_REF_GUARD(obj);
    jmethodID destory = _ctx->destory->get_method_id();
    // call destroy
    env->CallVoidMethod(_udaf_handle, destory, obj);
    CHECK_UDF_CALL_EXCEPTION(env, _function_context);
    _ctx->states->remove(_function_context, env, state);
}

jvalue UDAFFunction::finalize(int state) {
    auto& helper = JVMFunctionHelper::getInstance();
    JNIEnv* env = helper.getEnv();
    auto obj = helper.convert_handle_to_jobject(_function_context, state);
    LOCAL_REF_GUARD(obj);
    jmethodID finalize = _ctx->finalize->get_method_id();
    jvalue res;
    res.l = env->CallObjectMethod(_udaf_handle, finalize, obj);
    CHECK_UDF_CALL_EXCEPTION(env, _function_context);
    return res;
}

void AggBatchCallStub::batch_update_single(int num_rows, jobject state, jobject* input, int cols) {
    jvalue jni_inputs[3 + cols];
    jni_inputs[0].i = num_rows;
    jni_inputs[1].l = _caller;
    jni_inputs[2].l = state;
    for (int i = 0; i < cols; ++i) {
        jni_inputs[3 + i].l = input[i];
    }
    auto* env = JVMFunctionHelper::getInstance().getEnv();
    env->CallStaticVoidMethodA(_stub_clazz.clazz(), env->FromReflectedMethod(_stub_method.handle()), jni_inputs);
    CHECK_UDF_CALL_EXCEPTION(env, this->_ctx);
}

StatusOr<jobject> BatchEvaluateStub::batch_evaluate(int num_rows, jobject* input, int cols) {
    jvalue jni_inputs[2 + cols];
    jni_inputs[0].i = num_rows;
    jni_inputs[1].l = _caller;
    for (int i = 0; i < cols; ++i) {
        jni_inputs[2 + i].l = input[i];
    }
    auto* env = JVMFunctionHelper::getInstance().getEnv();
    auto res = env->CallStaticObjectMethodA(_stub_clazz.clazz(), env->FromReflectedMethod(_stub_method.handle()),
                                            jni_inputs);
    RETURN_ERROR_IF_JNI_EXCEPTION(env);
    return res;
}

Status JavaListStub::add(jobject element) {
    auto [env, helper] = JVMFunctionHelper::getInstanceWithEnv();
    const auto& meta = helper.list_meta();
    env->CallVoidMethod(_list, meta.list_add, element);
    RETURN_ERROR_IF_JNI_EXCEPTION(env);
    return Status::OK();
}

StatusOr<jobject> JavaListStub::get(int index) {
    auto [env, helper] = JVMFunctionHelper::getInstanceWithEnv();
    const auto& meta = helper.list_meta();
    auto res = env->CallObjectMethod(_list, meta.list_get, index);
    RETURN_ERROR_IF_JNI_EXCEPTION(env);
    return res;
}

StatusOr<size_t> JavaListStub::size() {
    auto [env, helper] = JVMFunctionHelper::getInstanceWithEnv();
    const auto& meta = helper.list_meta();
    auto res = env->CallIntMethod(_list, meta.list_size);
    RETURN_ERROR_IF_JNI_EXCEPTION(env);
    return res;
}

void UDAFFunction::update(jvalue* val) {
    auto [env, helper] = JVMFunctionHelper::getInstanceWithEnv();
    jmethodID update = _ctx->update->get_method_id();
    env->CallVoidMethodA(_udaf_handle, update, val);
    CHECK_UDF_CALL_EXCEPTION(env, _function_context);
}

void UDAFFunction::merge(int state, jobject buffer) {
    auto [env, helper] = JVMFunctionHelper::getInstanceWithEnv();
    auto obj = helper.convert_handle_to_jobject(_function_context, state);
    LOCAL_REF_GUARD(obj);
    jmethodID merge = _ctx->merge->get_method_id();
    env->CallVoidMethod(_udaf_handle, merge, obj, buffer);
    CHECK_UDF_CALL_EXCEPTION(env, _function_context);
}

void UDAFFunction::serialize(int state, jobject buffer) {
    auto [env, helper] = JVMFunctionHelper::getInstanceWithEnv();
    auto obj = helper.convert_handle_to_jobject(_function_context, state);
    LOCAL_REF_GUARD(obj);
    jmethodID serialize = _ctx->serialize->get_method_id();
    env->CallVoidMethod(_udaf_handle, serialize, obj, buffer);
    CHECK_UDF_CALL_EXCEPTION(env, _function_context);
}

int UDAFFunction::serialize_size(int state) {
    auto [env, helper] = JVMFunctionHelper::getInstanceWithEnv();
    auto obj = helper.convert_handle_to_jobject(_function_context, state);
    LOCAL_REF_GUARD(obj);
    jmethodID serialize_size = _ctx->serialize_size->get_method_id();
    int sz = env->CallIntMethod(obj, serialize_size);
    CHECK_UDF_CALL_EXCEPTION(env, _function_context);
    return sz;
}

void UDAFFunction::reset(int state) {
    auto [env, helper] = JVMFunctionHelper::getInstanceWithEnv();
    auto obj = helper.convert_handle_to_jobject(_function_context, state);
    LOCAL_REF_GUARD(obj);
    jmethodID reset = _ctx->reset->get_method_id();
    env->CallVoidMethod(_udaf_handle, reset, obj);
    CHECK_UDF_CALL_EXCEPTION(env, _function_context);
}

jobject UDAFFunction::window_update_batch(int state, int peer_group_start, int peer_group_end, int frame_start,
                                          int frame_end, int col_sz, jobject* cols) {
    auto [env, helper] = JVMFunctionHelper::getInstanceWithEnv();
    auto obj = helper.convert_handle_to_jobject(_function_context, state);
    LOCAL_REF_GUARD(obj);

    jmethodID window_update = _ctx->window_update->get_method_id();
    jvalue jvalues[5 + col_sz];
    jvalues[0].l = obj;
    jvalues[1].j = peer_group_start;
    jvalues[2].j = peer_group_end;
    jvalues[3].j = frame_start;
    jvalues[4].j = frame_end;

    for (int i = 0; i < col_sz; ++i) {
        jvalues[i + 5].l = cols[i];
    }

    jobject res = env->CallObjectMethodA(_udaf_handle, window_update, jvalues);
    CHECK_UDF_CALL_EXCEPTION(env, _function_context);
    return res;
}

Status detect_java_runtime() {
    const char* p = std::getenv("JAVA_HOME");
    if (p == nullptr) {
        return Status::RuntimeError("env 'JAVA_HOME' is not set");
    }
    return Status::OK();
}

} // namespace starrocks
