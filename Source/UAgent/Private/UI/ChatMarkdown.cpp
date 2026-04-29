#include "ChatMarkdown.h"

#include "UAgentStyle.h"

#include "Styling/AppStyle.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SGridPanel.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/Text/SRichTextBlock.h"
#include "Widgets/Text/STextBlock.h"

namespace UAgent::ChatMarkdown {
namespace {
struct FBlock {
  enum class EKind {
    Paragraph,
    Heading,
    CodeBlock,
    UnorderedList,
    OrderedList,
    HRule,
    Table
  };
  EKind Kind = EKind::Paragraph;
  int32 HeadingLevel = 0;
  TArray<FString> Lines;
  // Table-specific: Rows[0] is the header row, subsequent rows are body.
  // ColumnAlignments comes from the separator line's `:---` / `---:` / `:---:`.
  TArray<TArray<FString>> Rows;
  TArray<EHorizontalAlignment> ColumnAlignments;
};

// SRichTextBlock treats '<' as the start of a markup tag. For literal
// angle brackets in narrative/inline code we swap in a fullwidth '<' so
// the parser can't mistake them for tag openers. Visual impact is
// minimal and it's cheaper than teaching the parser to escape.
FString EscapeForRichText(const FString &In) {
  return In.Replace(TEXT("<"), TEXT("＜"), ESearchCase::CaseSensitive);
}

bool IsHRule(const FString &Line) {
  const FString S = Line.TrimStartAndEnd();
  if (S.Len() < 3)
    return false;
  const TCHAR C = S[0];
  if (C != TEXT('-') && C != TEXT('*') && C != TEXT('_'))
    return false;
  for (TCHAR Ch : S) {
    if (Ch != C && Ch != TEXT(' '))
      return false;
  }
  return true;
}

bool IsUnorderedItem(const FString &Trimmed) {
  return Trimmed.Len() >= 2 &&
         (Trimmed[0] == TEXT('-') || Trimmed[0] == TEXT('*') ||
          Trimmed[0] == TEXT('+')) &&
         Trimmed[1] == TEXT(' ');
}

bool IsOrderedItem(const FString &Trimmed, int32 &OutPrefixLen) {
  int32 D = 0;
  while (D < Trimmed.Len() && FChar::IsDigit(Trimmed[D]))
    ++D;
  if (D > 0 && D + 1 < Trimmed.Len() && Trimmed[D] == TEXT('.') &&
      Trimmed[D + 1] == TEXT(' ')) {
    OutPrefixLen = D + 2;
    return true;
  }
  return false;
}

// Splits a markdown table row on '|'. Drops the leading/trailing empty
// cell that comes from GFM's optional leading/trailing pipe, but keeps
// internal empties (an empty cell in the middle is legitimate).
TArray<FString> SplitTableRow(const FString &Line) {
  TArray<FString> Parts;
  Line.TrimStartAndEnd().ParseIntoArray(Parts, TEXT("|"),
                                        /*InCullEmpty*/ false);
  if (Parts.Num() > 0 && Parts[0].TrimStartAndEnd().IsEmpty())
    Parts.RemoveAt(0);
  if (Parts.Num() > 0 && Parts.Last().TrimStartAndEnd().IsEmpty())
    Parts.Pop();
  for (FString &P : Parts)
    P = P.TrimStartAndEnd();
  return Parts;
}

// Returns true if Cell is a valid table-separator cell (dashes with
// optional leading/trailing colons for alignment). Writes the alignment.
bool TryParseAlignment(const FString &Cell, EHorizontalAlignment &Out) {
  const FString T = Cell.TrimStartAndEnd();
  if (T.Len() < 1)
    return false;
  const bool bStart = T[0] == TEXT(':');
  const bool bEnd = T[T.Len() - 1] == TEXT(':');
  bool bHasDash = false;
  for (int32 i = 0; i < T.Len(); ++i) {
    const TCHAR C = T[i];
    if (C == TEXT('-'))
      bHasDash = true;
    else if (C == TEXT(':')) {
      if (i != 0 && i != T.Len() - 1)
        return false;
    } else
      return false;
  }
  if (!bHasDash)
    return false;
  if (bStart && bEnd)
    Out = HAlign_Center;
  else if (bEnd)
    Out = HAlign_Right;
  else
    Out = HAlign_Left;
  return true;
}

// A table requires two consecutive lines: a header with '|' separators
// and a separator line whose cells parse as alignment tokens, with
// matching cell counts on both.
bool LooksLikeTableStart(const TArray<FString> &Lines, int32 Index,
                         TArray<EHorizontalAlignment> *OutAligns = nullptr) {
  if (Index + 1 >= Lines.Num())
    return false;
  const FString &Header = Lines[Index];
  const FString &Sep = Lines[Index + 1];
  if (!Header.Contains(TEXT("|")))
    return false;
  const TArray<FString> HeaderCells = SplitTableRow(Header);
  if (HeaderCells.Num() == 0)
    return false;
  const TArray<FString> SepCells = SplitTableRow(Sep);
  if (SepCells.Num() != HeaderCells.Num())
    return false;
  TArray<EHorizontalAlignment> Aligns;
  Aligns.Reserve(SepCells.Num());
  for (const FString &C : SepCells) {
    EHorizontalAlignment A = HAlign_Left;
    if (!TryParseAlignment(C, A))
      return false;
    Aligns.Add(A);
  }
  if (OutAligns)
    *OutAligns = MoveTemp(Aligns);
  return true;
}

// Walks RawLine once, emitting SRichTextBlock markup: **bold**, *italic*,
// `code` become <Markdown.Bold>…</> / <Markdown.Italic>…</> /
// <Markdown.Code>…</>. Anything outside a recognised delimiter passes through
// literally (with
// '<' escaped so it can't open a tag).
FString RenderInline(const FString &RawLine) {
  FString Out;
  Out.Reserve(RawLine.Len() + 16);
  const int32 N = RawLine.Len();
  int32 i = 0;
  while (i < N) {
    const TCHAR C = RawLine[i];
    if (C == TEXT('`')) {
      const int32 End = RawLine.Find(TEXT("`"), ESearchCase::CaseSensitive,
                                     ESearchDir::FromStart, i + 1);
      if (End != INDEX_NONE) {
        Out += TEXT("<Markdown.Code>");
        Out += EscapeForRichText(RawLine.Mid(i + 1, End - i - 1));
        Out += TEXT("</>");
        i = End + 1;
        continue;
      }
    } else if (C == TEXT('*') && i + 1 < N && RawLine[i + 1] == TEXT('*')) {
      const int32 End = RawLine.Find(TEXT("**"), ESearchCase::CaseSensitive,
                                     ESearchDir::FromStart, i + 2);
      if (End != INDEX_NONE && End > i + 2) {
        Out += TEXT("<Markdown.Bold>");
        Out += EscapeForRichText(RawLine.Mid(i + 2, End - i - 2));
        Out += TEXT("</>");
        i = End + 2;
        continue;
      }
    } else if (C == TEXT('*')) {
      const int32 End = RawLine.Find(TEXT("*"), ESearchCase::CaseSensitive,
                                     ESearchDir::FromStart, i + 1);
      if (End != INDEX_NONE && End > i + 1) {
        Out += TEXT("<Markdown.Italic>");
        Out += EscapeForRichText(RawLine.Mid(i + 1, End - i - 1));
        Out += TEXT("</>");
        i = End + 1;
        continue;
      }
    }

    if (C == TEXT('<'))
      Out += TEXT("＜");
    else
      Out.AppendChar(C);
    ++i;
  }
  return Out;
}

TArray<FBlock> ParseBlocks(const FString &Text) {
  TArray<FString> Lines;
  Text.ParseIntoArrayLines(Lines, /*InCullEmpty*/ false);

  TArray<FBlock> Blocks;
  const int32 N = Lines.Num();
  int32 i = 0;
  while (i < N) {
    const FString &Line = Lines[i];
    const FString Trimmed = Line.TrimStart();

    // Fenced code block
    if (Trimmed.StartsWith(TEXT("```"))) {
      FBlock B;
      B.Kind = FBlock::EKind::CodeBlock;
      ++i;
      while (i < N && !Lines[i].TrimStart().StartsWith(TEXT("```"))) {
        B.Lines.Add(Lines[i]);
        ++i;
      }
      if (i < N)
        ++i; // skip closing fence
      Blocks.Add(MoveTemp(B));
      continue;
    }

    if (IsHRule(Line)) {
      FBlock B;
      B.Kind = FBlock::EKind::HRule;
      Blocks.Add(MoveTemp(B));
      ++i;
      continue;
    }

    // Heading: 1..6 '#' then space
    if (Trimmed.StartsWith(TEXT("#"))) {
      int32 Level = 0;
      while (Level < Trimmed.Len() && Trimmed[Level] == TEXT('#') && Level < 6)
        ++Level;
      if (Level > 0 && Level < Trimmed.Len() && Trimmed[Level] == TEXT(' ')) {
        FBlock B;
        B.Kind = FBlock::EKind::Heading;
        B.HeadingLevel = Level;
        B.Lines.Add(Trimmed.Mid(Level + 1));
        Blocks.Add(MoveTemp(B));
        ++i;
        continue;
      }
    }

    if (IsUnorderedItem(Trimmed)) {
      FBlock B;
      B.Kind = FBlock::EKind::UnorderedList;
      while (i < N) {
        const FString Ti = Lines[i].TrimStart();
        if (!IsUnorderedItem(Ti))
          break;
        B.Lines.Add(Ti.Mid(2));
        ++i;
      }
      Blocks.Add(MoveTemp(B));
      continue;
    }

    {
      int32 PrefixLen = 0;
      if (IsOrderedItem(Trimmed, PrefixLen)) {
        FBlock B;
        B.Kind = FBlock::EKind::OrderedList;
        while (i < N) {
          const FString Ti = Lines[i].TrimStart();
          int32 P = 0;
          if (!IsOrderedItem(Ti, P))
            break;
          B.Lines.Add(Ti.Mid(P));
          ++i;
        }
        Blocks.Add(MoveTemp(B));
        continue;
      }
    }

    if (Line.TrimStartAndEnd().IsEmpty()) {
      ++i;
      continue;
    }

    // Table: header row + alignment separator row, then body rows
    // until a blank line or a line without '|'.
    {
      TArray<EHorizontalAlignment> Aligns;
      if (LooksLikeTableStart(Lines, i, &Aligns)) {
        FBlock B;
        B.Kind = FBlock::EKind::Table;
        B.ColumnAlignments = MoveTemp(Aligns);
        B.Rows.Add(SplitTableRow(Lines[i]));
        i += 2; // past header + separator
        while (i < N) {
          const FString Row = Lines[i].TrimStartAndEnd();
          if (Row.IsEmpty() || !Row.Contains(TEXT("|")))
            break;
          TArray<FString> Cells = SplitTableRow(Row);
          Cells.SetNum(B.Rows[0].Num()); // pad/truncate to header width
          B.Rows.Add(MoveTemp(Cells));
          ++i;
        }
        Blocks.Add(MoveTemp(B));
        continue;
      }
    }

    // Paragraph: consume until a blank line or a recognised block opener.
    FBlock B;
    B.Kind = FBlock::EKind::Paragraph;
    while (i < N) {
      const FString &L = Lines[i];
      const FString Tl = L.TrimStart();
      if (L.TrimStartAndEnd().IsEmpty())
        break;
      if (Tl.StartsWith(TEXT("```")))
        break;
      if (Tl.StartsWith(TEXT("#")))
        break;
      if (IsHRule(L))
        break;
      if (IsUnorderedItem(Tl))
        break;
      int32 P = 0;
      if (IsOrderedItem(Tl, P))
        break;
      if (LooksLikeTableStart(Lines, i))
        break;
      B.Lines.Add(L);
      ++i;
    }
    Blocks.Add(MoveTemp(B));
  }
  return Blocks;
}

TSharedRef<SWidget> MakeParagraph(const FString &Markup) {
  return SNew(SRichTextBlock)
      .Text(FText::FromString(Markup))
      .AutoWrapText(true)
      .DecoratorStyleSet(&FUAgentStyle::Get());
}

TSharedRef<SWidget> MakeHeading(const FString &Markup, int32 Level) {
  FName StyleName;
  switch (Level) {
  case 1:
    StyleName = TEXT("Markdown.H1");
    break;
  case 2:
    StyleName = TEXT("Markdown.H2");
    break;
  default:
    StyleName = TEXT("Markdown.H3");
    break;
  }
  const FTextBlockStyle &HeadStyle =
      FUAgentStyle::Get().GetWidgetStyle<FTextBlockStyle>(StyleName);
  return SNew(SRichTextBlock)
      .Text(FText::FromString(Markup))
      .AutoWrapText(true)
      .TextStyle(&HeadStyle)
      .DecoratorStyleSet(&FUAgentStyle::Get());
}

TSharedRef<SWidget> MakeCodeBlock(const FString &Joined) {
  const FTextBlockStyle &CodeStyle =
      FUAgentStyle::Get().GetWidgetStyle<FTextBlockStyle>("Markdown.CodeBlock");
  return SNew(SBorder)
      .BorderImage(FAppStyle::Get().GetBrush("ToolPanel.GroupBorder"))
      .BorderBackgroundColor(FLinearColor(0.06f, 0.06f, 0.08f, 1.0f))
      .Padding(FMargin(6, 4))[SNew(STextBlock)
                                  .Text(FText::FromString(Joined))
                                  .TextStyle(&CodeStyle)];
}

TSharedRef<SWidget> MakeListItem(const FString &Bullet,
                                 const FString &BodyMarkup) {
  TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);
  Row->AddSlot().AutoWidth().Padding(
      FMargin(6, 0, 6, 0))[SNew(STextBlock).Text(FText::FromString(Bullet))];
  Row->AddSlot().FillWidth(1.0f)[MakeParagraph(BodyMarkup)];
  return Row;
}
} // namespace

