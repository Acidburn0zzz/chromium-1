// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SYNC_API_ATTACHMENTS_ATTACHMENT_SERVICE_PROXY_H_
#define SYNC_API_ATTACHMENTS_ATTACHMENT_SERVICE_PROXY_H_

#include "base/basictypes.h"
#include "base/callback.h"
#include "base/memory/scoped_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/sequenced_task_runner.h"
#include "base/task_runner.h"
#include "sync/api/attachments/attachment.h"
#include "sync/api/attachments/attachment_service.h"
#include "sync/base/sync_export.h"

namespace syncer {

class SyncData;

// AttachmentServiceProxy wraps an AttachmentService allowing multiple threads
// to share the wrapped AttachmentService.
//
// Callbacks passed to methods on this class will be invoked in the same thread
// from which the method was called.
//
// This class does not own its wrapped AttachmentService object.  This class
// holds a WeakPtr to the wrapped object.  Once the the wrapped object is
// destroyed, method calls on this object will be no-ops.
//
//  Users of this class should take care to destroy the wrapped object on the
//  correct thread (wrapped_task_runner).
//
// This class is thread-safe.
class SYNC_EXPORT AttachmentServiceProxy : public AttachmentService {
 public:
  // Default copy and assignment are welcome.

  // Construct an invalid AttachmentServiceProxy.
  AttachmentServiceProxy();

  // Construct an AttachmentServiceProxy that forwards calls to |wrapped| on the
  // |wrapped_task_runner| thread.
  AttachmentServiceProxy(
      const scoped_refptr<base::SequencedTaskRunner>& wrapped_task_runner,
      base::WeakPtr<syncer::AttachmentService> wrapped);

  virtual ~AttachmentServiceProxy();

  // AttachmentService implementation.
  virtual void GetOrDownloadAttachments(const AttachmentIdList& attachment_ids,
                                        const GetOrDownloadCallback& callback)
      OVERRIDE;
  virtual void DropAttachments(const AttachmentIdList& attachment_ids,
                               const DropCallback& callback) OVERRIDE;
  virtual void OnSyncDataAdd(const SyncData& sync_data) OVERRIDE;
  virtual void OnSyncDataDelete(const SyncData& sync_data) OVERRIDE;
  virtual void OnSyncDataUpdate(const AttachmentIdList& old_attachment_ids,
                                const SyncData& updated_sync_data) OVERRIDE;

 private:
  scoped_refptr<base::SequencedTaskRunner> wrapped_task_runner_;
  // wrapped_ must only be dereferenced on the wrapped_task_runner_ thread.
  base::WeakPtr<AttachmentService> wrapped_;
};

}  // namespace syncer

#endif  // SYNC_API_ATTACHMENTS_ATTACHMENT_SERVICE_PROXY_H_
