// Generated by the protocol buffer compiler.  DO NOT EDIT!
// source: foo.proto

#ifndef GOOGLE_PROTOBUF_INCLUDED_foo_2eproto
#define GOOGLE_PROTOBUF_INCLUDED_foo_2eproto

#include <limits>
#include <string>

#include <google/protobuf/port_def.inc>
#if PROTOBUF_VERSION < 3012000
#error This file was generated by a newer version of protoc which is
#error incompatible with your Protocol Buffer headers. Please update
#error your headers.
#endif
#if 3012003 < PROTOBUF_MIN_PROTOC_VERSION
#error This file was generated by an older version of protoc which is
#error incompatible with your Protocol Buffer headers. Please
#error regenerate this file with a newer version of protoc.
#endif

#include <google/protobuf/port_undef.inc>
#include <google/protobuf/io/coded_stream.h>
#include <google/protobuf/arena.h>
#include <google/protobuf/arenastring.h>
#include <google/protobuf/generated_message_table_driven.h>
#include <google/protobuf/generated_message_util.h>
#include <google/protobuf/inlined_string_field.h>
#include <google/protobuf/metadata_lite.h>
#include <google/protobuf/generated_message_reflection.h>
#include <google/protobuf/message.h>
#include <google/protobuf/repeated_field.h>  // IWYU pragma: export
#include <google/protobuf/extension_set.h>  // IWYU pragma: export
#include <google/protobuf/service.h>
#include <google/protobuf/unknown_field_set.h>
#include "corpc_option.pb.h"
// @@protoc_insertion_point(includes)
#include <google/protobuf/port_def.inc>
#define PROTOBUF_INTERNAL_EXPORT_foo_2eproto
PROTOBUF_NAMESPACE_OPEN
namespace internal {
class AnyMetadata;
}  // namespace internal
PROTOBUF_NAMESPACE_CLOSE

// Internal implementation detail -- do not use these members.
struct TableStruct_foo_2eproto {
  static const ::PROTOBUF_NAMESPACE_ID::internal::ParseTableField entries[]
    PROTOBUF_SECTION_VARIABLE(protodesc_cold);
  static const ::PROTOBUF_NAMESPACE_ID::internal::AuxillaryParseTableField aux[]
    PROTOBUF_SECTION_VARIABLE(protodesc_cold);
  static const ::PROTOBUF_NAMESPACE_ID::internal::ParseTable schema[2]
    PROTOBUF_SECTION_VARIABLE(protodesc_cold);
  static const ::PROTOBUF_NAMESPACE_ID::internal::FieldMetadata field_metadata[];
  static const ::PROTOBUF_NAMESPACE_ID::internal::SerializationTable serialization_table[];
  static const ::PROTOBUF_NAMESPACE_ID::uint32 offsets[];
};
extern const ::PROTOBUF_NAMESPACE_ID::internal::DescriptorTable descriptor_table_foo_2eproto;
class FooRequest;
class FooRequestDefaultTypeInternal;
extern FooRequestDefaultTypeInternal _FooRequest_default_instance_;
class FooResponse;
class FooResponseDefaultTypeInternal;
extern FooResponseDefaultTypeInternal _FooResponse_default_instance_;
PROTOBUF_NAMESPACE_OPEN
template<> ::FooRequest* Arena::CreateMaybeMessage<::FooRequest>(Arena*);
template<> ::FooResponse* Arena::CreateMaybeMessage<::FooResponse>(Arena*);
PROTOBUF_NAMESPACE_CLOSE

// ===================================================================

