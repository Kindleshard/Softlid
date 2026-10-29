// Reading GitHub's release info: the values the updater and the DesktopLUT download pick out of it.
#include "Check.h"
#include "core/Json.h"

using namespace bb;

namespace {

// Shaped like GitHub's "latest release" reply (cut down), with the keys also appearing as values and nested.
constexpr std::string_view Release = R"({
  "url": "https://api.github.com/repos/Kindleshard/Softlid/releases/1",
  "html_url": "https://github.com/Kindleshard/Softlid/releases/tag/v1.2.0",
  "tag_name" : "v1.2.0",
  "name": "tag_name",
  "body": "Fixes \"quoted\" things\nand more",
  "assets": [
    {"name": "Softlid-1.2.0-portable.zip", "size": 6450000,
     "browser_download_url": "https://github.com/Kindleshard/Softlid/releases/download/v1.2.0/Softlid-1.2.0-portable.zip"},
    {"name": "Softlid.exe",
     "browser_download_url": "https://github.com/Kindleshard/Softlid/releases/download/v1.2.0/Softlid.exe"}
  ]
})";

} // namespace

TEST(JsonStringsOfAKey)
{
    auto urls = JsonStrings(Release, "browser_download_url");
    CHECK_EQ(urls.size(), size_t{2});
    CHECK(urls[1] == "https://github.com/Kindleshard/Softlid/releases/download/v1.2.0/Softlid.exe");
    CHECK(JsonStrings(Release, "tag_name") == std::vector<std::string>{"v1.2.0"});   // (not "name": "tag_name")
    CHECK(JsonStrings(Release, "body")[0] == "Fixes \"quoted\" things\nand more");
    CHECK(JsonStrings(Release, "size").empty());   // (a number)
    CHECK(JsonStrings(Release, "missing").empty());
    CHECK(JsonStrings(R"({"cut": "no end)", "cut").empty());
}

TEST(JsonFirstThatFits)
{
    CHECK(JsonFirst(Release, "browser_download_url", [](std::string_view u) { return u.ends_with(".zip"); }) ==
          "https://github.com/Kindleshard/Softlid/releases/download/v1.2.0/Softlid-1.2.0-portable.zip");
    CHECK(JsonFirst(Release, "browser_download_url", [](std::string_view u) { return u.ends_with(".msi"); }).empty());
}
