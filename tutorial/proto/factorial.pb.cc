// Generated by the protocol buffer compiler.  DO NOT EDIT!
// source: factorial.proto

#define INTERNAL_SUPPRESS_PROTOBUF_FIELD_DEPRECATION
#include "factorial.pb.h"

#include <algorithm>

#include <google/protobuf/stubs/common.h>
#include <google/protobuf/stubs/once.h>
#include <google/protobuf/io/coded_stream.h>
#include <google/protobuf/wire_format_lite_inl.h>
#include <google/protobuf/descriptor.h>
#include <google/protobuf/generated_message_reflection.h>
#include <google/protobuf/reflection_ops.h>
#include <google/protobuf/wire_format.h>
// @@protoc_insertion_point(includes)

namespace {

const ::google::protobuf::Descriptor* FactorialRequest_descriptor_ = NULL;
const ::google::protobuf::internal::GeneratedMessageReflection*
  FactorialRequest_reflection_ = NULL;
const ::google::protobuf::Descriptor* FactorialResponse_descriptor_ = NULL;
const ::google::protobuf::internal::GeneratedMessageReflection*
  FactorialResponse_reflection_ = NULL;
const ::google::protobuf::ServiceDescriptor* FactorialService_descriptor_ = NULL;

}  // namespace


void protobuf_AssignDesc_factorial_2eproto() {
  protobuf_AddDesc_factorial_2eproto();
  const ::google::protobuf::FileDescriptor* file =
    ::google::protobuf::DescriptorPool::generated_pool()->FindFileByName(
      "factorial.proto");
  GOOGLE_CHECK(file != NULL);
  FactorialRequest_descriptor_ = file->message_type(0);
  static const int FactorialRequest_offsets_[1] = {
    GOOGLE_PROTOBUF_GENERATED_MESSAGE_FIELD_OFFSET(FactorialRequest, n_),
  };
  FactorialRequest_reflection_ =
    new ::google::protobuf::internal::GeneratedMessageReflection(
      FactorialRequest_descriptor_,
      FactorialRequest::default_instance_,
      FactorialRequest_offsets_,
      GOOGLE_PROTOBUF_GENERATED_MESSAGE_FIELD_OFFSET(FactorialRequest, _has_bits_[0]),
      GOOGLE_PROTOBUF_GENERATED_MESSAGE_FIELD_OFFSET(FactorialRequest, _unknown_fields_),
      -1,
      ::google::protobuf::DescriptorPool::generated_pool(),
      ::google::protobuf::MessageFactory::generated_factory(),
      sizeof(FactorialRequest));
  FactorialResponse_descriptor_ = file->message_type(1);
  static const int FactorialResponse_offsets_[1] = {
    GOOGLE_PROTOBUF_GENERATED_MESSAGE_FIELD_OFFSET(FactorialResponse, result_),
  };
  FactorialResponse_reflection_ =
    new ::google::protobuf::internal::GeneratedMessageReflection(
      FactorialResponse_descriptor_,
      FactorialResponse::default_instance_,
      FactorialResponse_offsets_,
      GOOGLE_PROTOBUF_GENERATED_MESSAGE_FIELD_OFFSET(FactorialResponse, _has_bits_[0]),
      GOOGLE_PROTOBUF_GENERATED_MESSAGE_FIELD_OFFSET(FactorialResponse, _unknown_fields_),
      -1,
      ::google::protobuf::DescriptorPool::generated_pool(),
      ::google::protobuf::MessageFactory::generated_factory(),
      sizeof(FactorialResponse));
  FactorialService_descriptor_ = file->service(0);
}

namespace {

GOOGLE_PROTOBUF_DECLARE_ONCE(protobuf_AssignDescriptors_once_);
inline void protobuf_AssignDescriptorsOnce() {
  ::google::protobuf::GoogleOnceInit(&protobuf_AssignDescriptors_once_,
                 &protobuf_AssignDesc_factorial_2eproto);
}

void protobuf_RegisterTypes(const ::std::string&) {
  protobuf_AssignDescriptorsOnce();
  ::google::protobuf::MessageFactory::InternalRegisterGeneratedMessage(
    FactorialRequest_descriptor_, &FactorialRequest::default_instance());
  ::google::protobuf::MessageFactory::InternalRegisterGeneratedMessage(
    FactorialResponse_descriptor_, &FactorialResponse::default_instance());
}

}  // namespace

