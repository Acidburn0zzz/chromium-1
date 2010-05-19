// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/test/render_view_test.h"

#include "chrome/browser/extensions/extension_function_dispatcher.h"
#include "chrome/common/extensions/extension.h"
#include "chrome/common/native_web_keyboard_event.h"
#include "chrome/common/render_messages.h"
#include "chrome/common/renderer_preferences.h"
#include "chrome/renderer/extensions/event_bindings.h"
#include "chrome/renderer/extensions/extension_process_bindings.h"
#include "chrome/renderer/extensions/js_only_v8_extensions.h"
#include "chrome/renderer/extensions/renderer_extension_bindings.h"
#include "chrome/renderer/mock_render_process.h"
#include "chrome/renderer/renderer_main_platform_delegate.h"
#include "third_party/WebKit/WebKit/chromium/public/WebFrame.h"
#include "third_party/WebKit/WebKit/chromium/public/WebInputEvent.h"
#include "third_party/WebKit/WebKit/chromium/public/WebKit.h"
#include "third_party/WebKit/WebKit/chromium/public/WebScriptController.h"
#include "third_party/WebKit/WebKit/chromium/public/WebScriptSource.h"
#include "third_party/WebKit/WebKit/chromium/public/WebURLRequest.h"
#include "third_party/WebKit/WebKit/chromium/public/WebView.h"
#include "webkit/glue/webkit_glue.h"

#if defined(OS_LINUX)
#include "base/event_synthesis_gtk.h"
#endif

using WebKit::WebFrame;
using WebKit::WebScriptController;
using WebKit::WebScriptSource;
using WebKit::WebString;
using WebKit::WebURLRequest;

namespace {
const int32 kRouteId = 5;
const int32 kOpenerId = 7;
}  // namespace

RenderViewTest::RenderViewTest() {
}

RenderViewTest::~RenderViewTest() {
}

void RenderViewTest::ProcessPendingMessages() {
  msg_loop_.PostTask(FROM_HERE, new MessageLoop::QuitTask());
  msg_loop_.Run();
}

WebFrame* RenderViewTest::GetMainFrame() {
  return view_->webview()->mainFrame();
}

void RenderViewTest::ExecuteJavaScript(const char* js) {
  GetMainFrame()->executeScript(WebScriptSource(WebString::fromUTF8(js)));
}

void RenderViewTest::LoadHTML(const char* html) {
  std::string url_str = "data:text/html;charset=utf-8,";
  url_str.append(html);
  GURL url(url_str);

  GetMainFrame()->loadRequest(WebURLRequest(url));

  // The load actually happens asynchronously, so we pump messages to process
  // the pending continuation.
  ProcessPendingMessages();
}

void RenderViewTest::SetUp() {
  sandbox_init_wrapper_.reset(new SandboxInitWrapper());
  command_line_.reset(new CommandLine(CommandLine::ARGUMENTS_ONLY));
  params_.reset(new MainFunctionParams(*command_line_, *sandbox_init_wrapper_,
                                       NULL));
  platform_.reset(new RendererMainPlatformDelegate(*params_));
  platform_->PlatformInitialize();

  // Setting flags and really doing anything with WebKit is fairly fragile and
  // hacky, but this is the world we live in...
  webkit_glue::SetJavaScriptFlags(L" --expose-gc");
  WebKit::initialize(&webkitclient_);
  WebScriptController::registerExtension(BaseJsV8Extension::Get());
  WebScriptController::registerExtension(JsonSchemaJsV8Extension::Get());
  WebScriptController::registerExtension(EventBindings::Get());
  WebScriptController::registerExtension(ExtensionApiTestV8Extension::Get());
  WebScriptController::registerExtension(ExtensionProcessBindings::Get());
  WebScriptController::registerExtension(RendererExtensionBindings::Get());
  EventBindings::SetRenderThread(&render_thread_);

  // TODO(aa): Should some of this go to some other inheriting class?
  std::vector<std::string> names;
  ExtensionFunctionDispatcher::GetAllFunctionNames(&names);
  ExtensionProcessBindings::SetFunctionNames(names);

  std::vector<std::string> permissions(
      Extension::kPermissionNames,
      Extension::kPermissionNames + Extension::kNumPermissions);
  ExtensionProcessBindings::SetAPIPermissions("", permissions);

  mock_process_.reset(new MockRenderProcess);

  render_thread_.set_routing_id(kRouteId);

  // This needs to pass the mock render thread to the view.
  view_ = RenderView::Create(&render_thread_, 0, kOpenerId,
                             RendererPreferences(), WebPreferences(),
                             new SharedRenderViewCounter(0), kRouteId,
                             kInvalidSessionStorageNamespaceId);

  // Attach a pseudo keyboard device to this object.
  mock_keyboard_.reset(new MockKeyboard());
}

