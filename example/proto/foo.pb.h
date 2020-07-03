// Generated by the protocol buffer compiler.  DO NOT EDIT!
// source: foo.proto

#ifndef PROTOBUF_foo_2eproto__INCLUDED
#define PROTOBUF_foo_2eproto__INCLUDED

#include <string>

#include <google/protobuf/stubs/common.h>

#if GOOGLE_PROTOBUF_VERSION < 2006000
#error This file was generated by a newer version of protoc which is
#error incompatible with your Protocol Buffer headers.  Please update
#error your headers.
#endif
#if 2006001 < GOOGLE_PROTOBUF_MIN_PROTOC_VERSION
#error This file was generated by an older version of protoc which is
#error incompatible with your Protocol Buffer headers.  Please
#error regenerate this file with a newer version of protoc.
#endif

#include <google/protobuf/generated_message_util.h>
#include <google/protobuf/message.h>
#include <google/protobuf/repeated_field.h>
#include <google/protobuf/extension_set.h>
#include <google/protobuf/service.h>
#include <google/protobuf/unknown_field_set.h>
#include "corpc_option.pb.h"
// @@protoc_insertion_point(includes)

// Internal implementation detail -- do not call these.
void  protobuf_AddDesc_foo_2eproto();
void protobuf_AssignDesc_foo_2eproto();
void protobuf_ShutdownFile_foo_2eproto();

class FooRequest;
class FooResponse;

// ===================================================================

class FooRequest : public ::google::protobuf::Message {
 public:
  FooRequest();
  virtual ~FooRequest();

  FooRequest(const FooRequest& from);

  inline FooRequest& operator=(const FooRequest& from) {
    CopyFrom(from);
    return *this;
  }

  inline const ::google::protobuf::UnknownFieldSet& unknown_fields() const {
    return _unknown_fields_;
  }

  inline ::google::protobuf::UnknownFieldSet* mutable_unknown_fields() {
    return &_unknown_fields_;
  }

  static const ::google::protobuf::Descriptor* descriptor();
  static const FooRequest& default_instance();

  void Swap(FooRequest* other);

  // implements Message ----------------------------------------------

  FooRequest* New() const;
  void CopyFrom(const ::google::protobuf::Message& from);
  void MergeFrom(const ::google::protobuf::Message& from);
  void CopyFrom(const FooRequest& from);
  void MergeFrom(const FooRequest& from);
  void Clear();
  bool IsInitialized() const;

  int ByteSize() const;
  bool MergePartialFromCodedStream(
      ::google::protobuf::io::CodedInputStream* input);
  void SerializeWithCachedSizes(
      ::google::protobuf::io::CodedOutputStream* output) const;
  ::google::protobuf::uint8* SerializeWithCachedSizesToArray(::google::protobuf::uint8* output) const;
  int GetCachedSize() const { return _cached_size_; }
  private:
  void SharedCtor();
  void SharedDtor();
  void SetCachedSize(int size) const;
  public:
  ::google::protobuf::Metadata GetMetadata() const;

  // nested types ----------------------------------------------------

  // accessors -------------------------------------------------------

  // required string text = 1;
  inline bool has_text() const;
  inline void clear_text();
  static const int kTextFieldNumber = 1;
  inline const ::std::string& text() const;
  inline void set_text(const ::std::string& value);
  inline void set_text(const char* value);
  inline void set_text(const char* value, size_t size);
  inline ::std::string* mutable_text();
  inline ::std::string* release_text();
  inline void set_allocated_text(::std::string* text);

  // optional int32 times = 2;
  inline bool has_times() const;
  inline void clear_times();
  static const int kTimesFieldNumber = 2;
  inline ::google::protobuf::int32 times() const;
  inline void set_times(::google::protobuf::int32 value);

  // @@protoc_insertion_point(class_scope:FooRequest)
 private:
  inline void set_has_text();
  inline void clear_has_text();
  inline void set_has_times();
  inline void clear_has_times();

  ::google::protobuf::UnknownFieldSet _unknown_fields_;

  ::google::protobuf::uint32 _has_bits_[1];
  mutable int _cached_size_;
  ::std::string* text_;
  ::google::protobuf::int32 times_;
  friend void  protobuf_AddDesc_foo_2eproto();
  friend void protobuf_AssignDesc_foo_2eproto();
  friend void protobuf_ShutdownFile_foo_2eproto();

