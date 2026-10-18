// The boundary of lib/agent-core, enforced (task p1.5-16).
//
// Task p1.5-15 put three interfaces in front of the mesh, the uplink and the platform, and
// checked by name the thirteen files that were about to move (test/test_seams). This suite is the
// rule that replaces that list: it walks **every** file of lib/agent-core and fails on anything of
// a board in it, so that a file added to the library tomorrow is held to the same boundary without
// anybody remembering to add it anywhere.
//
// What a file of the library may include is a short list - its own headers as <agent/...>,
// ArduinoJson, and the C++ standard library - and that is the whole rule, stated as an allow list
// rather than a deny list: a header of Arduino's, the ESP-IDF's, FreeRTOS's or MeshCore's nobody
// thought to forbid is forbidden anyway. The deny list is still written out below, so that a
// failure says which rule was broken and not merely that a name was unexpected.
//
// Three more things it holds:
//
//   the names        `Serial`, `ESP.`, `esp_`, `freertos`, Arduino's free `delay()` and
//                    MeshCore's buffer constants appear nowhere in the library's code (comments
//                    stripped: a comment may say what the library's own constant stands for).
//                    `millis()` is the one that needs care - the library has its own, a member of
//                    IPlatform reached through the seam - so a call through an object is fine and
//                    a free call is only fine in a file that declares a `millis()` of its own.
//   the other side   nothing under src/ reaches into the library by a relative path: the board
//                    includes <agent/...> like any other consumer, and src/agent/ holds the TLS
//                    client and the root it verifies against and nothing else.
//   compiled whole   taking the address of JobRunner::claimAndRun and AgentLoop::loop makes the
//                    linker resolve both, which it can only do if [env:native] compiled the whole
//                    library - the two files no host build had ever compiled before this task.
//
// And the manifests: library.json, idf_component.yml and CMakeLists.txt agree with each other,
// with platformio.ini's ArduinoJson pin, and with the files on disk. The CMake file itself is
// parsed by tools/check-idf-component.cmake, which needs cmake and is a CI step; what is checkable
// from here is that every source is named in it.

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <agent/AgentLoop.h>
#include <agent/JobRunner.h>
#include <agent/Version.h>

#ifndef PROJECT_DIR
#error "PROJECT_DIR must be defined; platformio.ini's [env:native] does it"
#endif

