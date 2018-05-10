// Generated by the protocol buffer compiler.  DO NOT EDIT!
// source: corpc_thirdparty.proto

#define INTERNAL_SUPPRESS_PROTOBUF_FIELD_DEPRECATION
#include "corpc_thirdparty.pb.h"

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

namespace corpc {
namespace thirdparty {

namespace {

const ::google::protobuf::Descriptor* TakeResponse_descriptor_ = NULL;
const ::google::protobuf::internal::GeneratedMessageReflection*
  TakeResponse_reflection_ = NULL;
const ::google::protobuf::Descriptor* PutRequest_descriptor_ = NULL;
const ::google::protobuf::internal::GeneratedMessageReflection*
  PutRequest_reflection_ = NULL;
const ::google::protobuf::ServiceDescriptor* ThirdPartyService_descriptor_ = NULL;

}  // namespace


void protobuf_AssignDesc_corpc_5fthirdparty_2eproto() {
  protobuf_AddDesc_corpc_5fthirdparty_2eproto();
  const ::google::protobuf::FileDescriptor* file =
    ::google::protobuf::DescriptorPool::generated_pool()->FindFileByName(
      "corpc_thirdparty.proto");
  GOOGLE_CHECK(file != NULL);
  TakeResponse_descriptor_ = file->message_type(0);
  static const int TakeResponse_offsets_[1] = {
    GOOGLE_PROTOBUF_GENERATED_MESSAGE_FIELD_OFFSET(TakeResponse, handle_),
  };
  TakeResponse_reflection_ =
    new ::google::protobuf::internal::GeneratedMessageReflection(
      TakeResponse_descriptor_,
      TakeResponse::default_instance_,
      TakeResponse_offsets_,
      GOOGLE_PROTOBUF_GENERATED_MESSAGE_FIELD_OFFSET(TakeResponse, _has_bits_[0]),
      GOOGLE_PROTOBUF_GENERATED_MESSAGE_FIELD_OFFSET(TakeResponse, _unknown_fields_),
      -1,
      ::google::protobuf::DescriptorPool::generated_pool(),
      ::google::protobuf::MessageFactory::generated_factory(),
      sizeof(TakeResponse));
  PutRequest_descriptor_ = file->message_type(1);
  static const int PutRequest_offsets_[2] = {
    GOOGLE_PROTOBUF_GENERATED_MESSAGE_FIELD_OFFSET(PutRequest, handle_),
    GOOGLE_PROTOBUF_GENERATED_MESSAGE_FIELD_OFFSET(PutRequest, error_),
  };
  PutRequest_reflection_ =
    new ::google::protobuf::internal::GeneratedMessageReflection(
      PutRequest_descriptor_,
      PutRequest::default_instance_,
      PutRequest_offsets_,
      GOOGLE_PROTOBUF_GENERATED_MESSAGE_FIELD_OFFSET(PutRequest, _has_bits_[0]),
      GOOGLE_PROTOBUF_GENERATED_MESSAGE_FIELD_OFFSET(PutRequest, _unknown_fields_),
      -1,
      ::google::protobuf::DescriptorPool::generated_pool(),
      ::google::protobuf::MessageFactory::generated_factory(),
      sizeof(PutRequest));
  ThirdPartyService_descriptor_ = file->service(0);
}

namespace {

GOOGLE_PROTOBUF_DECLARE_ONCE(protobuf_AssignDescriptors_once_);
inline void protobuf_AssignDescriptorsOnce() {
  ::google::protobuf::GoogleOnceInit(&protobuf_AssignDescriptors_once_,
                 &protobuf_AssignDesc_corpc_5fthirdparty_2eproto);
}

void protobuf_RegisterTypes(const ::std::string&) {
  protobuf_AssignDescriptorsOnce();
  ::google::protobuf::MessageFactory::InternalRegisterGeneratedMessage(
    TakeResponse_descriptor_, &TakeResponse::default_instance());
  ::google::protobuf::MessageFactory::InternalRegisterGeneratedMessage(
    PutRequest_descriptor_, &PutRequest::default_instance());
}

}  // namespace

void protobuf_ShutdownFile_corpc_5fthirdparty_2eproto() {
  delete TakeResponse::default_instance_;
  delete TakeResponse_reflection_;
  delete PutRequest::default_instance_;
  delete PutRequest_reflection_;
}

void protobuf_AddDesc_corpc_5fthirdparty_2eproto() {
  static bool already_here = false;
  if (already_here) return;
  already_here = true;
  GOOGLE_PROTOBUF_VERIFY_VERSION;

  ::corpc::protobuf_AddDesc_corpc_5foption_2eproto();
  ::google::protobuf::DescriptorPool::InternalAddGeneratedFile(
    "\n\026corpc_thirdparty.proto\022\020corpc.thirdpar"
    "ty\032\022corpc_option.proto\"\036\n\014TakeResponse\022\016"
    "\n\006handle\030\001 \002(\004\"+\n\nPutRequest\022\016\n\006handle\030\001"
    " \002(\004\022\r\n\005error\030\002 \001(\0102\220\001\n\021ThirdPartyServic"
    "e\0229\n\004take\022\013.corpc.Void\032\036.corpc.thirdpart"
    "y.TakeResponse\"\004\220\361\004\001\022:\n\003put\022\034.corpc.thir"
    "dparty.PutRequest\032\013.corpc.Void\"\010\220\361\004\001\230\361\004\001"
    "\032\004\200\361\004\001B\003\200\001\001", 291);
  ::google::protobuf::MessageFactory::InternalRegisterGeneratedFile(
    "corpc_thirdparty.proto", &protobuf_RegisterTypes);
  TakeResponse::default_instance_ = new TakeResponse();
  PutRequest::default_instance_ = new PutRequest();
  TakeResponse::default_instance_->InitAsDefaultInstance();
  PutRequest::default_instance_->InitAsDefaultInstance();
  ::google::protobuf::internal::OnShutdown(&protobuf_ShutdownFile_corpc_5fthirdparty_2eproto);
}

// Force AddDescriptors() to be called at static initialization time.
struct StaticDescriptorInitializer_corpc_5fthirdparty_2eproto {
  StaticDescriptorInitializer_corpc_5fthirdparty_2eproto() {
    protobuf_AddDesc_corpc_5fthirdparty_2eproto();
  }
} static_descriptor_initializer_corpc_5fthirdparty_2eproto_;

// ===================================================================

#ifndef _MSC_VER
const int TakeResponse::kHandleFieldNumber;
#endif  // !_MSC_VER

TakeResponse::TakeResponse()
  : ::google::protobuf::Message() {
  SharedCtor();
  // @@protoc_insertion_point(constructor:corpc.thirdparty.TakeResponse)
}

void TakeResponse::InitAsDefaultInstance() {
}

TakeResponse::TakeResponse(const TakeResponse& from)
  : ::google::protobuf::Message() {
  SharedCtor();
  MergeFrom(from);
  // @@protoc_insertion_point(copy_constructor:corpc.thirdparty.TakeResponse)
}

void TakeResponse::SharedCtor() {
  _cached_size_ = 0;
  handle_ = GOOGLE_ULONGLONG(0);
  ::memset(_has_bits_, 0, sizeof(_has_bits_));
}

TakeResponse::~TakeResponse() {
  // @@protoc_insertion_point(destructor:corpc.thirdparty.TakeResponse)
  SharedDtor();
}

void TakeResponse::SharedDtor() {
  if (this != default_instance_) {
  }
}

void TakeResponse::SetCachedSize(int size) const {
  GOOGLE_SAFE_CONCURRENT_WRITES_BEGIN();
  _cached_size_ = size;
  GOOGLE_SAFE_CONCURRENT_WRITES_END();
}
const ::google::protobuf::Descriptor* TakeResponse::descriptor() {
  protobuf_AssignDescriptorsOnce();
  return TakeResponse_descriptor_;
}

const TakeResponse& TakeResponse::default_instance() {
  if (default_instance_ == NULL) protobuf_AddDesc_corpc_5fthirdparty_2eproto();
  return *default_instance_;
}

TakeResponse* TakeResponse::default_instance_ = NULL;

TakeResponse* TakeResponse::New() const {
  return new TakeResponse;
}

void TakeResponse::Clear() {
  handle_ = GOOGLE_ULONGLONG(0);
  ::memset(_has_bits_, 0, sizeof(_has_bits_));
  mutable_unknown_fields()->Clear();
}

bool TakeResponse::MergePartialFromCodedStream(
    ::google::protobuf::io::CodedInputStream* input) {
#define DO_(EXPRESSION) if (!(EXPRESSION)) goto failure
  ::google::protobuf::uint32 tag;
  // @@protoc_insertion_point(parse_start:corpc.thirdparty.TakeResponse)
  for (;;) {
    ::std::pair< ::google::protobuf::uint32, bool> p = input->ReadTagWithCutoff(127);
    tag = p.first;
    if (!p.second) goto handle_unusual;
    switch (::google::protobuf::internal::WireFormatLite::GetTagFieldNumber(tag)) {
      // required uint64 handle = 1;
      case 1: {
        if (tag == 8) {
          DO_((::google::protobuf::internal::WireFormatLite::ReadPrimitive<
                   ::google::protobuf::uint64, ::google::protobuf::internal::WireFormatLite::TYPE_UINT64>(
                 input, &handle_)));
          set_has_handle();
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
  // @@protoc_insertion_point(parse_success:corpc.thirdparty.TakeResponse)
  return true;
failure:
  // @@protoc_insertion_point(parse_failure:corpc.thirdparty.TakeResponse)
  return false;
#undef DO_
}

void TakeResponse::SerializeWithCachedSizes(
    ::google::protobuf::io::CodedOutputStream* output) const {
  // @@protoc_insertion_point(serialize_start:corpc.thirdparty.TakeResponse)
  // required uint64 handle = 1;
  if (has_handle()) {
    ::google::protobuf::internal::WireFormatLite::WriteUInt64(1, this->handle(), output);
  }

  if (!unknown_fields().empty()) {
    ::google::protobuf::internal::WireFormat::SerializeUnknownFields(
        unknown_fields(), output);
  }
  // @@protoc_insertion_point(serialize_end:corpc.thirdparty.TakeResponse)
}

::google::protobuf::uint8* TakeResponse::SerializeWithCachedSizesToArray(
    ::google::protobuf::uint8* target) const {
  // @@protoc_insertion_point(serialize_to_array_start:corpc.thirdparty.TakeResponse)
  // required uint64 handle = 1;
  if (has_handle()) {
    target = ::google::protobuf::internal::WireFormatLite::WriteUInt64ToArray(1, this->handle(), target);
  }

  if (!unknown_fields().empty()) {
    target = ::google::protobuf::internal::WireFormat::SerializeUnknownFieldsToArray(
        unknown_fields(), target);
  }
  // @@protoc_insertion_point(serialize_to_array_end:corpc.thirdparty.TakeResponse)
  return target;
}

int TakeResponse::ByteSize() const {
  int total_size = 0;

  if (_has_bits_[0 / 32] & (0xffu << (0 % 32))) {
    // required uint64 handle = 1;
    if (has_handle()) {
      total_size += 1 +
        ::google::protobuf::internal::WireFormatLite::UInt64Size(
          this->handle());
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

void TakeResponse::MergeFrom(const ::google::protobuf::Message& from) {
  GOOGLE_CHECK_NE(&from, this);
  const TakeResponse* source =
    ::google::protobuf::internal::dynamic_cast_if_available<const TakeResponse*>(
      &from);
  if (source == NULL) {
    ::google::protobuf::internal::ReflectionOps::Merge(from, this);
  } else {
    MergeFrom(*source);
  }
}

void TakeResponse::MergeFrom(const TakeResponse& from) {
  GOOGLE_CHECK_NE(&from, this);
  if (from._has_bits_[0 / 32] & (0xffu << (0 % 32))) {
    if (from.has_handle()) {
      set_handle(from.handle());
    }
  }
  mutable_unknown_fields()->MergeFrom(from.unknown_fields());
}

void TakeResponse::CopyFrom(const ::google::protobuf::Message& from) {
  if (&from == this) return;
  Clear();
  MergeFrom(from);
}

void TakeResponse::CopyFrom(const TakeResponse& from) {
  if (&from == this) return;
  Clear();
  MergeFrom(from);
}

bool TakeResponse::IsInitialized() const {
  if ((_has_bits_[0] & 0x00000001) != 0x00000001) return false;

  return true;
}

void TakeResponse::Swap(TakeResponse* other) {
  if (other != this) {
    std::swap(handle_, other->handle_);
    std::swap(_has_bits_[0], other->_has_bits_[0]);
    _unknown_fields_.Swap(&other->_unknown_fields_);
    std::swap(_cached_size_, other->_cached_size_);
  }
}

::google::protobuf::Metadata TakeResponse::GetMetadata() const {
  protobuf_AssignDescriptorsOnce();
  ::google::protobuf::Metadata metadata;
  metadata.descriptor = TakeResponse_descriptor_;
  metadata.reflection = TakeResponse_reflection_;
  return metadata;
}


// ===================================================================

#ifndef _MSC_VER
const int PutRequest::kHandleFieldNumber;
const int PutRequest::kErrorFieldNumber;
#endif  // !_MSC_VER

PutRequest::PutRequest()
  : ::google::protobuf::Message() {
  SharedCtor();
  // @@protoc_insertion_point(constructor:corpc.thirdparty.PutRequest)
}

void PutRequest::InitAsDefaultInstance() {
}

PutRequest::PutRequest(const PutRequest& from)
  : ::google::protobuf::Message() {
  SharedCtor();
  MergeFrom(from);
  // @@protoc_insertion_point(copy_constructor:corpc.thirdparty.PutRequest)
}

void PutRequest::SharedCtor() {
  _cached_size_ = 0;
  handle_ = GOOGLE_ULONGLONG(0);
  error_ = false;
  ::memset(_has_bits_, 0, sizeof(_has_bits_));
}

PutRequest::~PutRequest() {
  // @@protoc_insertion_point(destructor:corpc.thirdparty.PutRequest)
  SharedDtor();
}

void PutRequest::SharedDtor() {
  if (this != default_instance_) {
  }
}

void PutRequest::SetCachedSize(int size) const {
  GOOGLE_SAFE_CONCURRENT_WRITES_BEGIN();
  _cached_size_ = size;
  GOOGLE_SAFE_CONCURRENT_WRITES_END();
}
const ::google::protobuf::Descriptor* PutRequest::descriptor() {
  protobuf_AssignDescriptorsOnce();
  return PutRequest_descriptor_;
}

const PutRequest& PutRequest::default_instance() {
  if (default_instance_ == NULL) protobuf_AddDesc_corpc_5fthirdparty_2eproto();
  return *default_instance_;
}

PutRequest* PutRequest::default_instance_ = NULL;

PutRequest* PutRequest::New() const {
  return new PutRequest;
}

void PutRequest::Clear() {
#define OFFSET_OF_FIELD_(f) (reinterpret_cast<char*>(      \
  &reinterpret_cast<PutRequest*>(16)->f) - \
   reinterpret_cast<char*>(16))

#define ZR_(first, last) do {                              \
    size_t f = OFFSET_OF_FIELD_(first);                    \
    size_t n = OFFSET_OF_FIELD_(last) - f + sizeof(last);  \
    ::memset(&first, 0, n);                                \
  } while (0)

  ZR_(handle_, error_);

#undef OFFSET_OF_FIELD_
#undef ZR_

  ::memset(_has_bits_, 0, sizeof(_has_bits_));
  mutable_unknown_fields()->Clear();
}

bool PutRequest::MergePartialFromCodedStream(
    ::google::protobuf::io::CodedInputStream* input) {
#define DO_(EXPRESSION) if (!(EXPRESSION)) goto failure
  ::google::protobuf::uint32 tag;
  // @@protoc_insertion_point(parse_start:corpc.thirdparty.PutRequest)
  for (;;) {
    ::std::pair< ::google::protobuf::uint32, bool> p = input->ReadTagWithCutoff(127);
    tag = p.first;
    if (!p.second) goto handle_unusual;
    switch (::google::protobuf::internal::WireFormatLite::GetTagFieldNumber(tag)) {
      // required uint64 handle = 1;
      case 1: {
        if (tag == 8) {
          DO_((::google::protobuf::internal::WireFormatLite::ReadPrimitive<
                   ::google::protobuf::uint64, ::google::protobuf::internal::WireFormatLite::TYPE_UINT64>(
                 input, &handle_)));
          set_has_handle();
        } else {
          goto handle_unusual;
        }
        if (input->ExpectTag(16)) goto parse_error;
        break;
      }

      // optional bool error = 2;
      case 2: {
        if (tag == 16) {
         parse_error:
          DO_((::google::protobuf::internal::WireFormatLite::ReadPrimitive<
                   bool, ::google::protobuf::internal::WireFormatLite::TYPE_BOOL>(
                 input, &error_)));
          set_has_error();
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
  // @@protoc_insertion_point(parse_success:corpc.thirdparty.PutRequest)
  return true;
failure:
  // @@protoc_insertion_point(parse_failure:corpc.thirdparty.PutRequest)
  return false;
#undef DO_
}

void PutRequest::SerializeWithCachedSizes(
    ::google::protobuf::io::CodedOutputStream* output) const {
  // @@protoc_insertion_point(serialize_start:corpc.thirdparty.PutRequest)
  // required uint64 handle = 1;
  if (has_handle()) {
    ::google::protobuf::internal::WireFormatLite::WriteUInt64(1, this->handle(), output);
  }

  // optional bool error = 2;
  if (has_error()) {
    ::google::protobuf::internal::WireFormatLite::WriteBool(2, this->error(), output);
  }

  if (!unknown_fields().empty()) {
    ::google::protobuf::internal::WireFormat::SerializeUnknownFields(
        unknown_fields(), output);
  }
  // @@protoc_insertion_point(serialize_end:corpc.thirdparty.PutRequest)
}

::google::protobuf::uint8* PutRequest::SerializeWithCachedSizesToArray(
    ::google::protobuf::uint8* target) const {
  // @@protoc_insertion_point(serialize_to_array_start:corpc.thirdparty.PutRequest)
  // required uint64 handle = 1;
  if (has_handle()) {
    target = ::google::protobuf::internal::WireFormatLite::WriteUInt64ToArray(1, this->handle(), target);
  }

  // optional bool error = 2;
  if (has_error()) {
    target = ::google::protobuf::internal::WireFormatLite::WriteBoolToArray(2, this->error(), target);
  }

  if (!unknown_fields().empty()) {
    target = ::google::protobuf::internal::WireFormat::SerializeUnknownFieldsToArray(
        unknown_fields(), target);
  }
  // @@protoc_insertion_point(serialize_to_array_end:corpc.thirdparty.PutRequest)
  return target;
}

int PutRequest::ByteSize() const {
  int total_size = 0;

  if (_has_bits_[0 / 32] & (0xffu << (0 % 32))) {
    // required uint64 handle = 1;
    if (has_handle()) {
      total_size += 1 +
        ::google::protobuf::internal::WireFormatLite::UInt64Size(
          this->handle());
    }

    // optional bool error = 2;
    if (has_error()) {
      total_size += 1 + 1;
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

void PutRequest::MergeFrom(const ::google::protobuf::Message& from) {
  GOOGLE_CHECK_NE(&from, this);
  const PutRequest* source =
    ::google::protobuf::internal::dynamic_cast_if_available<const PutRequest*>(
      &from);
  if (source == NULL) {
    ::google::protobuf::internal::ReflectionOps::Merge(from, this);
  } else {
    MergeFrom(*source);
  }
}

void PutRequest::MergeFrom(const PutRequest& from) {
  GOOGLE_CHECK_NE(&from, this);
  if (from._has_bits_[0 / 32] & (0xffu << (0 % 32))) {
    if (from.has_handle()) {
      set_handle(from.handle());
    }
    if (from.has_error()) {
      set_error(from.error());
    }
  }
  mutable_unknown_fields()->MergeFrom(from.unknown_fields());
}

void PutRequest::CopyFrom(const ::google::protobuf::Message& from) {
  if (&from == this) return;
  Clear();
  MergeFrom(from);
}

void PutRequest::CopyFrom(const PutRequest& from) {
  if (&from == this) return;
  Clear();
  MergeFrom(from);
}

bool PutRequest::IsInitialized() const {
  if ((_has_bits_[0] & 0x00000001) != 0x00000001) return false;

  return true;
}

void PutRequest::Swap(PutRequest* other) {
  if (other != this) {
    std::swap(handle_, other->handle_);
    std::swap(error_, other->error_);
    std::swap(_has_bits_[0], other->_has_bits_[0]);
    _unknown_fields_.Swap(&other->_unknown_fields_);
    std::swap(_cached_size_, other->_cached_size_);
  }
}

::google::protobuf::Metadata PutRequest::GetMetadata() const {
  protobuf_AssignDescriptorsOnce();
  ::google::protobuf::Metadata metadata;
  metadata.descriptor = PutRequest_descriptor_;
  metadata.reflection = PutRequest_reflection_;
  return metadata;
}


// ===================================================================

ThirdPartyService::~ThirdPartyService() {}

const ::google::protobuf::ServiceDescriptor* ThirdPartyService::descriptor() {
  protobuf_AssignDescriptorsOnce();
  return ThirdPartyService_descriptor_;
}

const ::google::protobuf::ServiceDescriptor* ThirdPartyService::GetDescriptor() {
  protobuf_AssignDescriptorsOnce();
  return ThirdPartyService_descriptor_;
}

void ThirdPartyService::take(::google::protobuf::RpcController* controller,
                         const ::corpc::Void*,
                         ::corpc::thirdparty::TakeResponse*,
                         ::google::protobuf::Closure* done) {
  controller->SetFailed("Method take() not implemented.");
  done->Run();
}

void ThirdPartyService::put(::google::protobuf::RpcController* controller,
                         const ::corpc::thirdparty::PutRequest*,
                         ::corpc::Void*,
                         ::google::protobuf::Closure* done) {
  controller->SetFailed("Method put() not implemented.");
  done->Run();
}

void ThirdPartyService::CallMethod(const ::google::protobuf::MethodDescriptor* method,
                             ::google::protobuf::RpcController* controller,
                             const ::google::protobuf::Message* request,
                             ::google::protobuf::Message* response,
                             ::google::protobuf::Closure* done) {
  GOOGLE_DCHECK_EQ(method->service(), ThirdPartyService_descriptor_);
  switch(method->index()) {
    case 0:
      take(controller,
             ::google::protobuf::down_cast<const ::corpc::Void*>(request),
             ::google::protobuf::down_cast< ::corpc::thirdparty::TakeResponse*>(response),
             done);
      break;
    case 1:
      put(controller,
             ::google::protobuf::down_cast<const ::corpc::thirdparty::PutRequest*>(request),
             ::google::protobuf::down_cast< ::corpc::Void*>(response),
             done);
      break;
    default:
      GOOGLE_LOG(FATAL) << "Bad method index; this should never happen.";
      break;
  }
}

const ::google::protobuf::Message& ThirdPartyService::GetRequestPrototype(
    const ::google::protobuf::MethodDescriptor* method) const {
  GOOGLE_DCHECK_EQ(method->service(), descriptor());
  switch(method->index()) {
    case 0:
      return ::corpc::Void::default_instance();
    case 1:
      return ::corpc::thirdparty::PutRequest::default_instance();
    default:
      GOOGLE_LOG(FATAL) << "Bad method index; this should never happen.";
      return *reinterpret_cast< ::google::protobuf::Message*>(NULL);
  }
}

const ::google::protobuf::Message& ThirdPartyService::GetResponsePrototype(
    const ::google::protobuf::MethodDescriptor* method) const {
  GOOGLE_DCHECK_EQ(method->service(), descriptor());
  switch(method->index()) {
    case 0:
      return ::corpc::thirdparty::TakeResponse::default_instance();
    case 1:
      return ::corpc::Void::default_instance();
    default:
      GOOGLE_LOG(FATAL) << "Bad method index; this should never happen.";
      return *reinterpret_cast< ::google::protobuf::Message*>(NULL);
  }
}

ThirdPartyService_Stub::ThirdPartyService_Stub(::google::protobuf::RpcChannel* channel)
  : channel_(channel), owns_channel_(false) {}
ThirdPartyService_Stub::ThirdPartyService_Stub(
    ::google::protobuf::RpcChannel* channel,
    ::google::protobuf::Service::ChannelOwnership ownership)
  : channel_(channel),
    owns_channel_(ownership == ::google::protobuf::Service::STUB_OWNS_CHANNEL) {}
ThirdPartyService_Stub::~ThirdPartyService_Stub() {
  if (owns_channel_) delete channel_;
}

void ThirdPartyService_Stub::take(::google::protobuf::RpcController* controller,
                              const ::corpc::Void* request,
                              ::corpc::thirdparty::TakeResponse* response,
                              ::google::protobuf::Closure* done) {
  channel_->CallMethod(descriptor()->method(0),
                       controller, request, response, done);
}
void ThirdPartyService_Stub::put(::google::protobuf::RpcController* controller,
                              const ::corpc::thirdparty::PutRequest* request,
                              ::corpc::Void* response,
                              ::google::protobuf::Closure* done) {
  channel_->CallMethod(descriptor()->method(1),
                       controller, request, response, done);
}

// @@protoc_insertion_point(namespace_scope)

}  // namespace thirdparty
}  // namespace corpc

// @@protoc_insertion_point(global_scope)