void protobuf_ShutdownFile_factorial_2eproto() {
  delete FactorialRequest::default_instance_;
  delete FactorialRequest_reflection_;
  delete FactorialResponse::default_instance_;
  delete FactorialResponse_reflection_;
}

void protobuf_AddDesc_factorial_2eproto() {
  static bool already_here = false;
  if (already_here) return;
  already_here = true;
  GOOGLE_PROTOBUF_VERIFY_VERSION;

  ::corpc::protobuf_AddDesc_corpc_5foption_2eproto();
  ::google::protobuf::DescriptorPool::InternalAddGeneratedFile(
    "\n\017factorial.proto\032\022corpc_option.proto\"\035\n"
    "\020FactorialRequest\022\t\n\001n\030\001 \002(\r\"#\n\021Factoria"
    "lResponse\022\016\n\006result\030\001 \002(\0042R\n\020FactorialSe"
    "rvice\0228\n\tfactorial\022\021.FactorialRequest\032\022."
    "FactorialResponse\"\004\220\361\004\001\032\004\200\361\004\003B\003\200\001\001", 194);
  ::google::protobuf::MessageFactory::InternalRegisterGeneratedFile(
    "factorial.proto", &protobuf_RegisterTypes);
  FactorialRequest::default_instance_ = new FactorialRequest();
  FactorialResponse::default_instance_ = new FactorialResponse();
  FactorialRequest::default_instance_->InitAsDefaultInstance();
  FactorialResponse::default_instance_->InitAsDefaultInstance();
  ::google::protobuf::internal::OnShutdown(&protobuf_ShutdownFile_factorial_2eproto);
}

// Force AddDescriptors() to be called at static initialization time.
struct StaticDescriptorInitializer_factorial_2eproto {
  StaticDescriptorInitializer_factorial_2eproto() {
    protobuf_AddDesc_factorial_2eproto();
  }
} static_descriptor_initializer_factorial_2eproto_;

// ===================================================================

#ifndef _MSC_VER
const int FactorialRequest::kNFieldNumber;
#endif  // !_MSC_VER

FactorialRequest::FactorialRequest()
  : ::google::protobuf::Message() {
  SharedCtor();
  // @@protoc_insertion_point(constructor:FactorialRequest)
}

void FactorialRequest::InitAsDefaultInstance() {
}

FactorialRequest::FactorialRequest(const FactorialRequest& from)
  : ::google::protobuf::Message() {
  SharedCtor();
  MergeFrom(from);
  // @@protoc_insertion_point(copy_constructor:FactorialRequest)
}

void FactorialRequest::SharedCtor() {
  _cached_size_ = 0;
  n_ = 0u;
  ::memset(_has_bits_, 0, sizeof(_has_bits_));
}

FactorialRequest::~FactorialRequest() {
  // @@protoc_insertion_point(destructor:FactorialRequest)
  SharedDtor();
}

void FactorialRequest::SharedDtor() {
  if (this != default_instance_) {
  }
}

void FactorialRequest::SetCachedSize(int size) const {
  GOOGLE_SAFE_CONCURRENT_WRITES_BEGIN();
  _cached_size_ = size;
  GOOGLE_SAFE_CONCURRENT_WRITES_END();
}
const ::google::protobuf::Descriptor* FactorialRequest::descriptor() {
  protobuf_AssignDescriptorsOnce();
  return FactorialRequest_descriptor_;
}

const FactorialRequest& FactorialRequest::default_instance() {
  if (default_instance_ == NULL) protobuf_AddDesc_factorial_2eproto();
  return *default_instance_;
}

FactorialRequest* FactorialRequest::default_instance_ = NULL;

FactorialRequest* FactorialRequest::New() const {
  return new FactorialRequest;
}

void FactorialRequest::Clear() {
  n_ = 0u;
  ::memset(_has_bits_, 0, sizeof(_has_bits_));
  mutable_unknown_fields()->Clear();
}

