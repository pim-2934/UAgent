#include "EditorLogSink.h"

#include "HAL/PlatformTime.h"

namespace UAgent {
FEditorLogSink::FEditorLogSink(int32 InCapacity)
    : Capacity(FMath::Max(InCapacity, 64)) {
  Buffer.Reserve(Capacity);
}

FEditorLogSink &FEditorLogSink::Get() {
  static FEditorLogSink Instance;
  return Instance;
}

void FEditorLogSink::Serialize(const TCHAR *V, ELogVerbosity::Type Verbosity,
                               const FName &Category) {
  if (!V)
    return;

  FScopeLock Lock(&Mutex);
  if (Buffer.Num() >= Capacity) {
    Buffer.RemoveAt(0, 1, EAllowShrinking::No);
  }

  FEditorLogLine Line;
  Line.Id = NextId++;
  Line.Time = FPlatformTime::Seconds();
  Line.Verbosity = Verbosity;
  Line.Category = Category;
  Line.Message = V;
  Buffer.Add(MoveTemp(Line));
}

void FEditorLogSink::GetLines(int64 SinceId, int32 Limit,
                              FName CategoryContains,
                              ELogVerbosity::Type MinVerbosity,
                              TArray<FEditorLogLine> &OutLines,
                              int64 &OutNextId) const {
  FScopeLock Lock(&Mutex);
  OutNextId = NextId;

  const FString CategoryStr =
      CategoryContains.IsNone() ? FString() : CategoryContains.ToString();
  for (const FEditorLogLine &L : Buffer) {
    if (L.Id <= SinceId)
      continue;
    if (L.Verbosity > MinVerbosity)
      continue; // lower verbosity value = higher severity
    if (!CategoryStr.IsEmpty() &&
        !L.Category.ToString().Contains(CategoryStr, ESearchCase::IgnoreCase))
      continue;

    OutLines.Add(L);
    if (OutLines.Num() >= Limit)
      break;
  }
}
} // namespace UAgent