class FooRequest PROTOBUF_FINAL :
    public ::PROTOBUF_NAMESPACE_ID::Message /* @@protoc_insertion_point(class_definition:FooRequest) */ {
 public:
  inline FooRequest() : FooRequest(nullptr) {};
  virtual ~FooRequest();

  FooRequest(const FooRequest& from);
  FooRequest(FooRequest&& from) noexcept
    : FooRequest() {
    *this = ::std::move(from);
  }

  inline FooRequest& operator=(const FooRequest& from) {
    CopyFrom(from);
    return *this;
  }
  inline FooRequest& operator=(FooRequest&& from) noexcept {
    if (GetArena() == from.GetArena()) {
      if (this != &from) InternalSwap(&from);
    } else {
      CopyFrom(from);
    }
    return *this;
  }

  static const ::PROTOBUF_NAMESPACE_ID::Descriptor* descriptor() {
    return GetDescriptor();
  }
  static const ::PROTOBUF_NAMESPACE_ID::Descriptor* GetDescriptor() {
    return GetMetadataStatic().descriptor;
  }
  static const ::PROTOBUF_NAMESPACE_ID::Reflection* GetReflection() {
    return GetMetadataStatic().reflection;
  }
  static const FooRequest& default_instance();

  static void InitAsDefaultInstance();  // FOR INTERNAL USE ONLY
  static inline const FooRequest* internal_default_instance() {
    return reinterpret_cast<const FooRequest*>(
               &_FooRequest_default_instance_);
  }
  static constexpr int kIndexInFileMessages =
    0;

  friend void swap(FooRequest& a, FooRequest& b) {
    a.Swap(&b);
  }
  inline void Swap(FooRequest* other) {
    if (other == this) return;
    if (GetArena() == other->GetArena()) {
      InternalSwap(other);
    } else {
      ::PROTOBUF_NAMESPACE_ID::internal::GenericSwap(this, other);
    }
  }
  void UnsafeArenaSwap(FooRequest* other) {
    if (other == this) return;
    GOOGLE_DCHECK(GetArena() == other->GetArena());
    InternalSwap(other);
  }

  // implements Message ----------------------------------------------

  inline FooRequest* New() const final {
    return CreateMaybeMessage<FooRequest>(nullptr);
  }

  FooRequest* New(::PROTOBUF_NAMESPACE_ID::Arena* arena) const final {
    return CreateMaybeMessage<FooRequest>(arena);
  }
  void CopyFrom(const ::PROTOBUF_NAMESPACE_ID::Message& from) final;
  void MergeFrom(const ::PROTOBUF_NAMESPACE_ID::Message& from) final;
  void CopyFrom(const FooRequest& from);
  void MergeFrom(const FooRequest& from);
  PROTOBUF_ATTRIBUTE_REINITIALIZES void Clear() final;
  bool IsInitialized() const final;

  size_t ByteSizeLong() const final;
  const char* _InternalParse(const char* ptr, ::PROTOBUF_NAMESPACE_ID::internal::ParseContext* ctx) final;
  ::PROTOBUF_NAMESPACE_ID::uint8* _InternalSerialize(
      ::PROTOBUF_NAMESPACE_ID::uint8* target, ::PROTOBUF_NAMESPACE_ID::io::EpsCopyOutputStream* stream) const final;
  int GetCachedSize() const final { return _cached_size_.Get(); }

  private:
  inline void SharedCtor();
  inline void SharedDtor();
  void SetCachedSize(int size) const final;
  void InternalSwap(FooRequest* other);
  friend class ::PROTOBUF_NAMESPACE_ID::internal::AnyMetadata;
  static ::PROTOBUF_NAMESPACE_ID::StringPiece FullMessageName() {
    return "FooRequest";
  }
  protected:
  explicit FooRequest(::PROTOBUF_NAMESPACE_ID::Arena* arena);
  private:
  static void ArenaDtor(void* object);
  inline void RegisterArenaDtor(::PROTOBUF_NAMESPACE_ID::Arena* arena);
  public:

  ::PROTOBUF_NAMESPACE_ID::Metadata GetMetadata() const final;
  private:
  static ::PROTOBUF_NAMESPACE_ID::Metadata GetMetadataStatic() {
    ::PROTOBUF_NAMESPACE_ID::internal::AssignDescriptors(&::descriptor_table_foo_2eproto);
    return ::descriptor_table_foo_2eproto.file_level_metadata[kIndexInFileMessages];
  }

  public:

  // nested types ----------------------------------------------------

  // accessors -------------------------------------------------------

  enum : int {
    kTextFieldNumber = 1,
    kTimesFieldNumber = 2,
  };
  // string text = 1;
  void clear_text();
  const std::string& text() const;
  void set_text(const std::string& value);
  void set_text(std::string&& value);
  void set_text(const char* value);
  void set_text(const char* value, size_t size);
  std::string* mutable_text();
  std::string* release_text();
  void set_allocated_text(std::string* text);
  GOOGLE_PROTOBUF_RUNTIME_DEPRECATED("The unsafe_arena_ accessors for"
  "    string fields are deprecated and will be removed in a"
  "    future release.")
  std::string* unsafe_arena_release_text();
  GOOGLE_PROTOBUF_RUNTIME_DEPRECATED("The unsafe_arena_ accessors for"
  "    string fields are deprecated and will be removed in a"
  "    future release.")
  void unsafe_arena_set_allocated_text(
      std::string* text);
  private:
  const std::string& _internal_text() const;
  void _internal_set_text(const std::string& value);
  std::string* _internal_mutable_text();
  public:

  // int32 times = 2;
  void clear_times();
  ::PROTOBUF_NAMESPACE_ID::int32 times() const;
  void set_times(::PROTOBUF_NAMESPACE_ID::int32 value);
  private:
  ::PROTOBUF_NAMESPACE_ID::int32 _internal_times() const;
  void _internal_set_times(::PROTOBUF_NAMESPACE_ID::int32 value);
  public:

  // @@protoc_insertion_point(class_scope:FooRequest)
 private:
  class _Internal;

  template <typename T> friend class ::PROTOBUF_NAMESPACE_ID::Arena::InternalHelper;
  typedef void InternalArenaConstructable_;
  typedef void DestructorSkippable_;
  ::PROTOBUF_NAMESPACE_ID::internal::ArenaStringPtr text_;
  ::PROTOBUF_NAMESPACE_ID::int32 times_;
  mutable ::PROTOBUF_NAMESPACE_ID::internal::CachedSize _cached_size_;
  friend struct ::TableStruct_foo_2eproto;
};
// -------------------------------------------------------------------

