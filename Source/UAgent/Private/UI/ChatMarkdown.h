#pragma once

#include "CoreMinimal.h"
#include "Widgets/SWidget.h"

namespace UAgent::ChatMarkdown {
/**
 * Parses a markdown string into a Slate widget tree. Supported: paragraphs
 * with inline bold/italic/code, fenced code blocks, ATX headings (#–###),
 * bullet and ordered lists, horizontal rules. Unknown syntax falls through
 * as plain text.
 */
TSharedRef<SWidget> BuildWidget(const FString &Text);
} // namespace UAgent::ChatMarkdown
