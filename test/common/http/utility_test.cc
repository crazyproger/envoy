#include <cstdint>
#include <string>

#include "common/config/protocol_json.h"
#include "common/http/exception.h"
#include "common/http/header_map_impl.h"
#include "common/http/utility.h"
#include "common/network/address_impl.h"

#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"
#include "spdlog/spdlog.h"

namespace Envoy {
namespace Http {

TEST(HttpUtility, parseQueryString) {
  EXPECT_EQ(Utility::QueryParams(), Utility::parseQueryString("/hello"));
  EXPECT_EQ(Utility::QueryParams(), Utility::parseQueryString("/hello?"));
  EXPECT_EQ(Utility::QueryParams({{"hello", ""}}), Utility::parseQueryString("/hello?hello"));
  EXPECT_EQ(Utility::QueryParams({{"hello", "world"}}),
            Utility::parseQueryString("/hello?hello=world"));
  EXPECT_EQ(Utility::QueryParams({{"hello", ""}}), Utility::parseQueryString("/hello?hello="));
  EXPECT_EQ(Utility::QueryParams({{"hello", ""}}), Utility::parseQueryString("/hello?hello=&"));
  EXPECT_EQ(Utility::QueryParams({{"hello", ""}, {"hello2", "world2"}}),
            Utility::parseQueryString("/hello?hello=&hello2=world2"));
  EXPECT_EQ(Utility::QueryParams({{"name", "admin"}, {"level", "trace"}}),
            Utility::parseQueryString("/logging?name=admin&level=trace"));
}

TEST(HttpUtility, getResponseStatus) {
  EXPECT_THROW(Utility::getResponseStatus(TestHeaderMapImpl{}), CodecClientException);
  EXPECT_EQ(200U, Utility::getResponseStatus(TestHeaderMapImpl{{":status", "200"}}));
}

TEST(HttpUtility, isInternalRequest) {
  EXPECT_FALSE(Utility::isInternalRequest(TestHeaderMapImpl{}));
  EXPECT_FALSE(
      Utility::isInternalRequest(TestHeaderMapImpl{{"x-forwarded-for", "10.0.0.1,10.0.0.2"}}));
  EXPECT_FALSE(Utility::isInternalRequest(TestHeaderMapImpl{{"x-forwarded-for", "50.0.0.1"}}));
  EXPECT_FALSE(Utility::isInternalRequest(TestHeaderMapImpl{{"x-forwarded-for", "blah"}}));

  EXPECT_TRUE(Utility::isInternalRequest(TestHeaderMapImpl{{"x-forwarded-for", "10.0.0.0"}}));
  EXPECT_TRUE(Utility::isInternalRequest(TestHeaderMapImpl{{"x-forwarded-for", "10.255.255.255"}}));

  EXPECT_FALSE(Utility::isInternalRequest(TestHeaderMapImpl{{"x-forwarded-for", "172.0.0.0"}}));
  EXPECT_TRUE(Utility::isInternalRequest(TestHeaderMapImpl{{"x-forwarded-for", "172.16.0.0"}}));
  EXPECT_TRUE(Utility::isInternalRequest(TestHeaderMapImpl{{"x-forwarded-for", "172.31.255.255"}}));
  EXPECT_FALSE(Utility::isInternalRequest(TestHeaderMapImpl{{"x-forwarded-for", "172.32.0.0"}}));

  EXPECT_FALSE(Utility::isInternalRequest(TestHeaderMapImpl{{"x-forwarded-for", "192.0.0.0"}}));
  EXPECT_TRUE(Utility::isInternalRequest(TestHeaderMapImpl{{"x-forwarded-for", "192.168.0.0"}}));
  EXPECT_TRUE(
      Utility::isInternalRequest(TestHeaderMapImpl{{"x-forwarded-for", "192.168.255.255"}}));

  EXPECT_TRUE(Utility::isInternalRequest(TestHeaderMapImpl{{"x-forwarded-for", "127.0.0.1"}}));
}

TEST(HttpUtility, isWebSocketUpgradeRequest) {
  EXPECT_FALSE(Utility::isWebSocketUpgradeRequest(TestHeaderMapImpl{}));
  EXPECT_FALSE(Utility::isWebSocketUpgradeRequest(TestHeaderMapImpl{{"connection", "upgrade"}}));
  EXPECT_FALSE(Utility::isWebSocketUpgradeRequest(TestHeaderMapImpl{{"upgrade", "websocket"}}));
  EXPECT_FALSE(Utility::isWebSocketUpgradeRequest(
      TestHeaderMapImpl{{"Connection", "close"}, {"Upgrade", "websocket"}}));

  EXPECT_TRUE(Utility::isWebSocketUpgradeRequest(
      TestHeaderMapImpl{{"Connection", "upgrade"}, {"Upgrade", "websocket"}}));
  EXPECT_TRUE(Utility::isWebSocketUpgradeRequest(
      TestHeaderMapImpl{{"connection", "upgrade"}, {"upgrade", "websocket"}}));
  EXPECT_TRUE(Utility::isWebSocketUpgradeRequest(
      TestHeaderMapImpl{{"connection", "Upgrade"}, {"upgrade", "WebSocket"}}));
}

TEST(HttpUtility, appendXff) {
  {
    TestHeaderMapImpl headers;
    Network::Address::Ipv4Instance address("127.0.0.1");
    Utility::appendXff(headers, address);
    EXPECT_EQ("127.0.0.1", headers.get_("x-forwarded-for"));
  }

  {
    TestHeaderMapImpl headers{{"x-forwarded-for", "10.0.0.1"}};
    Network::Address::Ipv4Instance address("127.0.0.1");
    Utility::appendXff(headers, address);
    EXPECT_EQ("10.0.0.1, 127.0.0.1", headers.get_("x-forwarded-for"));
  }

  {
    TestHeaderMapImpl headers{{"x-forwarded-for", "10.0.0.1"}};
    Network::Address::PipeInstance address("/foo");
    Utility::appendXff(headers, address);
    EXPECT_EQ("10.0.0.1", headers.get_("x-forwarded-for"));
  }
}

TEST(HttpUtility, createSslRedirectPath) {
  {
    TestHeaderMapImpl headers{{":authority", "www.lyft.com"}, {":path", "/hello"}};
    EXPECT_EQ("https://www.lyft.com/hello", Utility::createSslRedirectPath(headers));
  }
}

namespace {

Http2Settings parseHttp2SettingsFromJson(const std::string& json_string) {
  envoy::api::v2::Http2ProtocolOptions http2_protocol_options;
  auto json_object_ptr = Json::Factory::loadFromString(json_string);
  Config::ProtocolJson::translateHttp2ProtocolOptions(
      json_object_ptr->getString("http_codec_options", ""),
      *json_object_ptr->getObject("http2_settings", true), http2_protocol_options);
  return Utility::parseHttp2Settings(http2_protocol_options);
}

} // namespace

TEST(HttpUtility, parseHttp2Settings) {
  {
    auto http2_settings = parseHttp2SettingsFromJson("{}");
    EXPECT_EQ(Http2Settings::DEFAULT_HPACK_TABLE_SIZE, http2_settings.hpack_table_size_);
    EXPECT_EQ(Http2Settings::DEFAULT_MAX_CONCURRENT_STREAMS,
              http2_settings.max_concurrent_streams_);
    EXPECT_EQ(Http2Settings::DEFAULT_INITIAL_STREAM_WINDOW_SIZE,
              http2_settings.initial_stream_window_size_);
    EXPECT_EQ(Http2Settings::DEFAULT_INITIAL_CONNECTION_WINDOW_SIZE,
              http2_settings.initial_connection_window_size_);
  }

  {
    auto http2_settings = parseHttp2SettingsFromJson(R"raw({
                                          "http2_settings" : {
                                            "hpack_table_size": 1,
                                            "max_concurrent_streams": 2,
                                            "initial_stream_window_size": 3,
                                            "initial_connection_window_size": 4
                                          }
                                        })raw");
    EXPECT_EQ(1U, http2_settings.hpack_table_size_);
    EXPECT_EQ(2U, http2_settings.max_concurrent_streams_);
    EXPECT_EQ(3U, http2_settings.initial_stream_window_size_);
    EXPECT_EQ(4U, http2_settings.initial_connection_window_size_);
  }