class FooResponse PROTOBUF_FINAL :
    public ::PROTOBUF_NAMESPACE_ID::Message /* @@protoc_insertion_point(class_definition:FooResponse) */ {
 public:
  inline FooResponse() : FooResponse(nullptr) {};
  virtual ~FooResponse();

  FooResponse(const FooResponse& from);
  FooResponse(FooResponse&& from) noexcept
    : FooResponse() {
    *this = ::std::move(from);
  }

  inline FooResponse& operator=(const FooResponse& from) {
    CopyFrom(from);
    return *this;
  }
  inline FooResponse& operator=(FooResponse&& from) noexcept {
    if (GetArena() == from.GetArena()) {
      if (this != &from) InternalSwap(&from);
    } else {
      CopyFrom(from);
    }
    return *this;
  }

  static const ::PROTOBUF_NAMESPACE_ID::Descriptor* descriptor() {
    return GetDescriptor();
  }
  static const ::PROTOBUF_NAMESPACE_ID::Descriptor* GetDescriptor() {
    return GetMetadataStatic().descriptor;
  }
  static const ::PROTOBUF_NAMESPACE_ID::Reflection* GetReflection() {
    return GetMetadataStatic().reflection;
  }
  static const FooResponse& default_instance();

  static void InitAsDefaultInstance();  // FOR INTERNAL USE ONLY
  static inline const FooResponse* internal_default_instance() {
    return reinterpret_cast<const FooResponse*>(
               &_FooResponse_default_instance_);
  }
  static constexpr int kIndexInFileMessages =
    1;

  friend void swap(FooResponse& a, FooResponse& b) {
    a.Swap(&b);
  }
  inline void Swap(FooResponse* other) {
    if (other == this) return;
    if (GetArena() == other->GetArena()) {
      InternalSwap(other);
    } else {
      ::PROTOBUF_NAMESPACE_ID::internal::GenericSwap(this, other);
    }
  }
  void UnsafeArenaSwap(FooResponse* other) {
    if (other == this) return;
    GOOGLE_DCHECK(GetArena() == other->GetArena());
    InternalSwap(other);
  }

  // implements Message ----------------------------------------------

  inline FooResponse* New() const final {
    return CreateMaybeMessage<FooResponse>(nullptr);
  }

  FooResponse* New(::PROTOBUF_NAMESPACE_ID::Arena* arena) const final {
    return CreateMaybeMessage<FooResponse>(arena);
  }
  void CopyFrom(const ::PROTOBUF_NAMESPACE_ID::Message& from) final;
  void MergeFrom(const ::PROTOBUF_NAMESPACE_ID::Message& from) final;
  void CopyFrom(const FooResponse& from);
  void MergeFrom(const FooResponse& from);
  PROTOBUF_ATTRIBUTE_REINITIALIZES void Clear() final;
  bool IsInitialized() const final;

  size_t ByteSizeLong() const final;
  const char* _InternalParse(const char* ptr, ::PROTOBUF_NAMESPACE_ID::internal::ParseContext* ctx) final;
  ::PROTOBUF_NAMESPACE_ID::uint8* _InternalSerialize(
      ::PROTOBUF_NAMESPACE_ID::uint8* target, ::PROTOBUF_NAMESPACE_ID::io::EpsCopyOutputStream* stream) const final;
  int GetCachedSize() const final { return _cached_size_.Get(); }

  private:
  inline void SharedCtor();
  inline void SharedDtor();
  void SetCachedSize(int size) const final;
  void InternalSwap(FooResponse* other);
  friend class ::PROTOBUF_NAMESPACE_ID::internal::AnyMetadata;
  static ::PROTOBUF_NAMESPACE_ID::StringPiece FullMessageName() {
    return "FooResponse";
  }
  protected:
  explicit FooResponse(::PROTOBUF_NAMESPACE_ID::Arena* arena);
  private:
  static void ArenaDtor(void* object);
  inline void RegisterArenaDtor(::PROTOBUF_NAMESPACE_ID::Arena* arena);
  public:

  ::PROTOBUF_NAMESPACE_ID::Metadata GetMetadata() const final;
  private:
  static ::PROTOBUF_NAMESPACE_ID::Metadata GetMetadataStatic() {
    ::PROTOBUF_NAMESPACE_ID::internal::AssignDescriptors(&::descriptor_table_foo_2eproto);
    return ::descriptor_table_foo_2eproto.file_level_metadata[kIndexInFileMessages];
  }

  public:

  // nested types ----------------------------------------------------

  // accessors -------------------------------------------------------

  enum : int {
    kTextFieldNumber = 1,
    kResultFieldNumber = 2,
  };
  // string text = 1;
  void clear_text();
  const std::string& text() const;
  void set_text(const std::string& value);
  void set_text(std::string&& value);
  void set_text(const char* value);
  void set_text(const char* value, size_t size);
  std::string* mutable_text();
  std::string* release_text();
  void set_allocated_text(std::string* text);
  GOOGLE_PROTOBUF_RUNTIME_DEPRECATED("The unsafe_arena_ accessors for"
  "    string fields are deprecated and will be removed in a"
  "    future release.")
  std::string* unsafe_arena_release_text();
  GOOGLE_PROTOBUF_RUNTIME_DEPRECATED("The unsafe_arena_ accessors for"
  "    string fields are deprecated and will be removed in a"
  "    future release.")
  void unsafe_arena_set_allocated_text(
      std::string* text);
  private:
  const std::string& _internal_text() const;
  void _internal_set_text(const std::string& value);
  std::string* _internal_mutable_text();
  public:

  // bool result = 2;
  void clear_result();
  bool result() const;
  void set_result(bool value);
  private:
  bool _internal_result() const;
  void _internal_set_result(bool value);
  public:

  // @@protoc_insertion_point(class_scope:FooResponse)
 private:
  class _Internal;

  template <typename T> friend class ::PROTOBUF_NAMESPACE_ID::Arena::InternalHelper;
  typedef void InternalArenaConstructable_;
  typedef void DestructorSkippable_;
  ::PROTOBUF_NAMESPACE_ID::internal::ArenaStringPtr text_;
  bool result_;
  mutable ::PROTOBUF_NAMESPACE_ID::internal::CachedSize _cached_size_;
  friend struct ::TableStruct_foo_2eproto;
};
// ===================================================================