void RenderViewTest::TearDown() {
  // Try very hard to collect garbage before shutting down.
  GetMainFrame()->collectGarbage();
  GetMainFrame()->collectGarbage();

  render_thread_.SendCloseMessage();

  // Run the loop so the release task from the renderwidget executes.
  ProcessPendingMessages();

  EventBindings::SetRenderThread(NULL);

  view_ = NULL;

  mock_process_.reset();

  // After resetting the view_ and mock_process_ we may get some new tasks
  // which need to be processed before shutting down WebKit
  // (http://crbug.com/21508).
  msg_loop_.RunAllPending();

  WebKit::shutdown();

  mock_keyboard_.reset();

  platform_->PlatformUninitialize();
  platform_.reset();
  params_.reset();
  command_line_.reset();
  sandbox_init_wrapper_.reset();
}

int RenderViewTest::SendKeyEvent(MockKeyboard::Layout layout,
                                 int key_code,
                                 MockKeyboard::Modifiers modifiers,
                                 std::wstring* output) {
#if defined(OS_WIN)
  // Retrieve the Unicode character for the given tuple (keyboard-layout,
  // key-code, and modifiers).
  // Exit when a keyboard-layout driver cannot assign a Unicode character to
  // the tuple to prevent sending an invalid key code to the RenderView object.
  CHECK(mock_keyboard_.get());
  CHECK(output);
  int length = mock_keyboard_->GetCharacters(layout, key_code, modifiers,
                                             output);
  if (length != 1)
    return -1;

  // Create IPC messages from Windows messages and send them to our
  // back-end.
  // A keyboard event of Windows consists of three Windows messages:
  // WM_KEYDOWN, WM_CHAR, and WM_KEYUP.
  // WM_KEYDOWN and WM_KEYUP sends virtual-key codes. On the other hand,
  // WM_CHAR sends a composed Unicode character.
  NativeWebKeyboardEvent keydown_event(NULL, WM_KEYDOWN, key_code, 0);
  SendNativeKeyEvent(keydown_event);

  NativeWebKeyboardEvent char_event(NULL, WM_CHAR, (*output)[0], 0);
  SendNativeKeyEvent(char_event);

  NativeWebKeyboardEvent keyup_event(NULL, WM_KEYUP, key_code, 0);
  SendNativeKeyEvent(keyup_event);

  return length;
#elif defined(OS_LINUX)
  // We ignore |layout|, which means we are only testing the layout of the
  // current locale. TODO(estade): fix this to respect |layout|.
  std::vector<GdkEvent*> events;
  base::SynthesizeKeyPressEvents(
      NULL, static_cast<base::KeyboardCode>(key_code),
      modifiers & (MockKeyboard::LEFT_CONTROL | MockKeyboard::RIGHT_CONTROL),
      modifiers & (MockKeyboard::LEFT_SHIFT | MockKeyboard::RIGHT_SHIFT),
      modifiers & (MockKeyboard::LEFT_ALT | MockKeyboard::RIGHT_ALT),
      &events);

  guint32 unicode_key = 0;
  for (size_t i = 0; i < events.size(); ++i) {
    // Only send the up/down events for key press itself (skip the up/down
    // events for the modifier keys).
    if ((i + 1) == (events.size() / 2) || i == (events.size() / 2)) {
      unicode_key = gdk_keyval_to_unicode(events[i]->key.keyval);
      NativeWebKeyboardEvent webkit_event(&events[i]->key);
      SendNativeKeyEvent(webkit_event);

      // Need to add a char event after the key down.
      if (webkit_event.type == WebKit::WebInputEvent::RawKeyDown) {
        NativeWebKeyboardEvent char_event = webkit_event;
        char_event.type = WebKit::WebInputEvent::Char;
        char_event.skip_in_browser = true;
        SendNativeKeyEvent(char_event);
      }
    }
    gdk_event_free(events[i]);
  }

  *output = std::wstring(1, unicode_key);
  return 1;
#else
  NOTIMPLEMENTED();
  return L'\0';
#endif
}

void RenderViewTest::SendNativeKeyEvent(
    const NativeWebKeyboardEvent& key_event) {
  scoped_ptr<IPC::Message> input_message(new ViewMsg_HandleInputEvent(0));
  input_message->WriteData(reinterpret_cast<const char*>(&key_event),
                           sizeof(WebKit::WebKeyboardEvent));
  view_->OnHandleInputEvent(*input_message);
}
