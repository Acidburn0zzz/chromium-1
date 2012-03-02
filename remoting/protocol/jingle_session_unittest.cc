// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "remoting/protocol/jingle_session.h"

#include "base/bind.h"
#include "base/message_loop.h"
#include "base/time.h"
#include "base/test/test_timeouts.h"
#include "net/socket/socket.h"
#include "net/socket/stream_socket.h"
#include "remoting/base/constants.h"
#include "remoting/protocol/authenticator.h"
#include "remoting/protocol/channel_authenticator.h"
#include "remoting/protocol/connection_tester.h"
#include "remoting/protocol/fake_authenticator.h"
#include "remoting/protocol/jingle_session_manager.h"
#include "remoting/protocol/libjingle_transport_factory.h"
#include "remoting/jingle_glue/jingle_thread.h"
#include "remoting/jingle_glue/fake_signal_strategy.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

using testing::_;
using testing::AtMost;
using testing::DeleteArg;
using testing::DoAll;
using testing::InSequence;
using testing::Invoke;
using testing::InvokeWithoutArgs;
using testing::Return;
using testing::SaveArg;
using testing::SetArgumentPointee;
using testing::WithArg;

namespace remoting {
namespace protocol {

namespace {

const char kHostJid[] = "host1@gmail.com/123";
const char kClientJid[] = "host2@gmail.com/321";

// Send 100 messages 1024 bytes each. UDP messages are sent with 10ms delay
// between messages (about 1 second for 100 messages).
const int kMessageSize = 1024;
const int kMessages = 100;
const char kChannelName[] = "test_channel";

void QuitCurrentThread() {
  MessageLoop::current()->PostTask(FROM_HERE, MessageLoop::QuitClosure());
}

ACTION(QuitThread) {
  QuitCurrentThread();
}

ACTION_P(QuitThreadOnCounter, counter) {
  --(*counter);
  EXPECT_GE(*counter, 0);
  if (*counter == 0)
    QuitCurrentThread();
}

class MockSessionManagerListener : public SessionManager::Listener {
 public:
  MOCK_METHOD0(OnSessionManagerReady, void());
  MOCK_METHOD2(OnIncomingSession,
               void(Session*,
                    SessionManager::IncomingSessionResponse*));
};

class MockSessionCallback {
 public:
  MOCK_METHOD1(OnStateChange, void(Session::State));
};

class MockStreamChannelCallback {
 public:
  MOCK_METHOD1(OnDone, void(net::StreamSocket* socket));
};

}  // namespace

class JingleSessionTest : public testing::Test {
 public:
  JingleSessionTest() {
    talk_base::ThreadManager::Instance()->WrapCurrentThread();
    message_loop_.reset(
        new JingleThreadMessageLoop(talk_base::Thread::Current()));
  }

  // Helper method that handles OnIncomingSession().
  void SetHostSession(Session* session) {
    DCHECK(session);
    host_session_.reset(session);
    host_session_->SetStateChangeCallback(
        base::Bind(&MockSessionCallback::OnStateChange,
                   base::Unretained(&host_connection_callback_)));

    session->set_config(SessionConfig::GetDefault());
  }

  void OnClientChannelCreated(scoped_ptr<net::StreamSocket> socket) {
    client_channel_callback_.OnDone(socket.get());
    client_socket_ = socket.Pass();
  }

  void OnHostChannelCreated(scoped_ptr<net::StreamSocket> socket) {
    host_channel_callback_.OnDone(socket.get());
    host_socket_ = socket.Pass();
  }

 protected:
  virtual void SetUp() {
  }

  virtual void TearDown() {
    CloseSessions();
    CloseSessionManager();
    message_loop_->RunAllPending();
  }

  void CloseSessions() {
    host_socket_.reset();
    host_session_.reset();
    client_socket_.reset();
    client_session_.reset();
  }

  void CreateSessionManagers(int auth_round_trips,
                        FakeAuthenticator::Action auth_action) {
    host_signal_strategy_.reset(new FakeSignalStrategy(kHostJid));
    client_signal_strategy_.reset(new FakeSignalStrategy(kClientJid));
    FakeSignalStrategy::Connect(host_signal_strategy_.get(),
                                client_signal_strategy_.get());

    EXPECT_CALL(host_server_listener_, OnSessionManagerReady())
        .Times(1);
    host_server_.reset(new JingleSessionManager(
        scoped_ptr<TransportFactory>(new LibjingleTransportFactory())));
    host_server_->Init(host_signal_strategy_.get(), &host_server_listener_,
                       NetworkSettings(false));

    scoped_ptr<AuthenticatorFactory> factory(
        new FakeHostAuthenticatorFactory(auth_round_trips, auth_action, true));
    host_server_->set_authenticator_factory(factory.Pass());

    EXPECT_CALL(client_server_listener_, OnSessionManagerReady())
        .Times(1);
    client_server_.reset(new JingleSessionManager(
        scoped_ptr<TransportFactory>(new LibjingleTransportFactory())));
    client_server_->Init(client_signal_strategy_.get(),
                         &client_server_listener_, NetworkSettings(
                             TransportConfig::NAT_TRAVERSAL_OUTGOING));
  }