class FooService_Stub;

class FooService : public ::PROTOBUF_NAMESPACE_ID::Service {
 protected:
  // This class should be treated as an abstract interface.
  inline FooService() {};
 public:
  virtual ~FooService();

  typedef FooService_Stub Stub;

  static const ::PROTOBUF_NAMESPACE_ID::ServiceDescriptor* descriptor();

  virtual void Foo(::PROTOBUF_NAMESPACE_ID::RpcController* controller,
                       const ::FooRequest* request,
                       ::FooResponse* response,
                       ::google::protobuf::Closure* done);

  // implements Service ----------------------------------------------

  const ::PROTOBUF_NAMESPACE_ID::ServiceDescriptor* GetDescriptor();
  void CallMethod(const ::PROTOBUF_NAMESPACE_ID::MethodDescriptor* method,
                  ::PROTOBUF_NAMESPACE_ID::RpcController* controller,
                  const ::PROTOBUF_NAMESPACE_ID::Message* request,
                  ::PROTOBUF_NAMESPACE_ID::Message* response,
                  ::google::protobuf::Closure* done);
  const ::PROTOBUF_NAMESPACE_ID::Message& GetRequestPrototype(
    const ::PROTOBUF_NAMESPACE_ID::MethodDescriptor* method) const;
  const ::PROTOBUF_NAMESPACE_ID::Message& GetResponsePrototype(
    const ::PROTOBUF_NAMESPACE_ID::MethodDescriptor* method) const;

