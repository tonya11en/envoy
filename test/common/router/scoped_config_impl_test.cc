#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/route/v3/route.pb.h"
#include "envoy/config/route/v3/scoped_route.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"

#include "source/common/protobuf/utility.h"
#include "source/common/router/scoped_config_impl.h"

#include "test/mocks/router/mocks.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Router {
namespace {

using ::Envoy::Http::TestRequestHeaderMapImpl;
using ::testing::NiceMock;

class FooFragment : public ScopeKeyFragmentBase {
public:
  uint64_t hash() const override { return 1; }
};

TEST(ScopeKeyFragmentBaseTest, EqualSign) {
  FooFragment foo;
  StringKeyFragment bar("a random string");

  EXPECT_NE(foo, bar);
}

TEST(ScopeKeyFragmentBaseTest, HashStable) {
  FooFragment foo1;
  FooFragment foo2;

  // Two FooFragments equal because their hash equals.
  EXPECT_EQ(foo1, foo2);
  EXPECT_EQ(foo1.hash(), foo2.hash());

  // Hash value doesn't change.
  StringKeyFragment a("abcdefg");
  auto hash_value = a.hash();
  for (int i = 0; i < 100; ++i) {
    EXPECT_EQ(hash_value, a.hash());
    EXPECT_EQ(StringKeyFragment("abcdefg").hash(), hash_value);
  }
}

TEST(StringKeyFragmentTest, Empty) {
  StringKeyFragment a("");
  StringKeyFragment b("");
  EXPECT_EQ(a, b);
  EXPECT_EQ(a.hash(), b.hash());

  StringKeyFragment non_empty("ABC");

  EXPECT_NE(a, non_empty);
  EXPECT_NE(a.hash(), non_empty.hash());
}

TEST(StringKeyFragmentTest, Normal) {
  StringKeyFragment str("Abc");

  StringKeyFragment same_str("Abc");
  EXPECT_EQ(str, same_str);

  StringKeyFragment upper_cased_str("ABC");
  EXPECT_NE(str, upper_cased_str);

  StringKeyFragment another_str("DEF");
  EXPECT_NE(str, another_str);
}

TEST(HeaderValueExtractorImplDeathTest, InvalidConfig) {
  ScopedRoutes::ScopeKeyBuilder::FragmentBuilder config;

  // Index non-zero when element separator is an empty string.
  std::string yaml_plain = R"EOF(
  header_value_extractor:
   name: 'foo_header'
   element_separator: ''
   index: 1
)EOF";
  TestUtility::loadFromYaml(yaml_plain, config);

  EXPECT_THROW_WITH_REGEX((FragmentBuilderImpl(config)), ProtoValidationException,
                          "Index > 0 for empty string element separator.");
  // extract_type not set.
  yaml_plain = R"EOF(
  header_value_extractor:
   name: 'foo_header'
   element_separator: ''
)EOF";
  TestUtility::loadFromYaml(yaml_plain, config);

  EXPECT_THROW_WITH_REGEX((FragmentBuilderImpl(config)), ProtoValidationException,
                          "HeaderValueExtractor extract_type not set.+");
}

TEST(HeaderValueExtractorImplTest, HeaderExtractionByIndex) {
  ScopedRoutes::ScopeKeyBuilder::FragmentBuilder config;
  std::string yaml_plain = R"EOF(
  header_value_extractor:
   name: 'foo_header'
   element_separator: ','
   index: 1
)EOF";

  TestUtility::loadFromYaml(yaml_plain, config);
  FragmentBuilderImpl extractor(std::move(config));
  std::unique_ptr<ScopeKeyFragmentBase> fragment = extractor.computeFragment(
      TestRequestHeaderMapImpl{{"foo_header", "part-0,part-1:value_bluh"}}, {}, {});

  EXPECT_NE(fragment, nullptr);
  EXPECT_EQ(*fragment, StringKeyFragment{"part-1:value_bluh"});

  // No such header.
  fragment = extractor.computeFragment(TestRequestHeaderMapImpl{{"bar_header", "part-0"}}, {}, {});
  EXPECT_EQ(fragment, nullptr);

  // Empty header value.
  fragment = extractor.computeFragment(
      TestRequestHeaderMapImpl{
          {"foo_header", ""},
      },
      {}, {});
  EXPECT_EQ(fragment, nullptr);

  // Index out of bound.
  fragment =
      extractor.computeFragment(TestRequestHeaderMapImpl{{"foo_header", "part-0"}, {}}, {}, {});
  EXPECT_EQ(fragment, nullptr);

  // Element is empty.
  fragment = extractor.computeFragment(
      TestRequestHeaderMapImpl{
          {"foo_header", "part-0,,,bluh"},
      },
      {}, {});
  EXPECT_NE(fragment, nullptr);
  EXPECT_EQ(*fragment, StringKeyFragment(""));
}

