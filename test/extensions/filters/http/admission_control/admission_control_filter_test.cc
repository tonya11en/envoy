#include <chrono>

#include "envoy/extensions/filters/http/admission_control/v3alpha/admission_control.pb.h"
#include "envoy/extensions/filters/http/admission_control/v3alpha/admission_control.pb.validate.h"

#include "common/stats/isolated_store_impl.h"

#include "extensions/filters/http/admission_control/admission_control.h"

#include "test/mocks/runtime/mocks.h"
#include "test/mocks/server/mocks.h"
#include "test/mocks/thread_local/mocks.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::NiceMock;
using testing::Return;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AdmissionControl {
namespace {

class MockThreadLocalController : public ThreadLocal::ThreadLocalObject,
                                  public ThreadLocalController {
public:
  MockThreadLocalController() {}
  MOCK_METHOD(uint32_t, requestTotalCount, (), (override));
  MOCK_METHOD(uint32_t, requestSuccessCount, (), (override));
  MOCK_METHOD(void, recordSuccess, (), (override));
  MOCK_METHOD(void, recordFailure, (), (override));
};

class TestConfig : public AdmissionControlFilterConfig {
public:
  TestConfig(const AdmissionControlProto& proto_config, Runtime::Loader& runtime,
             TimeSource& time_source, Runtime::RandomGenerator& random, Stats::Scope& scope,
             ThreadLocal::SlotPtr&& tls, MockThreadLocalController& controller)
      : AdmissionControlFilterConfig(proto_config, runtime, time_source, random, scope,
                                     std::move(tls)),
        controller_(controller) {}
  virtual ThreadLocalController& getController() const override { return controller_; }

private:
  MockThreadLocalController& controller_;
};

class AdmissionControlTest : public testing::Test {
public:
  AdmissionControlTest() {}

  std::shared_ptr<AdmissionControlFilterConfig> makeConfig(const std::string& yaml) {
    AdmissionControlProto proto;
    TestUtility::loadFromYamlAndValidate(yaml, proto);
    auto tls = context_.threadLocal().allocateSlot();
    return std::make_shared<TestConfig>(proto, runtime_, time_system_, random_, scope_,
                                        std::move(tls), controller_);
  }

  void setupFilter(std::shared_ptr<AdmissionControlFilterConfig> config) {
    filter_ = std::make_shared<AdmissionControlFilter>(config, "test_prefix.");
    filter_->setDecoderFilterCallbacks(decoder_callbacks_);
  }

  void sampleGrpcRequest(std::string&& grpc_status) {
    Http::TestResponseHeaderMapImpl headers{{"content-type", "application/grpc"},
                                            {"grpc-status", grpc_status}};
    filter_->encodeHeaders(headers, true);
  }

  void sampleHttpRequest(std::string&& http_error_code) {
    Http::TestResponseHeaderMapImpl headers{{":status", http_error_code}};
    filter_->encodeHeaders(headers, true);
  }

  void expectHttpSuccess(std::string&& code) {
    Http::RequestHeaderMapImpl request_headers;
    EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));
    EXPECT_CALL(controller_, recordSuccess());
    sampleHttpRequest(std::move(code));
  }

  void expectHttpFail(std::string&& code) {
    Http::RequestHeaderMapImpl request_headers;
    EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));
    EXPECT_CALL(controller_, recordFailure());
    sampleHttpRequest(std::move(code));
  }

  void expectGrpcSuccess(std::string&& code) {
    Http::RequestHeaderMapImpl request_headers;
    EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));
    EXPECT_CALL(controller_, recordSuccess());
    sampleGrpcRequest(std::move(code));
  }

  void expectGrpcFail(std::string&& code) {
    Http::RequestHeaderMapImpl request_headers;
    EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));
    EXPECT_CALL(controller_, recordFailure());
    sampleGrpcRequest(std::move(code));
  }

protected:
  std::string stats_prefix_{""};
  NiceMock<Runtime::MockLoader> runtime_;
  NiceMock<Server::Configuration::MockFactoryContext> context_;
  Stats::IsolatedStoreImpl scope_;
  Event::SimulatedTimeSystem time_system_;
  NiceMock<Runtime::MockRandomGenerator> random_;
  std::shared_ptr<AdmissionControlFilter> filter_;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks_;
  MockThreadLocalController controller_;
  const std::string default_yaml_{R"EOF(
enabled:
  default_value: true
  runtime_key: "foo.enabled"
sampling_window: 10s
aggression_coefficient:
  default_value: 1.0
  runtime_key: "foo.aggression"
default_success_criteria:
  http_status:
  grpc_status:
)EOF"};
};