 private:
  GOOGLE_DISALLOW_EVIL_CONSTRUCTORS(FooService);
};

class FooService_Stub : public FooService {
 public:
  FooService_Stub(::PROTOBUF_NAMESPACE_ID::RpcChannel* channel);
  FooService_Stub(::PROTOBUF_NAMESPACE_ID::RpcChannel* channel,
                   ::PROTOBUF_NAMESPACE_ID::Service::ChannelOwnership ownership);
  ~FooService_Stub();

  inline ::PROTOBUF_NAMESPACE_ID::RpcChannel* channel() { return channel_; }

  // implements FooService ------------------------------------------

  void Foo(::PROTOBUF_NAMESPACE_ID::RpcController* controller,
                       const ::FooRequest* request,
                       ::FooResponse* response,
                       ::google::protobuf::Closure* done);
 private:
  ::PROTOBUF_NAMESPACE_ID::RpcChannel* channel_;
  bool owns_channel_;
  GOOGLE_DISALLOW_EVIL_CONSTRUCTORS(FooService_Stub);
};


// ===================================================================


// ===================================================================

#ifdef __GNUC__
  #pragma GCC diagnostic push
  #pragma GCC diagnostic ignored "-Wstrict-aliasing"
#endif  // __GNUC__
// FooRequest

