// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file defines FileStream::Context class.
// The general design of FileStream is as follows: file_stream.h defines
// FileStream class which basically is just an "wrapper" not containing any
// specific implementation details. It re-routes all its method calls to
// the instance of FileStream::Context (FileStream holds a scoped_ptr to
// FileStream::Context instance). Context was extracted into a different class
// to be able to do and finish all async operations even when FileStream
// instance is deleted. So FileStream's destructor can schedule file
// closing to be done by Context in WorkerPool (or the TaskRunner passed to
// constructor) and then just return (releasing Context pointer from
// scoped_ptr) without waiting for actual closing to complete.
// Implementation of FileStream::Context is divided in two parts: some methods
// and members are platform-independent and some depend on the platform. This
// header file contains the complete definition of Context class including all
// platform-dependent parts (because of that it has a lot of #if-#else
// branching). Implementations of all platform-independent methods are
// located in file_stream_context.cc, and all platform-dependent methods are
// in file_stream_context_{win,posix}.cc. This separation provides better
// readability of Context's code. And we tried to make as much Context code
// platform-independent as possible. So file_stream_context_{win,posix}.cc are
// much smaller than file_stream_context.cc now.

#ifndef NET_BASE_FILE_STREAM_CONTEXT_H_
#define NET_BASE_FILE_STREAM_CONTEXT_H_

#include <stdint.h>

#include "base/files/file.h"
#include "base/logging.h"
#include "base/message_loop/message_pump_for_io.h"
#include "base/task/single_thread_task_runner.h"
#include "base/task/task_runner.h"
#include "base/types/expected.h"
#include "build/build_config.h"
#include "net/base/completion_once_callback.h"
#include "net/base/file_stream.h"
#include "net/base/net_errors.h"

#if BUILDFLAG(IS_POSIX) || BUILDFLAG(IS_FUCHSIA)
#include <errno.h>
#endif

namespace base {
class FilePath;
}

namespace net {

class IOBuffer;

// Implementation for a FileStream. See file_stream.h for documentation.
#if BUILDFLAG(IS_POSIX) || BUILDFLAG(IS_FUCHSIA)
class FileStream::Context {
#endif

 public:
  ////////////////////////////////////////////////////////////////////////////
  // Platform-dependent methods implemented in
  // file_stream_context_{win,posix}.cc.
  ////////////////////////////////////////////////////////////////////////////

  explicit Context(scoped_refptr<base::TaskRunner> task_runner);
  Context(base::File file, scoped_refptr<base::TaskRunner> task_runner);
  Context(const Context&) = delete;
  Context& operator=(const Context&) = delete;
#if BUILDFLAG(IS_POSIX) || BUILDFLAG(IS_FUCHSIA)
  ~Context();
#endif

  int Read(IOBuffer* buf, int buf_len, CompletionOnceCallback callback);

  int Write(IOBuffer* buf, int buf_len, CompletionOnceCallback callback);


  bool async_in_progress() const { return async_in_progress_; }

  ////////////////////////////////////////////////////////////////////////////
  // Platform-independent methods implemented in file_stream_context.cc.
  ////////////////////////////////////////////////////////////////////////////

  // Destroys the context. It can be deleted in the method or deletion can be
  // deferred if some asynchronous operation is now in progress or if file is
  // not closed yet.
  void Orphan();

  void Open(const base::FilePath& path,
            int open_flags,
            CompletionOnceCallback callback);

  void Close(CompletionOnceCallback callback);

  // Seeks |offset| bytes from the start of the file.
  void Seek(int64_t offset, FileStream::SeekCallback callback);

  void GetFileInfo(base::File::Info* file_info,
                   CompletionOnceCallback callback);

  void Flush(CompletionOnceCallback callback);

  bool IsOpen() const;

 private:
  struct IOResult {
    IOResult();
    IOResult(int64_t result, logging::SystemErrorCode os_error);
    static IOResult FromOSError(logging::SystemErrorCode os_error);

    int64_t result;
    logging::SystemErrorCode os_error;  // Set only when result < 0.
  };

  struct OpenResult {
   public:
    OpenResult();
    OpenResult(base::File file, IOResult error_code);
    OpenResult(OpenResult&& other);
    OpenResult& operator=(OpenResult&& other);
    OpenResult(const OpenResult&) = delete;
    OpenResult& operator=(const OpenResult&) = delete;

    base::File file;
    IOResult error_code;
  };

  ////////////////////////////////////////////////////////////////////////////
  // Platform-independent methods implemented in file_stream_context.cc.
  ////////////////////////////////////////////////////////////////////////////

  OpenResult OpenFileImpl(const base::FilePath& path, int open_flags);

  IOResult GetFileInfoImpl(base::File::Info* file_info);

  IOResult CloseFileImpl();

  IOResult FlushFileImpl();

  void OnOpenCompleted(CompletionOnceCallback callback, OpenResult open_result);

  void CloseAndDelete();

  // Called when Open(), Close(), GetFileInfo(), or Flush() completes.
  // |result| contains the result or a network error code.
  void OnAsyncCompleted(CompletionOnceCallback callback,
                        const IOResult& result);

  // Called when Seek() completes. Creates a base::expected result from the
  // IOResult and runs the callback.
  void OnSeekCompleted(FileStream::SeekCallback callback,
                       const IOResult& result);

  ////////////////////////////////////////////////////////////////////////////
  // Platform-dependent methods implemented in
  // file_stream_context_{win,posix}.cc.
  ////////////////////////////////////////////////////////////////////////////

  // Adjusts the position from where the data is read.
  IOResult SeekFileImpl(int64_t offset);

  void OnFileOpened();

#if BUILDFLAG(IS_POSIX) || BUILDFLAG(IS_FUCHSIA)
  // ReadFileImpl() is a simple wrapper around read() that handles EINTR
  // signals and calls RecordAndMapError() to map errno to net error codes.
  IOResult ReadFileImpl(scoped_refptr<IOBuffer> buf, int buf_len);

  // WriteFileImpl() is a simple wrapper around write() that handles EINTR
  // signals and calls MapSystemError() to map errno to net error codes.
  // It tries to write to completion.
  IOResult WriteFileImpl(scoped_refptr<IOBuffer> buf, int buf_len);
#endif  // BUILDFLAG(IS_WIN)

  base::File file_;
  bool async_in_progress_ = false;

  bool orphaned_ = false;
  const scoped_refptr<base::TaskRunner> task_runner_;

};

}  // namespace net

#endif  // NET_BASE_FILE_STREAM_CONTEXT_H_