  void InitAsDefaultInstance();
  static FooRequest* default_instance_;
};
// -------------------------------------------------------------------

class FooResponse : public ::google::protobuf::Message {
 public:
  FooResponse();
  virtual ~FooResponse();

  FooResponse(const FooResponse& from);

  inline FooResponse& operator=(const FooResponse& from) {
    CopyFrom(from);
    return *this;
  }

  inline const ::google::protobuf::UnknownFieldSet& unknown_fields() const {
    return _unknown_fields_;
  }

  inline ::google::protobuf::UnknownFieldSet* mutable_unknown_fields() {
    return &_unknown_fields_;
  }

  static const ::google::protobuf::Descriptor* descriptor();
  static const FooResponse& default_instance();

  void Swap(FooResponse* other);

  // implements Message ----------------------------------------------

  FooResponse* New() const;
  void CopyFrom(const ::google::protobuf::Message& from);
  void MergeFrom(const ::google::protobuf::Message& from);
  void CopyFrom(const FooResponse& from);
  void MergeFrom(const FooResponse& from);
  void Clear();
  bool IsInitialized() const;

  int ByteSize() const;
  bool MergePartialFromCodedStream(
      ::google::protobuf::io::CodedInputStream* input);
  void SerializeWithCachedSizes(
      ::google::protobuf::io::CodedOutputStream* output) const;
  ::google::protobuf::uint8* SerializeWithCachedSizesToArray(::google::protobuf::uint8* output) const;
  int GetCachedSize() const { return _cached_size_; }
  private:
  void SharedCtor();
  void SharedDtor();
  void SetCachedSize(int size) const;
  public:
  ::google::protobuf::Metadata GetMetadata() const;

  // nested types ----------------------------------------------------

  // accessors -------------------------------------------------------

  // required string text = 1;
  inline bool has_text() const;
  inline void clear_text();
  static const int kTextFieldNumber = 1;
  inline const ::std::string& text() const;
  inline void set_text(const ::std::string& value);
  inline void set_text(const char* value);
  inline void set_text(const char* value, size_t size);
  inline ::std::string* mutable_text();
  inline ::std::string* release_text();
  inline void set_allocated_text(::std::string* text);

  // optional bool result = 2;
  inline bool has_result() const;
  inline void clear_result();
  static const int kResultFieldNumber = 2;
  inline bool result() const;
  inline void set_result(bool value);

  // @@protoc_insertion_point(class_scope:FooResponse)
 private:
  inline void set_has_text();
  inline void clear_has_text();
  inline void set_has_result();
  inline void clear_has_result();

  ::google::protobuf::UnknownFieldSet _unknown_fields_;

  ::google::protobuf::uint32 _has_bits_[1];
  mutable int _cached_size_;
  ::std::string* text_;
  bool result_;
  friend void  protobuf_AddDesc_foo_2eproto();
  friend void protobuf_AssignDesc_foo_2eproto();
  friend void protobuf_ShutdownFile_foo_2eproto();

  void InitAsDefaultInstance();
  static FooResponse* default_instance_;
};
// ===================================================================

class FooService_Stub;

class FooService : public ::google::protobuf::Service {
 protected:
  // This class should be treated as an abstract interface.
  inline FooService() {};
 public:
  virtual ~FooService();

  typedef FooService_Stub Stub;

  static const ::google::protobuf::ServiceDescriptor* descriptor();

  virtual void Foo(::google::protobuf::RpcController* controller,
                       const ::FooRequest* request,
                       ::FooResponse* response,
                       ::google::protobuf::Closure* done);

  // implements Service ----------------------------------------------

  const ::google::protobuf::ServiceDescriptor* GetDescriptor();
  void CallMethod(const ::google::protobuf::MethodDescriptor* method,
                  ::google::protobuf::RpcController* controller,
                  const ::google::protobuf::Message* request,
                  ::google::protobuf::Message* response,
                  ::google::protobuf::Closure* done);
  const ::google::protobuf::Message& GetRequestPrototype(
    const ::google::protobuf::MethodDescriptor* method) const;
  const ::google::protobuf::Message& GetResponsePrototype(
    const ::google::protobuf::MethodDescriptor* method) const;

