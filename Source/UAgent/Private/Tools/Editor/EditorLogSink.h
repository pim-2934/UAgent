#pragma once

#include "CoreMinimal.h"
#include "Delegates/IDelegateInstance.h"
#include "HAL/CriticalSection.h"
#include "Misc/OutputDevice.h"

namespace UAgent {
struct FEditorLogLine {
  int64 Id = 0;
  double Time = 0;
  ELogVerbosity::Type Verbosity = ELogVerbosity::Log;
  FName Category;
  FString Message;
};

/**
 * Bounded ring-buffer FOutputDevice. Singleton instance is registered with GLog
 * in FUAgentModule::StartupModule so tools can tail the editor log
 * without replaying the entire session.
 */
class FEditorLogSink : public FOutputDevice {
public:
  explicit FEditorLogSink(int32 InCapacity = 5000);

  virtual void Serialize(const TCHAR *V, ELogVerbosity::Type Verbosity,
                         const FName &Category) override;
  virtual bool CanBeUsedOnAnyThread() const override { return true; }
  virtual bool CanBeUsedOnMultipleThreads() const override { return true; }

  /** Snapshot of lines with Id > SinceId, up to Limit entries. Caller gets the
   * highest Id in OutNextId. */
  void GetLines(int64 SinceId, int32 Limit, FName CategoryContains,
                ELogVerbosity::Type MinVerbosity,
                TArray<FEditorLogLine> &OutLines, int64 &OutNextId) const;

  /**
   * Hook FEditorDelegates::PreBeginPIE so the sink stamps the most recent
   * PIE-start cursor. read_editor_log uses this to clip log results to the
   * current session's PIE run. Idempotent.
   */
  void BindPieDelegates();
  void UnbindPieDelegates();

  /** Log id boundary captured at the most recent PreBeginPIE, or 0 if no PIE
   * has started since the sink was created. Pass this as `SinceId` in
   * GetLines() to receive only lines emitted during the latest PIE run. */
  int64 GetLastPieStartId() const;

  static FEditorLogSink &Get();

private:
  mutable FCriticalSection Mutex;
  TArray<FEditorLogLine> Buffer;
  int32 Capacity;
  int64 NextId = 1;
  int64 LastPieStartId = 0;
  FDelegateHandle PreBeginPieHandle;
};
} // namespace UAgent