// string text = 1;
inline void FooRequest::clear_text() {
  text_.ClearToEmpty(&::PROTOBUF_NAMESPACE_ID::internal::GetEmptyStringAlreadyInited(), GetArena());
}
inline const std::string& FooRequest::text() const {
  // @@protoc_insertion_point(field_get:FooRequest.text)
  return _internal_text();
}
inline void FooRequest::set_text(const std::string& value) {
  _internal_set_text(value);
  // @@protoc_insertion_point(field_set:FooRequest.text)
}
inline std::string* FooRequest::mutable_text() {
  // @@protoc_insertion_point(field_mutable:FooRequest.text)
  return _internal_mutable_text();
}
inline const std::string& FooRequest::_internal_text() const {
  return text_.Get();
}
inline void FooRequest::_internal_set_text(const std::string& value) {
  
  text_.Set(&::PROTOBUF_NAMESPACE_ID::internal::GetEmptyStringAlreadyInited(), value, GetArena());
}
inline void FooRequest::set_text(std::string&& value) {
  
  text_.Set(
    &::PROTOBUF_NAMESPACE_ID::internal::GetEmptyStringAlreadyInited(), ::std::move(value), GetArena());
  // @@protoc_insertion_point(field_set_rvalue:FooRequest.text)
}
inline void FooRequest::set_text(const char* value) {
  GOOGLE_DCHECK(value != nullptr);
  
  text_.Set(&::PROTOBUF_NAMESPACE_ID::internal::GetEmptyStringAlreadyInited(), ::std::string(value),
              GetArena());
  // @@protoc_insertion_point(field_set_char:FooRequest.text)
}
inline void FooRequest::set_text(const char* value,
    size_t size) {
  
  text_.Set(&::PROTOBUF_NAMESPACE_ID::internal::GetEmptyStringAlreadyInited(), ::std::string(
      reinterpret_cast<const char*>(value), size), GetArena());
  // @@protoc_insertion_point(field_set_pointer:FooRequest.text)
}
inline std::string* FooRequest::_internal_mutable_text() {
  
  return text_.Mutable(&::PROTOBUF_NAMESPACE_ID::internal::GetEmptyStringAlreadyInited(), GetArena());
}
inline std::string* FooRequest::release_text() {
  // @@protoc_insertion_point(field_release:FooRequest.text)
  return text_.Release(&::PROTOBUF_NAMESPACE_ID::internal::GetEmptyStringAlreadyInited(), GetArena());
}
inline void FooRequest::set_allocated_text(std::string* text) {
  if (text != nullptr) {
    
  } else {
    
  }
  text_.SetAllocated(&::PROTOBUF_NAMESPACE_ID::internal::GetEmptyStringAlreadyInited(), text,
      GetArena());
  // @@protoc_insertion_point(field_set_allocated:FooRequest.text)
}
inline std::string* FooRequest::unsafe_arena_release_text() {
  // @@protoc_insertion_point(field_unsafe_arena_release:FooRequest.text)
  GOOGLE_DCHECK(GetArena() != nullptr);
  
  return text_.UnsafeArenaRelease(&::PROTOBUF_NAMESPACE_ID::internal::GetEmptyStringAlreadyInited(),
      GetArena());
}
inline void FooRequest::unsafe_arena_set_allocated_text(
    std::string* text) {
  GOOGLE_DCHECK(GetArena() != nullptr);
  if (text != nullptr) {
    
  } else {
    
  }
  text_.UnsafeArenaSetAllocated(&::PROTOBUF_NAMESPACE_ID::internal::GetEmptyStringAlreadyInited(),
      text, GetArena());
  // @@protoc_insertion_point(field_unsafe_arena_set_allocated:FooRequest.text)
}

// int32 times = 2;
inline void FooRequest::clear_times() {
  times_ = 0;
}
inline ::PROTOBUF_NAMESPACE_ID::int32 FooRequest::_internal_times() const {
  return times_;
}
inline ::PROTOBUF_NAMESPACE_ID::int32 FooRequest::times() const {
  // @@protoc_insertion_point(field_get:FooRequest.times)
  return _internal_times();
}
inline void FooRequest::_internal_set_times(::PROTOBUF_NAMESPACE_ID::int32 value) {
  
  times_ = value;
}
inline void FooRequest::set_times(::PROTOBUF_NAMESPACE_ID::int32 value) {
  _internal_set_times(value);
  // @@protoc_insertion_point(field_set:FooRequest.times)
}

// -------------------------------------------------------------------

// FooResponse

