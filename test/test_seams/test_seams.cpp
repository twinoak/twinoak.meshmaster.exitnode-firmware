// The seams (task p1.5-15): the files task p1.5-16 moved into lib/agent-core - JobRunner,
// AgentLoop, LoginSequence, ConfigApply and the four interfaces - include nothing of the board,
// and the board's adapters stand behind the interfaces the way the task says.
//
// The shape is test_tls_policy's: a walk over named files for named strings. It was written
// before the library existed, when JobRunner.cpp and AgentLoop.cpp were in no host build at all
// and a file that *compiles* against Arduino was exactly what had to be caught before 16 moved
// it; since 16 the whole library is compiled here as well (test/test_library), and this suite is
// still what holds the *shape* - which object stands behind which interface, and who draws the
// CLI timestamp. Only the paths moved.
//
// The four interface headers are included below, so that this test is also a host build of them.

#include <gtest/gtest.h>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <agent/IJobExtension.h>
#include <agent/IMesh.h>
#include <agent/IPlatform.h>
#include <agent/IUplink.h>
#include <agent/TextOut.h>

#ifndef PROJECT_DIR
#error "PROJECT_DIR must be defined; platformio.ini's [env:native] does it"
#endif

namespace {

namespace fs = std::filesystem;

std::string readFile(const fs::path& p) {
  std::ifstream in(p, std::ios::binary);
  return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

// The files 16 moves: nothing of Arduino, the ESP-IDF, WiFi, NVS, MeshCore or this repository's
// board-only headers may be included by any of them.
const char* const kPortable[] = {
    "lib/agent-core/include/agent/JobRunner.h",
    "lib/agent-core/src/JobRunner.cpp",
    "lib/agent-core/include/agent/AgentLoop.h",
    "lib/agent-core/src/AgentLoop.cpp",
    "lib/agent-core/include/agent/LoginSequence.h",
    "lib/agent-core/src/LoginSequence.cpp",
    "lib/agent-core/include/agent/ConfigApply.h",
    "lib/agent-core/src/ConfigApply.cpp",
    "lib/agent-core/include/agent/IMesh.h",
    "lib/agent-core/include/agent/IUplink.h",
    "lib/agent-core/include/agent/IPlatform.h",
    "lib/agent-core/include/agent/IJobExtension.h",
    "lib/agent-core/include/agent/TextOut.h",
};

bool startsWith(const std::string& s, const char* prefix) { return s.rfind(prefix, 0) == 0; }

// The header names a portable file may not include. A name is what stands between the quotes or
// the angle brackets of an #include line.
bool isBoardHeader(const std::string& name) {
  if (name == "Arduino.h" || name == "HTTPClient.h" || name == "Preferences.h" || name == "SPIFFS.h") return true;
  if (name == "Stream.h" || name == "Print.h") return true;                  // Arduino's text sinks
  if (startsWith(name, "esp_") || name.find("WiFi") != std::string::npos) return true;
  if (name == "MeshCore.h" || name == "Mesh.h" || name == "mesh/MeshCoreHeaders.h") return true;
  if (name == "mesh/AgentMesh.h" || name == "AgentClient.h" || name == "Health.h") return true;
  if (startsWith(name, "net/") || startsWith(name, "platform/") || name == "settings/Store.h") return true;
  return false;
}

// Names of the board's that must not appear in a portable file at all, include lines or not.
const char* const kBoardIdentifiers[] = {"MAX_PACKET_PAYLOAD", "MAX_PATH_SIZE", "PUB_KEY_SIZE", "esp_random",
                                         "ESP.get", "Serial.", "::millis(", "delay(1)"};

// The text with its line comments taken out: a comment may name MeshCore's constant to say what
// the library's stands for, and that is not a use of it.
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

std::vector<std::string> includesOf(const std::string& text) {
  std::vector<std::string> names;
  size_t pos = 0;
  while ((pos = text.find("#include", pos)) != std::string::npos) {
    const size_t open = text.find_first_of("\"<", pos);
    if (open == std::string::npos) break;
    const char close = text[open] == '"' ? '"' : '>';
    const size_t end = text.find(close, open + 1);
    if (end == std::string::npos) break;
    names.push_back(text.substr(open + 1, end - open - 1));
    pos = end;
  }
  return names;
}

// A fake of each seam, compiled here and nowhere else: the interfaces are implementable on a host
// with nothing of the board, which is what 17's fakes will be.
class HostClock : public mm::IClock {
 public:
  bool hasTime() const override { return true; }
  uint32_t now() const override { return 1790172000u; }
  bool fromNetwork() const override { return true; }
  const char* sourceName() const override { return "host"; }
  bool takeServerTime(uint32_t, int32_t& difference) override {
    difference = 0;
    return false;
  }
};

class HostPlatform : public mm::IPlatform {
 public:
  uint32_t millis() override { return ms_; }
  void delayMs(uint32_t ms) override { ms_ += ms; }
  uint32_t random() override { return 4; }
  mm::IClock& clock() override { return clock_; }
  uint32_t freeHeap() override { return 100000; }
  uint32_t minFreeHeap() override { return 90000; }
  [[noreturn]] void rebootForHealth(const char*) override { std::abort(); }
  uint32_t selfReboots() override { return 0; }

 private:
  HostClock clock_;
  uint32_t ms_ = 0;
};

class StringOut : public mm::TextOut {
 public:
  void write(const char* text, size_t len) override { out.append(text, len); }
  std::string out;
};

class NoExtension : public mm::IJobExtension {
 public:
  int typeCount() const override { return 1; }
  const char* type(int) const override { return "walter.sleep"; }
  bool run(const mm::ClaimedJob&, mm::JobContext&, mm::ResultWriter&) override { return false; }
};

}  // namespace

// ---- the portable files -----------------------------------------------------------------------

TEST(Seams, ThePortableFilesIncludeNothingOfTheBoard) {
  const fs::path root(PROJECT_DIR);
  for (const char* rel : kPortable) {
    const std::string text = readFile(root / rel);
    ASSERT_FALSE(text.empty()) << rel << " is missing";
    for (const std::string& name : includesOf(text)) {
      EXPECT_FALSE(isBoardHeader(name)) << rel << " includes " << name;
    }
    const std::string code = withoutComments(text);
    for (const char* id : kBoardIdentifiers) {
      EXPECT_EQ(code.find(id), std::string::npos) << rel << " names " << id;
    }
  }
}

TEST(Seams, TheRunnerAndTheLoopReadNoSettingAndHoldNoStaticScratch) {
  const fs::path root(PROJECT_DIR);
  const std::string loop = readFile(root / "lib/agent-core/src/AgentLoop.cpp");
  EXPECT_EQ(loop.find("findKey("), std::string::npos) << "the heartbeat's radio block comes in as HeartbeatIdentity";
  EXPECT_EQ(loop.find("settings_"), std::string::npos);
  EXPECT_NE(loop.find("identity_->radio"), std::string::npos);

  const std::string header = readFile(root / "lib/agent-core/include/agent/JobRunner.h");
  EXPECT_NE(header.find("struct Scratch {"), std::string::npos);
  EXPECT_NE(header.find("Scratch scratch_;"), std::string::npos) << "the runner owns its scratch";

  const std::string runner = readFile(root / "lib/agent-core/src/JobRunner.cpp");
  const size_t anon = runner.find("namespace {");
  const size_t anonEnd = runner.find("}  // namespace", anon);
  ASSERT_NE(anon, std::string::npos);
  ASSERT_NE(anonEnd, std::string::npos);
  const std::string statics = runner.substr(anon, anonEnd - anon);
  EXPECT_EQ(statics.find("ClaimedJob g_jobs["), std::string::npos) << "the claim's jobs are the scratch's";
  EXPECT_EQ(statics.find("char g_data["), std::string::npos);
  EXPECT_EQ(statics.find("char g_body["), std::string::npos);
  EXPECT_EQ(statics.find("JobLog g_log;"), std::string::npos);
}

// ---- the adapters -------------------------------------------------------------------------------

TEST(Seams, TheBoardsObjectsStandBehindTheInterfaces) {
  const fs::path root(PROJECT_DIR);
  const std::string mesh = readFile(root / "src/mesh/AgentMesh.h");
  EXPECT_NE(mesh.find("class AgentMesh : public mesh::Mesh, public IMesh"), std::string::npos);
  EXPECT_NE(mesh.find("static_assert(kMeshReplyDataCap >= MAX_PACKET_PAYLOAD"), std::string::npos)
      << "MeshCore's payload size is held against the library's constant";
  EXPECT_NE(mesh.find("static_assert(kMeshPathCap >= MAX_PATH_SIZE"), std::string::npos);

  const std::string client = readFile(root / "src/agent/AgentClient.h");
  EXPECT_NE(client.find("class AgentClient : public IUplink"), std::string::npos);
  EXPECT_EQ(client.find("struct Call"), std::string::npos) << "the Call record is the seam's (IUplink.h)";
  EXPECT_EQ(client.find("struct Outcome"), std::string::npos);
  const std::string uplink = readFile(root / "lib/agent-core/include/agent/IUplink.h");
  EXPECT_NE(uplink.find("const char* via = \"\";"), std::string::npos) << "a Call says how it went in one word, not a Root";
  EXPECT_EQ(uplink.find("Root root"), std::string::npos) << "the root is the WiFi client's business";

  const std::string platform = readFile(root / "src/platform/Esp32Platform.h");
  EXPECT_NE(platform.find("class Esp32Platform : public IPlatform"), std::string::npos);
  const std::string clock = readFile(root / "src/net/Clock.h");
  EXPECT_NE(clock.find("class AgentClock : public IClock"), std::string::npos);

  const std::string main = readFile(root / "src/main.cpp");
  EXPECT_NE(main.find("jobs.begin(the_mesh, server, platform, &pumpDuringAJob);"), std::string::npos);
  EXPECT_NE(main.find("agent.begin(server, platform, the_mesh, jobs, identity);"), std::string::npos);
  EXPECT_EQ(main.find("setExtension("), std::string::npos) << "the Heltec sets no extension";
}

// sendCli returns the timestamp it used, drawn by the mesh at the send, and nothing hands one in.
TEST(Seams, TheMeshDrawsTheCliTimestampAndHandsItBack) {
  const fs::path root(PROJECT_DIR);
  const std::string iface = readFile(root / "lib/agent-core/include/agent/IMesh.h");
  EXPECT_NE(iface.find("virtual SendResult sendCli(const char* text, bool sensitive, uint32_t& timestamp) = 0;"),
            std::string::npos);
  // And so the interface asks for no timestamp of its own: task p1.5-17 dropped `uniqueTime()`
  // from it (the follow-up 15 left), because the mesh draws every send's own and the link has no
  // request for one. The in-process mesh keeps the method; the interface does not.
  EXPECT_EQ(iface.find("virtual uint32_t uniqueTime()"), std::string::npos)
      << "IMesh asks for a timestamp again; nothing in the library needs one";

  const std::string mesh = readFile(root / "src/mesh/AgentMesh.cpp");
  const size_t fn = mesh.find("SendResult AgentMesh::sendCli(const char* text, bool sensitive, uint32_t& timestamp) {");
  ASSERT_NE(fn, std::string::npos);
  const size_t drawn = mesh.find("timestamp = uniqueTime();", fn);
  const size_t copied = mesh.find("std::memcpy(temp, &timestamp, 4);", fn);
  ASSERT_NE(drawn, std::string::npos);
  ASSERT_NE(copied, std::string::npos);
  EXPECT_LT(drawn, copied) << "the timestamp in the packet is the one drawn here";

  const std::string runner = readFile(root / "lib/agent-core/src/JobRunner.cpp");
  EXPECT_EQ(runner.find("mesh_->uniqueTime()"), std::string::npos) << "the runner draws no timestamp of its own any more";
  EXPECT_NE(runner.find("mesh_->sendCli(command, sensitive, tag)"), std::string::npos);
  EXPECT_NE(runner.find("uint32_t nextTimestamp() override { return 0; }"), std::string::npos)
      << "the apply engine's series is the host test's; on the board the mesh draws it";
}

// ---- the extension ------------------------------------------------------------------------------

TEST(Seams, AnUnknownTypeIsOfferedToTheExtensionBeforeItIsUnsupported) {
  const fs::path root(PROJECT_DIR);
  const std::string runner = readFile(root / "lib/agent-core/src/JobRunner.cpp");
  const size_t from = runner.find("Outcome JobRunner::runJob(const ClaimedJob& job, Context& ctx, size_t& dataLen) {");
  const size_t to = runner.find("class JobRunner::ExtensionJob", from);
  ASSERT_NE(from, std::string::npos);
  ASSERT_NE(to, std::string::npos);
  const std::string body = runner.substr(from, to - from);
  const size_t offered = body.find("const bool offered = extension_ != nullptr && !isKnownJobType(job.type);");
  const size_t canRun = body.find("if (!offered && !canRun(job, why, sizeof why)) {");
  const size_t handed = body.find("if (offered) return runExtension(job, ctx, dataLen);");
  const size_t unsupported = body.rfind("return Outcome::Unsupported;");
  ASSERT_NE(offered, std::string::npos);
  ASSERT_NE(canRun, std::string::npos);
  ASSERT_NE(handed, std::string::npos);
  ASSERT_NE(unsupported, std::string::npos);
  EXPECT_LT(offered, canRun) << "the thirteen's class rules do not refuse a type the extension may run";
  EXPECT_LT(handed, unsupported) << "offered before unsupported";

  const std::string loop = readFile(root / "lib/agent-core/src/AgentLoop.cpp");
  const size_t hb = loop.find("void AgentLoop::buildHeartbeat(Heartbeat& hb) {");
  const size_t hbEnd = loop.find("\n}", hb);
  ASSERT_NE(hb, std::string::npos);
  const std::string build = loop.substr(hb, hbEnd - hb);
  EXPECT_NE(build.find("jobs_->extension()"), std::string::npos) << "the heartbeat appends the extension's types";
  EXPECT_NE(build.find("extension->type(i)"), std::string::npos);
  EXPECT_LT(build.find("kJobTypes[i]"), build.find("extension->type(i)")) << "the thirteen first";
}

// ---- the interfaces on a host -------------------------------------------------------------------

TEST(Seams, TheInterfacesAreImplementableWithNothingOfTheBoard) {
  HostPlatform platform;
  mm::IPlatform& p = platform;
  EXPECT_EQ(0u, p.millis());
  p.delayMs(5);
  EXPECT_EQ(5u, p.millis());
  int32_t difference = 1;
  EXPECT_FALSE(p.clock().takeServerTime(1790172000u, difference));
  EXPECT_EQ(0, difference);
  EXPECT_TRUE(p.clock().hasTime());
  EXPECT_TRUE(p.clock().fromNetwork());
  EXPECT_STREQ("host", p.clock().sourceName());

  NoExtension extension;
  mm::IJobExtension& e = extension;
  EXPECT_EQ(1, e.typeCount());
  EXPECT_STREQ("walter.sleep", e.type(0));

  // The reply struct is the library's: sized by its own constants, MeshCore's at the pin.
  EXPECT_EQ(184u, mm::kMeshReplyDataCap);
  EXPECT_EQ(64u, mm::kMeshPathCap);
  mm::MeshReply reply;
  EXPECT_EQ(sizeof reply.data, mm::kMeshReplyDataCap);
  EXPECT_EQ(sizeof reply.traceHashes, mm::kMeshPathCap);
  EXPECT_EQ(mm::MeshReply::Kind::None, reply.kind);
}

// TextOut writes Print's bytes: "\r\n" ends a line, printf formats as vsnprintf does.
TEST(Seams, TextOutWritesWhatAStreamWouldHave) {
  StringOut out;
  out.println("state     running");
  out.printf("next      in %ld s (poll %d s, budget %d/min gap %d s)\r\n", 17L, 30, 2, 20);
  out.print("x");
  out.println();
  EXPECT_EQ(std::string("state     running\r\nnext      in 17 s (poll 30 s, budget 2/min gap 20 s)\r\nx\r\n"), out.out);

  // A line longer than the buffer is cut, not written past it.
  StringOut cut;
  std::string longText(500, 'a');
  cut.printf("%s", longText.c_str());
  EXPECT_EQ(383u, cut.out.size());
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