 private:
  GOOGLE_DISALLOW_EVIL_CONSTRUCTORS(FooService);
};

class FooService_Stub : public FooService {
 public:
  FooService_Stub(::google::protobuf::RpcChannel* channel);
  FooService_Stub(::google::protobuf::RpcChannel* channel,
                   ::google::protobuf::Service::ChannelOwnership ownership);
  ~FooService_Stub();

  inline ::google::protobuf::RpcChannel* channel() { return channel_; }

  // implements FooService ------------------------------------------

  void Foo(::google::protobuf::RpcController* controller,
                       const ::FooRequest* request,
                       ::FooResponse* response,
                       ::google::protobuf::Closure* done);
 private:
  ::google::protobuf::RpcChannel* channel_;
  bool owns_channel_;
  GOOGLE_DISALLOW_EVIL_CONSTRUCTORS(FooService_Stub);
};


// ===================================================================


// ===================================================================

// FooRequest

// required string text = 1;
inline bool FooRequest::has_text() const {
  return (_has_bits_[0] & 0x00000001u) != 0;
}
inline void FooRequest::set_has_text() {
  _has_bits_[0] |= 0x00000001u;
}
inline void FooRequest::clear_has_text() {
  _has_bits_[0] &= ~0x00000001u;
}
inline void FooRequest::clear_text() {
  if (text_ != &::google::protobuf::internal::GetEmptyStringAlreadyInited()) {
    text_->clear();
  }
  clear_has_text();
}
inline const ::std::string& FooRequest::text() const {
  // @@protoc_insertion_point(field_get:FooRequest.text)
  return *text_;
}
inline void FooRequest::set_text(const ::std::string& value) {
  set_has_text();
  if (text_ == &::google::protobuf::internal::GetEmptyStringAlreadyInited()) {
    text_ = new ::std::string;
  }
  text_->assign(value);
  // @@protoc_insertion_point(field_set:FooRequest.text)
}
inline void FooRequest::set_text(const char* value) {
  set_has_text();
  if (text_ == &::google::protobuf::internal::GetEmptyStringAlreadyInited()) {
    text_ = new ::std::string;
  }
  text_->assign(value);
  // @@protoc_insertion_point(field_set_char:FooRequest.text)
}
inline void FooRequest::set_text(const char* value, size_t size) {
  set_has_text();
  if (text_ == &::google::protobuf::internal::GetEmptyStringAlreadyInited()) {
    text_ = new ::std::string;
  }
  text_->assign(reinterpret_cast<const char*>(value), size);
  // @@protoc_insertion_point(field_set_pointer:FooRequest.text)
}
inline ::std::string* FooRequest::mutable_text() {
  set_has_text();
  if (text_ == &::google::protobuf::internal::GetEmptyStringAlreadyInited()) {
    text_ = new ::std::string;
  }
  // @@protoc_insertion_point(field_mutable:FooRequest.text)
  return text_;
}
inline ::std::string* FooRequest::release_text() {
  clear_has_text();
  if (text_ == &::google::protobuf::internal::GetEmptyStringAlreadyInited()) {
    return NULL;
  } else {
    ::std::string* temp = text_;
    text_ = const_cast< ::std::string*>(&::google::protobuf::internal::GetEmptyStringAlreadyInited());
    return temp;
  }
}
inline void FooRequest::set_allocated_text(::std::string* text) {
  if (text_ != &::google::protobuf::internal::GetEmptyStringAlreadyInited()) {
    delete text_;
  }
  if (text) {
    set_has_text();
    text_ = text;
  } else {
    clear_has_text();
    text_ = const_cast< ::std::string*>(&::google::protobuf::internal::GetEmptyStringAlreadyInited());
  }
  // @@protoc_insertion_point(field_set_allocated:FooRequest.text)
}

// optional int32 times = 2;
inline bool FooRequest::has_times() const {
  return (_has_bits_[0] & 0x00000002u) != 0;
}
inline void FooRequest::set_has_times() {
  _has_bits_[0] |= 0x00000002u;
}
inline void FooRequest::clear_has_times() {
  _has_bits_[0] &= ~0x00000002u;
}
inline void FooRequest::clear_times() {
  times_ = 0;
  clear_has_times();
}
inline ::google::protobuf::int32 FooRequest::times() const {
  // @@protoc_insertion_point(field_get:FooRequest.times)
  return times_;
}
inline void FooRequest::set_times(::google::protobuf::int32 value) {
  set_has_times();
  times_ = value;
  // @@protoc_insertion_point(field_set:FooRequest.times)
}