// string text = 1;
inline void FooResponse::clear_text() {
  text_.ClearToEmpty(&::PROTOBUF_NAMESPACE_ID::internal::GetEmptyStringAlreadyInited(), GetArena());
}
inline const std::string& FooResponse::text() const {
  // @@protoc_insertion_point(field_get:FooResponse.text)
  return _internal_text();
}
inline void FooResponse::set_text(const std::string& value) {
  _internal_set_text(value);
  // @@protoc_insertion_point(field_set:FooResponse.text)
}
inline std::string* FooResponse::mutable_text() {
  // @@protoc_insertion_point(field_mutable:FooResponse.text)
  return _internal_mutable_text();
}
inline const std::string& FooResponse::_internal_text() const {
  return text_.Get();
}
inline void FooResponse::_internal_set_text(const std::string& value) {
  
  text_.Set(&::PROTOBUF_NAMESPACE_ID::internal::GetEmptyStringAlreadyInited(), value, GetArena());
}
inline void FooResponse::set_text(std::string&& value) {
  
  text_.Set(
    &::PROTOBUF_NAMESPACE_ID::internal::GetEmptyStringAlreadyInited(), ::std::move(value), GetArena());
  // @@protoc_insertion_point(field_set_rvalue:FooResponse.text)
}
inline void FooResponse::set_text(const char* value) {
  GOOGLE_DCHECK(value != nullptr);
  
  text_.Set(&::PROTOBUF_NAMESPACE_ID::internal::GetEmptyStringAlreadyInited(), ::std::string(value),
              GetArena());
  // @@protoc_insertion_point(field_set_char:FooResponse.text)
}
inline void FooResponse::set_text(const char* value,
    size_t size) {
  
  text_.Set(&::PROTOBUF_NAMESPACE_ID::internal::GetEmptyStringAlreadyInited(), ::std::string(
      reinterpret_cast<const char*>(value), size), GetArena());
  // @@protoc_insertion_point(field_set_pointer:FooResponse.text)
}
inline std::string* FooResponse::_internal_mutable_text() {
  
  return text_.Mutable(&::PROTOBUF_NAMESPACE_ID::internal::GetEmptyStringAlreadyInited(), GetArena());
}
inline std::string* FooResponse::release_text() {
  // @@protoc_insertion_point(field_release:FooResponse.text)
  return text_.Release(&::PROTOBUF_NAMESPACE_ID::internal::GetEmptyStringAlreadyInited(), GetArena());
}
inline void FooResponse::set_allocated_text(std::string* text) {
  if (text != nullptr) {
    
  } else {
    
  }
  text_.SetAllocated(&::PROTOBUF_NAMESPACE_ID::internal::GetEmptyStringAlreadyInited(), text,
      GetArena());
  // @@protoc_insertion_point(field_set_allocated:FooResponse.text)
}
inline std::string* FooResponse::unsafe_arena_release_text() {
  // @@protoc_insertion_point(field_unsafe_arena_release:FooResponse.text)
  GOOGLE_DCHECK(GetArena() != nullptr);
  
  return text_.UnsafeArenaRelease(&::PROTOBUF_NAMESPACE_ID::internal::GetEmptyStringAlreadyInited(),
      GetArena());
}
inline void FooResponse::unsafe_arena_set_allocated_text(
    std::string* text) {
  GOOGLE_DCHECK(GetArena() != nullptr);
  if (text != nullptr) {
    
  } else {
    
  }
  text_.UnsafeArenaSetAllocated(&::PROTOBUF_NAMESPACE_ID::internal::GetEmptyStringAlreadyInited(),
      text, GetArena());
  // @@protoc_insertion_point(field_unsafe_arena_set_allocated:FooResponse.text)
}

// bool result = 2;
inline void FooResponse::clear_result() {
  result_ = false;
}
inline bool FooResponse::_internal_result() const {
  return result_;
}
inline bool FooResponse::result() const {
  // @@protoc_insertion_point(field_get:FooResponse.result)
  return _internal_result();
}
inline void FooResponse::_internal_set_result(bool value) {
  
  result_ = value;
}
inline void FooResponse::set_result(bool value) {
  _internal_set_result(value);
  // @@protoc_insertion_point(field_set:FooResponse.result)
}

#ifdef __GNUC__
  #pragma GCC diagnostic pop
#endif  // __GNUC__
// -------------------------------------------------------------------


// @@protoc_insertion_point(namespace_scope)


// @@protoc_insertion_point(global_scope)

#include <google/protobuf/port_undef.inc>
#endif  // GOOGLE_PROTOBUF_INCLUDED_GOOGLE_PROTOBUF_INCLUDED_foo_2eproto
