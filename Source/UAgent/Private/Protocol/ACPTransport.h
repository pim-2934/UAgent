#pragma once

#include "Containers/Queue.h"
#include "Containers/Ticker.h"
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "HAL/PlatformProcess.h"
#include "HAL/Runnable.h"
#include "HAL/ThreadSafeBool.h"

class FRunnableThread;

namespace UAgent {
DECLARE_DELEGATE_OneParam(FOnAcpMessage, TSharedRef<FJsonObject> /*Message*/);
DECLARE_DELEGATE_TwoParams(FOnAcpTransportExit, int32 /*ExitCode*/,
                           FString /*Stderr*/);

/**
 * Abstract ACP transport. Production uses FACPTransport (subprocess stdio);
 * tests inject a mock that feeds canned messages into OnMessage without ever
 * spawning a process. FACPClient depends only on this interface.
 */
class IACPTransport {
public:
  virtual ~IACPTransport() = default;

  virtual bool Start(const FString &Command, const TArray<FString> &Args,
                     const FString &WorkingDir) = 0;

  /** Terminate the subprocess and join the reader thread. Safe to call twice.
   */
  virtual void Shutdown() = 0;

  virtual bool IsRunning() const = 0;

  /** Serialize a JSON object and write it as a single \n-terminated line. */
  virtual bool Send(const TSharedRef<FJsonObject> &Message) = 0;

  /** Fired on the game thread whenever a complete inbound JSON-RPC message has
   * been parsed. */
  FOnAcpMessage OnMessage;

  /** Fired on the game thread when the subprocess exits. */
  FOnAcpTransportExit OnExit;
};

/**
 * Subprocess-stdio transport. Spawns an external ACP agent process and pumps
 * newline-delimited JSON-RPC messages in both directions. Inbound messages
 * are parsed on a worker thread and dispatched onto the game thread via
 * FTSTicker.
 */
class FACPTransport : public IACPTransport, public FRunnable {
public:
  FACPTransport();
  virtual ~FACPTransport() override;

  virtual bool Start(const FString &Command, const TArray<FString> &Args,
                     const FString &WorkingDir) override;

  virtual void Shutdown() override;

  virtual bool IsRunning() const override { return bRunning; }

  virtual bool Send(const TSharedRef<FJsonObject> &Message) override;

protected:
  // FRunnable
  virtual uint32 Run() override;
  virtual void Stop() override { bStopRequested = true; }
  virtual void Exit() override;

private:
  bool DrainInbound(float Dt);

  FProcHandle ProcHandle;
  void *StdinReadChild = nullptr;
  void *StdinWriteParent = nullptr;
  void *StdoutReadParent = nullptr;
  void *StdoutWriteChild = nullptr;
  void *StderrReadParent = nullptr;
  void *StderrWriteChild = nullptr;

  FRunnableThread *Thread = nullptr;
  FThreadSafeBool bStopRequested;
  bool bRunning = false;
  FThreadSafeBool bExitReported;

  FCriticalSection WriteCs;
  // Raw byte buffers — UTF-8 is only decoded once a complete line has
  // been accumulated, so multi-byte codepoints straddling ReadPipe
  // chunk boundaries don't get mangled.
  TArray<uint8> StdoutBuffer;
  TArray<uint8> StderrBuffer;
  FString StderrText;

  // Store TSharedPtr (not TSharedRef) because TQueue default-constructs its
  // sentinel node's element, and TSharedRef has no public default constructor.
  TQueue<TSharedPtr<FJsonObject>, EQueueMode::Mpsc> InboundQueue;

  FTSTicker::FDelegateHandle TickerHandle;
  int32 LastExitCode = 0;
};
} // namespace UAgent
