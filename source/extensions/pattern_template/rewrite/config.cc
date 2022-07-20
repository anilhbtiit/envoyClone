#include "source/extensions/pattern_template/rewrite/config.h"

#include "envoy/registry/registry.h"
#include "envoy/router/pattern_template.h"

namespace Envoy {
namespace Extensions {
namespace PatternTemplate {
namespace Rewrite {

REGISTER_FACTORY(PatternTemplateRewritePredicateFactory,
                 Router::PatternTemplateRewritePredicateFactory);
} // namespace Rewrite
} // namespace PatternTemplate
} // namespace Extensions
} // namespace Envoy