bool FactorialRequest::MergePartialFromCodedStream(
    ::google::protobuf::io::CodedInputStream* input) {
#define DO_(EXPRESSION) if (!(EXPRESSION)) goto failure
  ::google::protobuf::uint32 tag;
  // @@protoc_insertion_point(parse_start:FactorialRequest)
  for (;;) {
    ::std::pair< ::google::protobuf::uint32, bool> p = input->ReadTagWithCutoff(127);
    tag = p.first;
    if (!p.second) goto handle_unusual;
    switch (::google::protobuf::internal::WireFormatLite::GetTagFieldNumber(tag)) {
      // required uint32 n = 1;
      case 1: {
        if (tag == 8) {
          DO_((::google::protobuf::internal::WireFormatLite::ReadPrimitive<
                   ::google::protobuf::uint32, ::google::protobuf::internal::WireFormatLite::TYPE_UINT32>(
                 input, &n_)));
          set_has_n();
        } else {
          goto handle_unusual;
        }
        if (input->ExpectAtEnd()) goto success;
        break;
      }

      default: {
      handle_unusual:
        if (tag == 0 ||
            ::google::protobuf::internal::WireFormatLite::GetTagWireType(tag) ==
            ::google::protobuf::internal::WireFormatLite::WIRETYPE_END_GROUP) {
          goto success;
        }
        DO_(::google::protobuf::internal::WireFormat::SkipField(
              input, tag, mutable_unknown_fields()));
        break;
      }
    }
  }
success:
  // @@protoc_insertion_point(parse_success:FactorialRequest)
  return true;
failure:
  // @@protoc_insertion_point(parse_failure:FactorialRequest)
  return false;
#undef DO_
}

void FactorialRequest::SerializeWithCachedSizes(
    ::google::protobuf::io::CodedOutputStream* output) const {
  // @@protoc_insertion_point(serialize_start:FactorialRequest)
  // required uint32 n = 1;
  if (has_n()) {
    ::google::protobuf::internal::WireFormatLite::WriteUInt32(1, this->n(), output);
  }

  if (!unknown_fields().empty()) {
    ::google::protobuf::internal::WireFormat::SerializeUnknownFields(
        unknown_fields(), output);
  }
  // @@protoc_insertion_point(serialize_end:FactorialRequest)
}

::google::protobuf::uint8* FactorialRequest::SerializeWithCachedSizesToArray(
    ::google::protobuf::uint8* target) const {
  // @@protoc_insertion_point(serialize_to_array_start:FactorialRequest)
  // required uint32 n = 1;
  if (has_n()) {
    target = ::google::protobuf::internal::WireFormatLite::WriteUInt32ToArray(1, this->n(), target);
  }

  if (!unknown_fields().empty()) {
    target = ::google::protobuf::internal::WireFormat::SerializeUnknownFieldsToArray(
        unknown_fields(), target);
  }
  // @@protoc_insertion_point(serialize_to_array_end:FactorialRequest)
  return target;
}

int FactorialRequest::ByteSize() const {
  int total_size = 0;

  if (_has_bits_[0 / 32] & (0xffu << (0 % 32))) {
    // required uint32 n = 1;
    if (has_n()) {
      total_size += 1 +
        ::google::protobuf::internal::WireFormatLite::UInt32Size(
          this->n());
    }

  }
  if (!unknown_fields().empty()) {
    total_size +=
      ::google::protobuf::internal::WireFormat::ComputeUnknownFieldsSize(
        unknown_fields());
  }
  GOOGLE_SAFE_CONCURRENT_WRITES_BEGIN();
  _cached_size_ = total_size;
  GOOGLE_SAFE_CONCURRENT_WRITES_END();
  return total_size;
}

void FactorialRequest::MergeFrom(const ::google::protobuf::Message& from) {
  GOOGLE_CHECK_NE(&from, this);
  const FactorialRequest* source =
    ::google::protobuf::internal::dynamic_cast_if_available<const FactorialRequest*>(
      &from);
  if (source == NULL) {
    ::google::protobuf::internal::ReflectionOps::Merge(from, this);
  } else {
    MergeFrom(*source);
  }
}