TEST(HeaderValueExtractorImplTest, HeaderExtractionByKey) {
  ScopedRoutes::ScopeKeyBuilder::FragmentBuilder config;
  std::string yaml_plain = R"EOF(
  header_value_extractor:
   name: 'foo_header'
   element_separator: ';'
   element:
    key: 'bar'
    separator: '=>'
)EOF";

  TestUtility::loadFromYaml(yaml_plain, config);
  FragmentBuilderImpl extractor(std::move(config));
  std::unique_ptr<ScopeKeyFragmentBase> fragment = extractor.computeFragment(
      TestRequestHeaderMapImpl{
          {"foo_header", "part-0;bar=>bluh;foo=>foo_value"},
      },
      {}, {});

  EXPECT_NE(fragment, nullptr);
  EXPECT_EQ(*fragment, StringKeyFragment{"bluh"});

  // No such header.
  fragment = extractor.computeFragment(
      TestRequestHeaderMapImpl{
          {"bluh", "part-0;"},
      },
      {}, {});
  EXPECT_EQ(fragment, nullptr);

  // Empty header value.
  fragment = extractor.computeFragment(
      TestRequestHeaderMapImpl{
          {"foo_header", ""},
      },
      {}, {});
  EXPECT_EQ(fragment, nullptr);

  // No such key.
  fragment = extractor.computeFragment(
      TestRequestHeaderMapImpl{
          {"foo_header", "part-0"},
      },
      {}, {});
  EXPECT_EQ(fragment, nullptr);

  // Empty value.
  fragment = extractor.computeFragment(
      TestRequestHeaderMapImpl{
          {"foo_header", "bluh;;bar=>;foo=>last_value"},
      },
      {}, {});
  EXPECT_NE(fragment, nullptr);
  EXPECT_EQ(*fragment, StringKeyFragment{""});

  // Duplicate values, the first value returned.
  fragment = extractor.computeFragment(
      TestRequestHeaderMapImpl{
          {"foo_header", "bluh;;bar=>value1;bar=>value2;bluh;;bar=>last_value"},
      },
      {}, {});
  EXPECT_NE(fragment, nullptr);
  EXPECT_EQ(*fragment, StringKeyFragment{"value1"});

  // No separator in the element, value is set to empty string.
  fragment = extractor.computeFragment(
      TestRequestHeaderMapImpl{
          {"foo_header", "bluh;;bar;bar=>value2;bluh;;bar=>last_value"},
      },
      {}, {});
  EXPECT_NE(fragment, nullptr);
  EXPECT_EQ(*fragment, StringKeyFragment{""});
}

TEST(HeaderValueExtractorImplTest, ElementSeparatorEmpty) {
  ScopedRoutes::ScopeKeyBuilder::FragmentBuilder config;
  std::string yaml_plain = R"EOF(
  header_value_extractor:
   name: 'foo_header'
   element_separator: ''
   element:
    key: 'bar'
    separator: '='
)EOF";

  TestUtility::loadFromYaml(yaml_plain, config);
  FragmentBuilderImpl extractor(std::move(config));
  std::unique_ptr<ScopeKeyFragmentBase> fragment = extractor.computeFragment(
      TestRequestHeaderMapImpl{
          {"foo_header", "bar=b;c=d;e=f"},
      },
      {}, {});
  EXPECT_NE(fragment, nullptr);
  EXPECT_EQ(*fragment, StringKeyFragment{"b;c=d;e=f"});

  fragment = extractor.computeFragment(
      TestRequestHeaderMapImpl{
          {"foo_header", "a=b;bar=d;e=f"},
      },
      {}, {});
  EXPECT_EQ(fragment, nullptr);
}

