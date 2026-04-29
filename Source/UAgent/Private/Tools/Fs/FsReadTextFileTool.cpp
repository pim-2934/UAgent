#include "../../Protocol/ACPToolRegistry.h"
#include "../BuiltinTools.h"
#include "FsPathUtils.h"

#include "Misc/FileHelper.h"

namespace UAgent {
namespace {
class FFsReadTextFileTool : public IACPTool {
public:
  virtual FString GetMethod() const override {
    return TEXT("fs/read_text_file");
  }

  virtual bool IsReadOnly() const override { return true; }

  virtual FToolResponse
  Execute(const TSharedPtr<FJsonObject> &Params) override {
    if (!Params.IsValid())
      return FToolResponse::InvalidParams(TEXT("missing params"));

    FString Path;
    Params->TryGetStringField(TEXT("path"), Path);
    if (Path.IsEmpty()) {
      return FToolResponse::InvalidParams(TEXT("missing path"));
    }
    if (!FsPathUtils::IsInsideProject(Path)) {
      return FToolResponse::Fail(-32603, TEXT("path outside project"));
    }

    FString Content;
    if (!FFileHelper::LoadFileToString(Content, *Path)) {
      return FToolResponse::Fail(
          -32000, FString::Printf(TEXT("read failed: %s"), *Path));
    }

    int32 LineStart = 1;
    int32 LineLimit = INT32_MAX;
    Params->TryGetNumberField(TEXT("line"), LineStart);
    Params->TryGetNumberField(TEXT("limit"), LineLimit);
    if (LineStart > 1 || LineLimit < INT32_MAX) {
      TArray<FString> Lines;
      Content.ParseIntoArrayLines(Lines, /*CullEmpty=*/false);
      TArray<FString> Slice;
      for (int32 i = FMath::Max(1, LineStart) - 1;
           i < Lines.Num() && Slice.Num() < LineLimit; ++i) {
        Slice.Add(Lines[i]);
      }
      Content = FString::Join(Slice, TEXT("\n"));
    }

    TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("content"), Content);
    return FToolResponse::Ok(Result);
  }
};
} // namespace

TSharedRef<IACPTool> CreateFsReadTextFileTool() {
  return MakeShared<FFsReadTextFileTool>();
}
} // namespace UAgent
