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
  EXPECT_NE(runner.find("mesh_->sendCli(line, sensitive, tag)"), std::string::npos)
      << "the runner hands the line and its sensitivity and takes the timestamp back; `line` is the command with "
         "the console prefix the repeater reflects on it since task p1.10-49, and `nextCliPrefix` draws that prefix "
         "in the same lambda, before the send";
  const size_t cli = runner.find("Outcome JobRunner::cliLine(");
  ASSERT_NE(cli, std::string::npos);
  const size_t prefixDrawn = runner.find("nextCliPrefix(job, ctx, command, escape);", cli);
  const size_t lineSent = runner.find("mesh_->sendCli(line, sensitive, tag)", cli);
  ASSERT_NE(prefixDrawn, std::string::npos);
  ASSERT_NE(lineSent, std::string::npos);
  EXPECT_LT(prefixDrawn, lineSent) << "the prefix on the wire is the one drawn for this attempt";
  EXPECT_NE(runner.find("uint32_t nextTimestamp() override { return 0; }"), std::string::npos)
      << "the apply engine's series is the host test's; on the board the mesh draws it";
}

// The width the library traces a direct neighbour at is the mesh's own, and the interface's
// default is the rail's two bytes (task p1.10-39; the rail is README.md's *path hashes are two
// bytes or more, never one*, meshmaster.dk's D-33).
TEST(Seams, TheMeshTellsTheLibraryTheWidthItFloodsAt) {
  const fs::path root(PROJECT_DIR);
  const std::string iface = readFile(root / "lib/agent-core/include/agent/IMesh.h");
  EXPECT_NE(iface.find("virtual uint8_t floodPathHashSize() const { return 2; }"), std::string::npos)
      << "the default is the rail's width, so a mesh that overrides nothing never traces at one byte";

  const std::string mesh = readFile(root / "src/mesh/AgentMesh.h");
  EXPECT_NE(mesh.find("uint8_t floodPathHashSize() const override { return kFloodPathHashSize; }"), std::string::npos)
      << "this board answers with the width its floods originate at";

  // And the runner asks, in the one place it chooses a width at all: a direct neighbour, where no
  // hop was learned. Nowhere in it is a width a literal.
  const std::string runner = readFile(root / "lib/agent-core/src/JobRunner.cpp");
  const size_t from = runner.find("Outcome JobRunner::runTrace(");
  ASSERT_NE(from, std::string::npos);
  const size_t to = runner.find("\nOutcome JobRunner::", from + 1);
  ASSERT_NE(to, std::string::npos);
  const std::string body = runner.substr(from, to - from);
  EXPECT_NE(body.find("hashSize = mesh_->floodPathHashSize();"), std::string::npos);
  EXPECT_EQ(runner.find("hashSize = 1"), std::string::npos) << "the library picks no path-hash width of its own";
}

// The same door, asked **before** the job (task p1.10-43): the wake prices a claimed job, and the
// price has to come from the same question the run will ask. The shape of that, in one place: the
// new predicate is defaulted false on the interface, it is the job's own repeater key the runner
// hands it, and `wakeJobs` asks it **once** and reads that one answer into both bounds - the
// transaction price and the millisecond estimate - so the two can never disagree with each other
// or with `spend`.
TEST(Seams, TheWakeAsksTheSameDoorBeforeItPricesAJob) {
  const fs::path root(PROJECT_DIR);
  const std::string iface = readFile(root / "lib/agent-core/include/agent/IMesh.h");
  const size_t declared = iface.find("virtual bool answersLocallyFor(const char* publicKeyHex) const {");
  ASSERT_NE(declared, std::string::npos) << "the door is asked for a key, not for the job in progress";
  EXPECT_NE(iface.find("return false;", declared), std::string::npos)
      << "the default is the air, so a mesh that overrides nothing is priced as it always was";

  // One predicate, in the runner, from the job's own repeater key - the same shape as
  // meshDeclines() and asked by the same caller.
  const std::string runner = readFile(root / "lib/agent-core/src/JobRunner.cpp");
  EXPECT_NE(runner.find("return mesh_->answersLocallyFor(job.repeaterKey);"), std::string::npos);

  const std::string loop = readFile(root / "lib/agent-core/src/AgentLoop.cpp");
  const size_t from = loop.find("void AgentLoop::wakeJobs(WakeReport& report) {");
  ASSERT_NE(from, std::string::npos);
  const size_t to = loop.find("\nvoid AgentLoop::wakeObservations", from);
  ASSERT_NE(to, std::string::npos);
  const std::string body = loop.substr(from, to - from);
  const std::string theQuestion = "const bool local = jobs_->meshAnswersLocally(job);";
  const size_t asked = body.find(theQuestion);
  ASSERT_NE(asked, std::string::npos);
  EXPECT_EQ(body.find("meshAnswersLocally", asked + theQuestion.size()), std::string::npos)
      << "one question, asked once a job";
  const size_t priced = body.find("const int price = local ? 0 : transactionsOf(job);");
  const size_t timed = body.find("estimateJobMs(job, local)");
  ASSERT_NE(priced, std::string::npos) << "a local job spends no transaction, so it is priced at none";
  ASSERT_NE(timed, std::string::npos);
  EXPECT_LT(asked, priced) << "the door is read before the transaction price";
  EXPECT_LT(asked, timed) << "and before the estimate";
  EXPECT_EQ(loop.find("estimateJobMs(job)"), std::string::npos) << "nothing prices a job without telling it the door";
}