// Helper function which makes a ScopeKey from a list of strings.
ScopeKey makeKey(const std::vector<const char*>& parts) {
  ScopeKey key;
  for (const auto& part : parts) {
    key.addFragment(std::make_unique<StringKeyFragment>(part));
  }
  return key;
}

TEST(ScopeKeyDeathTest, AddNullFragment) {
  ScopeKey key;
#if !defined(NDEBUG)
  EXPECT_DEBUG_DEATH(key.addFragment(nullptr), "null fragment not allowed in ScopeKey.");
#endif
}

TEST(ScopeKeyTest, Unmatches) {
  ScopeKey key1;
  ScopeKey key2;
  // Empty key != empty key.
  EXPECT_NE(key1, key2);

  // Empty key != non-empty key.
  EXPECT_NE(key1, makeKey({""}));

  EXPECT_EQ(makeKey({"a", "b", "c"}), makeKey({"a", "b", "c"}));

  // Order matters.
  EXPECT_EQ(makeKey({"a", "b", "c"}), makeKey({"a", "b", "c"}));
  EXPECT_NE(makeKey({"a", "c", "b"}), makeKey({"a", "b", "c"}));

  // Two keys of different length won't match.
  EXPECT_NE(makeKey({"a", "b"}), makeKey({"a", "b", "c"}));

  // Case sensitive.
  EXPECT_NE(makeKey({"a", "b"}), makeKey({"A", "b"}));
}

TEST(ScopeKeyTest, Matches) {
  // An empty string fragment equals another.
  EXPECT_EQ(makeKey({"", ""}), makeKey({"", ""}));
  EXPECT_EQ(makeKey({"a", "", ""}), makeKey({"a", "", ""}));

  // Non empty fragments comparison.
  EXPECT_EQ(makeKey({"A", "b"}), makeKey({"A", "b"}));
}

TEST(ScopeKeyBuilderImplTest, Parse) {
  std::string yaml_plain = R"EOF(
  fragments:
  - header_value_extractor:
      name: 'foo_header'
      element_separator: ','
      element:
        key: 'bar'
        separator: '='
  - header_value_extractor:
      name: 'bar_header'
      element_separator: ';'
      index: 2
)EOF";

  ScopedRoutes::ScopeKeyBuilder config;
  TestUtility::loadFromYaml(yaml_plain, config);
  ScopeKeyBuilderImpl key_builder(std::move(config));

  ScopeKeyPtr key = key_builder.computeScopeKey(
      TestRequestHeaderMapImpl{
          {"foo_header", "a=b,bar=bar_value,e=f"},
          {"bar_header", "a=b;bar=bar_value;index2"},
      },
      {}, {});
  EXPECT_NE(key, nullptr);
  EXPECT_EQ(*key, makeKey({"bar_value", "index2"}));

  // Empty string fragment is fine.
  key = key_builder.computeScopeKey(
      TestRequestHeaderMapImpl{
          {"foo_header", "a=b,bar,e=f"},
          {"bar_header", "a=b;bar=bar_value;"},
      },
      {}, {});
  EXPECT_NE(key, nullptr);
  EXPECT_EQ(*key, makeKey({"", ""}));

  // Key not found.
  key = key_builder.computeScopeKey(
      TestRequestHeaderMapImpl{
          {"foo_header", "a=b,meh,e=f"},
          {"bar_header", "a=b;bar=bar_value;"},
      },
      {}, {});
  EXPECT_EQ(key, nullptr);

  // Index out of bound.
  key = key_builder.computeScopeKey(
      TestRequestHeaderMapImpl{
          {"foo_header", "a=b,bar=bar_value,e=f"},
          {"bar_header", "a=b;bar=bar_value"},
      },
      {}, {});
  EXPECT_EQ(key, nullptr);

  // Header missing.
  key = key_builder.computeScopeKey(
      TestRequestHeaderMapImpl{
          {"foo_header", "a=b,bar=bar_value,e=f"},
          {"foobar_header", "a=b;bar=bar_value;index2"},
      },
      {}, {});
  EXPECT_EQ(key, nullptr);

  // Header value empty.
  key = key_builder.computeScopeKey(
      TestRequestHeaderMapImpl{
          {"foo_header", ""},
          {"bar_header", "a=b;bar=bar_value;index2"},
      },
      {}, {});
  EXPECT_EQ(key, nullptr);

  // Case sensitive.
  key = key_builder.computeScopeKey(
      TestRequestHeaderMapImpl{
          {"foo_header", "a=b,Bar=bar_value,e=f"},
          {"bar_header", "a=b;bar=bar_value;index2"},
      },
      {}, {});
  EXPECT_EQ(key, nullptr);
}

