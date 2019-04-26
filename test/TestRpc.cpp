#include "Headers.hpp"

#include "gtest/gtest.h"

#include "ZmqBiDirectionalRpc.hpp"

namespace codefs {
class RpcTest : public testing::Test {
 protected:
  virtual void SetUp() { srand(1); }
};

void runServer(const string& address, int* tasksLeft, bool bind, bool flaky,
               bool barrier, bool reconnect) {
  {
    ZmqBiDirectionalRpc server(address, bind);
    server.setFlaky(flaky);
    sleep(3);

    vector<string> payloads = {"Hello", "World", "How", "Are", "You", "Today"};
    map<RpcId, string> uidPayloadMap;

    for (int a = 0; a <= 100; a++) {
      server.update();
      usleep(10 * 1000);
      if (reconnect && a % 50 == 3) {
        server.reconnect();
      }
      if (a && a % 10 == 0) {
        server.heartbeat();
      }
      if (a < payloads.size()) {
        if (barrier && a % 2 == 0) {
          server.barrier();
        }
        uidPayloadMap.insert(
            make_pair(server.request(payloads[a]), payloads[a] + payloads[a]));
      }
      while (server.hasIncomingRequest()) {
        auto idPayload = server.getFirstIncomingRequest();
        server.reply(idPayload.id, idPayload.payload + idPayload.payload);
      }
    }

    bool done = false;
    for (int a = 0; *tasksLeft; a++) {
      if (reconnect && rand() % 500 == 0) {
        server.reconnect();
      }
      usleep(10 * 1000);
      server.update();
      if (a % 100 == 0) {
        server.heartbeat();
      }
      while (server.hasIncomingRequest()) {
        auto idPayload = server.getFirstIncomingRequest();
        server.reply(idPayload.id, idPayload.payload + idPayload.payload);
      }
      while (server.hasIncomingReply()) {
        auto reply = server.getFirstIncomingReply();
        auto it = uidPayloadMap.find(reply.id);
        EXPECT_NE(it, uidPayloadMap.end());
        EXPECT_EQ(it->second, reply.payload);
        uidPayloadMap.erase(it);
      }
      if (!done && uidPayloadMap.empty()) {
        done = true;
        (*tasksLeft)--;
      }
    }

    // TODO: We may still have work to do so check for other server to be done
    sleep(3);

    EXPECT_TRUE(uidPayloadMap.empty());

    server.shutdown();
    LOG(INFO) << "DESTROYING SERVER";
  }

  LOG(INFO) << "SERVER EXITING";
}

TEST_F(RpcTest, ReadWrite) {
  char dirSchema[] = "/tmp/TestRpc.XXXXXX";
  string dirName = mkdtemp(dirSchema);
  string address = string("ipc://") + dirName + "/ipc";

  int tasksLeft = 2;
  thread serverThread(runServer, address, &tasksLeft, true, false, false,
                      false);
  sleep(1);
  thread clientThread(runServer, address, &tasksLeft, false, false, false,
                      false);
  serverThread.join();
  clientThread.join();

  boost::filesystem::remove_all(dirName);
}

TEST_F(RpcTest, FlakyReadWrite) {
  char dirSchema[] = "/tmp/TestRpc.XXXXXX";
  string dirName = mkdtemp(dirSchema);
  string address = string("ipc://") + dirName + "/ipc";

  int tasksLeft = 2;
  thread serverThread(runServer, address, &tasksLeft, true, true, false, false);
  sleep(1);
  thread clientThread(runServer, address, &tasksLeft, false, true, false,
                      false);
  serverThread.join();
  clientThread.join();

  boost::filesystem::remove_all(dirName);
}

TEST_F(RpcTest, FlakyReadWriteDisconnect) {
  char dirSchema[] = "/tmp/TestRpc.XXXXXX";
  string dirName = mkdtemp(dirSchema);
  string address = string("ipc://") + dirName + "/ipc";

  int tasksLeft = 2;
  thread serverThread(runServer, address, &tasksLeft, true, true, false, true);
  sleep(1);
  thread clientThread(runServer, address, &tasksLeft, false, true, false, true);
  serverThread.join();
  clientThread.join();

  boost::filesystem::remove_all(dirName);
}

TEST_F(RpcTest, FlakyReadWriteBarrier) {
  char dirSchema[] = "/tmp/TestRpc.XXXXXX";
  string dirName = mkdtemp(dirSchema);
  string address = string("ipc://") + dirName + "/ipc";

  int tasksLeft = 2;
  thread serverThread(runServer, address, &tasksLeft, true, true, true, false);
  sleep(1);
  thread clientThread(runServer, address, &tasksLeft, false, true, true, false);
  serverThread.join();
  clientThread.join();

  boost::filesystem::remove_all(dirName);
}
}  // namespace codefs