  void CloseSessionManager() {
    if (host_server_.get()) {
      host_server_->Close();
      host_server_.reset();
    }
    if (client_server_.get()) {
      client_server_->Close();
      client_server_.reset();
    }
    host_signal_strategy_.reset();
    client_signal_strategy_.reset();
  }

  void InitiateConnection(int auth_round_trips,
                          FakeAuthenticator::Action auth_action,
                          bool expect_fail) {
    EXPECT_CALL(host_server_listener_, OnIncomingSession(_, _))
        .WillOnce(DoAll(
            WithArg<0>(Invoke(this, &JingleSessionTest::SetHostSession)),
            SetArgumentPointee<1>(protocol::SessionManager::ACCEPT)));

    {
      InSequence dummy;

      EXPECT_CALL(host_connection_callback_,
                  OnStateChange(Session::CONNECTED))
          .Times(AtMost(1));
      if (expect_fail) {
        EXPECT_CALL(host_connection_callback_,
                    OnStateChange(Session::FAILED))
            .Times(1);
      } else {
        EXPECT_CALL(host_connection_callback_,
                    OnStateChange(Session::AUTHENTICATED))
            .Times(1);
        // Expect that the connection will be closed eventually.
        EXPECT_CALL(host_connection_callback_,
                    OnStateChange(Session::CLOSED))
            .Times(AtMost(1));
      }
    }

    {
      InSequence dummy;

      EXPECT_CALL(client_connection_callback_,
                  OnStateChange(Session::CONNECTING))
          .Times(1);
      EXPECT_CALL(client_connection_callback_,
                  OnStateChange(Session::CONNECTED))
          .Times(AtMost(1));
      if (expect_fail) {
        EXPECT_CALL(client_connection_callback_,
                    OnStateChange(Session::FAILED))
            .Times(1);
      } else {
        EXPECT_CALL(client_connection_callback_,
                    OnStateChange(Session::AUTHENTICATED))
            .Times(1);
        // Expect that the connection will be closed eventually.
        EXPECT_CALL(client_connection_callback_,
                    OnStateChange(Session::CLOSED))
            .Times(AtMost(1));
      }
    }

    scoped_ptr<Authenticator> authenticator(new FakeAuthenticator(
        FakeAuthenticator::CLIENT, auth_round_trips, auth_action, true));

    client_session_ = client_server_->Connect(
        kHostJid, authenticator.Pass(),
        CandidateSessionConfig::CreateDefault(),
        base::Bind(&MockSessionCallback::OnStateChange,
                   base::Unretained(&client_connection_callback_)));

    message_loop_->RunAllPending();
  }

  void CreateChannel() {
    client_session_->CreateStreamChannel(kChannelName, base::Bind(
        &JingleSessionTest::OnClientChannelCreated, base::Unretained(this)));
    host_session_->CreateStreamChannel(kChannelName, base::Bind(
        &JingleSessionTest::OnHostChannelCreated, base::Unretained(this)));

    int counter = 2;
    EXPECT_CALL(client_channel_callback_, OnDone(_))
        .WillOnce(QuitThreadOnCounter(&counter));
    EXPECT_CALL(host_channel_callback_, OnDone(_))
        .WillOnce(QuitThreadOnCounter(&counter));
    message_loop_->Run();

    EXPECT_TRUE(client_socket_.get());
    EXPECT_TRUE(host_socket_.get());
  }

  scoped_ptr<JingleThreadMessageLoop> message_loop_;

  scoped_ptr<FakeSignalStrategy> host_signal_strategy_;
  scoped_ptr<FakeSignalStrategy> client_signal_strategy_;

  scoped_ptr<JingleSessionManager> host_server_;
  MockSessionManagerListener host_server_listener_;
  scoped_ptr<JingleSessionManager> client_server_;
  MockSessionManagerListener client_server_listener_;

  scoped_ptr<Session> host_session_;
  MockSessionCallback host_connection_callback_;
  scoped_ptr<Session> client_session_;
  MockSessionCallback client_connection_callback_;

  MockStreamChannelCallback client_channel_callback_;
  MockStreamChannelCallback host_channel_callback_;

