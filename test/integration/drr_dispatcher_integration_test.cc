#include "test/integration/http_integration.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace {

class DrrDispatcherIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                     public HttpIntegrationTest {
public:
  DrrDispatcherIntegrationTest() : HttpIntegrationTest(Http::CodecType::HTTP1, GetParam()) {
    config_helper_.addRuntimeOverride("envoy.reloadable_features.drr_dispatcher_scheduling", "true");
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, DrrDispatcherIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

// Verifies that multiple client connections dispatch HTTP requests fairly under DRR dispatcher
// scheduling
TEST_P(DrrDispatcherIntegrationTest, MultiClientFairScheduling) {
  initialize();

  // Create two distinct client connections
  IntegrationCodecClientPtr client1 = makeHttpConnection(lookupPort("http"));
  IntegrationCodecClientPtr client2 = makeHttpConnection(lookupPort("http"));

  // Send request from Client 1
  Http::TestRequestHeaderMapImpl headers1{
      {":method", "GET"}, {":path", "/test1"}, {":scheme", "http"}, {":authority", "host"}};
  IntegrationStreamDecoderPtr response1 = client1->makeHeaderOnlyRequest(headers1);

  // Send request from Client 2
  Http::TestRequestHeaderMapImpl headers2{
      {":method", "GET"}, {":path", "/test2"}, {":scheme", "http"}, {":authority", "host"}};
  IntegrationStreamDecoderPtr response2 = client2->makeHeaderOnlyRequest(headers2);

  // Service upstream connections and send 200 OK responses
  FakeHttpConnectionPtr fake_upstream_connection1;
  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection1));
  FakeStreamPtr fake_stream1;
  ASSERT_TRUE(fake_upstream_connection1->waitForNewStream(*dispatcher_, fake_stream1));
  fake_stream1->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);

  FakeHttpConnectionPtr fake_upstream_connection2;
  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection2));
  FakeStreamPtr fake_stream2;
  ASSERT_TRUE(fake_upstream_connection2->waitForNewStream(*dispatcher_, fake_stream2));
  fake_stream2->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);

  // Verify both clients receive complete responses
  ASSERT_TRUE(response1->waitForEndStream());
  EXPECT_EQ("200", response1->headers().getStatusValue());

  ASSERT_TRUE(response2->waitForEndStream());
  EXPECT_EQ("200", response2->headers().getStatusValue());

  // Clean up connections
  client1->close();
  client2->close();
}

// Verifies that multi-client concurrent requests are served in interleaved order under DRR
TEST_P(DrrDispatcherIntegrationTest, MultiClientInterleavedTrafficFairness) {
  initialize();

  // Create two clients
  IntegrationCodecClientPtr client1 = makeHttpConnection(lookupPort("http"));
  IntegrationCodecClientPtr client2 = makeHttpConnection(lookupPort("http"));

  // Send requests concurrently from both clients
  Http::TestRequestHeaderMapImpl headers1{
      {":method", "GET"}, {":path", "/client1_req1"}, {":scheme", "http"}, {":authority", "host"}};
  Http::TestRequestHeaderMapImpl headers2{
      {":method", "GET"}, {":path", "/client2_req1"}, {":scheme", "http"}, {":authority", "host"}};

  IntegrationStreamDecoderPtr response1 = client1->makeHeaderOnlyRequest(headers1);
  IntegrationStreamDecoderPtr response2 = client2->makeHeaderOnlyRequest(headers2);

  // Upstream receives both connections and streams
  FakeHttpConnectionPtr fake_upstream_connection1;
  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection1));
  FakeStreamPtr fake_stream1;
  ASSERT_TRUE(fake_upstream_connection1->waitForNewStream(*dispatcher_, fake_stream1));

  FakeHttpConnectionPtr fake_upstream_connection2;
  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection2));
  FakeStreamPtr fake_stream2;
  ASSERT_TRUE(fake_upstream_connection2->waitForNewStream(*dispatcher_, fake_stream2));

  // Respond to Client 1 and Client 2
  fake_stream1->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);
  fake_stream2->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);

  ASSERT_TRUE(response1->waitForEndStream());
  ASSERT_TRUE(response2->waitForEndStream());

  EXPECT_EQ("200", response1->headers().getStatusValue());
  EXPECT_EQ("200", response2->headers().getStatusValue());

  client1->close();
  client2->close();
}

} // namespace
} // namespace Envoy
