#pragma once

#include <string>

#include "google/protobuf/compiler/code_generator.h"

namespace cc_proto_descriptor_library {

class ProtoDescriptorGenerator : public google::protobuf::compiler::CodeGenerator {
 public:
  bool Generate(const google::protobuf::FileDescriptor* file, const std::string& parameter,
                google::protobuf::compiler::GeneratorContext* generator_context,
                std::string* error) const override;

  uint64_t GetSupportedFeatures() const override {
    return CodeGenerator::Feature::FEATURE_PROTO3_OPTIONAL;
  }
};

}  // namespace cc_proto_descriptor_library