class ScopedRouteInfoTest : public testing::Test {
public:
  void SetUp() override {
    std::string yaml_plain = R"EOF(
    name: foo_scope
    route_configuration_name: foo_route
    key:
      fragments:
        - string_key: foo
        - string_key: bar
)EOF";
    TestUtility::loadFromYaml(yaml_plain, scoped_route_config_);

    route_config_ = std::make_shared<NiceMock<MockConfig>>();
    route_config_->name_ = "foo_route";
  }

  envoy::config::route::v3::RouteConfiguration route_configuration_;
  envoy::config::route::v3::ScopedRouteConfiguration scoped_route_config_;
  std::shared_ptr<MockConfig> route_config_;
  std::unique_ptr<ScopedRouteInfo> info_;
};

TEST_F(ScopedRouteInfoTest, Creation) {
  envoy::config::route::v3::ScopedRouteConfiguration config_copy = scoped_route_config_;
  info_ = std::make_unique<ScopedRouteInfo>(std::move(scoped_route_config_), route_config_);
  EXPECT_EQ(info_->routeConfig().get(), route_config_.get());
  EXPECT_TRUE(TestUtility::protoEqual(info_->configProto(), config_copy));
  EXPECT_EQ(info_->scopeName(), "foo_scope");
  EXPECT_EQ(info_->scopeKey(), makeKey({"foo", "bar"}));
}

class ScopedConfigImplTest : public testing::Test {
public:
  void SetUp() override {
    std::string yaml_plain = R"EOF(
  fragments:
  - header_value_extractor:
      name: 'foo_header'
      element_separator: ','
      element:
        key: 'bar'
        separator: '='
  - header_value_extractor:
      name: 'bar_header'
      element_separator: ';'
      index: 2
)EOF";
    TestUtility::loadFromYaml(yaml_plain, key_builder_config_);

    scope_info_a_ = makeScopedRouteInfo(R"EOF(
    name: foo_scope
    route_configuration_name: foo_route
    key:
      fragments:
        - string_key: foo
        - string_key: bar
)EOF");
    scope_info_a_v2_ = makeScopedRouteInfo(R"EOF(
    name: foo_scope
    route_configuration_name: foo_route
    key:
      fragments:
        - string_key: xyz
        - string_key: xyz
)EOF");
    scope_info_b_ = makeScopedRouteInfo(R"EOF(
    name: bar_scope
    route_configuration_name: bar_route
    key:
      fragments:
        - string_key: bar
        - string_key: baz
)EOF");
  }
  std::shared_ptr<ScopedRouteInfo> makeScopedRouteInfo(const std::string& route_config_yaml) {
    envoy::config::route::v3::ScopedRouteConfiguration scoped_route_config;
    TestUtility::loadFromYaml(route_config_yaml, scoped_route_config);

    std::shared_ptr<MockConfig> route_config = std::make_shared<NiceMock<MockConfig>>();
    route_config->name_ = scoped_route_config.route_configuration_name();
    return std::make_shared<ScopedRouteInfo>(std::move(scoped_route_config),
                                             std::move(route_config));
  }

  std::shared_ptr<ScopedRouteInfo> scope_info_a_;
  std::shared_ptr<ScopedRouteInfo> scope_info_a_v2_;
  std::shared_ptr<ScopedRouteInfo> scope_info_b_;
  ScopedRoutes::ScopeKeyBuilder key_builder_config_;
  std::unique_ptr<ScopedConfigImpl> scoped_config_impl_;
};