  {
    auto http2_settings = parseHttp2SettingsFromJson(R"raw({
                                         "http_codec_options": "no_compression"
                                        })raw");
    EXPECT_EQ(0, http2_settings.hpack_table_size_);
    EXPECT_EQ(Http2Settings::DEFAULT_MAX_CONCURRENT_STREAMS,
              http2_settings.max_concurrent_streams_);
    EXPECT_EQ(Http2Settings::DEFAULT_INITIAL_STREAM_WINDOW_SIZE,
              http2_settings.initial_stream_window_size_);
    EXPECT_EQ(Http2Settings::DEFAULT_INITIAL_CONNECTION_WINDOW_SIZE,
              http2_settings.initial_connection_window_size_);
  }

  {
    const auto json = R"raw({
                              "http_codec_options": "no_compression",
                              "http2_settings" : {
                                "hpack_table_size": 1
                              }
                            })raw";
    EXPECT_THROW_WITH_MESSAGE(
        parseHttp2SettingsFromJson(json), EnvoyException,
        "'http_codec_options.no_compression' conflicts with 'http2_settings.hpack_table_size'");
  }
}

TEST(HttpUtility, TwoAddressesInXFF) {
  const std::string first_address = "34.0.0.1";
  const std::string second_address = "10.0.0.1";
  TestHeaderMapImpl request_headers{
      {"x-forwarded-for", fmt::format("{0}, {0}, {1}", first_address, second_address)}};
  EXPECT_EQ(second_address, Utility::getLastAddressFromXFF(request_headers));
}

TEST(HttpUtility, EmptyXFF) {
  {
    TestHeaderMapImpl request_headers{{"x-forwarded-for", ""}};
    EXPECT_EQ("", Utility::getLastAddressFromXFF(request_headers));
  }

  {
    TestHeaderMapImpl request_headers;
    EXPECT_EQ("", Utility::getLastAddressFromXFF(request_headers));
  }
}

TEST(HttpUtility, OneAddressInXFF) {
  const std::string first_address = "34.0.0.1";
  TestHeaderMapImpl request_headers{{"x-forwarded-for", first_address}};
  EXPECT_EQ(first_address, Utility::getLastAddressFromXFF(request_headers));
}

TEST(HttpUtility, TestParseCookie) {
  TestHeaderMapImpl headers{
      {"someheader", "10.0.0.1"},
      {"cookie", "somekey=somevalue; someotherkey=someothervalue"},
      {"cookie", "abc=def; token=abc123; Expires=Wed, 09 Jun 2021 10:18:14 GMT"},
      {"cookie", "key2=value2; key3=value3"}};

  std::string key{"token"};
  std::string value = Utility::parseCookieValue(headers, key);
  EXPECT_EQ(value, "abc123");
}

TEST(HttpUtility, TestParseCookieBadValues) {
  TestHeaderMapImpl headers{{"cookie", "token1=abc123; = "},
                            {"cookie", "token2=abc123;   "},
                            {"cookie", "; token3=abc123;"},
                            {"cookie", "=; token4=\"abc123\""}};

  EXPECT_EQ(Utility::parseCookieValue(headers, "token1"), "abc123");
  EXPECT_EQ(Utility::parseCookieValue(headers, "token2"), "abc123");
  EXPECT_EQ(Utility::parseCookieValue(headers, "token3"), "abc123");
  EXPECT_EQ(Utility::parseCookieValue(headers, "token4"), "abc123");
}

TEST(HttpUtility, TestParseCookieWithQuotes) {
  TestHeaderMapImpl headers{
      {"someheader", "10.0.0.1"},
      {"cookie", "dquote=\"; quoteddquote=\"\"\""},
      {"cookie", "leadingdquote=\"foobar;"},
      {"cookie", "abc=def; token=\"abc123\"; Expires=Wed, 09 Jun 2021 10:18:14 GMT"}};

  EXPECT_EQ(Utility::parseCookieValue(headers, "token"), "abc123");
  EXPECT_EQ(Utility::parseCookieValue(headers, "dquote"), "\"");
  EXPECT_EQ(Utility::parseCookieValue(headers, "quoteddquote"), "\"");
  EXPECT_EQ(Utility::parseCookieValue(headers, "leadingdquote"), "\"foobar");
}

} // namespace Http
} // namespace Envoy
