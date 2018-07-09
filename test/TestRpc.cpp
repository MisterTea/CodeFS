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
  virtual pair<bool, string> request(const string& payload) {
    return make_pair(true, payload + payload);
  }
};

void runServer(const string& address) {
  ReflectionHandler handler;
  BiDirectionalRpc server(&handler, address, true);
  sleep(3);

  auto firstUuid = server.request("Hello");
  auto secondUuid = server.request("World");

  for(int a=0;a<100;a++) {
    server.update();
    usleep(10 * 1000);
  }

  EXPECT_EQ(server.hasReply(firstUuid), true);
  EXPECT_EQ(server.hasReply(secondUuid), true);
  EXPECT_EQ(server.getReply(firstUuid), "HelloHello");
  EXPECT_EQ(server.getReply(secondUuid), "WorldWorld");
}

void runClient(const string& address) {
  ReflectionHandler handler;
  BiDirectionalRpc client(&handler, address, false);
  sleep(3);

  auto firstUuid = client.request("hello");
  auto secondUuid = client.request("world");

  for(int a=0;a<100;a++) {
    client.update();
    usleep(10 * 1000);
  }

  EXPECT_EQ(client.hasReply(firstUuid), true);
  EXPECT_EQ(client.hasReply(secondUuid), true);
  EXPECT_EQ(client.getReply(firstUuid), "hellohello");
  EXPECT_EQ(client.getReply(secondUuid), "worldworld");
}

TEST_F(RpcTest, ReadWrite) {
  char dirSchema[] = "/tmp/TestRpc.XXXXXX";
  string dirName = mkdtemp(dirSchema);
  string address = string("ipc://") + dirName + "/ipc";

  thread serverThread(runServer, address);
  sleep(1);
  thread clientThread(runClient, address);
  serverThread.join();
  clientThread.join();

  boost::filesystem::remove_all(dirName);
}
}  // namespace codefs