// Test a ScopedConfigImpl returns the correct route Config.
TEST_F(ScopedConfigImplTest, PickRoute) {
  scoped_config_impl_ = std::make_unique<ScopedConfigImpl>(std::move(key_builder_config_));
  scoped_config_impl_->addOrUpdateRoutingScopes({scope_info_a_});
  scoped_config_impl_->addOrUpdateRoutingScopes({scope_info_b_});

  // Key (foo, bar) maps to scope_info_a_.
  ConfigConstSharedPtr route_config = scoped_config_impl_->getRouteConfig(
      TestRequestHeaderMapImpl{
          {"foo_header", ",,key=value,bar=foo,"},
          {"bar_header", ";val1;bar;val3"},
      },
      {}, {});
  EXPECT_EQ(route_config, scope_info_a_->routeConfig());

  // Key (bar, baz) maps to scope_info_b_.
  route_config = scoped_config_impl_->getRouteConfig(
      TestRequestHeaderMapImpl{
          {"foo_header", ",,key=value,bar=bar,"},
          {"bar_header", ";val1;baz;val3"},
      },
      {}, {});
  EXPECT_EQ(route_config, scope_info_b_->routeConfig());

  // No such key (bar, NOT_BAZ).
  route_config = scoped_config_impl_->getRouteConfig(
      TestRequestHeaderMapImpl{
          {"foo_header", ",key=value,bar=bar,"},
          {"bar_header", ";val1;NOT_BAZ;val3"},
      },
      {}, {});
  EXPECT_EQ(route_config, nullptr);
}

// Test a ScopedConfigImpl returns the correct route Config before and after scope config update.
TEST_F(ScopedConfigImplTest, Update) {
  scoped_config_impl_ = std::make_unique<ScopedConfigImpl>(std::move(key_builder_config_));

  TestRequestHeaderMapImpl headers{
      {"foo_header", ",,key=value,bar=foo,"},
      {"bar_header", ";val1;bar;val3"},
  };
  // Empty ScopeConfig.
  EXPECT_EQ(scoped_config_impl_->getRouteConfig(headers, {}, {}), nullptr);

  // Add scope_key (bar, baz).
  scoped_config_impl_->addOrUpdateRoutingScopes({scope_info_b_});
  // scope_info_a_ not found
  EXPECT_EQ(scoped_config_impl_->getRouteConfig(headers, {}, {}), nullptr);
  // scope_info_b_ found
  EXPECT_EQ(
      scoped_config_impl_->getRouteConfig(
          TestRequestHeaderMapImpl{{"foo_header", ",,key=v,bar=bar,"}, {"bar_header", ";val1;baz"}},
          {}, {}),
      scope_info_b_->routeConfig());

  // Add scope_key (foo, bar).
  scoped_config_impl_->addOrUpdateRoutingScopes({scope_info_a_});
  // Found scope_info_a_.
  EXPECT_EQ(scoped_config_impl_->getRouteConfig(headers, {}, {}), scope_info_a_->routeConfig());

  // Update scope foo_scope.
  scoped_config_impl_->addOrUpdateRoutingScopes({scope_info_a_v2_});
  EXPECT_EQ(scoped_config_impl_->getRouteConfig(headers, {}, {}), nullptr);

  // foo_scope now is keyed by (xyz, xyz).
  EXPECT_EQ(
      scoped_config_impl_->getRouteConfig(
          TestRequestHeaderMapImpl{{"foo_header", ",bar=xyz,foo=bar"}, {"bar_header", ";;xyz"}}, {},
          {}),
      scope_info_a_v2_->routeConfig());

  // Remove scope "foo_scope".
  scoped_config_impl_->removeRoutingScopes({"foo_scope"});
  // scope_info_a_ is gone.
  EXPECT_EQ(scoped_config_impl_->getRouteConfig(headers, {}, {}), nullptr);

  // Now delete some non-existent scopes.
  EXPECT_NO_THROW(scoped_config_impl_->removeRoutingScopes(
      {"foo_scope1", "base_scope", "bluh_scope", "xyz_scope"}));
}

class ScopedConfigImplMetadataTest : public testing::Test {
public:
  void SetUp() override {
    const std::string conn_yaml_plain = R"EOF(
  fragments:
  - conn_metadata_value_extractor:
      metadata_key:
        # This is the filter metadata namespace.
        key: unit.test.xxx
        path:
          # This is the lookup key whose value we extract for the fragment.
        - key: test.key.foo
  - conn_metadata_value_extractor:
      metadata_key:
        # This is the filter metadata namespace.
        key: unit.test.xxx
        path:
          # This is the lookup key whose value we extract for the fragment.
        - key: test.key.bar
)EOF";
    TestUtility::loadFromYaml(conn_yaml_plain, conn_md_key_builder_config_);

