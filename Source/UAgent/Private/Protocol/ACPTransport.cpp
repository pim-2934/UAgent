#include "ACPTransport.h"
#include "ACPTypes.h"
#include "HAL/RunnableThread.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace UAgent {
FACPTransport::FACPTransport() = default;

FACPTransport::~FACPTransport() { Shutdown(); }

bool FACPTransport::Start(const FString &Command, const TArray<FString> &Args,
                          const FString &WorkingDir) {
  if (bRunning) {
    UE_LOG(LogUAgent, Warning,
           TEXT("FACPTransport::Start called while already running"));
    return false;
  }

  if (Command.IsEmpty()) {
    UE_LOG(LogUAgent, Error,
           TEXT("FACPTransport::Start: empty AgentCommand — configure in "
                "Project Settings → Plugins → UAgent"));
    return false;
  }

  // For stdin the child inherits the READ end; pass bWritePipeLocal=true so the
  // parent's write handle is NOT inheritable and the child's read handle IS.
  // For stdout/stderr the default is correct (child inherits the write end).
  if (!FPlatformProcess::CreatePipe(StdinReadChild, StdinWriteParent,
                                    /*bWritePipeLocal=*/true) ||
      !FPlatformProcess::CreatePipe(StdoutReadParent, StdoutWriteChild) ||
      !FPlatformProcess::CreatePipe(StderrReadParent, StderrWriteChild)) {
    UE_LOG(LogUAgent, Error, TEXT("FACPTransport::Start: CreatePipe failed"));
    return false;
  }

  FString JoinedArgs;
  for (const FString &A : Args) {
    if (!JoinedArgs.IsEmpty())
      JoinedArgs += TEXT(" ");
    JoinedArgs +=
        A.Contains(TEXT(" ")) ? FString::Printf(TEXT("\"%s\""), *A) : A;
  }

  uint32 OutProcessId = 0;
  const TCHAR *WorkDirPtr = WorkingDir.IsEmpty() ? nullptr : *WorkingDir;

  ProcHandle = FPlatformProcess::CreateProc(
      *Command, *JoinedArgs,
      /*bLaunchDetached=*/false,
      /*bLaunchHidden=*/true,
      /*bLaunchReallyHidden=*/true, &OutProcessId,
      /*PriorityModifier=*/0, WorkDirPtr,
      /*PipeWriteChild (child stdout)=*/StdoutWriteChild,
      /*PipeReadChild  (child stdin) =*/StdinReadChild,
      /*PipeStdErrChild              =*/StderrWriteChild);

  if (!ProcHandle.IsValid() || !FPlatformProcess::IsProcRunning(ProcHandle)) {
    UE_LOG(LogUAgent, Error,
           TEXT("FACPTransport::Start: failed to launch '%s %s'"), *Command,
           *JoinedArgs);
    FPlatformProcess::ClosePipe(StdinReadChild, StdinWriteParent);
    FPlatformProcess::ClosePipe(StdoutReadParent, StdoutWriteChild);
    FPlatformProcess::ClosePipe(StderrReadParent, StderrWriteChild);
    StdinReadChild = StdinWriteParent = nullptr;
    StdoutReadParent = StdoutWriteChild = nullptr;
    StderrReadParent = StderrWriteChild = nullptr;
    ProcHandle.Reset();
    return false;
  }

  bStopRequested = false;
  bExitReported = false;
  bRunning = true;

  Thread = FRunnableThread::Create(this, TEXT("UAgentTransport"), 0,
                                   TPri_BelowNormal);

  TickerHandle = FTSTicker::GetCoreTicker().AddTicker(
      FTickerDelegate::CreateRaw(this, &FACPTransport::DrainInbound), 0.0f);

  UE_LOG(LogUAgent, Log, TEXT("FACPTransport started: %s %s (pid=%u)"),
         *Command, *JoinedArgs, OutProcessId);
  return true;
}

void FACPTransport::Shutdown() {
  if (!bRunning && !ProcHandle.IsValid() && Thread == nullptr) {
    return;
  }

  bStopRequested = true;

  if (ProcHandle.IsValid()) {
    if (FPlatformProcess::IsProcRunning(ProcHandle)) {
      FPlatformProcess::TerminateProc(ProcHandle, /*bKillTree=*/true);
    }
    FPlatformProcess::CloseProc(ProcHandle);
    ProcHandle.Reset();
  }

  if (Thread) {
    Thread->WaitForCompletion();
    delete Thread;
    Thread = nullptr;
  }

  if (StdinReadChild || StdinWriteParent) {
    FPlatformProcess::ClosePipe(StdinReadChild, StdinWriteParent);
    StdinReadChild = StdinWriteParent = nullptr;
  }
  if (StdoutReadParent || StdoutWriteChild) {
    FPlatformProcess::ClosePipe(StdoutReadParent, StdoutWriteChild);
    StdoutReadParent = StdoutWriteChild = nullptr;
  }
  if (StderrReadParent || StderrWriteChild) {
    FPlatformProcess::ClosePipe(StderrReadParent, StderrWriteChild);
    StderrReadParent = StderrWriteChild = nullptr;
  }

  if (TickerHandle.IsValid()) {
    FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
    TickerHandle.Reset();
  }

  DrainInbound(0.0f);

  bRunning = false;

  if (!bExitReported) {
    bExitReported = true;
    if (OnExit.IsBound()) {
      OnExit.Execute(LastExitCode, StderrText);
    }
  }
}