namespace {

namespace fs = std::filesystem;

std::string readFile(const fs::path& p) {
  std::ifstream in(p, std::ios::binary);
  return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

fs::path libRoot() { return fs::path(PROJECT_DIR) / "lib" / "agent-core"; }

// Every .h and .cpp of the library, as paths relative to the repository root.
std::vector<std::string> libraryFiles() {
  std::vector<std::string> out;
  for (const auto& item : fs::recursive_directory_iterator(libRoot())) {
    if (!item.is_regular_file()) continue;
    const std::string ext = item.path().extension().string();
    if (ext != ".h" && ext != ".cpp") continue;
    out.push_back(fs::relative(item.path(), fs::path(PROJECT_DIR)).generic_string());
  }
  std::sort(out.begin(), out.end());
  return out;
}

bool startsWith(const std::string& s, const char* prefix) { return s.rfind(prefix, 0) == 0; }

// The text with its line comments taken out: a comment may name a thing of the board's to say what
// the library's own stands for, and that is not a use of it. (The same helper as test_seams's.)
std::string withoutComments(const std::string& text) {
  std::string out;
  size_t pos = 0;
  while (pos < text.size()) {
    const size_t comment = text.find("//", pos);
    const size_t end = comment == std::string::npos ? text.size() : comment;
    out.append(text, pos, end - pos);
    if (comment == std::string::npos) break;
    const size_t eol = text.find('\n', comment);
    pos = eol == std::string::npos ? text.size() : eol;
  }
  return out;
}

struct Include {
  std::string name;
  int line = 0;
};

std::vector<Include> includesOf(const std::string& text) {
  std::vector<Include> names;
  size_t pos = 0;
  int line = 1;
  size_t counted = 0;
  while ((pos = text.find("#include", pos)) != std::string::npos) {
    for (; counted < pos; counted++) {
      if (text[counted] == '\n') line++;
    }
    const size_t open = text.find_first_of("\"<", pos);
    if (open == std::string::npos) break;
    const char close = text[open] == '"' ? '"' : '>';
    const size_t end = text.find(close, open + 1);
    if (end == std::string::npos) break;
    names.push_back({text.substr(open + 1, end - open - 1), line});
    pos = end;
  }
  return names;
}

// A C++ standard header: <cstdint>, <string>, <new>. No dot and no slash is the whole test, and
// it is exact enough - every header of a platform has an extension or a directory.
bool isStandardHeader(const std::string& name) {
  return name.find('.') == std::string::npos && name.find('/') == std::string::npos;
}

// What the task forbids by name, for a failure that says which rule it is.
const char* forbiddenHeaderRule(const std::string& name) {
  if (name == "Arduino.h") return "Arduino";
  if (name.find("WiFi") != std::string::npos) return "WiFi";
  if (name == "Preferences.h") return "NVS (Preferences)";
  if (startsWith(name, "esp_") || name.find("/esp_") != std::string::npos) return "the ESP-IDF";
  if (startsWith(name, "freertos/")) return "FreeRTOS";
  if (name == "HTTPClient.h" || name == "WiFiClientSecure.h") return "the Arduino HTTP client";
  if (name == "MeshCore.h" || name == "Mesh.h" || startsWith(name, "helpers/") ||
      name.find("MeshCore") != std::string::npos) {
    return "MeshCore";
  }
  if (name == "Stream.h" || name == "Print.h" || name == "SPIFFS.h") return "Arduino";
  return nullptr;
}

// A file that declares a `millis()` of its own may call it without an object: the runner and the
// loop each have a private one over the platform seam (declared in their header, called from
// their .cpp), and IPlatform declares the virtual.
bool declaresItsOwnMillis(const std::string& rel, const std::string& code) {
  if (code.find("uint32_t millis()") != std::string::npos) return true;
  const fs::path p(rel);
  if (p.extension() != ".cpp") return false;
  const fs::path header = libRoot() / "include" / "agent" / (p.stem().string() + ".h");
  return fs::exists(header) && readFile(header).find("uint32_t millis()") != std::string::npos;
}

// An occurrence of `millis(` that is not reached through an object, in a file that has no
// `millis()` of its own, is Arduino's - which is what this forbids.
std::vector<int> freeMillisCalls(const std::string& rel, const std::string& code) {
  std::vector<int> lines;
  if (declaresItsOwnMillis(rel, code)) return lines;
  size_t pos = 0;
  while ((pos = code.find("millis(", pos)) != std::string::npos) {
    const bool throughAnObject = pos >= 2 && (code.compare(pos - 2, 2, "->") == 0 ||
                                              code.compare(pos - 2, 2, "::") == 0 ||
                                              code[pos - 1] == '.');
    if (!throughAnObject) {
      lines.push_back(1 + static_cast<int>(std::count(code.begin(), code.begin() + static_cast<long>(pos), '\n')));
    }
    pos += 7;
  }
  return lines;
}

const char* const kForbiddenNames[] = {
    "Serial",               // the board's port: the library formats a line and hands it to a sink
    "ESP.",                 // the Arduino ESP object
    "esp_",                 // the ESP-IDF
    "freertos",             // its scheduler
    "MAX_PACKET_PAYLOAD",   // MeshCore's buffer sizes: the library has its own constants
    "MAX_PATH_SIZE",
    "PUB_KEY_SIZE",
};

std::string fileAt(const char* relative) { return readFile(fs::path(PROJECT_DIR) / relative); }

}  // namespace

// ---- what a file of the library may include ---------------------------------------------------

TEST(Library, EveryIncludeOfTheLibraryIsItsOwnOrArduinoJsonOrTheStandardLibrary) {
  const std::vector<std::string> files = libraryFiles();
  ASSERT_GE(files.size(), 48u) << "the walk found almost nothing - is PROJECT_DIR " << PROJECT_DIR << " right?";

  for (const std::string& rel : files) {
    const std::string text = fileAt(rel.c_str());
    ASSERT_FALSE(text.empty()) << rel << " is empty";
    for (const Include& inc : includesOf(text)) {
      const char* rule = forbiddenHeaderRule(inc.name);
      if (rule != nullptr) {
        ADD_FAILURE() << rel << ":" << inc.line << " includes " << inc.name << " - that is " << rule
                      << ", and the library is built for a platform that has none of it";
        continue;
      }
      // ArduinoJson is reached through one header and not from a source directly: `agent/Json.h`
      // wraps the include in a diagnostic pragma for the one warning the dependency raises in our
      // translation units, and a second way in would get that warning back under a consumer's
      // -Werror (task p1.6-20; the header says what the warning is).
      if (inc.name == "ArduinoJson.h") {
        EXPECT_EQ(rel, "lib/agent-core/include/agent/Json.h")
            << rel << ":" << inc.line
            << " includes <ArduinoJson.h> directly; inside the library the dependency is reached "
               "as <agent/Json.h>, which is where its one warning is turned off";
        continue;
      }
      const bool allowed = startsWith(inc.name, "agent/") || isStandardHeader(inc.name);
      EXPECT_TRUE(allowed) << rel << ":" << inc.line << " includes " << inc.name
                           << ", which is neither one of the library's own <agent/...> headers, nor "
                              "ArduinoJson, nor the C++ standard library";
    }
  }
}

// No relative include crosses the boundary from the inside: a library header is reached as
// <agent/X.h> inside the library too, so that a consumer's include path is the only one that has
// to be right.
TEST(Library, NoFileOfTheLibraryIncludesAnythingWithQuotes) {
  for (const std::string& rel : libraryFiles()) {
    const std::string text = fileAt(rel.c_str());
    EXPECT_EQ(text.find("#include \""), std::string::npos)
        << rel << " includes a header with quotes; inside the library every include is <agent/...>";
  }
}

// ---- what a file of the library may name ------------------------------------------------------

TEST(Library, NoFileOfTheLibraryNamesAnythingOfTheBoard) {
  for (const std::string& rel : libraryFiles()) {
    const std::string code = withoutComments(fileAt(rel.c_str()));
    for (const char* name : kForbiddenNames) {
      EXPECT_EQ(code.find(name), std::string::npos) << rel << " names " << name;
    }
    // Arduino's free delay(); the seam's is IPlatform::delayMs, which this does not match.
    EXPECT_EQ(code.find("delay("), std::string::npos) << rel << " calls delay()";
    for (int line : freeMillisCalls(rel, code)) {
      ADD_FAILURE() << rel << ":" << line << " calls millis() with no object and declares none of "
                                             "its own - the library's clock is IPlatform::millis()";
    }
  }
}

// ---- the board's side of the same boundary ----------------------------------------------------

TEST(Library, SrcAgentHoldsTheTlsClientAndTheRootAndNothingElse) {
  std::vector<std::string> left;
  for (const auto& item : fs::directory_iterator(fs::path(PROJECT_DIR) / "src" / "agent")) {
    left.push_back(item.path().filename().generic_string());
  }
  std::sort(left.begin(), left.end());
  const std::vector<std::string> expected = {"AgentClient.cpp", "AgentClient.h", "TlsRoots.h"};
  EXPECT_EQ(expected, left) << "src/agent/ is the WiFi uplink and the root it verifies against; "
                               "everything else portable is lib/agent-core's";
}

TEST(Library, NothingUnderSrcReachesIntoTheLibraryByARelativePath) {
  std::vector<std::string> libHeaders;
  for (const auto& item : fs::directory_iterator(libRoot() / "include" / "agent")) {
    libHeaders.push_back(item.path().filename().generic_string());
  }
  ASSERT_GE(libHeaders.size(), 20u);

  for (const auto& item : fs::recursive_directory_iterator(fs::path(PROJECT_DIR) / "src")) {
    if (!item.is_regular_file()) continue;
    const std::string ext = item.path().extension().string();
    if (ext != ".h" && ext != ".cpp") continue;
    const std::string rel = fs::relative(item.path(), fs::path(PROJECT_DIR)).generic_string();
    const std::string text = readFile(item.path());
    for (const Include& inc : includesOf(text)) {
      if (inc.name.size() < 3 || inc.name.find('"') != std::string::npos) continue;
      const std::string base = inc.name.substr(inc.name.find_last_of('/') + 1);
      const bool isALibraryHeader =
          std::find(libHeaders.begin(), libHeaders.end(), base) != libHeaders.end();
      if (!isALibraryHeader) continue;
      EXPECT_EQ("agent/" + base, inc.name)
          << rel << ":" << inc.line << " reaches a library header as " << inc.name
          << "; from outside the library every one of them is <agent/" << base << ">";
    }
  }
  // And with angle brackets, as a consumer's include path gives them.
  for (const auto& item : fs::recursive_directory_iterator(fs::path(PROJECT_DIR) / "src")) {
    if (!item.is_regular_file() || item.path().extension() != ".h") continue;
    const std::string text = readFile(item.path());
    EXPECT_EQ(text.find("#include \"agent/"), std::string::npos)
        << fs::relative(item.path(), fs::path(PROJECT_DIR)).generic_string()
        << " includes a library header with quotes";
  }
}

// ---- compiled whole --------------------------------------------------------------------------

// The point of the task: [env:native] builds the library whole, so the two files that had never
// been compiled on a host are compiled now. Nothing here *runs* them - that is task 17, which
// gives them fakes - but taking the address of a member function is an undefined symbol until the
// object file is in the link, so this fails to build the moment the library stops being built.
TEST(Library, TheRunnerAndTheLoopAreCompiledAndLinkedOnTheHost) {
  int (mm::JobRunner::*claimAndRun)() = &mm::JobRunner::claimAndRun;
  void (mm::AgentLoop::*loop)() = &mm::AgentLoop::loop;
  EXPECT_TRUE(claimAndRun != nullptr);
  EXPECT_TRUE(loop != nullptr);

  // And the library's own version, from its manifest, is a string the firmware can print.
  EXPECT_STREQ("0.3.0", AGENT_CORE_VERSION);
}

// ---- the manifests ---------------------------------------------------------------------------

TEST(Library, TheManifestNamesTheLibraryItsVersionAndOneDependency) {
  const std::string manifest = fileAt("lib/agent-core/library.json");
  ASSERT_FALSE(manifest.empty());
  EXPECT_NE(manifest.find("\"name\": \"agent-core\""), std::string::npos);
  EXPECT_NE(manifest.find("\"version\": \"" AGENT_CORE_VERSION "\""), std::string::npos);
  EXPECT_NE(manifest.find("\"frameworks\": \"*\""), std::string::npos) << "any framework";
  EXPECT_NE(manifest.find("\"platforms\": \"*\""), std::string::npos) << "any platform";
  EXPECT_NE(manifest.find("\"includeDir\": \"include\""), std::string::npos);
  EXPECT_NE(manifest.find("\"srcDir\": \"src\""), std::string::npos);
  // -Werror follows the files: build_src_flags in platformio.ini reaches src/ and not lib/.
  EXPECT_NE(manifest.find("\"-Werror\""), std::string::npos);

  // One dependency, at the exact version platformio.ini's [libs] pins.
  const std::string ini = fileAt("platformio.ini");
  const size_t pin = ini.find("bblanchon/ArduinoJson @ ");
  ASSERT_NE(pin, std::string::npos);
  const std::string version = ini.substr(pin + 24, ini.find('\n', pin) - pin - 24);
  EXPECT_NE(manifest.find("\"bblanchon/ArduinoJson\": \"" + version + "\""), std::string::npos)
      << "library.json's ArduinoJson is not platformio.ini's " << version;

  const std::string component = fileAt("lib/agent-core/idf_component.yml");
  ASSERT_FALSE(component.empty());
  EXPECT_NE(component.find("version: \"" AGENT_CORE_VERSION "\""), std::string::npos)
      << "idf_component.yml's version is not library.json's";
  EXPECT_NE(component.find("version: \"==" + version + "\""), std::string::npos)
      << "idf_component.yml's ArduinoJson is not platformio.ini's " << version;
}

TEST(Library, TheIdfComponentRegistersEverySourceOfTheLibrary) {
  const std::string cmake = fileAt("lib/agent-core/CMakeLists.txt");
  ASSERT_FALSE(cmake.empty());
  EXPECT_NE(cmake.find("idf_component_register("), std::string::npos);
  EXPECT_NE(cmake.find("INCLUDE_DIRS\n    \"include\""), std::string::npos);

  int sources = 0;
  for (const auto& item : fs::directory_iterator(libRoot() / "src")) {
    if (item.path().extension() != ".cpp") continue;
    sources++;
    const std::string line = "\"src/" + item.path().filename().generic_string() + "\"";
    EXPECT_NE(cmake.find(line), std::string::npos)
        << line << " is a source of the library and CMakeLists.txt does not register it";
  }
  EXPECT_GE(sources, 20);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