    const std::string filter_yaml_plain = R"EOF(
  fragments:
  - filter_metadata_value_extractor:
      metadata_key:
        # This is the filter metadata namespace.
        key: unit.test.xxx
        path:
          # This is the lookup key whose value we extract for the fragment.
        - key: test.key.foo
  - filter_metadata_value_extractor:
      metadata_key:
        # This is the filter metadata namespace.
        key: unit.test.xxx
        path:
          # This is the lookup key whose value we extract for the fragment.
        - key: test.key.bar
)EOF";
    TestUtility::loadFromYaml(filter_yaml_plain, filter_md_key_builder_config_);

    scope_info_a_ = makeScopedRouteInfo(R"EOF(
    name: foo_scope
    route_configuration_name: foo_route
    key:
      fragments:
        - string_key: foo_val
        - string_key: bar_val
)EOF");

    // Named the same as scope_info_a_ so we can verify route updates.
    scope_info_a_v2_ = makeScopedRouteInfo(R"EOF(
    name: foo_scope
    route_configuration_name: foo_route
    key:
      fragments:
        - string_key: xyz_val
        - string_key: xyz_val
)EOF");

    scope_info_b_ = makeScopedRouteInfo(R"EOF(
    name: bar_scope
    route_configuration_name: bar_route
    key:
      fragments:
        - string_key: bar_val
        - string_key: baz_val
)EOF");
  }

  std::shared_ptr<ScopedRouteInfo> makeScopedRouteInfo(const std::string& route_config_yaml) {
    envoy::config::route::v3::ScopedRouteConfiguration scoped_route_config;
    TestUtility::loadFromYaml(route_config_yaml, scoped_route_config);

    std::shared_ptr<MockConfig> route_config = std::make_shared<NiceMock<MockConfig>>();
    route_config->name_ = scoped_route_config.route_configuration_name();
    return std::make_shared<ScopedRouteInfo>(std::move(scoped_route_config),
                                             std::move(route_config));
  }

  std::shared_ptr<ScopedRouteInfo> scope_info_a_;
  std::shared_ptr<ScopedRouteInfo> scope_info_a_v2_;
  std::shared_ptr<ScopedRouteInfo> scope_info_b_;

  // Uses connection metadata.
  ScopedRoutes::ScopeKeyBuilder conn_md_key_builder_config_;

  // Uses filter metadata.
  ScopedRoutes::ScopeKeyBuilder filter_md_key_builder_config_;

  std::unique_ptr<ScopedConfigImpl> scoped_config_impl_;
  TestRequestHeaderMapImpl header_map_{};
  envoy::config::core::v3::Metadata metadata_;
};

