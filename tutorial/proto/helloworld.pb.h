// Generated by the protocol buffer compiler.  DO NOT EDIT!
// source: helloworld.proto

#ifndef PROTOBUF_helloworld_2eproto__INCLUDED
#define PROTOBUF_helloworld_2eproto__INCLUDED

#include <string>

#include <google/protobuf/stubs/common.h>

#if GOOGLE_PROTOBUF_VERSION < 2006000
#error This file was generated by a newer version of protoc which is
#error incompatible with your Protocol Buffer headers.  Please update
#error your headers.
#endif
#if 2006000 < GOOGLE_PROTOBUF_MIN_PROTOC_VERSION
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
void  protobuf_AddDesc_helloworld_2eproto();
void protobuf_AssignDesc_helloworld_2eproto();
void protobuf_ShutdownFile_helloworld_2eproto();

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

  // required string msg1 = 1;
  inline bool has_msg1() const;
  inline void clear_msg1();
  static const int kMsg1FieldNumber = 1;
  inline const ::std::string& msg1() const;
  inline void set_msg1(const ::std::string& value);
  inline void set_msg1(const char* value);
  inline void set_msg1(const char* value, size_t size);
  inline ::std::string* mutable_msg1();
  inline ::std::string* release_msg1();
  inline void set_allocated_msg1(::std::string* msg1);

  // required string msg2 = 2;
  inline bool has_msg2() const;
  inline void clear_msg2();
  static const int kMsg2FieldNumber = 2;
  inline const ::std::string& msg2() const;
  inline void set_msg2(const ::std::string& value);
  inline void set_msg2(const char* value);
  inline void set_msg2(const char* value, size_t size);
  inline ::std::string* mutable_msg2();
  inline ::std::string* release_msg2();
  inline void set_allocated_msg2(::std::string* msg2);

  // @@protoc_insertion_point(class_scope:FooRequest)
 private:
  inline void set_has_msg1();
  inline void clear_has_msg1();
  inline void set_has_msg2();
  inline void clear_has_msg2();

  ::google::protobuf::UnknownFieldSet _unknown_fields_;

  ::google::protobuf::uint32 _has_bits_[1];
  mutable int _cached_size_;
  ::std::string* msg1_;
  ::std::string* msg2_;
  friend void  protobuf_AddDesc_helloworld_2eproto();
  friend void protobuf_AssignDesc_helloworld_2eproto();
  friend void protobuf_ShutdownFile_helloworld_2eproto();

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

  // required string msg = 1;
  inline bool has_msg() const;
  inline void clear_msg();
  static const int kMsgFieldNumber = 1;
  inline const ::std::string& msg() const;
  inline void set_msg(const ::std::string& value);
  inline void set_msg(const char* value);
  inline void set_msg(const char* value, size_t size);
  inline ::std::string* mutable_msg();
  inline ::std::string* release_msg();
  inline void set_allocated_msg(::std::string* msg);

  // @@protoc_insertion_point(class_scope:FooResponse)
 private:
  inline void set_has_msg();
  inline void clear_has_msg();

  ::google::protobuf::UnknownFieldSet _unknown_fields_;

  ::google::protobuf::uint32 _has_bits_[1];
  mutable int _cached_size_;
  ::std::string* msg_;
  friend void  protobuf_AddDesc_helloworld_2eproto();
  friend void protobuf_AssignDesc_helloworld_2eproto();
  friend void protobuf_ShutdownFile_helloworld_2eproto();

  void InitAsDefaultInstance();
  static FooResponse* default_instance_;
};
// ===================================================================

class HelloWorldService_Stub;

class HelloWorldService : public ::google::protobuf::Service {
 protected:
  // This class should be treated as an abstract interface.
  inline HelloWorldService() {};
 public:
  virtual ~HelloWorldService();

  typedef HelloWorldService_Stub Stub;

  static const ::google::protobuf::ServiceDescriptor* descriptor();

  virtual void foo(::google::protobuf::RpcController* controller,
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
  GOOGLE_DISALLOW_EVIL_CONSTRUCTORS(HelloWorldService);
};

class HelloWorldService_Stub : public HelloWorldService {
 public:
  HelloWorldService_Stub(::google::protobuf::RpcChannel* channel);
  HelloWorldService_Stub(::google::protobuf::RpcChannel* channel,
                   ::google::protobuf::Service::ChannelOwnership ownership);
  ~HelloWorldService_Stub();

  inline ::google::protobuf::RpcChannel* channel() { return channel_; }

  // implements HelloWorldService ------------------------------------------

