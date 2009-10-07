// Copyright (c) 2009 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_EXTENSIONS_EXECUTE_CODE_IN_TAB_FUNCTION_H__
#define CHROME_BROWSER_EXTENSIONS_EXECUTE_CODE_IN_TAB_FUNCTION_H__

#include <string>
#include <vector>

#include "base/file_path.h"
#include "chrome/browser/extensions/extension_function.h"
#include "chrome/common/extensions/extension_resource.h"
#include "chrome/common/notification_service.h"
#include "chrome/common/notification_registrar.h"

class MessageLoop;

// Implement API call tabs.executeScript and tabs.insertCSS.
class ExecuteCodeInTabFunction : public AsyncExtensionFunction,
                                 public NotificationObserver {
 public:
  ExecuteCodeInTabFunction() : execute_tab_id_(-1) {}

 private:
  virtual bool RunImpl();

  virtual void Observe(NotificationType type,
                       const NotificationSource& source,
                       const NotificationDetails& details);

  // Called when contents from the file whose path is specified in JSON
  // arguments has been loaded.
  void DidLoadFile(bool success, const std::string& data);

  // Run in UI thread.  Code string contains the code to be executed.
  void Execute(const std::string& code_string);

  NotificationRegistrar registrar_;

  // Id of tab which executes code.
  int execute_tab_id_;

  // Contains extension resource built from path of file which is
  // specified in JSON arguments.
  ExtensionResource resource_;
};

#endif  // CHROME_BROWSER_EXTENSIONS_EXECUTE_CODE_IN_TAB_FUNCTION_H__