// Test a metadata-based ScopedConfigImpl returns the correct route Config.
TEST_F(ScopedConfigImplMetadataTest, SimplePickRoute) {
  // We'll start off with a scoped config that looks for conn metadata.
  scoped_config_impl_ = std::make_unique<ScopedConfigImpl>(std::move(conn_md_key_builder_config_));
  scoped_config_impl_->addOrUpdateRoutingScopes({scope_info_a_});
  scoped_config_impl_->addOrUpdateRoutingScopes({scope_info_b_});

  // Populate filter metadata, but for a filter not specified in the config. This should result in
  // no route config being returned.
  auto kvs =
      MessageUtil::keyValueStruct({{"test.key.foo", "foo_val"}, {"test.key.bar", "bar_val"}});
  (*metadata_.mutable_filter_metadata())["the.wrong.filter"].MergeFrom(kvs);
  ConfigConstSharedPtr route_config =
      scoped_config_impl_->getRouteConfig(header_map_, metadata_, {});
  EXPECT_EQ(route_config, nullptr);

  // We now put those kv pairs in the correct filter, so now the route config should be returned.
  (*metadata_.mutable_filter_metadata())["unit.test.xxx"].MergeFrom(kvs);

  // Key (foo_val, bar_val) maps to scope_info_a_.
  route_config = scoped_config_impl_->getRouteConfig(header_map_, metadata_, {});
  EXPECT_EQ(route_config, scope_info_a_->routeConfig());

  // Let's verify it looks in the correct metadata by passing the conn metadata as filter
  // metadata. We should get no route.
  route_config =
      scoped_config_impl_->getRouteConfig(header_map_, {} /* conn */, metadata_ /* filter */);
  EXPECT_EQ(route_config, nullptr);

  // Update filter metadata to select a different scope config.
  kvs = MessageUtil::keyValueStruct({{"test.key.foo", "bar_val"}, {"test.key.bar", "baz_val"}});
  (*metadata_.mutable_filter_metadata())["unit.test.xxx"].MergeFrom(kvs);

  // Key (bar_val, baz_val) maps to scope_info_b_.
  route_config = scoped_config_impl_->getRouteConfig(header_map_, metadata_, {});
  EXPECT_EQ(route_config, scope_info_b_->routeConfig());

  // Let's reset the scope config to something that expects filter metadata.
  scoped_config_impl_ =
      std::make_unique<ScopedConfigImpl>(std::move(filter_md_key_builder_config_));
  scoped_config_impl_->addOrUpdateRoutingScopes({scope_info_a_});
  scoped_config_impl_->addOrUpdateRoutingScopes({scope_info_b_});

  // Same as the previous call to getRouteConfig above, but now it should be looking for it in the
  // filter metadata. It should return no route, since the populated metadata is passed as conn
  // metadata.
  route_config = scoped_config_impl_->getRouteConfig(header_map_, metadata_, {});
  EXPECT_EQ(route_config, nullptr);

  // Passing the metadata as filter metadata should result in a route being chosen.
  route_config =
      scoped_config_impl_->getRouteConfig(header_map_, {} /* conn */, metadata_ /* filter */);
  EXPECT_EQ(route_config, scope_info_b_->routeConfig());

  // Update filter metadata to bogus values so nothing gets picked again.
  kvs = MessageUtil::keyValueStruct(
      {{"test.key.foo", "bogus_val_1"}, {"test.key.bar", "bogus_val_2"}});
  (*metadata_.mutable_filter_metadata())["unit.test.xxx"].MergeFrom(kvs);

  route_config = scoped_config_impl_->getRouteConfig(header_map_, {}, metadata_);
  EXPECT_EQ(route_config, nullptr);
}

// Test a metadata-based ScopedConfigImpl honors route updates.
TEST_F(ScopedConfigImplMetadataTest, UpdateRoute) {
  // We'll start off with a scoped config that looks for conn metadata.
  scoped_config_impl_ = std::make_unique<ScopedConfigImpl>(std::move(conn_md_key_builder_config_));
  scoped_config_impl_->addOrUpdateRoutingScopes({scope_info_a_});

  // There's no metadata, so nothing gets picked.
  ConfigConstSharedPtr route_config =
      scoped_config_impl_->getRouteConfig(header_map_, metadata_, {});
  EXPECT_EQ(route_config, nullptr);

  // Populate filter metadata.
  auto kvs =
      MessageUtil::keyValueStruct({{"test.key.foo", "foo_val"}, {"test.key.bar", "bar_val"}});
  (*metadata_.mutable_filter_metadata())["unit.test.xxx"].MergeFrom(kvs);

  // Key (foo_val, bar_val) maps to scope_info_a_.
  route_config = scoped_config_impl_->getRouteConfig(header_map_, metadata_, {});
  EXPECT_EQ(route_config, scope_info_a_->routeConfig());

  // Update the scope named "foo_scope". With the same metadata, it should not return any route
  // config, since the match criteria has changed.
  scoped_config_impl_->addOrUpdateRoutingScopes({scope_info_a_v2_});
  route_config = scoped_config_impl_->getRouteConfig(header_map_, metadata_, {});
  EXPECT_EQ(route_config, nullptr);

  // Update filter metadata to select the new scope
  kvs = MessageUtil::keyValueStruct({{"test.key.foo", "xyz_val"}, {"test.key.bar", "xyz_val"}});
  (*metadata_.mutable_filter_metadata())["unit.test.xxx"].MergeFrom(kvs);

  // Should now pick the v2 route config with the new metadata.
  route_config = scoped_config_impl_->getRouteConfig(header_map_, metadata_, {});
  EXPECT_EQ(route_config, scope_info_a_v2_->routeConfig());
}

} // namespace
} // namespace Router
} // namespace Envoy