TEST_F(AdmissionControlTest, FilterDisabled) {
  const std::string yaml = R"EOF(
enabled:
  default_value: false
  runtime_key: "foo.enabled"
sampling_window: 10s
aggression_coefficient:
  default_value: 1.0
  runtime_key: "foo.aggression"
default_success_criteria:
  http_status:
  grpc_status:
)EOF";

  auto config = makeConfig(yaml);
  setupFilter(config);

  // Fail lots of requests so that we would normally expect a ~100% rejection rate. It should pass
  // below since the filter is disabled.
  EXPECT_CALL(controller_, requestTotalCount()).Times(0);
  EXPECT_CALL(controller_, requestSuccessCount()).Times(0);

  // We expect no rejections.
  Http::RequestHeaderMapImpl request_headers;
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));
}

TEST_F(AdmissionControlTest, DisregardHealthChecks) {
  auto config = makeConfig(default_yaml_);
  setupFilter(config);

  StreamInfo::MockStreamInfo stream_info;
  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillOnce(testing::ReturnRef(stream_info));
  EXPECT_CALL(stream_info, healthCheck()).WillOnce(Return(true));

  // Fail lots of requests so that we would normally expect a ~100% rejection rate. It should pass
  // below since the request is a healthcheck.
  EXPECT_CALL(controller_, requestTotalCount()).Times(0);
  EXPECT_CALL(controller_, requestSuccessCount()).Times(0);

  Http::TestRequestHeaderMapImpl request_headers;
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));
}

TEST_F(AdmissionControlTest, FilterBehaviorBasic) {
  auto config = makeConfig(default_yaml_);
  setupFilter(config);

  // Fail lots of requests so that we can expect a ~100% rejection rate.
  EXPECT_CALL(controller_, requestTotalCount()).WillRepeatedly(Return(1000));
  EXPECT_CALL(controller_, requestSuccessCount()).WillRepeatedly(Return(0));

  // We expect rejections due to the failure rate.
  EXPECT_EQ(0, scope_.counter("test_prefix.rq_rejected").value());
  Http::TestRequestHeaderMapImpl request_headers;
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers, true));
  EXPECT_EQ(1, scope_.counter("test_prefix.rq_rejected").value());

  // Now we pretend as if the historical data has been phased out.
  EXPECT_CALL(controller_, requestTotalCount()).WillRepeatedly(Return(0));
  EXPECT_CALL(controller_, requestSuccessCount()).WillRepeatedly(Return(0));

  // Should continue forwarding since SR has become stale and there's no additional data. This also
  // verifies that HTTP 200s are default successes.
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));
  EXPECT_CALL(controller_, recordSuccess());
  sampleHttpRequest("200");

  // Fail exactly half of the requests so we get a ~50% rejection rate.
  EXPECT_CALL(controller_, requestTotalCount()).WillRepeatedly(Return(1000));
  EXPECT_CALL(controller_, requestSuccessCount()).WillRepeatedly(Return(500));

  // Random numbers in the range [0,1e4) are considered for the rejection calculation. One request
  // should fail and the other should pass.
  EXPECT_CALL(random_, random()).WillOnce(Return(5500));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));
  EXPECT_CALL(controller_, recordFailure());
  sampleHttpRequest("503");

  EXPECT_CALL(random_, random()).WillOnce(Return(4500));
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers, true));
}

// TODO (tonya11en) more tests around logic of specifying error codes. Still WIP.

TEST_F(AdmissionControlTest, HttpErrorCodes) {
  const std::string yaml = R"EOF(
default_success_criteria:
  http_status:
    - Http3xx
  grpc_status:
)EOF";

  auto config = makeConfig(yaml);
  setupFilter(config);

  EXPECT_CALL(controller_, requestTotalCount()).WillRepeatedly(Return(0));
  EXPECT_CALL(controller_, requestSuccessCount()).WillRepeatedly(Return(0));

  setupFilter(config);
  expectHttpSuccess("300");

  setupFilter(config);
  expectHttpSuccess("301");

  setupFilter(config);
  expectHttpSuccess("302");

  setupFilter(config);
  expectHttpFail("200");

  setupFilter(config);
  expectHttpFail("400");

  setupFilter(config);
  expectHttpFail("500");
}

