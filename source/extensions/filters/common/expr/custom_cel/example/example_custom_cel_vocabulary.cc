#include "source/extensions/filters/common/expr/custom_cel/example/example_custom_cel_vocabulary.h"

#include "envoy/extensions/expr/custom_cel_vocabulary/example/v3/config.pb.h"
#include "envoy/extensions/expr/custom_cel_vocabulary/example/v3/config.pb.validate.h"
#include "envoy/registry/registry.h"

#include "source/extensions/filters/common/expr/custom_cel/custom_cel_vocabulary.h"
#include "source/extensions/filters/common/expr/custom_cel/example/custom_cel_functions.h"
#include "source/extensions/filters/common/expr/custom_cel/example/custom_cel_variables.h"

#include "eval/public/activation.h"
#include "eval/public/cel_function_adapter.h"
#include "eval/public/cel_value.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace Expr {
namespace Custom_Cel {
namespace Example {

void ThrowException(absl::string_view function_name, absl::Status status);

void ExampleCustomCelVocabulary::fillActivation(Activation* activation, Protobuf::Arena& arena,
                                                const StreamInfo::StreamInfo& info,
                                                const Http::RequestHeaderMap* request_headers,
                                                const Http::ResponseHeaderMap* response_headers,
                                                const Http::ResponseTrailerMap* response_trailers) {
  setRequestHeaders(request_headers);
  setResponseHeaders(response_headers);
  setResponseTrailers(response_trailers);
  // variables
  activation->InsertValueProducer(
      CustomCelVariablesSetName,
      std::make_unique<CustomCelVariablesWrapper>(arena, info, request_headers, response_headers,
                                                  response_trailers));
  // Lazily evaluated functions only
  absl::Status status;
  status =
      activation->InsertFunction(std::make_unique<getDoubleCelFunction>(LazyEvalFuncNameGetDouble));
  if (!status.ok()) {
    ThrowException(LazyEvalFuncNameGetDouble, status);
  }
  status = activation->InsertFunction(
      std::make_unique<getProductCelFunction>(LazyEvalFuncNameGetProduct));
  if (!status.ok()) {
    ThrowException(LazyEvalFuncNameGetProduct, status);
  }
  status = activation->InsertFunction(std::make_unique<get99CelFunction>(LazyEvalFuncNameGet99));
  if (!status.ok()) {
    ThrowException(LazyEvalFuncNameGet99, status);
  }
}

void ExampleCustomCelVocabulary::registerFunctions(CelFunctionRegistry* registry) const {
  absl::Status status;
  // lazily evaluated functions
  status = registry->RegisterLazyFunction(
      getDoubleCelFunction::createDescriptor(LazyEvalFuncNameGetDouble));
  if (!status.ok()) {
    ThrowException(LazyEvalFuncNameGetDouble, status);
  }
  status = registry->RegisterLazyFunction(
      getProductCelFunction::createDescriptor(LazyEvalFuncNameGetProduct));
  if (!status.ok()) {
    ThrowException(LazyEvalFuncNameGetProduct, status);
  }
  status =
      registry->RegisterLazyFunction(get99CelFunction::createDescriptor(
          LazyEvalFuncNameGet99));
  if (!status.ok()) {
    ThrowException(LazyEvalFuncNameGet99, status);
  }

  // eagerly evaluated functions
  status = google::api::expr::runtime::FunctionAdapter<CelValue,
                                                       int64_t>::CreateAndRegister(
      EagerEvalFuncNameGetNextInt, false, getNextInt, registry);
  if (!status.ok()) {
    ThrowException(EagerEvalFuncNameGetNextInt, status);
  }
  status = google::api::expr::runtime::FunctionAdapter<CelValue,
                                                       int64_t>::CreateAndRegister(
      EagerEvalFuncNameGetSquareOf, true, getSquareOf, registry);
  if (!status.ok()) {
    ThrowException(EagerEvalFuncNameGetSquareOf, status);
  }
}

CustomCelVocabularyPtr ExampleCustomCelVocabularyFactory::createCustomCelVocabulary(
    const Protobuf::Message& config, ProtobufMessage::ValidationVisitor& validation_visitor) {
  // calling downcastAndValidate but not using the results
  // an exception will be thrown if the config is not validated
  MessageUtil::downcastAndValidate<const ExampleCustomCelVocabularyConfig&>(config,
                                                                            validation_visitor);
  return std::make_unique<ExampleCustomCelVocabulary>();
}

void ThrowException(absl::string_view function_name, absl::Status status) {
  throw EnvoyException(
      fmt::format("failed to register function '{}': {}", function_name, status.message()));
}

REGISTER_FACTORY(ExampleCustomCelVocabularyFactory, CustomCelVocabularyFactory);

} // namespace Example
} // namespace Custom_Cel
} // namespace Expr
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
