#include "Headers.hpp"

#include "gtest/gtest.h"

#include "BiDirectionalRpc.hpp"

namespace codefs {
class RpcTest : public testing::Test {
 protected:
  virtual void SetUp() { srand(1); }
};

class ReflectionHandler : public BiDirectionalRpc::Handler {
 public:
  ReflectionHandler(bool _flaky) : flaky(_flaky) {}
  virtual pair<bool, string> request(const string& payload) {
    if (flaky && rand() % 2 != 0) {
      // 50% drop rate
      return make_pair(false, "");
    }
    return make_pair(true, payload + payload);
  }

 protected:
  bool flaky;
};

void runServer(const string& address, bool bind, bool flaky) {
  {
    ReflectionHandler handler(flaky);
    BiDirectionalRpc server(&handler, address, bind);
    sleep(3);

    vector<string> payloads = {"Hello", "World", "How", "Are", "You", "Today"};
    vector<sole::uuid> uids;

    for (int a = 0; a < 500; a++) {
      server.update();
      usleep(10 * 1000);
      if (a && a % 10 == 0) {
        server.heartbeat();
      }
      if (a < payloads.size()) {
        uids.push_back(server.request(payloads[a]));
      }
    }

    for (int a = 0; a < payloads.size(); a++) {
      EXPECT_EQ(server.hasReply(uids[a]), true);
      EXPECT_EQ(server.getReply(uids[a]), payloads[a] + payloads[a]);
    }

    server.shutdown();
    LOG(INFO) << "DESTROYING SERVER";
  }

  LOG(INFO) << "SERVER EXITING";
}

TEST_F(RpcTest, ReadWrite) {
  char dirSchema[] = "/tmp/TestRpc.XXXXXX";
  string dirName = mkdtemp(dirSchema);
  string address = string("ipc://") + dirName + "/ipc";

  thread serverThread(runServer, address, true, false);
  sleep(1);
  thread clientThread(runServer, address, false, false);
  serverThread.join();
  clientThread.join();

  boost::filesystem::remove_all(dirName);
}

TEST_F(RpcTest, FlakyReadWrite) {
  char dirSchema[] = "/tmp/TestRpc.XXXXXX";
  string dirName = mkdtemp(dirSchema);
  string address = string("ipc://") + dirName + "/ipc";

  thread serverThread(runServer, address, true, true);
  sleep(1);
  thread clientThread(runServer, address, false, true);
  serverThread.join();
  clientThread.join();

  boost::filesystem::remove_all(dirName);
}
}  // namespace codefs