  void foo(::google::protobuf::RpcController* controller,
                       const ::FooRequest* request,
                       ::FooResponse* response,
                       ::google::protobuf::Closure* done);
 private:
  ::google::protobuf::RpcChannel* channel_;
  bool owns_channel_;
  GOOGLE_DISALLOW_EVIL_CONSTRUCTORS(HelloWorldService_Stub);
};


// ===================================================================


// ===================================================================

// FooRequest

// required string msg1 = 1;
inline bool FooRequest::has_msg1() const {
  return (_has_bits_[0] & 0x00000001u) != 0;
}
inline void FooRequest::set_has_msg1() {
  _has_bits_[0] |= 0x00000001u;
}
inline void FooRequest::clear_has_msg1() {
  _has_bits_[0] &= ~0x00000001u;
}
inline void FooRequest::clear_msg1() {
  if (msg1_ != &::google::protobuf::internal::GetEmptyStringAlreadyInited()) {
    msg1_->clear();
  }
  clear_has_msg1();
}
inline const ::std::string& FooRequest::msg1() const {
  // @@protoc_insertion_point(field_get:FooRequest.msg1)
  return *msg1_;
}
inline void FooRequest::set_msg1(const ::std::string& value) {
  set_has_msg1();
  if (msg1_ == &::google::protobuf::internal::GetEmptyStringAlreadyInited()) {
    msg1_ = new ::std::string;
  }
  msg1_->assign(value);
  // @@protoc_insertion_point(field_set:FooRequest.msg1)
}
inline void FooRequest::set_msg1(const char* value) {
  set_has_msg1();
  if (msg1_ == &::google::protobuf::internal::GetEmptyStringAlreadyInited()) {
    msg1_ = new ::std::string;
  }
  msg1_->assign(value);
  // @@protoc_insertion_point(field_set_char:FooRequest.msg1)
}
inline void FooRequest::set_msg1(const char* value, size_t size) {
  set_has_msg1();
  if (msg1_ == &::google::protobuf::internal::GetEmptyStringAlreadyInited()) {
    msg1_ = new ::std::string;
  }
  msg1_->assign(reinterpret_cast<const char*>(value), size);
  // @@protoc_insertion_point(field_set_pointer:FooRequest.msg1)
}
inline ::std::string* FooRequest::mutable_msg1() {
  set_has_msg1();
  if (msg1_ == &::google::protobuf::internal::GetEmptyStringAlreadyInited()) {
    msg1_ = new ::std::string;
  }
  // @@protoc_insertion_point(field_mutable:FooRequest.msg1)
  return msg1_;
}
inline ::std::string* FooRequest::release_msg1() {
  clear_has_msg1();
  if (msg1_ == &::google::protobuf::internal::GetEmptyStringAlreadyInited()) {
    return NULL;
  } else {
    ::std::string* temp = msg1_;
    msg1_ = const_cast< ::std::string*>(&::google::protobuf::internal::GetEmptyStringAlreadyInited());
    return temp;
  }
}
inline void FooRequest::set_allocated_msg1(::std::string* msg1) {
  if (msg1_ != &::google::protobuf::internal::GetEmptyStringAlreadyInited()) {
    delete msg1_;
  }
  if (msg1) {
    set_has_msg1();
    msg1_ = msg1;
  } else {
    clear_has_msg1();
    msg1_ = const_cast< ::std::string*>(&::google::protobuf::internal::GetEmptyStringAlreadyInited());
  }
  // @@protoc_insertion_point(field_set_allocated:FooRequest.msg1)
}

// required string msg2 = 2;
inline bool FooRequest::has_msg2() const {
  return (_has_bits_[0] & 0x00000002u) != 0;
}
inline void FooRequest::set_has_msg2() {
  _has_bits_[0] |= 0x00000002u;
}
inline void FooRequest::clear_has_msg2() {
  _has_bits_[0] &= ~0x00000002u;
}
inline void FooRequest::clear_msg2() {
  if (msg2_ != &::google::protobuf::internal::GetEmptyStringAlreadyInited()) {
    msg2_->clear();
  }
  clear_has_msg2();
}
inline const ::std::string& FooRequest::msg2() const {
  // @@protoc_insertion_point(field_get:FooRequest.msg2)
  return *msg2_;
}
inline void FooRequest::set_msg2(const ::std::string& value) {
  set_has_msg2();
  if (msg2_ == &::google::protobuf::internal::GetEmptyStringAlreadyInited()) {
    msg2_ = new ::std::string;
  }
  msg2_->assign(value);
  // @@protoc_insertion_point(field_set:FooRequest.msg2)
}
inline void FooRequest::set_msg2(const char* value) {
  set_has_msg2();
  if (msg2_ == &::google::protobuf::internal::GetEmptyStringAlreadyInited()) {
    msg2_ = new ::std::string;
  }
  msg2_->assign(value);
  // @@protoc_insertion_point(field_set_char:FooRequest.msg2)
}
inline void FooRequest::set_msg2(const char* value, size_t size) {
  set_has_msg2();
  if (msg2_ == &::google::protobuf::internal::GetEmptyStringAlreadyInited()) {
    msg2_ = new ::std::string;
  }
  msg2_->assign(reinterpret_cast<const char*>(value), size);
  // @@protoc_insertion_point(field_set_pointer:FooRequest.msg2)
}
inline ::std::string* FooRequest::mutable_msg2() {
  set_has_msg2();
  if (msg2_ == &::google::protobuf::internal::GetEmptyStringAlreadyInited()) {
    msg2_ = new ::std::string;
  }
  // @@protoc_insertion_point(field_mutable:FooRequest.msg2)
  return msg2_;
}
inline ::std::string* FooRequest::release_msg2() {
  clear_has_msg2();
  if (msg2_ == &::google::protobuf::internal::GetEmptyStringAlreadyInited()) {
    return NULL;
  } else {
    ::std::string* temp = msg2_;
    msg2_ = const_cast< ::std::string*>(&::google::protobuf::internal::GetEmptyStringAlreadyInited());
    return temp;
  }
}
inline void FooRequest::set_allocated_msg2(::std::string* msg2) {
  if (msg2_ != &::google::protobuf::internal::GetEmptyStringAlreadyInited()) {
    delete msg2_;
  }
  if (msg2) {
    set_has_msg2();
    msg2_ = msg2;
  } else {
    clear_has_msg2();
    msg2_ = const_cast< ::std::string*>(&::google::protobuf::internal::GetEmptyStringAlreadyInited());
  }
  // @@protoc_insertion_point(field_set_allocated:FooRequest.msg2)
}

// -------------------------------------------------------------------

// FooResponse

// required string msg = 1;
inline bool FooResponse::has_msg() const {
  return (_has_bits_[0] & 0x00000001u) != 0;
}
inline void FooResponse::set_has_msg() {
  _has_bits_[0] |= 0x00000001u;
}
inline void FooResponse::clear_has_msg() {
  _has_bits_[0] &= ~0x00000001u;
}
inline void FooResponse::clear_msg() {
  if (msg_ != &::google::protobuf::internal::GetEmptyStringAlreadyInited()) {
    msg_->clear();
  }
  clear_has_msg();
}
inline const ::std::string& FooResponse::msg() const {
  // @@protoc_insertion_point(field_get:FooResponse.msg)
  return *msg_;
}
inline void FooResponse::set_msg(const ::std::string& value) {
  set_has_msg();
  if (msg_ == &::google::protobuf::internal::GetEmptyStringAlreadyInited()) {
    msg_ = new ::std::string;
  }
  msg_->assign(value);
  // @@protoc_insertion_point(field_set:FooResponse.msg)
}
inline void FooResponse::set_msg(const char* value) {
  set_has_msg();
  if (msg_ == &::google::protobuf::internal::GetEmptyStringAlreadyInited()) {
    msg_ = new ::std::string;
  }
  msg_->assign(value);
  // @@protoc_insertion_point(field_set_char:FooResponse.msg)
}
inline void FooResponse::set_msg(const char* value, size_t size) {
  set_has_msg();
  if (msg_ == &::google::protobuf::internal::GetEmptyStringAlreadyInited()) {
    msg_ = new ::std::string;
  }
  msg_->assign(reinterpret_cast<const char*>(value), size);
  // @@protoc_insertion_point(field_set_pointer:FooResponse.msg)
}
inline ::std::string* FooResponse::mutable_msg() {
  set_has_msg();
  if (msg_ == &::google::protobuf::internal::GetEmptyStringAlreadyInited()) {
    msg_ = new ::std::string;
  }
  // @@protoc_insertion_point(field_mutable:FooResponse.msg)
  return msg_;
}
inline ::std::string* FooResponse::release_msg() {
  clear_has_msg();
  if (msg_ == &::google::protobuf::internal::GetEmptyStringAlreadyInited()) {
    return NULL;
  } else {
    ::std::string* temp = msg_;
    msg_ = const_cast< ::std::string*>(&::google::protobuf::internal::GetEmptyStringAlreadyInited());
    return temp;
  }
}
inline void FooResponse::set_allocated_msg(::std::string* msg) {
  if (msg_ != &::google::protobuf::internal::GetEmptyStringAlreadyInited()) {
    delete msg_;
  }
  if (msg) {
    set_has_msg();
    msg_ = msg;
  } else {
    clear_has_msg();
    msg_ = const_cast< ::std::string*>(&::google::protobuf::internal::GetEmptyStringAlreadyInited());
  }
  // @@protoc_insertion_point(field_set_allocated:FooResponse.msg)
}


// @@protoc_insertion_point(namespace_scope)

#ifndef SWIG
namespace google {
namespace protobuf {


}  // namespace google
}  // namespace protobuf
#endif  // SWIG

// @@protoc_insertion_point(global_scope)

#endif  // PROTOBUF_helloworld_2eproto__INCLUDED