TSharedRef<SWidget> BuildWidget(const FString &Text) {
  const TArray<FBlock> Blocks = ParseBlocks(Text);
  TSharedRef<SVerticalBox> Root = SNew(SVerticalBox);
  for (const FBlock &B : Blocks) {
    TSharedRef<SWidget> Child = SNullWidget::NullWidget;
    switch (B.Kind) {
    case FBlock::EKind::Paragraph: {
      const FString Joined = FString::Join(B.Lines, TEXT("\n"));
      Child = MakeParagraph(RenderInline(Joined));
      break;
    }
    case FBlock::EKind::Heading: {
      const FString Head = B.Lines.Num() > 0 ? B.Lines[0] : FString();
      Child = MakeHeading(RenderInline(Head), B.HeadingLevel);
      break;
    }
    case FBlock::EKind::CodeBlock: {
      const FString Joined = FString::Join(B.Lines, TEXT("\n"));
      Child = MakeCodeBlock(Joined);
      break;
    }
    case FBlock::EKind::UnorderedList: {
      TSharedRef<SVerticalBox> List = SNew(SVerticalBox);
      for (const FString &Item : B.Lines) {
        List->AddSlot().AutoHeight().Padding(
            FMargin(0, 1))[MakeListItem(TEXT("•"), RenderInline(Item))];
      }
      Child = List;
      break;
    }
    case FBlock::EKind::OrderedList: {
      TSharedRef<SVerticalBox> List = SNew(SVerticalBox);
      for (int32 k = 0; k < B.Lines.Num(); ++k) {
        List->AddSlot().AutoHeight().Padding(FMargin(0, 1))[MakeListItem(
            FString::Printf(TEXT("%d."), k + 1), RenderInline(B.Lines[k]))];
      }
      Child = List;
      break;
    }
    case FBlock::EKind::HRule: {
      Child = SNew(SBorder)
                  .BorderImage(FAppStyle::Get().GetBrush("Separator"))
                  .Padding(FMargin(0, 2));
      break;
    }
    case FBlock::EKind::Table: {
      if (B.Rows.Num() == 0)
        break;
      const int32 NumCols = B.Rows[0].Num();
      const FTextBlockStyle &BoldStyle =
          FUAgentStyle::Get().GetWidgetStyle<FTextBlockStyle>("Markdown.Bold");
      const FTextBlockStyle &NormalStyle =
          FAppStyle::Get().GetWidgetStyle<FTextBlockStyle>("NormalText");

      TSharedRef<SGridPanel> Grid = SNew(SGridPanel);
      // Equal column fill so AutoWrapText in cells has a width to
      // wrap against; otherwise the grid stretches to fit one line.
      for (int32 c = 0; c < NumCols; ++c) {
        Grid->SetColumnFill(c, 1.0f);
      }

      for (int32 r = 0; r < B.Rows.Num(); ++r) {
        const bool bHeader = (r == 0);
        for (int32 c = 0; c < B.Rows[r].Num(); ++c) {
          const EHorizontalAlignment HAlign = B.ColumnAlignments.IsValidIndex(c)
                                                  ? B.ColumnAlignments[c]
                                                  : HAlign_Left;

          TSharedRef<SRichTextBlock> Cell =
              SNew(SRichTextBlock)
                  .Text(FText::FromString(RenderInline(B.Rows[r][c])))
                  .AutoWrapText(true)
                  .TextStyle(bHeader ? &BoldStyle : &NormalStyle)
                  .DecoratorStyleSet(&FUAgentStyle::Get());

          Grid->AddSlot(c, r).HAlign(HAlign).Padding(FMargin(6, 3))[Cell];
        }
      }
      Child = Grid;
      break;
    }
    }
    Root->AddSlot().AutoHeight().Padding(FMargin(0, 0, 0, 4))[Child];
  }
  return Root;
}
} // namespace UAgent::ChatMarkdown