// -------------------------------------------------------------------

// FooResponse

// required string text = 1;
inline bool FooResponse::has_text() const {
  return (_has_bits_[0] & 0x00000001u) != 0;
}
inline void FooResponse::set_has_text() {
  _has_bits_[0] |= 0x00000001u;
}
inline void FooResponse::clear_has_text() {
  _has_bits_[0] &= ~0x00000001u;
}
inline void FooResponse::clear_text() {
  if (text_ != &::google::protobuf::internal::GetEmptyStringAlreadyInited()) {
    text_->clear();
  }
  clear_has_text();
}
inline const ::std::string& FooResponse::text() const {
  // @@protoc_insertion_point(field_get:FooResponse.text)
  return *text_;
}
inline void FooResponse::set_text(const ::std::string& value) {
  set_has_text();
  if (text_ == &::google::protobuf::internal::GetEmptyStringAlreadyInited()) {
    text_ = new ::std::string;
  }
  text_->assign(value);
  // @@protoc_insertion_point(field_set:FooResponse.text)
}
inline void FooResponse::set_text(const char* value) {
  set_has_text();
  if (text_ == &::google::protobuf::internal::GetEmptyStringAlreadyInited()) {
    text_ = new ::std::string;
  }
  text_->assign(value);
  // @@protoc_insertion_point(field_set_char:FooResponse.text)
}
inline void FooResponse::set_text(const char* value, size_t size) {
  set_has_text();
  if (text_ == &::google::protobuf::internal::GetEmptyStringAlreadyInited()) {
    text_ = new ::std::string;
  }
  text_->assign(reinterpret_cast<const char*>(value), size);
  // @@protoc_insertion_point(field_set_pointer:FooResponse.text)
}
inline ::std::string* FooResponse::mutable_text() {
  set_has_text();
  if (text_ == &::google::protobuf::internal::GetEmptyStringAlreadyInited()) {
    text_ = new ::std::string;
  }
  // @@protoc_insertion_point(field_mutable:FooResponse.text)
  return text_;
}
inline ::std::string* FooResponse::release_text() {
  clear_has_text();
  if (text_ == &::google::protobuf::internal::GetEmptyStringAlreadyInited()) {
    return NULL;
  } else {
    ::std::string* temp = text_;
    text_ = const_cast< ::std::string*>(&::google::protobuf::internal::GetEmptyStringAlreadyInited());
    return temp;
  }
}
inline void FooResponse::set_allocated_text(::std::string* text) {
  if (text_ != &::google::protobuf::internal::GetEmptyStringAlreadyInited()) {
    delete text_;
  }
  if (text) {
    set_has_text();
    text_ = text;
  } else {
    clear_has_text();
    text_ = const_cast< ::std::string*>(&::google::protobuf::internal::GetEmptyStringAlreadyInited());
  }
  // @@protoc_insertion_point(field_set_allocated:FooResponse.text)
}

// optional bool result = 2;
inline bool FooResponse::has_result() const {
  return (_has_bits_[0] & 0x00000002u) != 0;
}
inline void FooResponse::set_has_result() {
  _has_bits_[0] |= 0x00000002u;
}
inline void FooResponse::clear_has_result() {
  _has_bits_[0] &= ~0x00000002u;
}
inline void FooResponse::clear_result() {
  result_ = false;
  clear_has_result();
}
inline bool FooResponse::result() const {
  // @@protoc_insertion_point(field_get:FooResponse.result)
  return result_;
}
inline void FooResponse::set_result(bool value) {
  set_has_result();
  result_ = value;
  // @@protoc_insertion_point(field_set:FooResponse.result)
}


// @@protoc_insertion_point(namespace_scope)

#ifndef SWIG
namespace google {
namespace protobuf {


}  // namespace google
}  // namespace protobuf
#endif  // SWIG

// @@protoc_insertion_point(global_scope)

#endif  // PROTOBUF_foo_2eproto__INCLUDED
