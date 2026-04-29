#include "UAgentStyle.h"
#include "Framework/Application/SlateApplication.h"
#include "Interfaces/IPluginManager.h"
#include "Slate/SlateGameResources.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "Styling/SlateStyleMacros.h"
#include "Styling/SlateStyleRegistry.h"
#include "Styling/SlateTypes.h"

#define RootToContentDir Style->RootToContentDir

TSharedPtr<FSlateStyleSet> FUAgentStyle::StyleInstance = nullptr;

void FUAgentStyle::Initialize() {
  if (!StyleInstance.IsValid()) {
    StyleInstance = Create();
    FSlateStyleRegistry::RegisterSlateStyle(*StyleInstance);
  }
}

void FUAgentStyle::Shutdown() {
  FSlateStyleRegistry::UnRegisterSlateStyle(*StyleInstance);
  ensure(StyleInstance.IsUnique());
  StyleInstance.Reset();
}

FName FUAgentStyle::GetStyleSetName() {
  static FName StyleSetName(TEXT("UAgentStyle"));
  return StyleSetName;
}

const FVector2D Icon16x16(16.0f, 16.0f);
const FVector2D Icon20x20(20.0f, 20.0f);

TSharedRef<FSlateStyleSet> FUAgentStyle::Create() {
  TSharedRef<FSlateStyleSet> Style =
      MakeShareable(new FSlateStyleSet("UAgentStyle"));
  Style->SetContentRoot(
      IPluginManager::Get().FindPlugin("UAgent")->GetBaseDir() /
      TEXT("Resources"));

  Style->Set("UAgent.OpenPluginWindow",
             new IMAGE_BRUSH_SVG(TEXT("PlaceholderButtonIcon"), Icon20x20));

  // Markdown rendering styles — referenced by SRichTextBlock via
  // DecoratorStyleSet.
  const FTextBlockStyle BaseText =
      FAppStyle::Get().GetWidgetStyle<FTextBlockStyle>("NormalText");

  FTextBlockStyle Bold = BaseText;
  Bold.SetFont(FCoreStyle::GetDefaultFontStyle("Bold", 10));
  Style->Set("Markdown.Bold", Bold);

  FTextBlockStyle Italic = BaseText;
  Italic.SetFont(FCoreStyle::GetDefaultFontStyle("Italic", 10));
  Style->Set("Markdown.Italic", Italic);

  FTextBlockStyle Code = BaseText;
  Code.SetFont(FCoreStyle::GetDefaultFontStyle("Mono", 10));
  Code.SetColorAndOpacity(FSlateColor(FLinearColor(0.90f, 0.80f, 0.55f)));
  Style->Set("Markdown.Code", Code);

  FTextBlockStyle CodeBlock = BaseText;
  CodeBlock.SetFont(FCoreStyle::GetDefaultFontStyle("Mono", 10));
  Style->Set("Markdown.CodeBlock", CodeBlock);

  FTextBlockStyle H1 = BaseText;
  H1.SetFont(FCoreStyle::GetDefaultFontStyle("Bold", 16));
  FTextBlockStyle H2 = BaseText;
  H2.SetFont(FCoreStyle::GetDefaultFontStyle("Bold", 14));
  FTextBlockStyle H3 = BaseText;
  H3.SetFont(FCoreStyle::GetDefaultFontStyle("Bold", 12));
  Style->Set("Markdown.H1", H1);
  Style->Set("Markdown.H2", H2);
  Style->Set("Markdown.H3", H3);

  return Style;
}

void FUAgentStyle::ReloadTextures() {
  if (FSlateApplication::IsInitialized()) {
    FSlateApplication::Get().GetRenderer()->ReloadTextureResources();
  }
}

const ISlateStyle &FUAgentStyle::Get() { return *StyleInstance; }