void FactorialRequest::MergeFrom(const FactorialRequest& from) {
  GOOGLE_CHECK_NE(&from, this);
  if (from._has_bits_[0 / 32] & (0xffu << (0 % 32))) {
    if (from.has_n()) {
      set_n(from.n());
    }
  }
  mutable_unknown_fields()->MergeFrom(from.unknown_fields());
}

void FactorialRequest::CopyFrom(const ::google::protobuf::Message& from) {
  if (&from == this) return;
  Clear();
  MergeFrom(from);
}

void FactorialRequest::CopyFrom(const FactorialRequest& from) {
  if (&from == this) return;
  Clear();
  MergeFrom(from);
}

bool FactorialRequest::IsInitialized() const {
  if ((_has_bits_[0] & 0x00000001) != 0x00000001) return false;

  return true;
}

void FactorialRequest::Swap(FactorialRequest* other) {
  if (other != this) {
    std::swap(n_, other->n_);
    std::swap(_has_bits_[0], other->_has_bits_[0]);
    _unknown_fields_.Swap(&other->_unknown_fields_);
    std::swap(_cached_size_, other->_cached_size_);
  }
}

::google::protobuf::Metadata FactorialRequest::GetMetadata() const {
  protobuf_AssignDescriptorsOnce();
  ::google::protobuf::Metadata metadata;
  metadata.descriptor = FactorialRequest_descriptor_;
  metadata.reflection = FactorialRequest_reflection_;
  return metadata;
}


// ===================================================================

#ifndef _MSC_VER
const int FactorialResponse::kResultFieldNumber;
#endif  // !_MSC_VER

FactorialResponse::FactorialResponse()
  : ::google::protobuf::Message() {
  SharedCtor();
  // @@protoc_insertion_point(constructor:FactorialResponse)
}

void FactorialResponse::InitAsDefaultInstance() {
}

FactorialResponse::FactorialResponse(const FactorialResponse& from)
  : ::google::protobuf::Message() {
  SharedCtor();
  MergeFrom(from);
  // @@protoc_insertion_point(copy_constructor:FactorialResponse)
}

void FactorialResponse::SharedCtor() {
  _cached_size_ = 0;
  result_ = GOOGLE_ULONGLONG(0);
  ::memset(_has_bits_, 0, sizeof(_has_bits_));
}

FactorialResponse::~FactorialResponse() {
  // @@protoc_insertion_point(destructor:FactorialResponse)
  SharedDtor();
}

void FactorialResponse::SharedDtor() {
  if (this != default_instance_) {
  }
}

void FactorialResponse::SetCachedSize(int size) const {
  GOOGLE_SAFE_CONCURRENT_WRITES_BEGIN();
  _cached_size_ = size;
  GOOGLE_SAFE_CONCURRENT_WRITES_END();
}
const ::google::protobuf::Descriptor* FactorialResponse::descriptor() {
  protobuf_AssignDescriptorsOnce();
  return FactorialResponse_descriptor_;
}

const FactorialResponse& FactorialResponse::default_instance() {
  if (default_instance_ == NULL) protobuf_AddDesc_factorial_2eproto();
  return *default_instance_;
}

FactorialResponse* FactorialResponse::default_instance_ = NULL;

FactorialResponse* FactorialResponse::New() const {
  return new FactorialResponse;
}

void FactorialResponse::Clear() {
  result_ = GOOGLE_ULONGLONG(0);
  ::memset(_has_bits_, 0, sizeof(_has_bits_));
  mutable_unknown_fields()->Clear();
}

