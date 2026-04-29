#include "../../Protocol/ACPToolRegistry.h"
#include "../BuiltinTools.h"
#include "FsPathUtils.h"

#include "Misc/FileHelper.h"

namespace UAgent {
namespace {
class FFsWriteTextFileTool : public IACPTool {
public:
  virtual FString GetMethod() const override {
    return TEXT("fs/write_text_file");
  }

  virtual bool IsReadOnly() const override { return false; }

  virtual FToolResponse
  Execute(const TSharedPtr<FJsonObject> &Params) override {
    if (!Params.IsValid())
      return FToolResponse::InvalidParams(TEXT("missing params"));

    FString Path, Content;
    Params->TryGetStringField(TEXT("path"), Path);
    Params->TryGetStringField(TEXT("content"), Content);
    if (Path.IsEmpty()) {
      return FToolResponse::InvalidParams(TEXT("missing path"));
    }
    if (!FsPathUtils::IsInsideProject(Path)) {
      return FToolResponse::Fail(-32603, TEXT("path outside project"));
    }

    if (!FFileHelper::SaveStringToFile(Content, *Path)) {
      return FToolResponse::Fail(
          -32000, FString::Printf(TEXT("write failed: %s"), *Path));
    }
    return FToolResponse::Ok();
  }
};
} // namespace

TSharedRef<IACPTool> CreateFsWriteTextFileTool() {
  return MakeShared<FFsWriteTextFileTool>();
}
} // namespace UAgent