  scoped_ptr<net::StreamSocket> client_socket_;
  scoped_ptr<net::StreamSocket> host_socket_;
};


// Verify that we can create and destroy session managers without a
// connection.
TEST_F(JingleSessionTest, CreateAndDestoy) {
  CreateSessionManagers(1, FakeAuthenticator::ACCEPT);
}

// Verify that an incoming session can be rejected, and that the
// status of the connection is set to FAILED in this case.
TEST_F(JingleSessionTest, RejectConnection) {
  CreateSessionManagers(1, FakeAuthenticator::ACCEPT);

  // Reject incoming session.
  EXPECT_CALL(host_server_listener_, OnIncomingSession(_, _))
      .WillOnce(SetArgumentPointee<1>(protocol::SessionManager::DECLINE));

  {
    InSequence dummy;

    EXPECT_CALL(client_connection_callback_,
                OnStateChange(Session::CONNECTING))
        .Times(1);
    EXPECT_CALL(client_connection_callback_,
                OnStateChange(Session::FAILED))
        .Times(1);
  }

  scoped_ptr<Authenticator> authenticator(new FakeAuthenticator(
      FakeAuthenticator::CLIENT, 1, FakeAuthenticator::ACCEPT, true));
  client_session_ = client_server_->Connect(
      kHostJid, authenticator.Pass(), CandidateSessionConfig::CreateDefault(),
      base::Bind(&MockSessionCallback::OnStateChange,
                 base::Unretained(&client_connection_callback_)));

  message_loop_->RunAllPending();
}

// Verify that we can connect two endpoints with single-step authentication.
TEST_F(JingleSessionTest, Connect) {
  CreateSessionManagers(1, FakeAuthenticator::ACCEPT);
  InitiateConnection(1, FakeAuthenticator::ACCEPT, false);

  // Verify that the client specified correct initiator value.
  ASSERT_GT(host_signal_strategy_->received_messages().size(), 0U);
  const buzz::XmlElement* initiate_xml =
      host_signal_strategy_->received_messages().front();
  const buzz::XmlElement* jingle_element =
      initiate_xml->FirstNamed(buzz::QName(kJingleNamespace, "jingle"));
  ASSERT_TRUE(jingle_element);
  ASSERT_EQ(kClientJid,
            jingle_element->Attr(buzz::QName("", "initiator")));
}

// Verify that we can connect two endpoints with multi-step authentication.
TEST_F(JingleSessionTest, ConnectWithMultistep) {
  CreateSessionManagers(3, FakeAuthenticator::ACCEPT);
  InitiateConnection(3, FakeAuthenticator::ACCEPT, false);
}

// Verify that connection is terminated when single-step auth fails.
TEST_F(JingleSessionTest, ConnectWithBadAuth) {
  CreateSessionManagers(1, FakeAuthenticator::REJECT);
  InitiateConnection(1, FakeAuthenticator::ACCEPT, true);
}

// Verify that connection is terminated when multi-step auth fails.
TEST_F(JingleSessionTest, ConnectWithBadMultistepAuth) {
  CreateSessionManagers(3, FakeAuthenticator::REJECT);
  InitiateConnection(3, FakeAuthenticator::ACCEPT, true);
}

// Verify that data can be sent over stream channel.
TEST_F(JingleSessionTest, TestStreamChannel) {
  CreateSessionManagers(1, FakeAuthenticator::ACCEPT);
  ASSERT_NO_FATAL_FAILURE(
      InitiateConnection(1, FakeAuthenticator::ACCEPT, false));

  ASSERT_NO_FATAL_FAILURE(CreateChannel());

  StreamConnectionTester tester(host_socket_.get(), client_socket_.get(),
                                kMessageSize, kMessages);
  tester.Start();
  message_loop_->Run();
  tester.CheckResults();
}

// Verify that we can connect channels with multistep auth.
TEST_F(JingleSessionTest, TestMultistepAuthStreamChannel) {
  CreateSessionManagers(3, FakeAuthenticator::ACCEPT);
  ASSERT_NO_FATAL_FAILURE(
      InitiateConnection(3, FakeAuthenticator::ACCEPT, false));

  ASSERT_NO_FATAL_FAILURE(CreateChannel());

  StreamConnectionTester tester(host_socket_.get(), client_socket_.get(),
                                kMessageSize, kMessages);
  tester.Start();
  message_loop_->Run();
  tester.CheckResults();
}

// Verify that we shutdown properly when channel authentication fails.
TEST_F(JingleSessionTest, TestFailedChannelAuth) {
  CreateSessionManagers(1, FakeAuthenticator::REJECT_CHANNEL);
  ASSERT_NO_FATAL_FAILURE(
      InitiateConnection(1, FakeAuthenticator::ACCEPT, false));

  client_session_->CreateStreamChannel(kChannelName, base::Bind(
      &JingleSessionTest::OnClientChannelCreated, base::Unretained(this)));
  host_session_->CreateStreamChannel(kChannelName, base::Bind(
      &JingleSessionTest::OnHostChannelCreated, base::Unretained(this)));

  // Terminate the message loop when we get rejection notification
  // from the host.
  EXPECT_CALL(host_channel_callback_, OnDone(NULL))
      .WillOnce(QuitThread());
  EXPECT_CALL(client_channel_callback_, OnDone(_))
      .Times(AtMost(1));

  message_loop_->Run();

  EXPECT_TRUE(!host_socket_.get());
}

}  // namespace protocol
}  // namespace remoting