// The local door (task p1.10-40): the mesh says a job is answered off the air, and the gate is
// the only thing that changes. The shape of the decision, in one place - the default is false, so
// a consumer's tree compiles and behaves as it did; this board has no local door and keeps it;
// and in `spend` the lease is checked **before** the door is read, because a lease is the
// server's claim on the repeater and has nothing to do with the air.
TEST(Seams, TheMeshTellsTheLibraryWhenAJobIsNotOnTheAir) {
  const fs::path root(PROJECT_DIR);
  const std::string iface = readFile(root / "lib/agent-core/include/agent/IMesh.h");
  EXPECT_NE(iface.find("virtual bool answersLocally() const { return false; }"), std::string::npos)
      << "the default is the air, so a mesh that overrides nothing is paced as it always was";

  const std::string mesh = readFile(root / "src/mesh/AgentMesh.h");
  EXPECT_EQ(mesh.find("answersLocally"), std::string::npos)
      << "this board's radio is its own: AgentMesh keeps the interface's default";

  const std::string runner = readFile(root / "lib/agent-core/src/JobRunner.cpp");
  const size_t from = runner.find("bool JobRunner::spend(const ClaimedJob& job, Context& ctx, bool escape) {");
  ASSERT_NE(from, std::string::npos);
  const size_t to = runner.find("\n}\n", from);
  ASSERT_NE(to, std::string::npos);
  const std::string body = runner.substr(from, to - from);
  const size_t lease = body.find("if (!escape && leaseRanOut(job)) return false;");
  const size_t local = body.find("if (mesh_->answersLocally()) return true;");
  const size_t gate = body.find("gate_.waitMs(millis())");
  const size_t took = body.find("gate_.took(millis());");
  const size_t counted = body.find("ctx.transactions++;");
  ASSERT_NE(lease, std::string::npos);
  ASSERT_NE(local, std::string::npos);
  ASSERT_NE(gate, std::string::npos);
  ASSERT_NE(took, std::string::npos);
  ASSERT_NE(counted, std::string::npos);
  EXPECT_LT(lease, local) << "a local job still ends when its lease does";
  EXPECT_LT(local, gate) << "and never waits for the budget";
  EXPECT_LT(local, took) << "nor spends it";
  EXPECT_LT(local, counted) << "nor counts a transaction: EXIT-NODES §7 defines one as an exchange over the mesh";
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

// The interface says what it is (task p1.6-23). The mesh is asked what it cannot carry in the two
// places that matter - where the advertised list is built, and before a job is begun on it - and
// the Heltec's own mesh answers nothing of its own, so the board advertises the thirteen as it
// always did. And the two fields of MeshReply every mesh filled and nothing read are gone from
// the interface, the board's mesh and the fake alike.
TEST(Seams, TheMeshIsAskedWhatItCannotCarryAtTheHeartbeatAndBeforeAJobAndTheHeltecsMeshAnswersNothing) {
  const fs::path root(PROJECT_DIR);
  const std::string iface = readFile(root / "lib/agent-core/include/agent/IMesh.h");
  EXPECT_NE(iface.find("virtual bool supports(const char* jobType) const {"), std::string::npos)
      << "the question, with a default: a mesh that does not answer carries everything, as every mesh did until now";
  EXPECT_EQ(iface.find("bool viaPath"), std::string::npos) << "MeshReply::viaPath is gone";
  EXPECT_EQ(iface.find("float snr"), std::string::npos) << "MeshReply::snr is gone";

  const std::string loop = readFile(root / "lib/agent-core/src/AgentLoop.cpp");
  const size_t hb = loop.find("void AgentLoop::buildHeartbeat(Heartbeat& hb) {");
  const size_t hbEnd = loop.find("\n}", hb);
  ASSERT_NE(hb, std::string::npos);
  const std::string build = loop.substr(hb, hbEnd - hb);
  EXPECT_NE(build.find("if (mesh_->supports(kJobTypes[i])) jobTypes_[count++] = kJobTypes[i];"), std::string::npos)
      << "each of the thirteen is advertised only if the mesh carries it";
  EXPECT_NE(build.find("hb.transport = identity_->transport;"), std::string::npos) << "the transport is the identity's";
  EXPECT_EQ(build.find("\"onboard\""), std::string::npos) << "and no longer a constant of the loop's";

  const std::string runner = readFile(root / "lib/agent-core/src/JobRunner.cpp");
  const size_t from = runner.find("Outcome JobRunner::runJob(const ClaimedJob& job, Context& ctx, size_t& dataLen) {");
  const size_t to = runner.find("class JobRunner::ExtensionJob", from);
  ASSERT_NE(from, std::string::npos);
  const std::string body = runner.substr(from, to - from);
  const size_t asked = body.find("if (meshDeclines(job)) {");
  const size_t begun = body.find("mesh_->beginJob(");
  ASSERT_NE(asked, std::string::npos) << "the runner asks the mesh for one of the thirteen";
  ASSERT_NE(begun, std::string::npos);
  EXPECT_LT(asked, begun) << "asked before the job is begun on the mesh, so nothing is sent for a type it declined";
  EXPECT_NE(body.find("return Outcome::Unsupported;", asked), std::string::npos);
  EXPECT_LT(body.find("return Outcome::Unsupported;", asked), begun) << "and the answer is unsupported";
  // And the question itself is one function, so that a caller which prices a job before running
  // it asks the same thing the runner does (task p1.7-25).
  EXPECT_NE(runner.find("return !offered && !mesh_->supports(job.type);"), std::string::npos)
      << "meshDeclines() is where the question lives";

  const std::string board = readFile(root / "src/mesh/AgentMesh.h");
  EXPECT_EQ(board.find("supports("), std::string::npos)
      << "the Heltec's mesh takes the default and advertises exactly what it did before";
  EXPECT_EQ(board.find("viaPath"), std::string::npos);
  const std::string boardCpp = readFile(root / "src/mesh/AgentMesh.cpp");
  EXPECT_EQ(boardCpp.find("reply_.snr"), std::string::npos);
  EXPECT_EQ(boardCpp.find("viaPath"), std::string::npos);
  const std::string fake = readFile(root / "test/support/FakeMesh.h");
  EXPECT_EQ(fake.find("viaPath"), std::string::npos);
  EXPECT_EQ(fake.find("reply.snr"), std::string::npos);
}

// A wake that **prices** each claimed job before it runs it asks that same question first (task
// p1.7-25). A type the mesh declines spends no transaction, so there is no budget for it to be
// too large for; priced first, a bound `trace` on a mesh with no trace came back `failed`,
// *wake budget*, where `loop()` answered `unsupported` for the same job.
TEST(Seams, AWakeAsksTheMeshWhetherItCarriesAJobBeforeItPricesIt) {
  const fs::path root(PROJECT_DIR);
  const std::string loop = readFile(root / "lib/agent-core/src/AgentLoop.cpp");
  const size_t from = loop.find("void AgentLoop::wakeJobs(WakeReport& report) {");
  ASSERT_NE(from, std::string::npos);
  const size_t to = loop.find("\n}", from);
  ASSERT_NE(to, std::string::npos);
  const std::string body = loop.substr(from, to - from);
  const size_t asked = body.find("if (jobs_->meshDeclines(job)) {");
  const size_t priced = body.find("transactionsOf(job);");
  ASSERT_NE(asked, std::string::npos) << "the wake asks the runner's own question";
  ASSERT_NE(priced, std::string::npos);
  EXPECT_LT(asked, priced) << "asked before the job is priced";
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