bool FactorialResponse::MergePartialFromCodedStream(
    ::google::protobuf::io::CodedInputStream* input) {
#define DO_(EXPRESSION) if (!(EXPRESSION)) goto failure
  ::google::protobuf::uint32 tag;
  // @@protoc_insertion_point(parse_start:FactorialResponse)
  for (;;) {
    ::std::pair< ::google::protobuf::uint32, bool> p = input->ReadTagWithCutoff(127);
    tag = p.first;
    if (!p.second) goto handle_unusual;
    switch (::google::protobuf::internal::WireFormatLite::GetTagFieldNumber(tag)) {
      // required uint64 result = 1;
      case 1: {
        if (tag == 8) {
          DO_((::google::protobuf::internal::WireFormatLite::ReadPrimitive<
                   ::google::protobuf::uint64, ::google::protobuf::internal::WireFormatLite::TYPE_UINT64>(
                 input, &result_)));
          set_has_result();
        } else {
          goto handle_unusual;
        }
        if (input->ExpectAtEnd()) goto success;
        break;
      }

      default: {
      handle_unusual:
        if (tag == 0 ||
            ::google::protobuf::internal::WireFormatLite::GetTagWireType(tag) ==
            ::google::protobuf::internal::WireFormatLite::WIRETYPE_END_GROUP) {
          goto success;
        }
        DO_(::google::protobuf::internal::WireFormat::SkipField(
              input, tag, mutable_unknown_fields()));
        break;
      }
    }
  }
success:
  // @@protoc_insertion_point(parse_success:FactorialResponse)
  return true;
failure:
  // @@protoc_insertion_point(parse_failure:FactorialResponse)
  return false;
#undef DO_
}

void FactorialResponse::SerializeWithCachedSizes(
    ::google::protobuf::io::CodedOutputStream* output) const {
  // @@protoc_insertion_point(serialize_start:FactorialResponse)
  // required uint64 result = 1;
  if (has_result()) {
    ::google::protobuf::internal::WireFormatLite::WriteUInt64(1, this->result(), output);
  }

  if (!unknown_fields().empty()) {
    ::google::protobuf::internal::WireFormat::SerializeUnknownFields(
        unknown_fields(), output);
  }
  // @@protoc_insertion_point(serialize_end:FactorialResponse)
}

::google::protobuf::uint8* FactorialResponse::SerializeWithCachedSizesToArray(
    ::google::protobuf::uint8* target) const {
  // @@protoc_insertion_point(serialize_to_array_start:FactorialResponse)
  // required uint64 result = 1;
  if (has_result()) {
    target = ::google::protobuf::internal::WireFormatLite::WriteUInt64ToArray(1, this->result(), target);
  }

  if (!unknown_fields().empty()) {
    target = ::google::protobuf::internal::WireFormat::SerializeUnknownFieldsToArray(
        unknown_fields(), target);
  }
  // @@protoc_insertion_point(serialize_to_array_end:FactorialResponse)
  return target;
}

int FactorialResponse::ByteSize() const {
  int total_size = 0;

  if (_has_bits_[0 / 32] & (0xffu << (0 % 32))) {
    // required uint64 result = 1;
    if (has_result()) {
      total_size += 1 +
        ::google::protobuf::internal::WireFormatLite::UInt64Size(
          this->result());
    }

  }
  if (!unknown_fields().empty()) {
    total_size +=
      ::google::protobuf::internal::WireFormat::ComputeUnknownFieldsSize(
        unknown_fields());
  }
  GOOGLE_SAFE_CONCURRENT_WRITES_BEGIN();
  _cached_size_ = total_size;
  GOOGLE_SAFE_CONCURRENT_WRITES_END();
  return total_size;
}

void FactorialResponse::MergeFrom(const ::google::protobuf::Message& from) {
  GOOGLE_CHECK_NE(&from, this);
  const FactorialResponse* source =
    ::google::protobuf::internal::dynamic_cast_if_available<const FactorialResponse*>(
      &from);
  if (source == NULL) {
    ::google::protobuf::internal::ReflectionOps::Merge(from, this);
  } else {
    MergeFrom(*source);
  }
}

void FactorialResponse::MergeFrom(const FactorialResponse& from) {
  GOOGLE_CHECK_NE(&from, this);
  if (from._has_bits_[0 / 32] & (0xffu << (0 % 32))) {
    if (from.has_result()) {
      set_result(from.result());
    }
  }
  mutable_unknown_fields()->MergeFrom(from.unknown_fields());
}

void FactorialResponse::CopyFrom(const ::google::protobuf::Message& from) {
  if (&from == this) return;
  Clear();
  MergeFrom(from);
}

void FactorialResponse::CopyFrom(const FactorialResponse& from) {
  if (&from == this) return;
  Clear();
  MergeFrom(from);
}

