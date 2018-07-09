#include "Headers.hpp"

#include "gtest/gtest.h"

#include "BiDirectionalRpc.hpp"

namespace codefs {
class RpcTest : public testing::Test {
 protected:
  virtual void SetUp() { srand(1); }
};

void runServer(const string& address, bool bind, bool flaky, bool barrier) {
  {
    BiDirectionalRpc server(address, bind);
    server.setFlaky(flaky);
    sleep(3);

    vector<string> payloads = {"Hello", "World", "How", "Are", "You", "Today"};
    vector<RpcId> uids;

    for (int a = 0; a < 500; a++) {
      server.update();
      usleep(10 * 1000);
      if (a && a % 10 == 0) {
        server.heartbeat();
      }
      if (a < payloads.size()) {
        if (barrier && a % 2 == 0) {
          server.barrier();
        }
        uids.push_back(server.request(payloads[a]));
      }
      while (server.hasIncomingRequest()) {
        auto idPayload = server.consumeIncomingRequest();
        server.reply(idPayload.id, idPayload.payload + idPayload.payload);
      }
    }

    for (int a = 0; a < payloads.size(); a++) {
      EXPECT_EQ(server.hasIncomingReply(uids[a]), true);
      EXPECT_EQ(server.consumeIncomingReply(uids[a]),
                payloads[a] + payloads[a]);
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

  thread serverThread(runServer, address, true, false, false);
  sleep(1);
  thread clientThread(runServer, address, false, false, false);
  serverThread.join();
  clientThread.join();

  boost::filesystem::remove_all(dirName);
}

TEST_F(RpcTest, FlakyReadWrite) {
  char dirSchema[] = "/tmp/TestRpc.XXXXXX";
  string dirName = mkdtemp(dirSchema);
  string address = string("ipc://") + dirName + "/ipc";

  thread serverThread(runServer, address, true, true, false);
  sleep(1);
  thread clientThread(runServer, address, false, true, false);
  serverThread.join();
  clientThread.join();

  boost::filesystem::remove_all(dirName);
}

TEST_F(RpcTest, FlakyReadWriteBarrier) {
  char dirSchema[] = "/tmp/TestRpc.XXXXXX";
  string dirName = mkdtemp(dirSchema);
  string address = string("ipc://") + dirName + "/ipc";

  thread serverThread(runServer, address, true, true, true);
  sleep(1);
  thread clientThread(runServer, address, false, true, true);
  serverThread.join();
  clientThread.join();

  boost::filesystem::remove_all(dirName);
}
}  // namespace codefs
