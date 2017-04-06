#pragma once

#include <fstream>

#include "common/common/logger.h"
#include "common/common/utility.h"
#include "common/http/header_map_impl.h"
#include "common/http/headers.h"
#include "common/json/json_loader.h"
#include "common/router/config_impl.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/upstream/mocks.h"
#include "test/precompiled/precompiled_test.h"
#include "test/test_common/utility.h"
#include "test/tools/router_check/json/tool_config_schemas.h"

/** Class that store the configuration parameters of the router
 * check tool extracted from a json input file
 */
class ToolConfig {
public:
  ToolConfig(){};
  void parseFromJson(const Json::ObjectPtr& check_config);

  int random_lb_value_;
  Http::TestHeaderMapImpl headers_;
};

/**
 * Router check tool to check routes returned by a router
 */
class RouterCheckTool : Logger::Loggable<Logger::Id::testing> {
public:
  RouterCheckTool(){};

  /**
   * @param config_json specifies the json config file to be loaded
   * @param schema is the json schema to validate against
   * @return bool true if router config loaded successfully
   */

  Json::ObjectPtr loadJson(const std::string& config_json, const std::string& schema);

  /**
   * @param router_config_json specifies the router config json file
   * @return bool if json file loaded successfully and ConfigImpl object created
   * successfully
   */
  bool create(const std::string& router_config_json);

  /**
   * @param expected_route_json specifies the tool config json file
   * @return bool if all routes match what is expected
   */
  bool compareEntriesInJson(const std::string& expected_route_json);

  // Set whether to print out match case details
  void setShowDetails() { details_ = true; }

private:
  bool compareCluster(ToolConfig& tool_config, const std::string expected);
  bool compareVirtualCluster(ToolConfig& tool_config, const std::string expected);
  bool compareVirtualHost(ToolConfig& tool_config, const std::string expected);
  bool compareRewriteHost(ToolConfig& tool_config, const std::string expected);
  bool compareRewritePath(ToolConfig& tool_config, const std::string expected);
  bool compareRedirectPath(ToolConfig& tool_config, const std::string expected);

  /**
   * Compare the expected and acutal route parameter values. Print out
   * match details is details_ flag is set
   * @param actual holds the acutal route returned by the router
   * @param expected holds the expected parameter value of the route
   * @return true if acutal and expected match
   */
  bool compareResults(const std::string& actual, const std::string& expected);

  bool details_{false};

  // TODO(hennna): Switch away from mocks depending on feedback
  NiceMock<Runtime::MockLoader> runtime_;
  NiceMock<Upstream::MockClusterManager> cm_;
  Router::ConfigImplPtr config_;
};