bool FactorialResponse::IsInitialized() const {
  if ((_has_bits_[0] & 0x00000001) != 0x00000001) return false;

  return true;
}

void FactorialResponse::Swap(FactorialResponse* other) {
  if (other != this) {
    std::swap(result_, other->result_);
    std::swap(_has_bits_[0], other->_has_bits_[0]);
    _unknown_fields_.Swap(&other->_unknown_fields_);
    std::swap(_cached_size_, other->_cached_size_);
  }
}

::google::protobuf::Metadata FactorialResponse::GetMetadata() const {
  protobuf_AssignDescriptorsOnce();
  ::google::protobuf::Metadata metadata;
  metadata.descriptor = FactorialResponse_descriptor_;
  metadata.reflection = FactorialResponse_reflection_;
  return metadata;
}


// ===================================================================

FactorialService::~FactorialService() {}

const ::google::protobuf::ServiceDescriptor* FactorialService::descriptor() {
  protobuf_AssignDescriptorsOnce();
  return FactorialService_descriptor_;
}

const ::google::protobuf::ServiceDescriptor* FactorialService::GetDescriptor() {
  protobuf_AssignDescriptorsOnce();
  return FactorialService_descriptor_;
}

void FactorialService::factorial(::google::protobuf::RpcController* controller,
                         const ::FactorialRequest*,
                         ::FactorialResponse*,
                         ::google::protobuf::Closure* done) {
  controller->SetFailed("Method factorial() not implemented.");
  done->Run();
}

void FactorialService::CallMethod(const ::google::protobuf::MethodDescriptor* method,
                             ::google::protobuf::RpcController* controller,
                             const ::google::protobuf::Message* request,
                             ::google::protobuf::Message* response,
                             ::google::protobuf::Closure* done) {
  GOOGLE_DCHECK_EQ(method->service(), FactorialService_descriptor_);
  switch(method->index()) {
    case 0:
      factorial(controller,
             ::google::protobuf::down_cast<const ::FactorialRequest*>(request),
             ::google::protobuf::down_cast< ::FactorialResponse*>(response),
             done);
      break;
    default:
      GOOGLE_LOG(FATAL) << "Bad method index; this should never happen.";
      break;
  }
}

const ::google::protobuf::Message& FactorialService::GetRequestPrototype(
    const ::google::protobuf::MethodDescriptor* method) const {
  GOOGLE_DCHECK_EQ(method->service(), descriptor());
  switch(method->index()) {
    case 0:
      return ::FactorialRequest::default_instance();
    default:
      GOOGLE_LOG(FATAL) << "Bad method index; this should never happen.";
      return *reinterpret_cast< ::google::protobuf::Message*>(NULL);
  }
}

const ::google::protobuf::Message& FactorialService::GetResponsePrototype(
    const ::google::protobuf::MethodDescriptor* method) const {
  GOOGLE_DCHECK_EQ(method->service(), descriptor());
  switch(method->index()) {
    case 0:
      return ::FactorialResponse::default_instance();
    default:
      GOOGLE_LOG(FATAL) << "Bad method index; this should never happen.";
      return *reinterpret_cast< ::google::protobuf::Message*>(NULL);
  }
}

FactorialService_Stub::FactorialService_Stub(::google::protobuf::RpcChannel* channel)
  : channel_(channel), owns_channel_(false) {}
FactorialService_Stub::FactorialService_Stub(
    ::google::protobuf::RpcChannel* channel,
    ::google::protobuf::Service::ChannelOwnership ownership)
  : channel_(channel),
    owns_channel_(ownership == ::google::protobuf::Service::STUB_OWNS_CHANNEL) {}
FactorialService_Stub::~FactorialService_Stub() {
  if (owns_channel_) delete channel_;
}

void FactorialService_Stub::factorial(::google::protobuf::RpcController* controller,
                              const ::FactorialRequest* request,
                              ::FactorialResponse* response,
                              ::google::protobuf::Closure* done) {
  channel_->CallMethod(descriptor()->method(0),
                       controller, request, response, done);
}

// @@protoc_insertion_point(namespace_scope)

// @@protoc_insertion_point(global_scope)