bool FACPTransport::Send(const TSharedRef<FJsonObject> &Message) {
  if (!StdinWriteParent) {
    return false;
  }

  FString Serialized;
  TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
      TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(
          &Serialized);
  if (!FJsonSerializer::Serialize(Message, Writer)) {
    UE_LOG(LogUAgent, Error, TEXT("FACPTransport::Send: serialize failed"));
    return false;
  }

  // Use the binary WritePipe overload: the FString overload auto-appends its
  // own '\n' on top of ours, producing a blank line between messages that
  // strict NDJSON parsers (codex-acp's serde_json) reject as "EOF while
  // parsing a value at line 2 column 0". The lenient claude-agent-acp parser
  // happens to skip blanks, which masked this until we tried Codex.
  FTCHARToUTF8 Utf8(*Serialized, Serialized.Len());
  TArray<uint8> Bytes;
  Bytes.Append(reinterpret_cast<const uint8 *>(Utf8.Get()), Utf8.Length());
  Bytes.Add(static_cast<uint8>('\n'));

  FScopeLock Lock(&WriteCs);
  int32 BytesWritten = 0;
  const bool bOk = FPlatformProcess::WritePipe(
      StdinWriteParent, Bytes.GetData(), Bytes.Num(), &BytesWritten);
  UE_CLOG(!bOk, LogUAgent, Error,
          TEXT("FACPTransport::Send: WritePipe returned false"));
  UE_CLOG(bOk, LogUAgent, VeryVerbose, TEXT(">> %s"), *Serialized);
  return bOk;
}

// Drain already-buffered bytes for Buffer, calling LineSink(FString) on each
// trimmed UTF-8 line. Remaining partial-line bytes are left in Buffer so the
// next chunk can complete them — this is what keeps multi-byte UTF-8
// codepoints intact when they straddle ReadPipe chunks.
template <typename LineSink>
static void SplitLines(TArray<uint8> &Buffer, LineSink &&OnLine) {
  int32 LineStart = 0;
  for (int32 i = 0; i < Buffer.Num(); ++i) {
    if (Buffer[i] != '\n')
      continue;

    int32 LineLen = i - LineStart;
    if (LineLen > 0 && Buffer[LineStart + LineLen - 1] == '\r')
      --LineLen;

    if (LineLen > 0) {
      FUTF8ToTCHAR Conv(
          reinterpret_cast<const ANSICHAR *>(Buffer.GetData() + LineStart),
          LineLen);
      FString Line(Conv.Length(), Conv.Get());
      Line.TrimStartAndEndInline();
      if (!Line.IsEmpty()) {
        OnLine(Line);
      }
    }
    LineStart = i + 1;
  }
  if (LineStart > 0) {
    Buffer.RemoveAt(0, LineStart);
  }
}

uint32 FACPTransport::Run() {
  while (!bStopRequested) {
    if (ProcHandle.IsValid() && !FPlatformProcess::IsProcRunning(ProcHandle)) {
      int32 ExitCode = 0;
      FPlatformProcess::GetProcReturnCode(ProcHandle, &ExitCode);
      LastExitCode = ExitCode;
      break;
    }

    if (StdoutReadParent) {
      TArray<uint8> Chunk;
      if (FPlatformProcess::ReadPipeToArray(StdoutReadParent, Chunk) &&
          Chunk.Num() > 0) {
        StdoutBuffer.Append(Chunk);
        SplitLines(StdoutBuffer, [this](const FString &Line) {
          TSharedPtr<FJsonObject> Parsed;
          const TSharedRef<TJsonReader<>> Reader =
              TJsonReaderFactory<>::Create(Line);
          if (FJsonSerializer::Deserialize(Reader, Parsed) &&
              Parsed.IsValid()) {
            InboundQueue.Enqueue(Parsed);
          } else {
            UE_LOG(LogUAgent, Warning,
                   TEXT("FACPTransport: failed to parse line: %s"), *Line);
          }
        });
      }
    }

    if (StderrReadParent) {
      TArray<uint8> ErrChunk;
      if (FPlatformProcess::ReadPipeToArray(StderrReadParent, ErrChunk) &&
          ErrChunk.Num() > 0) {
        StderrBuffer.Append(ErrChunk);
        SplitLines(StderrBuffer, [this](const FString &Line) {
          UE_LOG(LogUAgent, Log, TEXT("[agent stderr] %s"), *Line);
          if (!StderrText.IsEmpty())
            StderrText.AppendChar(TEXT('\n'));
          StderrText.Append(Line);
          // Cap stderr retention so a long session doesn't leak memory;
          // OnExit only needs the tail for diagnostics anyway.
          constexpr int32 MaxStderrChars = 64 * 1024;
          if (StderrText.Len() > MaxStderrChars) {
            StderrText.RightChopInline(StderrText.Len() - MaxStderrChars,
                                       EAllowShrinking::No);
          }
        });
      }
    }

    FPlatformProcess::Sleep(0.01f);
  }
  return 0;
}

void FACPTransport::Exit() {}

bool FACPTransport::DrainInbound(float /*Dt*/) {
  TSharedPtr<FJsonObject> Msg;
  while (InboundQueue.Dequeue(Msg)) {
    if (Msg.IsValid() && OnMessage.IsBound()) {
      OnMessage.Execute(Msg.ToSharedRef());
    }
  }

  if (ProcHandle.IsValid() && !FPlatformProcess::IsProcRunning(ProcHandle) &&
      !bExitReported) {
    int32 ExitCode = 0;
    FPlatformProcess::GetProcReturnCode(ProcHandle, &ExitCode);
    LastExitCode = ExitCode;
    bExitReported = true;
    if (OnExit.IsBound()) {
      OnExit.Execute(ExitCode, StderrText);
    }
  }

  return true;
}
} // namespace UAgent