TEST_F(AdmissionControlTest, DefaultBehaviorTest) {
  const std::string yaml = R"EOF(
default_success_criteria:
  http_status:
  grpc_status:
)EOF";

  auto config = makeConfig(yaml);

  Http::TestRequestHeaderMapImpl request_headers;
  EXPECT_CALL(controller_, requestTotalCount()).WillRepeatedly(Return(0));
  EXPECT_CALL(controller_, requestSuccessCount()).WillRepeatedly(Return(0));

  setupFilter(config);
  expectGrpcSuccess("0");
  setupFilter(config);
  expectGrpcFail("7");
  setupFilter(config);
  expectGrpcFail("14");

  // Test 200 range.
  setupFilter(config);
  expectHttpSuccess("200");
  setupFilter(config);
  expectHttpSuccess("201");
  setupFilter(config);
  expectHttpSuccess("204");

  setupFilter(config);
  expectHttpFail("300");
  setupFilter(config);
  expectHttpFail("301");
  setupFilter(config);
  expectHttpFail("404");
  setupFilter(config);
  expectHttpFail("500");
}

TEST_F(AdmissionControlTest, HttpCodeInfluence) {
  const std::string yaml = R"EOF(
default_success_criteria:
  http_status:
  grpc_status:
    - status: PERMISSION_DENIED
    - status: UNIMPLEMENTED
)EOF";

  auto config = makeConfig(yaml);

  Http::TestRequestHeaderMapImpl request_headers;
  EXPECT_CALL(controller_, requestTotalCount()).WillRepeatedly(Return(0));
  EXPECT_CALL(controller_, requestSuccessCount()).WillRepeatedly(Return(0));

  setupFilter(config);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));

  // Verify that the HTTP 200 isn't causing this request to pass as a success even though it's an
  // unsuccessful GRPC request.
  EXPECT_CALL(controller_, recordFailure());
  Http::TestResponseHeaderMapImpl headers{
      {"content-type", "application/grpc"}, {"grpc-status", "0"}, {":status", "200"}};
  filter_->encodeHeaders(headers, true);
}

TEST_F(AdmissionControlTest, HttpCodeInfluence2) {
  const std::string yaml = R"EOF(
default_success_criteria:
  http_status:
    - Http3xx
  grpc_status:
    - status: PERMISSION_DENIED
    - status: UNIMPLEMENTED
)EOF";

  auto config = makeConfig(yaml);

  Http::TestRequestHeaderMapImpl request_headers;
  EXPECT_CALL(controller_, requestTotalCount()).WillRepeatedly(Return(0));
  EXPECT_CALL(controller_, requestSuccessCount()).WillRepeatedly(Return(0));

  setupFilter(config);

  // HTTP 2xx is not considered a success, but it's returned for all of the GRPC messages, so let's
  // make sure GRPC still gets evaluated correctly.
  expectGrpcSuccess("7");
  expectGrpcFail("0");

  // Verify that the HTTP behaves correctly as well. A code of 200 counts as a failure in the
  // config, so let's make sure it actually fails without a GRPC message type.
  expectHttpFail("200");
  expectHttpSuccess("301");
}

TEST_F(AdmissionControlTest, GrpcErrorCodes) {
  const std::string yaml = R"EOF(
default_success_criteria:
  http_status:
  grpc_status:
    - status: PERMISSION_DENIED
    - status: UNIMPLEMENTED
)EOF";

  auto config = makeConfig(yaml);

  Http::TestRequestHeaderMapImpl request_headers;
  EXPECT_CALL(controller_, requestTotalCount()).WillRepeatedly(Return(0));
  EXPECT_CALL(controller_, requestSuccessCount()).WillRepeatedly(Return(0));

  setupFilter(config);
  expectGrpcFail("0");

  setupFilter(config);
  expectGrpcSuccess("7");
}

} // namespace
} // namespace AdmissionControl
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy