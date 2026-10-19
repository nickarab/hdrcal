// Unit tests for the minimal JSON parser.

#include "hdrcal/json.hpp"
#include "test_framework.hpp"

#include <string>

HDRCAL_TEST(testParseJson_ObjectWithAllTypes_ParsesValues) {
    const hdrcal::JsonParseResult parsed = hdrcal::parseJson(R"({"name":"DP-2","enabled":true,"count":3.5,"missing":null})");
    HDRCAL_REQUIRE(parsed.value.has_value());
    HDRCAL_REQUIRE(parsed.error.empty());

    const auto* name = parsed.value->find("name");
    HDRCAL_REQUIRE(name != nullptr);
    HDRCAL_REQUIRE(name->asString() != nullptr);
    HDRCAL_REQUIRE_EQ(*name->asString(), std::string("DP-2"));

    const auto* enabled = parsed.value->find("enabled");
    HDRCAL_REQUIRE(enabled != nullptr && enabled->asBoolean() != nullptr);
    HDRCAL_REQUIRE(*enabled->asBoolean());

    const auto* count = parsed.value->find("count");
    HDRCAL_REQUIRE(count != nullptr && count->asNumber() != nullptr);
    HDRCAL_REQUIRE_EQ(*count->asNumber(), 3.5);

    const auto* missing = parsed.value->find("missing");
    HDRCAL_REQUIRE(missing != nullptr);
    HDRCAL_REQUIRE(missing->asBoolean() == nullptr && missing->asString() == nullptr && missing->asNumber() == nullptr);
}

HDRCAL_TEST(testParseJson_ArrayOfObjects_FindsNestedString) {
    const hdrcal::JsonParseResult parsed = hdrcal::parseJson(R"([{"name":"DP-2","currentFormat":"XRGB2101010"},{"name":"HDMI-A-1"}])");
    HDRCAL_REQUIRE(parsed.value.has_value());

    const auto* array = parsed.value->asArray();
    HDRCAL_REQUIRE(array != nullptr);
    HDRCAL_REQUIRE_EQ(array->size(), std::size_t{2});

    const hdrcal::JsonValue* secondFormat = array->at(1).find("currentFormat");
    HDRCAL_REQUIRE(secondFormat == nullptr);
}

HDRCAL_TEST(testParseJson_UnicodeEscape_DecodesUtf8) {
    const hdrcal::JsonParseResult parsed = hdrcal::parseJson(R"({"text":"a\u00e9b"})");
    HDRCAL_REQUIRE(parsed.value.has_value());

    const auto* text = parsed.value->find("text");
    HDRCAL_REQUIRE(text != nullptr && text->asString() != nullptr);
    HDRCAL_REQUIRE_EQ(*text->asString(), std::string("a\xC3\xA9"
                                                     "b"));
}

HDRCAL_TEST(testParseJson_SurrogatePair_DecodesUtf8) {
    const hdrcal::JsonParseResult parsed = hdrcal::parseJson(R"({"text":"\ud83d\ude00"})");
    HDRCAL_REQUIRE(parsed.value.has_value());

    const auto* text = parsed.value->find("text");
    HDRCAL_REQUIRE(text != nullptr && text->asString() != nullptr);
    HDRCAL_REQUIRE_EQ(*text->asString(), std::string("\xF0\x9F\x98\x80"));
}

HDRCAL_TEST(testParseJson_TrailingCharacters_ReturnsError) {
    const hdrcal::JsonParseResult parsed = hdrcal::parseJson(R"({"a":1} extra)");
    HDRCAL_REQUIRE(!parsed.value.has_value());
    HDRCAL_REQUIRE(!parsed.error.empty());
}

HDRCAL_TEST(testParseJson_UnterminatedString_ReturnsError) {
    const hdrcal::JsonParseResult parsed = hdrcal::parseJson(R"({"a":"b})");
    HDRCAL_REQUIRE(!parsed.value.has_value());
    HDRCAL_REQUIRE(!parsed.error.empty());
}

HDRCAL_TEST(testParseJson_EmptyInput_ReturnsError) {
    const hdrcal::JsonParseResult parsed = hdrcal::parseJson("");
    HDRCAL_REQUIRE(!parsed.value.has_value());
    HDRCAL_REQUIRE(!parsed.error.empty());
}

HDRCAL_TEST(testParseJson_DuplicateKey_ReturnsError) {
    const hdrcal::JsonParseResult parsed = hdrcal::parseJson(R"({"a":1,"a":2})");
    HDRCAL_REQUIRE(!parsed.value.has_value());
    HDRCAL_REQUIRE(!parsed.error.empty());
}
