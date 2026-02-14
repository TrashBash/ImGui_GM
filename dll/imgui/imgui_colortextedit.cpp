#include <algorithm>
#include <chrono>
#include <string>
#include <regex>
#include <cmath>

#include "imgui_colortextedit.h"

#include "imgui.h"

// TODO
// - multiline comments vs single-line: latter is blocking start of a ML

template<class InputIt1, class InputIt2, class BinaryPredicate>
bool equals(InputIt1 first1, InputIt1 last1,
	InputIt2 first2, InputIt2 last2, BinaryPredicate p)
{
	for (; first1 != last1 && first2 != last2; ++first1, ++first2)
	{
		if (!p(*first1, *first2))
			return false;
	}
	return first1 == last1 && first2 == last2;
}

TextEditor::TextEditor()
	: mLineSpacing(1.0f)
	, mUndoIndex(0)
	, mTabSize(4)
	, mOverwrite(false)
	, mReadOnly(false)
	, mWithinRender(false)
	, mScrollToCursor(false)
	, mScrollToTop(false)
	, mTextChanged(false)
	, mColorizerEnabled(true)
	, mTextStart(20.0f)
	, mLeftMargin(10)
	, mCursorPositionChanged(false)
	, mColorRangeMin(0)
	, mColorRangeMax(0)
	, mSelectionMode(SelectionMode::Normal)
	, mCheckComments(true)
	, mLastClick(-1.0f)
	, mHandleKeyboardInputs(true)
	, mHandleMouseInputs(true)
	, mIgnoreImGuiChild(false)
	, mShowWhitespaces(true)
	, mStartTime(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count())
{
	SetPalette(GetDarkPalette());
	SetLanguageDefinition(LanguageDefinition::HLSL());
	mLines.push_back(Line());
}

TextEditor::~TextEditor()
{
}

void TextEditor::SetLanguageDefinition(const LanguageDefinition& aLanguageDef)
{
	mLanguageDefinition = aLanguageDef;
	mRegexList.clear();

	for (auto& r : mLanguageDefinition.mTokenRegexStrings)
		mRegexList.push_back(std::make_pair(std::regex(r.first, std::regex_constants::optimize), r.second));

	Colorize();
}

void TextEditor::SetPalette(const Palette& aValue)
{
	mPaletteBase = aValue;
}

std::string TextEditor::GetText(const Coordinates& aStart, const Coordinates& aEnd) const
{
	std::string result;

	auto lstart = aStart.mLine;
	auto lend = aEnd.mLine;
	auto istart = GetCharacterIndex(aStart);
	auto iend = GetCharacterIndex(aEnd);
	size_t s = 0;

	for (size_t i = lstart; i < lend; i++)
		s += mLines[i].size();

	result.reserve(s + s / 8);

	while (istart < iend || lstart < lend)
	{
		if (lstart >= (int)mLines.size())
			break;

		auto& line = mLines[lstart];
		if (istart < (int)line.size())
		{
			result += line[istart].mChar;
			istart++;
		}
		else
		{
			istart = 0;
			++lstart;
			result += '\n';
		}
	}

	return result;
}

TextEditor::Coordinates TextEditor::GetActualCursorCoordinates() const
{
	return SanitizeCoordinates(mState.mCursorPosition);
}

TextEditor::Coordinates TextEditor::SanitizeCoordinates(const Coordinates& aValue) const
{
	auto line = aValue.mLine;
	auto column = aValue.mColumn;
	if (line >= (int)mLines.size())
	{
		if (mLines.empty())
		{
			line = 0;
			column = 0;
		}
		else
		{
			line = (int)mLines.size() - 1;
			column = GetLineMaxColumn(line);
		}
		return Coordinates(line, column);
	}
	else
	{
		column = mLines.empty() ? 0 : std::min(column, GetLineMaxColumn(line));
		return Coordinates(line, column);
	}
}

// https://en.wikipedia.org/wiki/UTF-8
// We assume that the char is a standalone character (<128) or a leading byte of an UTF-8 code sequence (non-10xxxxxx code)
static int UTF8CharLength(TextEditor::Char c)
{
	if ((c & 0xFE) == 0xFC)
		return 6;
	if ((c & 0xFC) == 0xF8)
		return 5;
	if ((c & 0xF8) == 0xF0)
		return 4;
	else if ((c & 0xF0) == 0xE0)
		return 3;
	else if ((c & 0xE0) == 0xC0)
		return 2;
	return 1;
}

// "Borrowed" from ImGui source
static inline int ImTextCharToUtf8(char* buf, int buf_size, unsigned int c)
{
	if (c < 0x80)
	{
		buf[0] = (char)c;
		return 1;
	}
	if (c < 0x800)
	{
		if (buf_size < 2) return 0;
		buf[0] = (char)(0xc0 + (c >> 6));
		buf[1] = (char)(0x80 + (c & 0x3f));
		return 2;
	}
	if (c >= 0xdc00 && c < 0xe000)
	{
		return 0;
	}
	if (c >= 0xd800 && c < 0xdc00)
	{
		if (buf_size < 4) return 0;
		buf[0] = (char)(0xf0 + (c >> 18));
		buf[1] = (char)(0x80 + ((c >> 12) & 0x3f));
		buf[2] = (char)(0x80 + ((c >> 6) & 0x3f));
		buf[3] = (char)(0x80 + ((c) & 0x3f));
		return 4;
	}
	//else if (c < 0x10000)
	{
		if (buf_size < 3) return 0;
		buf[0] = (char)(0xe0 + (c >> 12));
		buf[1] = (char)(0x80 + ((c >> 6) & 0x3f));
		buf[2] = (char)(0x80 + ((c) & 0x3f));
		return 3;
	}
}

void TextEditor::Advance(Coordinates& aCoordinates) const
{
	if (aCoordinates.mLine < (int)mLines.size())
	{
		auto& line = mLines[aCoordinates.mLine];
		auto cindex = GetCharacterIndex(aCoordinates);

		if (cindex + 1 < (int)line.size())
		{
			auto delta = UTF8CharLength(line[cindex].mChar);
			cindex = std::min(cindex + delta, (int)line.size() - 1);
		}
		else
		{
			++aCoordinates.mLine;
			cindex = 0;
		}
		aCoordinates.mColumn = GetCharacterColumn(aCoordinates.mLine, cindex);
	}
}

void TextEditor::DeleteRange(const Coordinates& aStart, const Coordinates& aEnd)
{
	assert(aEnd >= aStart);
	assert(!mReadOnly);

	//printf("D(%d.%d)-(%d.%d)\n", aStart.mLine, aStart.mColumn, aEnd.mLine, aEnd.mColumn);

	if (aEnd == aStart)
		return;

	auto start = GetCharacterIndex(aStart);
	auto end = GetCharacterIndex(aEnd);

	if (aStart.mLine == aEnd.mLine)
	{
		auto& line = mLines[aStart.mLine];
		auto n = GetLineMaxColumn(aStart.mLine);
		if (aEnd.mColumn >= n)
			line.erase(line.begin() + start, line.end());
		else
			line.erase(line.begin() + start, line.begin() + end);
	}
	else
	{
		auto& firstLine = mLines[aStart.mLine];
		auto& lastLine = mLines[aEnd.mLine];

		firstLine.erase(firstLine.begin() + start, firstLine.end());
		lastLine.erase(lastLine.begin(), lastLine.begin() + end);

		if (aStart.mLine < aEnd.mLine)
			firstLine.insert(firstLine.end(), lastLine.begin(), lastLine.end());

		if (aStart.mLine < aEnd.mLine)
			RemoveLine(aStart.mLine + 1, aEnd.mLine + 1);
	}

	mTextChanged = true;
}

int TextEditor::InsertTextAt(Coordinates& /* inout */ aWhere, const char* aValue)
{
	assert(!mReadOnly);

	int cindex = GetCharacterIndex(aWhere);
	int totalLines = 0;
	while (*aValue != '\0')
	{
		assert(!mLines.empty());

		if (*aValue == '\r')
		{
			// skip
			++aValue;
		}
		else if (*aValue == '\n')
		{
			if (cindex < (int)mLines[aWhere.mLine].size())
			{
				auto& newLine = InsertLine(aWhere.mLine + 1);
				auto& line = mLines[aWhere.mLine];
				newLine.insert(newLine.begin(), line.begin() + cindex, line.end());
				line.erase(line.begin() + cindex, line.end());
			}
			else
			{
				InsertLine(aWhere.mLine + 1);
			}
			++aWhere.mLine;
			aWhere.mColumn = 0;
			cindex = 0;
			++totalLines;
			++aValue;
		}
		else
		{
			auto& line = mLines[aWhere.mLine];
			auto d = UTF8CharLength(*aValue);
			while (d-- > 0 && *aValue != '\0')
				line.insert(line.begin() + cindex++, Glyph(*aValue++, PaletteIndex::Default));
			++aWhere.mColumn;
		}

		mTextChanged = true;
	}

	return totalLines;
}

void TextEditor::AddUndo(UndoRecord& aValue)
{
	assert(!mReadOnly);
	//printf("AddUndo: (@%d.%d) +\'%s' [%d.%d .. %d.%d], -\'%s', [%d.%d .. %d.%d] (@%d.%d)\n",
	//	aValue.mBefore.mCursorPosition.mLine, aValue.mBefore.mCursorPosition.mColumn,
	//	aValue.mAdded.c_str(), aValue.mAddedStart.mLine, aValue.mAddedStart.mColumn, aValue.mAddedEnd.mLine, aValue.mAddedEnd.mColumn,
	//	aValue.mRemoved.c_str(), aValue.mRemovedStart.mLine, aValue.mRemovedStart.mColumn, aValue.mRemovedEnd.mLine, aValue.mRemovedEnd.mColumn,
	//	aValue.mAfter.mCursorPosition.mLine, aValue.mAfter.mCursorPosition.mColumn
	//	);

	mUndoBuffer.resize((size_t)(mUndoIndex + 1));
	mUndoBuffer.back() = aValue;
	++mUndoIndex;
}

TextEditor::Coordinates TextEditor::ScreenPosToCoordinates(const ImVec2& aPosition) const
{
	ImVec2 origin = ImGui::GetCursorScreenPos();
	ImVec2 local(aPosition.x - origin.x, aPosition.y - origin.y);

	int lineNo = std::max(0, (int)floor(local.y / mCharAdvance.y));

	int columnCoord = 0;

	if (lineNo >= 0 && lineNo < (int)mLines.size())
	{
		auto& line = mLines.at(lineNo);

		int columnIndex = 0;
		float columnX = 0.0f;

		while ((size_t)columnIndex < line.size())
		{
			float columnWidth = 0.0f;

			if (line[columnIndex].mChar == '\t')
			{
				float spaceSize = ImGui::GetFont()->CalcTextSizeA(ImGui::GetFontSize(), FLT_MAX, -1.0f, " ").x;
				float oldX = columnX;
				float newColumnX = (1.0f + std::floor((1.0f + columnX) / (float(mTabSize) * spaceSize))) * (float(mTabSize) * spaceSize);
				columnWidth = newColumnX - oldX;
				if (mTextStart + columnX + columnWidth * 0.5f > local.x)
					break;
				columnX = newColumnX;
				columnCoord = (columnCoord / mTabSize) * mTabSize + mTabSize;
				columnIndex++;
			}
			else
			{
				char buf[7];
				auto d = UTF8CharLength(line[columnIndex].mChar);
				int i = 0;
				while (i < 6 && d-- > 0)
					buf[i++] = line[columnIndex++].mChar;
				buf[i] = '\0';
				columnWidth = ImGui::GetFont()->CalcTextSizeA(ImGui::GetFontSize(), FLT_MAX, -1.0f, buf).x;
				if (mTextStart + columnX + columnWidth * 0.5f > local.x)
					break;
				columnX += columnWidth;
				columnCoord++;
			}
		}
	}

	return SanitizeCoordinates(Coordinates(lineNo, columnCoord));
}

TextEditor::Coordinates TextEditor::FindWordStart(const Coordinates& aFrom) const
{
	Coordinates at = aFrom;
	if (at.mLine >= (int)mLines.size())
		return at;

	auto& line = mLines[at.mLine];
	auto cindex = GetCharacterIndex(at);

	if (cindex >= (int)line.size())
		return at;

	while (cindex > 0 && isspace(line[cindex].mChar))
		--cindex;

	auto cstart = (PaletteIndex)line[cindex].mColorIndex;
	while (cindex > 0)
	{
		auto c = line[cindex].mChar;
		if ((c & 0xC0) != 0x80)	// not UTF code sequence 10xxxxxx
		{
			if (c <= 32 && isspace(c))
			{
				cindex++;
				break;
			}
			if (cstart != (PaletteIndex)line[size_t(cindex - 1)].mColorIndex)
				break;
		}
		--cindex;
	}
	return Coordinates(at.mLine, GetCharacterColumn(at.mLine, cindex));
}

TextEditor::Coordinates TextEditor::FindWordEnd(const Coordinates& aFrom) const
{
	Coordinates at = aFrom;
	if (at.mLine >= (int)mLines.size())
		return at;

	auto& line = mLines[at.mLine];
	auto cindex = GetCharacterIndex(at);

	if (cindex >= (int)line.size())
		return at;

	bool prevspace = isspace(line[cindex].mChar) != 0;
	auto cstart = (PaletteIndex)line[cindex].mColorIndex;
	while (cindex < (int)line.size())
	{
		auto c = line[cindex].mChar;
		auto d = UTF8CharLength(c);
		if (cstart != (PaletteIndex)line[cindex].mColorIndex)
			break;

		if (prevspace != !!isspace(c))
		{
			if (isspace(c))
				while (cindex < (int)line.size() && isspace(line[cindex].mChar))
					++cindex;
			break;
		}
		cindex += d;
	}
	return Coordinates(aFrom.mLine, GetCharacterColumn(aFrom.mLine, cindex));
}

TextEditor::Coordinates TextEditor::FindNextWord(const Coordinates& aFrom) const
{
	Coordinates at = aFrom;
	if (at.mLine >= (int)mLines.size())
		return at;

	// skip to the next non-word character
	auto cindex = GetCharacterIndex(aFrom);
	bool isword = false;
	bool skip = false;
	if (cindex < (int)mLines[at.mLine].size())
	{
		auto& line = mLines[at.mLine];
		isword = isalnum(line[cindex].mChar) != 0;
		skip = isword;
	}

	while (!isword || skip)
	{
		if (at.mLine >= mLines.size())
		{
			auto l = std::max(0, (int)mLines.size() - 1);
			return Coordinates(l, GetLineMaxColumn(l));
		}

		auto& line = mLines[at.mLine];
		if (cindex < (int)line.size())
		{
			isword = isalnum(line[cindex].mChar) != 0;

			if (isword && !skip)
				return Coordinates(at.mLine, GetCharacterColumn(at.mLine, cindex));

			if (!isword)
				skip = false;

			cindex++;
		}
		else
		{
			cindex = 0;
			++at.mLine;
			skip = false;
			isword = false;
		}
	}

	return at;
}

int TextEditor::GetCharacterIndex(const Coordinates& aCoordinates) const
{
	if (aCoordinates.mLine >= mLines.size())
		return -1;
	auto& line = mLines[aCoordinates.mLine];
	int c = 0;
	int i = 0;
	for (; i < line.size() && c < aCoordinates.mColumn;)
	{
		if (line[i].mChar == '\t')
			c = (c / mTabSize) * mTabSize + mTabSize;
		else
			++c;
		i += UTF8CharLength(line[i].mChar);
	}
	return i;
}

int TextEditor::GetCharacterColumn(int aLine, int aIndex) const
{
	if (aLine >= mLines.size())
		return 0;
	auto& line = mLines[aLine];
	int col = 0;
	int i = 0;
	while (i < aIndex && i < (int)line.size())
	{
		auto c = line[i].mChar;
		i += UTF8CharLength(c);
		if (c == '\t')
			col = (col / mTabSize) * mTabSize + mTabSize;
		else
			col++;
	}
	return col;
}

int TextEditor::GetLineCharacterCount(int aLine) const
{
	if (aLine >= mLines.size())
		return 0;
	auto& line = mLines[aLine];
	int c = 0;
	for (unsigned i = 0; i < line.size(); c++)
		i += UTF8CharLength(line[i].mChar);
	return c;
}

int TextEditor::GetLineMaxColumn(int aLine) const
{
	if (aLine >= mLines.size())
		return 0;
	auto& line = mLines[aLine];
	int col = 0;
	for (unsigned i = 0; i < line.size(); )
	{
		auto c = line[i].mChar;
		if (c == '\t')
			col = (col / mTabSize) * mTabSize + mTabSize;
		else
			col++;
		i += UTF8CharLength(c);
	}
	return col;
}

bool TextEditor::IsOnWordBoundary(const Coordinates& aAt) const
{
	if (aAt.mLine >= (int)mLines.size() || aAt.mColumn == 0)
		return true;

	auto& line = mLines[aAt.mLine];
	auto cindex = GetCharacterIndex(aAt);
	if (cindex >= (int)line.size())
		return true;

	if (mColorizerEnabled)
		return line[cindex].mColorIndex != line[size_t(cindex - 1)].mColorIndex;

	return isspace(line[cindex].mChar) != isspace(line[cindex - 1].mChar);
}

void TextEditor::RemoveLine(int aStart, int aEnd)
{
	assert(!mReadOnly);
	assert(aEnd >= aStart);
	assert(mLines.size() > (size_t)(aEnd - aStart));

	ErrorMarkers etmp;
	for (auto& i : mErrorMarkers)
	{
		ErrorMarkers::value_type e(i.first >= aStart ? i.first - 1 : i.first, i.second);
		if (e.first >= aStart && e.first <= aEnd)
			continue;
		etmp.insert(e);
	}
	mErrorMarkers = std::move(etmp);

	Breakpoints btmp;
	for (auto i : mBreakpoints)
	{
		if (i >= aStart && i <= aEnd)
			continue;
		btmp.insert(i >= aStart ? i - 1 : i);
	}
	mBreakpoints = std::move(btmp);

	mLines.erase(mLines.begin() + aStart, mLines.begin() + aEnd);
	assert(!mLines.empty());

	mTextChanged = true;
}

void TextEditor::RemoveLine(int aIndex)
{
	assert(!mReadOnly);
	assert(mLines.size() > 1);

	ErrorMarkers etmp;
	for (auto& i : mErrorMarkers)
	{
		ErrorMarkers::value_type e(i.first > aIndex ? i.first - 1 : i.first, i.second);
		if (e.first - 1 == aIndex)
			continue;
		etmp.insert(e);
	}
	mErrorMarkers = std::move(etmp);

	Breakpoints btmp;
	for (auto i : mBreakpoints)
	{
		if (i == aIndex)
			continue;
		btmp.insert(i >= aIndex ? i - 1 : i);
	}
	mBreakpoints = std::move(btmp);

	mLines.erase(mLines.begin() + aIndex);
	assert(!mLines.empty());

	mTextChanged = true;
}

TextEditor::Line& TextEditor::InsertLine(int aIndex)
{
	assert(!mReadOnly);

	auto& result = *mLines.insert(mLines.begin() + aIndex, Line());

	ErrorMarkers etmp;
	for (auto& i : mErrorMarkers)
		etmp.insert(ErrorMarkers::value_type(i.first >= aIndex ? i.first + 1 : i.first, i.second));
	mErrorMarkers = std::move(etmp);

	Breakpoints btmp;
	for (auto i : mBreakpoints)
		btmp.insert(i >= aIndex ? i + 1 : i);
	mBreakpoints = std::move(btmp);

	return result;
}

std::string TextEditor::GetWordUnderCursor() const
{
	auto c = GetCursorPosition();
	return GetWordAt(c);
}

std::string TextEditor::GetWordAt(const Coordinates& aCoords) const
{
	auto start = FindWordStart(aCoords);
	auto end = FindWordEnd(aCoords);

	std::string r;

	auto istart = GetCharacterIndex(start);
	auto iend = GetCharacterIndex(end);

	for (auto it = istart; it < iend; ++it)
		r.push_back(mLines[aCoords.mLine][it].mChar);

	return r;
}

ImU32 TextEditor::GetGlyphColor(const Glyph& aGlyph) const
{
	if (!mColorizerEnabled)
		return mPalette[(int)PaletteIndex::Default];
	if (aGlyph.mComment)
		return mPalette[(int)PaletteIndex::Comment];
	if (aGlyph.mMultiLineComment)
		return mPalette[(int)PaletteIndex::MultiLineComment];
	auto const color = mPalette[(int)aGlyph.mColorIndex];
	if (aGlyph.mPreprocessor)
	{
		const auto ppcolor = mPalette[(int)PaletteIndex::Preprocessor];
		const int c0 = ((ppcolor & 0xff) + (color & 0xff)) / 2;
		const int c1 = (((ppcolor >> 8) & 0xff) + ((color >> 8) & 0xff)) / 2;
		const int c2 = (((ppcolor >> 16) & 0xff) + ((color >> 16) & 0xff)) / 2;
		const int c3 = (((ppcolor >> 24) & 0xff) + ((color >> 24) & 0xff)) / 2;
		return ImU32(c0 | (c1 << 8) | (c2 << 16) | (c3 << 24));
	}
	return color;
}

void TextEditor::HandleKeyboardInputs()
{
	ImGuiIO& io = ImGui::GetIO();
	auto shift = io.KeyShift;
	auto ctrl = io.ConfigMacOSXBehaviors ? io.KeySuper : io.KeyCtrl;
	auto alt = io.ConfigMacOSXBehaviors ? io.KeyCtrl : io.KeyAlt;

	if (ImGui::IsWindowFocused() && !io.AppFocusLost)
	{
		if (ImGui::IsWindowHovered())
			ImGui::SetMouseCursor(ImGuiMouseCursor_TextInput);
		//ImGui::CaptureKeyboardFromApp(true);

		io.WantCaptureKeyboard = true;
		io.WantTextInput = true;

		if (!IsReadOnly() && ctrl && !shift && !alt && ImGui::IsKeyPressed(ImGuiKey_Z))
			Undo();
		else if (!IsReadOnly() && !ctrl && !shift && alt && ImGui::IsKeyPressed(ImGuiKey_Backspace))
			Undo();
		else if (!IsReadOnly() && ctrl && !shift && !alt && ImGui::IsKeyPressed(ImGuiKey_Y))
			Redo();
		else if (!ctrl && !alt && ImGui::IsKeyPressed(ImGuiKey_UpArrow))
			MoveUp(1, shift);
		else if (!ctrl && !alt && ImGui::IsKeyPressed(ImGuiKey_DownArrow))
			MoveDown(1, shift);
		else if (!alt && ImGui::IsKeyPressed(ImGuiKey_LeftArrow))
			MoveLeft(1, shift, ctrl);
		else if (!alt && ImGui::IsKeyPressed(ImGuiKey_RightArrow))
			MoveRight(1, shift, ctrl);
		else if (!alt && ImGui::IsKeyPressed(ImGuiKey_PageUp))
			MoveUp(GetPageSize() - 4, shift);
		else if (!alt && ImGui::IsKeyPressed(ImGuiKey_PageDown))
			MoveDown(GetPageSize() - 4, shift);
		else if (!alt && ctrl && ImGui::IsKeyPressed(ImGuiKey_Home))
			MoveTop(shift);
		else if (ctrl && !alt && ImGui::IsKeyPressed(ImGuiKey_End))
			MoveBottom(shift);
		else if (!ctrl && !alt && ImGui::IsKeyPressed(ImGuiKey_Home))
			MoveHome(shift);
		else if (!ctrl && !alt && ImGui::IsKeyPressed(ImGuiKey_End))
			MoveEnd(shift);
		else if (!IsReadOnly() && !ctrl && !shift && !alt && ImGui::IsKeyPressed(ImGuiKey_Delete))
			Delete();
		else if (!IsReadOnly() && !ctrl && !alt && ImGui::IsKeyPressed(ImGuiKey_Backspace))
			Backspace();
		else if (!IsReadOnly() && ctrl && !alt && ImGui::IsKeyPressed(ImGuiKey_Backspace))
		{
			// delete word 
		}
		else if (!ctrl && !shift && !alt && ImGui::IsKeyPressed(ImGuiKey_Insert))
			mOverwrite ^= true;
		else if (ctrl && !shift && !alt && ImGui::IsKeyPressed(ImGuiKey_Insert))
			Copy();
		else if (ctrl && !shift && !alt && ImGui::IsKeyPressed(ImGuiKey_C))
			Copy();
		else if (!IsReadOnly() && !ctrl && shift && !alt && ImGui::IsKeyPressed(ImGuiKey_Insert))
			Paste();
		else if (!IsReadOnly() && ctrl && !shift && !alt && ImGui::IsKeyPressed(ImGuiKey_V))
			Paste();
		else if (ctrl && !shift && !alt && ImGui::IsKeyPressed(ImGuiKey_X))
			Cut();
		else if (!ctrl && shift && !alt && ImGui::IsKeyPressed(ImGuiKey_Delete))
			Cut();
		else if (ctrl && !shift && !alt && ImGui::IsKeyPressed(ImGuiKey_A))
			SelectAll();
		else if (!IsReadOnly() && !ctrl && !shift && !alt && ImGui::IsKeyPressed(ImGuiKey_Enter))
			EnterCharacter('\n', false);
		else if (!IsReadOnly() && !ctrl && !alt && ImGui::IsKeyPressed(ImGuiKey_Tab))
			EnterCharacter('\t', shift);

		if (!IsReadOnly() && !io.InputQueueCharacters.empty())
		{
			for (int i = 0; i < io.InputQueueCharacters.Size; i++)
			{
				auto c = io.InputQueueCharacters[i];
				if (c != 0 && c != 0x7F && (c == '\n' || c >= 32))
                    EnterCharacter(c, shift);
			}
			io.InputQueueCharacters.resize(0);
		}
	}
}

void TextEditor::HandleMouseInputs()
{
	ImGuiIO& io = ImGui::GetIO();
	auto shift = io.KeyShift;
	auto ctrl = io.ConfigMacOSXBehaviors ? io.KeySuper : io.KeyCtrl;
	auto alt = io.ConfigMacOSXBehaviors ? io.KeyCtrl : io.KeyAlt;

	if (ImGui::IsWindowHovered())
	{
		if (!shift && !alt)
		{
			auto click = ImGui::IsMouseClicked(0);
			auto doubleClick = ImGui::IsMouseDoubleClicked(0);
			auto t = ImGui::GetTime();
			auto tripleClick = click && !doubleClick && (mLastClick != -1.0f && (t - mLastClick) < io.MouseDoubleClickTime);

			/*
			Left mouse button triple click
			*/

			if (tripleClick)
			{
				if (!ctrl)
				{
					mState.mCursorPosition = mInteractiveStart = mInteractiveEnd = ScreenPosToCoordinates(ImGui::GetMousePos());
					mSelectionMode = SelectionMode::Line;
					SetSelection(mInteractiveStart, mInteractiveEnd, mSelectionMode);
				}

				mLastClick = -1.0f;
			}

			/*
			Left mouse button double click
			*/

			else if (doubleClick)
			{
				if (!ctrl)
				{
					mState.mCursorPosition = mInteractiveStart = mInteractiveEnd = ScreenPosToCoordinates(ImGui::GetMousePos());
					if (mSelectionMode == SelectionMode::Line)
						mSelectionMode = SelectionMode::Normal;
					else
						mSelectionMode = SelectionMode::Word;
					SetSelection(mInteractiveStart, mInteractiveEnd, mSelectionMode);
				}

				mLastClick = (float)ImGui::GetTime();
			}

			/*
			Left mouse button click
			*/
			else if (click)
			{
				mState.mCursorPosition = mInteractiveStart = mInteractiveEnd = ScreenPosToCoordinates(ImGui::GetMousePos());
				if (ctrl)
					mSelectionMode = SelectionMode::Word;
				else
					mSelectionMode = SelectionMode::Normal;
				SetSelection(mInteractiveStart, mInteractiveEnd, mSelectionMode);

				mLastClick = (float)ImGui::GetTime();
			}
			// Mouse left button dragging (=> update selection)
			else if (ImGui::IsMouseDragging(0) && ImGui::IsMouseDown(0))
			{
				io.WantCaptureMouse = true;
				mState.mCursorPosition = mInteractiveEnd = ScreenPosToCoordinates(ImGui::GetMousePos());
				SetSelection(mInteractiveStart, mInteractiveEnd, mSelectionMode);
			}
		}
	}
}

void TextEditor::Render()
{
	/* Compute mCharAdvance regarding to scaled font size (Ctrl + mouse wheel)*/
	const float fontSize = ImGui::GetFont()->CalcTextSizeA(ImGui::GetFontSize(), FLT_MAX, -1.0f, "#", nullptr, nullptr).x;
	mCharAdvance = ImVec2(fontSize, ImGui::GetTextLineHeightWithSpacing() * mLineSpacing);

	/* Update palette with the current alpha from style */
	for (int i = 0; i < (int)PaletteIndex::Max; ++i)
	{
		auto color = ImGui::ColorConvertU32ToFloat4(mPaletteBase[i]);
		color.w *= ImGui::GetStyle().Alpha;
		mPalette[i] = ImGui::ColorConvertFloat4ToU32(color);
	}

	assert(mLineBuffer.empty());

	auto contentSize = ImGui::GetWindowContentRegionMax();
	auto drawList = ImGui::GetWindowDrawList();
	float longest(mTextStart);

	if (mScrollToTop)
	{
		mScrollToTop = false;
		ImGui::SetScrollY(0.f);
	}

	ImVec2 cursorScreenPos = ImGui::GetCursorScreenPos();
	auto scrollX = ImGui::GetScrollX();
	auto scrollY = ImGui::GetScrollY();

	auto lineNo = (int)floor(scrollY / mCharAdvance.y);
	auto globalLineMax = (int)mLines.size();
	auto lineMax = std::max(0, std::min((int)mLines.size() - 1, lineNo + (int)floor((scrollY + contentSize.y) / mCharAdvance.y)));

	// Deduce mTextStart by evaluating mLines size (global lineMax) plus two spaces as text width
	char buf[16];
	snprintf(buf, 16, " %d ", globalLineMax);
	mTextStart = ImGui::GetFont()->CalcTextSizeA(ImGui::GetFontSize(), FLT_MAX, -1.0f, buf, nullptr, nullptr).x + mLeftMargin;

	if (!mLines.empty())
	{
		float spaceSize = ImGui::GetFont()->CalcTextSizeA(ImGui::GetFontSize(), FLT_MAX, -1.0f, " ", nullptr, nullptr).x;

		while (lineNo <= lineMax)
		{
			ImVec2 lineStartScreenPos = ImVec2(cursorScreenPos.x, cursorScreenPos.y + lineNo * mCharAdvance.y);
			ImVec2 textScreenPos = ImVec2(lineStartScreenPos.x + mTextStart, lineStartScreenPos.y);

			auto& line = mLines[lineNo];
			longest = std::max(mTextStart + TextDistanceToLineStart(Coordinates(lineNo, GetLineMaxColumn(lineNo))), longest);
			auto columnNo = 0;
			Coordinates lineStartCoord(lineNo, 0);
			Coordinates lineEndCoord(lineNo, GetLineMaxColumn(lineNo));

			// Draw selection for the current line
			float sstart = -1.0f;
			float ssend = -1.0f;

			assert(mState.mSelectionStart <= mState.mSelectionEnd);
			if (mState.mSelectionStart <= lineEndCoord)
				sstart = mState.mSelectionStart > lineStartCoord ? TextDistanceToLineStart(mState.mSelectionStart) : 0.0f;
			if (mState.mSelectionEnd > lineStartCoord)
				ssend = TextDistanceToLineStart(mState.mSelectionEnd < lineEndCoord ? mState.mSelectionEnd : lineEndCoord);

			if (mState.mSelectionEnd.mLine > lineNo)
				ssend += mCharAdvance.x;

			if (sstart != -1 && ssend != -1 && sstart < ssend)
			{
				ImVec2 vstart(lineStartScreenPos.x + mTextStart + sstart, lineStartScreenPos.y);
				ImVec2 vend(lineStartScreenPos.x + mTextStart + ssend, lineStartScreenPos.y + mCharAdvance.y);
				drawList->AddRectFilled(vstart, vend, mPalette[(int)PaletteIndex::Selection]);
			}

			// Draw breakpoints
			auto start = ImVec2(lineStartScreenPos.x + scrollX, lineStartScreenPos.y);

			if (mBreakpoints.count(lineNo + 1) != 0)
			{
				auto end = ImVec2(lineStartScreenPos.x + contentSize.x + 2.0f * scrollX, lineStartScreenPos.y + mCharAdvance.y);
				drawList->AddRectFilled(start, end, mPalette[(int)PaletteIndex::Breakpoint]);
			}

			// Draw error markers
			auto errorIt = mErrorMarkers.find(lineNo + 1);
			if (errorIt != mErrorMarkers.end())
			{
				auto end = ImVec2(lineStartScreenPos.x + contentSize.x + 2.0f * scrollX, lineStartScreenPos.y + mCharAdvance.y);
				drawList->AddRectFilled(start, end, mPalette[(int)PaletteIndex::ErrorMarker]);

				if (ImGui::IsMouseHoveringRect(lineStartScreenPos, end))
				{
					ImGui::BeginTooltip();
					ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.2f, 0.2f, 1.0f));
					ImGui::Text("Error at line %d:", errorIt->first);
					ImGui::PopStyleColor();
					ImGui::Separator();
					ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 0.2f, 1.0f));
					ImGui::Text("%s", errorIt->second.c_str());
					ImGui::PopStyleColor();
					ImGui::EndTooltip();
				}
			}

			// Draw line number (right aligned)
			snprintf(buf, 16, "%d  ", lineNo + 1);

			auto lineNoWidth = ImGui::GetFont()->CalcTextSizeA(ImGui::GetFontSize(), FLT_MAX, -1.0f, buf, nullptr, nullptr).x;
			drawList->AddText(ImVec2(lineStartScreenPos.x + mTextStart - lineNoWidth, lineStartScreenPos.y), mPalette[(int)PaletteIndex::LineNumber], buf);

			if (mState.mCursorPosition.mLine == lineNo)
			{
				auto focused = ImGui::IsWindowFocused();

				// Highlight the current line (where the cursor is)
				if (!HasSelection())
				{
					auto end = ImVec2(start.x + contentSize.x + scrollX, start.y + mCharAdvance.y);
					drawList->AddRectFilled(start, end, mPalette[(int)(focused ? PaletteIndex::CurrentLineFill : PaletteIndex::CurrentLineFillInactive)]);
					drawList->AddRect(start, end, mPalette[(int)PaletteIndex::CurrentLineEdge], 1.0f);
				}

				// Render the cursor
				if (focused)
				{
					auto timeEnd = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
					auto elapsed = timeEnd - mStartTime;
					if (elapsed > 400)
					{
						float width = 1.0f;
						auto cindex = GetCharacterIndex(mState.mCursorPosition);
						float cx = TextDistanceToLineStart(mState.mCursorPosition);

						if (mOverwrite && cindex < (int)line.size())
						{
							auto c = line[cindex].mChar;
							if (c == '\t')
							{
								auto x = (1.0f + std::floor((1.0f + cx) / (float(mTabSize) * spaceSize))) * (float(mTabSize) * spaceSize);
								width = x - cx;
							}
							else
							{
								char buf2[2];
								buf2[0] = line[cindex].mChar;
								buf2[1] = '\0';
								width = ImGui::GetFont()->CalcTextSizeA(ImGui::GetFontSize(), FLT_MAX, -1.0f, buf2).x;
							}
						}
						ImVec2 cstart(textScreenPos.x + cx, lineStartScreenPos.y);
						ImVec2 cend(textScreenPos.x + cx + width, lineStartScreenPos.y + mCharAdvance.y);
						drawList->AddRectFilled(cstart, cend, mPalette[(int)PaletteIndex::Cursor]);
						if (elapsed > 800)
							mStartTime = timeEnd;
					}
				}
			}

			// Render colorized text
			auto prevColor = line.empty() ? mPalette[(int)PaletteIndex::Default] : GetGlyphColor(line[0]);
			ImVec2 bufferOffset;

			for (int i = 0; i < line.size();)
			{
				auto& glyph = line[i];
				auto color = GetGlyphColor(glyph);

				if ((color != prevColor || glyph.mChar == '\t' || glyph.mChar == ' ') && !mLineBuffer.empty())
				{
					const ImVec2 newOffset(textScreenPos.x + bufferOffset.x, textScreenPos.y + bufferOffset.y);
					drawList->AddText(newOffset, prevColor, mLineBuffer.c_str());
					auto textSize = ImGui::GetFont()->CalcTextSizeA(ImGui::GetFontSize(), FLT_MAX, -1.0f, mLineBuffer.c_str(), nullptr, nullptr);
					bufferOffset.x += textSize.x;
					mLineBuffer.clear();
				}
				prevColor = color;

				if (glyph.mChar == '\t')
				{
					auto oldX = bufferOffset.x;
					bufferOffset.x = (1.0f + std::floor((1.0f + bufferOffset.x) / (float(mTabSize) * spaceSize))) * (float(mTabSize) * spaceSize);
					++i;

					if (mShowWhitespaces)
					{
						const auto s = ImGui::GetFontSize();
						const auto x1 = textScreenPos.x + oldX + 1.0f;
						const auto x2 = textScreenPos.x + bufferOffset.x - 1.0f;
						const auto y = textScreenPos.y + bufferOffset.y + s * 0.5f;
						const ImVec2 p1(x1, y);
						const ImVec2 p2(x2, y);
						const ImVec2 p3(x2 - s * 0.2f, y - s * 0.2f);
						const ImVec2 p4(x2 - s * 0.2f, y + s * 0.2f);
						drawList->AddLine(p1, p2, 0x90909090);
						drawList->AddLine(p2, p3, 0x90909090);
						drawList->AddLine(p2, p4, 0x90909090);
					}
				}
				else if (glyph.mChar == ' ')
				{
					if (mShowWhitespaces)
					{
						const auto s = ImGui::GetFontSize();
						const auto x = textScreenPos.x + bufferOffset.x + spaceSize * 0.5f;
						const auto y = textScreenPos.y + bufferOffset.y + s * 0.5f;
						drawList->AddCircleFilled(ImVec2(x, y), 1.5f, 0x80808080, 4);
					}
					bufferOffset.x += spaceSize;
					i++;
				}
				else
				{
					auto l = UTF8CharLength(glyph.mChar);
					while (l-- > 0)
						mLineBuffer.push_back(line[i++].mChar);
				}
				++columnNo;
			}

			if (!mLineBuffer.empty())
			{
				const ImVec2 newOffset(textScreenPos.x + bufferOffset.x, textScreenPos.y + bufferOffset.y);
				drawList->AddText(newOffset, prevColor, mLineBuffer.c_str());
				mLineBuffer.clear();
			}

			++lineNo;
		}

		// Draw a tooltip on known identifiers/preprocessor symbols
		if (ImGui::IsMousePosValid())
		{
			auto id = GetWordAt(ScreenPosToCoordinates(ImGui::GetMousePos()));
			if (!id.empty())
			{
				auto it = mLanguageDefinition.mIdentifiers.find(id);
				if (it != mLanguageDefinition.mIdentifiers.end())
				{
					ImGui::BeginTooltip();
					ImGui::TextUnformatted(it->second.mDeclaration.c_str());
					ImGui::EndTooltip();
				}
				else
				{
					auto pi = mLanguageDefinition.mPreprocIdentifiers.find(id);
					if (pi != mLanguageDefinition.mPreprocIdentifiers.end())
					{
						ImGui::BeginTooltip();
						ImGui::TextUnformatted(pi->second.mDeclaration.c_str());
						ImGui::EndTooltip();
					}
				}
			}
		}
	}


	ImGui::Dummy(ImVec2((longest + 2), mLines.size() * mCharAdvance.y));

	if (mScrollToCursor)
	{
		EnsureCursorVisible();
		ImGui::SetWindowFocus();
		mScrollToCursor = false;
	}
}

void TextEditor::Render(const char* aTitle, const ImVec2& aSize, bool aBorder)
{
	mWithinRender = true;
	mTextChanged = false;
	mCursorPositionChanged = false;

	ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::ColorConvertU32ToFloat4(mPalette[(int)PaletteIndex::Background]));
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
	if (!mIgnoreImGuiChild)
		ImGui::BeginChild(aTitle, aSize, aBorder, ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_AlwaysHorizontalScrollbar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoNavInputs);

	if (mHandleKeyboardInputs)
	{
		HandleKeyboardInputs();
		ImGui::PushItemFlag(ImGuiItemFlags_NoTabStop, false);
	}

	if (mHandleMouseInputs)
		HandleMouseInputs();

	ColorizeInternal();
	Render();

	if (mHandleKeyboardInputs)
		ImGui::PopItemFlag();

	if (!mIgnoreImGuiChild)
		ImGui::EndChild();

	ImGui::PopStyleVar();
	ImGui::PopStyleColor();

	mWithinRender = false;
}

void TextEditor::SetText(const std::string& aText)
{
	mLines.clear();
	mLines.emplace_back(Line());
	for (auto chr : aText)
	{
		if (chr == '\r')
		{
			// ignore the carriage return character
		}
		else if (chr == '\n')
			mLines.emplace_back(Line());
		else
		{
			mLines.back().emplace_back(Glyph(chr, PaletteIndex::Default));
		}
	}

	mTextChanged = true;
	mScrollToTop = true;

	mUndoBuffer.clear();
	mUndoIndex = 0;

	Colorize();
}

void TextEditor::SetTextLines(const std::vector<std::string>& aLines)
{
	mLines.clear();

	if (aLines.empty())
	{
		mLines.emplace_back(Line());
	}
	else
	{
		mLines.resize(aLines.size());

		for (size_t i = 0; i < aLines.size(); ++i)
		{
			const std::string& aLine = aLines[i];

			mLines[i].reserve(aLine.size());
			for (size_t j = 0; j < aLine.size(); ++j)
				mLines[i].emplace_back(Glyph(aLine[j], PaletteIndex::Default));
		}
	}

	mTextChanged = true;
	mScrollToTop = true;

	mUndoBuffer.clear();
	mUndoIndex = 0;

	Colorize();
}

void TextEditor::EnterCharacter(ImWchar aChar, bool aShift)
{
	assert(!mReadOnly);

	UndoRecord u;

	u.mBefore = mState;

	if (HasSelection())
	{
		if (aChar == '\t' && mState.mSelectionStart.mLine != mState.mSelectionEnd.mLine)
		{

			auto start = mState.mSelectionStart;
			auto end = mState.mSelectionEnd;
			auto originalEnd = end;

			if (start > end)
				std::swap(start, end);
			start.mColumn = 0;
			//			end.mColumn = end.mLine < mLines.size() ? mLines[end.mLine].size() : 0;
			if (end.mColumn == 0 && end.mLine > 0)
				--end.mLine;
			if (end.mLine >= (int)mLines.size())
				end.mLine = mLines.empty() ? 0 : (int)mLines.size() - 1;
			end.mColumn = GetLineMaxColumn(end.mLine);

			//if (end.mColumn >= GetLineMaxColumn(end.mLine))
			//	end.mColumn = GetLineMaxColumn(end.mLine) - 1;

			u.mRemovedStart = start;
			u.mRemovedEnd = end;
			u.mRemoved = GetText(start, end);

			bool modified = false;

			for (int i = start.mLine; i <= end.mLine; i++)
			{
				auto& line = mLines[i];
				if (aShift)
				{
					if (!line.empty())
					{
						if (line.front().mChar == '\t')
						{
							line.erase(line.begin());
							modified = true;
						}
						else
						{
							for (int j = 0; j < mTabSize && !line.empty() && line.front().mChar == ' '; j++)
							{
								line.erase(line.begin());
								modified = true;
							}
						}
					}
				}
				else
				{
					line.insert(line.begin(), Glyph('\t', TextEditor::PaletteIndex::Background));
					modified = true;
				}
			}

			if (modified)
			{
				start = Coordinates(start.mLine, GetCharacterColumn(start.mLine, 0));
				Coordinates rangeEnd;
				if (originalEnd.mColumn != 0)
				{
					end = Coordinates(end.mLine, GetLineMaxColumn(end.mLine));
					rangeEnd = end;
					u.mAdded = GetText(start, end);
				}
				else
				{
					end = Coordinates(originalEnd.mLine, 0);
					rangeEnd = Coordinates(end.mLine - 1, GetLineMaxColumn(end.mLine - 1));
					u.mAdded = GetText(start, rangeEnd);
				}

				u.mAddedStart = start;
				u.mAddedEnd = rangeEnd;
				u.mAfter = mState;

				mState.mSelectionStart = start;
				mState.mSelectionEnd = end;
				AddUndo(u);

				mTextChanged = true;

				EnsureCursorVisible();
			}

			return;
		} // c == '\t'
		else
		{
			u.mRemoved = GetSelectedText();
			u.mRemovedStart = mState.mSelectionStart;
			u.mRemovedEnd = mState.mSelectionEnd;
			DeleteSelection();
		}
	} // HasSelection

	auto coord = GetActualCursorCoordinates();
	u.mAddedStart = coord;

	assert(!mLines.empty());

	if (aChar == '\n')
	{
		InsertLine(coord.mLine + 1);
		auto& line = mLines[coord.mLine];
		auto& newLine = mLines[coord.mLine + 1];

		if (mLanguageDefinition.mAutoIndentation)
			for (size_t it = 0; it < line.size() && isascii(line[it].mChar) && isblank(line[it].mChar); ++it)
				newLine.push_back(line[it]);

		const size_t whitespaceSize = newLine.size();
		auto cindex = GetCharacterIndex(coord);
		newLine.insert(newLine.end(), line.begin() + cindex, line.end());
		line.erase(line.begin() + cindex, line.begin() + line.size());
		SetCursorPosition(Coordinates(coord.mLine + 1, GetCharacterColumn(coord.mLine + 1, (int)whitespaceSize)));
		u.mAdded = (char)aChar;
	}
	else
	{
		char buf[7];
		int e = ImTextCharToUtf8(buf, 7, aChar);
		if (e > 0)
		{
			buf[e] = '\0';
			auto& line = mLines[coord.mLine];
			auto cindex = GetCharacterIndex(coord);

			if (mOverwrite && cindex < (int)line.size())
			{
				auto d = UTF8CharLength(line[cindex].mChar);

				u.mRemovedStart = mState.mCursorPosition;
				u.mRemovedEnd = Coordinates(coord.mLine, GetCharacterColumn(coord.mLine, cindex + d));

				while (d-- > 0 && cindex < (int)line.size())
				{
					u.mRemoved += line[cindex].mChar;
					line.erase(line.begin() + cindex);
				}
			}

			for (auto p = buf; *p != '\0'; p++, ++cindex)
				line.insert(line.begin() + cindex, Glyph(*p, PaletteIndex::Default));
			u.mAdded = buf;

			SetCursorPosition(Coordinates(coord.mLine, GetCharacterColumn(coord.mLine, cindex)));
		}
		else
			return;
	}

	mTextChanged = true;

	u.mAddedEnd = GetActualCursorCoordinates();
	u.mAfter = mState;

	AddUndo(u);

	Colorize(coord.mLine - 1, 3);
	EnsureCursorVisible();
}

void TextEditor::SetReadOnly(bool aValue)
{
	mReadOnly = aValue;
}

void TextEditor::SetColorizerEnable(bool aValue)
{
	mColorizerEnabled = aValue;
}

void TextEditor::SetCursorPosition(const Coordinates& aPosition)
{
	if (mState.mCursorPosition != aPosition)
	{
		mState.mCursorPosition = aPosition;
		mCursorPositionChanged = true;
		EnsureCursorVisible();
	}
}

void TextEditor::SetSelectionStart(const Coordinates& aPosition)
{
	mState.mSelectionStart = SanitizeCoordinates(aPosition);
	if (mState.mSelectionStart > mState.mSelectionEnd)
		std::swap(mState.mSelectionStart, mState.mSelectionEnd);
}

void TextEditor::SetSelectionEnd(const Coordinates& aPosition)
{
	mState.mSelectionEnd = SanitizeCoordinates(aPosition);
	if (mState.mSelectionStart > mState.mSelectionEnd)
		std::swap(mState.mSelectionStart, mState.mSelectionEnd);
}

void TextEditor::SetSelection(const Coordinates& aStart, const Coordinates& aEnd, SelectionMode aMode)
{
	auto oldSelStart = mState.mSelectionStart;
	auto oldSelEnd = mState.mSelectionEnd;

	mState.mSelectionStart = SanitizeCoordinates(aStart);
	mState.mSelectionEnd = SanitizeCoordinates(aEnd);
	if (mState.mSelectionStart > mState.mSelectionEnd)
		std::swap(mState.mSelectionStart, mState.mSelectionEnd);

	switch (aMode)
	{
	case TextEditor::SelectionMode::Normal:
		break;
	case TextEditor::SelectionMode::Word:
	{
		mState.mSelectionStart = FindWordStart(mState.mSelectionStart);
		if (!IsOnWordBoundary(mState.mSelectionEnd))
			mState.mSelectionEnd = FindWordEnd(FindWordStart(mState.mSelectionEnd));
		break;
	}
	case TextEditor::SelectionMode::Line:
	{
		const auto lineNo = mState.mSelectionEnd.mLine;
		const auto lineSize = (size_t)lineNo < mLines.size() ? mLines[lineNo].size() : 0;
		mState.mSelectionStart = Coordinates(mState.mSelectionStart.mLine, 0);
		mState.mSelectionEnd = Coordinates(lineNo, GetLineMaxColumn(lineNo));
		break;
	}
	default:
		break;
	}

	if (mState.mSelectionStart != oldSelStart ||
		mState.mSelectionEnd != oldSelEnd)
		mCursorPositionChanged = true;
}

void TextEditor::SetTabSize(int aValue)
{
	mTabSize = std::max(0, std::min(32, aValue));
}

void TextEditor::InsertText(const std::string& aValue)
{
	InsertText(aValue.c_str());
}

void TextEditor::InsertText(const char* aValue)
{
	if (aValue == nullptr)
		return;

	auto pos = GetActualCursorCoordinates();
	auto start = std::min(pos, mState.mSelectionStart);
	int totalLines = pos.mLine - start.mLine;

	totalLines += InsertTextAt(pos, aValue);

	SetSelection(pos, pos);
	SetCursorPosition(pos);
	Colorize(start.mLine - 1, totalLines + 2);
}

void TextEditor::DeleteSelection()
{
	assert(mState.mSelectionEnd >= mState.mSelectionStart);

	if (mState.mSelectionEnd == mState.mSelectionStart)
		return;

	DeleteRange(mState.mSelectionStart, mState.mSelectionEnd);

	SetSelection(mState.mSelectionStart, mState.mSelectionStart);
	SetCursorPosition(mState.mSelectionStart);
	Colorize(mState.mSelectionStart.mLine, 1);
}

void TextEditor::MoveUp(int aAmount, bool aSelect)
{
	auto oldPos = mState.mCursorPosition;
	mState.mCursorPosition.mLine = std::max(0, mState.mCursorPosition.mLine - aAmount);
	if (oldPos != mState.mCursorPosition)
	{
		if (aSelect)
		{
			if (oldPos == mInteractiveStart)
				mInteractiveStart = mState.mCursorPosition;
			else if (oldPos == mInteractiveEnd)
				mInteractiveEnd = mState.mCursorPosition;
			else
			{
				mInteractiveStart = mState.mCursorPosition;
				mInteractiveEnd = oldPos;
			}
		}
		else
			mInteractiveStart = mInteractiveEnd = mState.mCursorPosition;
		SetSelection(mInteractiveStart, mInteractiveEnd);

		EnsureCursorVisible();
	}
}

void TextEditor::MoveDown(int aAmount, bool aSelect)
{
	assert(mState.mCursorPosition.mColumn >= 0);
	auto oldPos = mState.mCursorPosition;
	mState.mCursorPosition.mLine = std::max(0, std::min((int)mLines.size() - 1, mState.mCursorPosition.mLine + aAmount));

	if (mState.mCursorPosition != oldPos)
	{
		if (aSelect)
		{
			if (oldPos == mInteractiveEnd)
				mInteractiveEnd = mState.mCursorPosition;
			else if (oldPos == mInteractiveStart)
				mInteractiveStart = mState.mCursorPosition;
			else
			{
				mInteractiveStart = oldPos;
				mInteractiveEnd = mState.mCursorPosition;
			}
		}
		else
			mInteractiveStart = mInteractiveEnd = mState.mCursorPosition;
		SetSelection(mInteractiveStart, mInteractiveEnd);

		EnsureCursorVisible();
	}
}

static bool IsUTFSequence(char c)
{
	return (c & 0xC0) == 0x80;
}

void TextEditor::MoveLeft(int aAmount, bool aSelect, bool aWordMode)
{
	if (mLines.empty())
		return;

	auto oldPos = mState.mCursorPosition;
	mState.mCursorPosition = GetActualCursorCoordinates();
	auto line = mState.mCursorPosition.mLine;
	auto cindex = GetCharacterIndex(mState.mCursorPosition);

	while (aAmount-- > 0)
	{
		if (cindex == 0)
		{
			if (line > 0)
			{
				--line;
				if ((int)mLines.size() > line)
					cindex = (int)mLines[line].size();
				else
					cindex = 0;
			}
		}
		else
		{
			--cindex;
			if (cindex > 0)
			{
				if ((int)mLines.size() > line)
				{
					while (cindex > 0 && IsUTFSequence(mLines[line][cindex].mChar))
						--cindex;
				}
			}
		}

		mState.mCursorPosition = Coordinates(line, GetCharacterColumn(line, cindex));
		if (aWordMode)
		{
			mState.mCursorPosition = FindWordStart(mState.mCursorPosition);
			cindex = GetCharacterIndex(mState.mCursorPosition);
		}
	}

	mState.mCursorPosition = Coordinates(line, GetCharacterColumn(line, cindex));

	assert(mState.mCursorPosition.mColumn >= 0);
	if (aSelect)
	{
		if (oldPos == mInteractiveStart)
			mInteractiveStart = mState.mCursorPosition;
		else if (oldPos == mInteractiveEnd)
			mInteractiveEnd = mState.mCursorPosition;
		else
		{
			mInteractiveStart = mState.mCursorPosition;
			mInteractiveEnd = oldPos;
		}
	}
	else
		mInteractiveStart = mInteractiveEnd = mState.mCursorPosition;
	SetSelection(mInteractiveStart, mInteractiveEnd, aSelect && aWordMode ? SelectionMode::Word : SelectionMode::Normal);

	EnsureCursorVisible();
}

void TextEditor::MoveRight(int aAmount, bool aSelect, bool aWordMode)
{
	auto oldPos = mState.mCursorPosition;

	if (mLines.empty() || oldPos.mLine >= mLines.size())
		return;

	auto cindex = GetCharacterIndex(mState.mCursorPosition);
	while (aAmount-- > 0)
	{
		auto lindex = mState.mCursorPosition.mLine;
		auto& line = mLines[lindex];

		if (cindex >= line.size())
		{
			if (mState.mCursorPosition.mLine < mLines.size() - 1)
			{
				mState.mCursorPosition.mLine = std::max(0, std::min((int)mLines.size() - 1, mState.mCursorPosition.mLine + 1));
				mState.mCursorPosition.mColumn = 0;
			}
			else
				return;
		}
		else
		{
			cindex += UTF8CharLength(line[cindex].mChar);
			mState.mCursorPosition = Coordinates(lindex, GetCharacterColumn(lindex, cindex));
			if (aWordMode)
				mState.mCursorPosition = FindNextWord(mState.mCursorPosition);
		}
	}

	if (aSelect)
	{
		if (oldPos == mInteractiveEnd)
			mInteractiveEnd = SanitizeCoordinates(mState.mCursorPosition);
		else if (oldPos == mInteractiveStart)
			mInteractiveStart = mState.mCursorPosition;
		else
		{
			mInteractiveStart = oldPos;
			mInteractiveEnd = mState.mCursorPosition;
		}
	}
	else
		mInteractiveStart = mInteractiveEnd = mState.mCursorPosition;
	SetSelection(mInteractiveStart, mInteractiveEnd, aSelect && aWordMode ? SelectionMode::Word : SelectionMode::Normal);

	EnsureCursorVisible();
}

void TextEditor::MoveTop(bool aSelect)
{
	auto oldPos = mState.mCursorPosition;
	SetCursorPosition(Coordinates(0, 0));

	if (mState.mCursorPosition != oldPos)
	{
		if (aSelect)
		{
			mInteractiveEnd = oldPos;
			mInteractiveStart = mState.mCursorPosition;
		}
		else
			mInteractiveStart = mInteractiveEnd = mState.mCursorPosition;
		SetSelection(mInteractiveStart, mInteractiveEnd);
	}
}

void TextEditor::TextEditor::MoveBottom(bool aSelect)
{
	auto oldPos = GetCursorPosition();
	auto newPos = Coordinates((int)mLines.size() - 1, 0);
	SetCursorPosition(newPos);
	if (aSelect)
	{
		mInteractiveStart = oldPos;
		mInteractiveEnd = newPos;
	}
	else
		mInteractiveStart = mInteractiveEnd = newPos;
	SetSelection(mInteractiveStart, mInteractiveEnd);
}

void TextEditor::MoveHome(bool aSelect)
{
	auto oldPos = mState.mCursorPosition;
	SetCursorPosition(Coordinates(mState.mCursorPosition.mLine, 0));

	if (mState.mCursorPosition != oldPos)
	{
		if (aSelect)
		{
			if (oldPos == mInteractiveStart)
				mInteractiveStart = mState.mCursorPosition;
			else if (oldPos == mInteractiveEnd)
				mInteractiveEnd = mState.mCursorPosition;
			else
			{
				mInteractiveStart = mState.mCursorPosition;
				mInteractiveEnd = oldPos;
			}
		}
		else
			mInteractiveStart = mInteractiveEnd = mState.mCursorPosition;
		SetSelection(mInteractiveStart, mInteractiveEnd);
	}
}

void TextEditor::MoveEnd(bool aSelect)
{
	auto oldPos = mState.mCursorPosition;
	SetCursorPosition(Coordinates(mState.mCursorPosition.mLine, GetLineMaxColumn(oldPos.mLine)));

	if (mState.mCursorPosition != oldPos)
	{
		if (aSelect)
		{
			if (oldPos == mInteractiveEnd)
				mInteractiveEnd = mState.mCursorPosition;
			else if (oldPos == mInteractiveStart)
				mInteractiveStart = mState.mCursorPosition;
			else
			{
				mInteractiveStart = oldPos;
				mInteractiveEnd = mState.mCursorPosition;
			}
		}
		else
			mInteractiveStart = mInteractiveEnd = mState.mCursorPosition;
		SetSelection(mInteractiveStart, mInteractiveEnd);
	}
}

void TextEditor::Delete()
{
	assert(!mReadOnly);

	if (mLines.empty())
		return;

	UndoRecord u;
	u.mBefore = mState;

	if (HasSelection())
	{
		u.mRemoved = GetSelectedText();
		u.mRemovedStart = mState.mSelectionStart;
		u.mRemovedEnd = mState.mSelectionEnd;

		DeleteSelection();
	}
	else
	{
		auto pos = GetActualCursorCoordinates();
		SetCursorPosition(pos);
		auto& line = mLines[pos.mLine];

		if (pos.mColumn == GetLineMaxColumn(pos.mLine))
		{
			if (pos.mLine == (int)mLines.size() - 1)
				return;

			u.mRemoved = '\n';
			u.mRemovedStart = u.mRemovedEnd = GetActualCursorCoordinates();
			Advance(u.mRemovedEnd);

			auto& nextLine = mLines[pos.mLine + 1];
			line.insert(line.end(), nextLine.begin(), nextLine.end());
			RemoveLine(pos.mLine + 1);
		}
		else
		{
			auto cindex = GetCharacterIndex(pos);
			u.mRemovedStart = u.mRemovedEnd = GetActualCursorCoordinates();
			u.mRemovedEnd.mColumn++;
			u.mRemoved = GetText(u.mRemovedStart, u.mRemovedEnd);

			auto d = UTF8CharLength(line[cindex].mChar);
			while (d-- > 0 && cindex < (int)line.size())
				line.erase(line.begin() + cindex);
		}

		mTextChanged = true;

		Colorize(pos.mLine, 1);
	}

	u.mAfter = mState;
	AddUndo(u);
}

void TextEditor::Backspace()
{
	assert(!mReadOnly);

	if (mLines.empty())
		return;

	UndoRecord u;
	u.mBefore = mState;

	if (HasSelection())
	{
		u.mRemoved = GetSelectedText();
		u.mRemovedStart = mState.mSelectionStart;
		u.mRemovedEnd = mState.mSelectionEnd;

		DeleteSelection();
	}
	else
	{
		auto pos = GetActualCursorCoordinates();
		SetCursorPosition(pos);

		if (mState.mCursorPosition.mColumn == 0)
		{
			if (mState.mCursorPosition.mLine == 0)
				return;

			u.mRemoved = '\n';
			u.mRemovedStart = u.mRemovedEnd = Coordinates(pos.mLine - 1, GetLineMaxColumn(pos.mLine - 1));
			Advance(u.mRemovedEnd);

			auto& line = mLines[mState.mCursorPosition.mLine];
			auto& prevLine = mLines[mState.mCursorPosition.mLine - 1];
			auto prevSize = GetLineMaxColumn(mState.mCursorPosition.mLine - 1);
			prevLine.insert(prevLine.end(), line.begin(), line.end());

			ErrorMarkers etmp;
			for (auto& i : mErrorMarkers)
				etmp.insert(ErrorMarkers::value_type(i.first - 1 == mState.mCursorPosition.mLine ? i.first - 1 : i.first, i.second));
			mErrorMarkers = std::move(etmp);

			RemoveLine(mState.mCursorPosition.mLine);
			--mState.mCursorPosition.mLine;
			mState.mCursorPosition.mColumn = prevSize;
		}
		else
		{
			auto& line = mLines[mState.mCursorPosition.mLine];
			auto cindex = GetCharacterIndex(pos) - 1;
			auto cend = cindex + 1;
			while (cindex > 0 && IsUTFSequence(line[cindex].mChar))
				--cindex;

			//if (cindex > 0 && UTF8CharLength(line[cindex].mChar) > 1)
			//	--cindex;

			u.mRemovedStart = u.mRemovedEnd = GetActualCursorCoordinates();
			--u.mRemovedStart.mColumn;
			--mState.mCursorPosition.mColumn;

			while (cindex < line.size() && cend-- > cindex)
			{
				u.mRemoved += line[cindex].mChar;
				line.erase(line.begin() + cindex);
			}
		}

		mTextChanged = true;

		EnsureCursorVisible();
		Colorize(mState.mCursorPosition.mLine, 1);
	}

	u.mAfter = mState;
	AddUndo(u);
}

void TextEditor::SelectWordUnderCursor()
{
	auto c = GetCursorPosition();
	SetSelection(FindWordStart(c), FindWordEnd(c));
}

void TextEditor::SelectAll()
{
	SetSelection(Coordinates(0, 0), Coordinates((int)mLines.size(), 0));
}

bool TextEditor::HasSelection() const
{
	return mState.mSelectionEnd > mState.mSelectionStart;
}

void TextEditor::Copy()
{
	if (HasSelection())
	{
		ImGui::SetClipboardText(GetSelectedText().c_str());
	}
	else
	{
		if (!mLines.empty())
		{
			std::string str;
			auto& line = mLines[GetActualCursorCoordinates().mLine];
			for (auto& g : line)
				str.push_back(g.mChar);
			ImGui::SetClipboardText(str.c_str());
		}
	}
}

void TextEditor::Cut()
{
	if (IsReadOnly())
	{
		Copy();
	}
	else
	{
		if (HasSelection())
		{
			UndoRecord u;
			u.mBefore = mState;
			u.mRemoved = GetSelectedText();
			u.mRemovedStart = mState.mSelectionStart;
			u.mRemovedEnd = mState.mSelectionEnd;

			Copy();
			DeleteSelection();

			u.mAfter = mState;
			AddUndo(u);
		}
	}
}

void TextEditor::Paste()
{
	if (IsReadOnly())
		return;

	auto clipText = ImGui::GetClipboardText();
	if (clipText != nullptr && strlen(clipText) > 0)
	{
		UndoRecord u;
		u.mBefore = mState;

		if (HasSelection())
		{
			u.mRemoved = GetSelectedText();
			u.mRemovedStart = mState.mSelectionStart;
			u.mRemovedEnd = mState.mSelectionEnd;
			DeleteSelection();
		}

		u.mAdded = clipText;
		u.mAddedStart = GetActualCursorCoordinates();

		InsertText(clipText);

		u.mAddedEnd = GetActualCursorCoordinates();
		u.mAfter = mState;
		AddUndo(u);
	}
}

bool TextEditor::CanUndo() const
{
	return !mReadOnly && mUndoIndex > 0;
}

bool TextEditor::CanRedo() const
{
	return !mReadOnly && mUndoIndex < (int)mUndoBuffer.size();
}

void TextEditor::Undo(int aSteps)
{
	while (CanUndo() && aSteps-- > 0)
		mUndoBuffer[--mUndoIndex].Undo(this);
}

void TextEditor::Redo(int aSteps)
{
	while (CanRedo() && aSteps-- > 0)
		mUndoBuffer[mUndoIndex++].Redo(this);
}

const TextEditor::Palette& TextEditor::GetDarkPalette()
{
	const static Palette p = { {
			0xff7f7f7f,	// Default
			0xffd69c56,	// Keyword	
			0xff00ff00,	// Number
			0xff7070e0,	// String
			0xff70a0e0, // Char literal
			0xffffffff, // Punctuation
			0xff408080,	// Preprocessor
			0xffaaaaaa, // Identifier
			0xff9bc64d, // Known identifier
			0xffc040a0, // Preproc identifier
			0xff206020, // Comment (single line)
			0xff406020, // Comment (multi line)
			0xff101010, // Background
			0xffe0e0e0, // Cursor
			0x80a06020, // Selection
			0x800020ff, // ErrorMarker
			0x40f08000, // Breakpoint
			0xff707000, // Line number
			0x40000000, // Current line fill
			0x40808080, // Current line fill (inactive)
			0x40a0a0a0, // Current line edge
		} };
	return p;
}

const TextEditor::Palette& TextEditor::GetLightPalette()
{
	const static Palette p = { {
			0xff7f7f7f,	// None
			0xffff0c06,	// Keyword	
			0xff008000,	// Number
			0xff2020a0,	// String
			0xff304070, // Char literal
			0xff000000, // Punctuation
			0xff406060,	// Preprocessor
			0xff404040, // Identifier
			0xff606010, // Known identifier
			0xffc040a0, // Preproc identifier
			0xff205020, // Comment (single line)
			0xff405020, // Comment (multi line)
			0xffffffff, // Background
			0xff000000, // Cursor
			0x80600000, // Selection
			0xa00010ff, // ErrorMarker
			0x80f08000, // Breakpoint
			0xff505000, // Line number
			0x40000000, // Current line fill
			0x40808080, // Current line fill (inactive)
			0x40000000, // Current line edge
		} };
	return p;
}

const TextEditor::Palette& TextEditor::GetRetroBluePalette()
{
	const static Palette p = { {
			0xff00ffff,	// None
			0xffffff00,	// Keyword	
			0xff00ff00,	// Number
			0xff808000,	// String
			0xff808000, // Char literal
			0xffffffff, // Punctuation
			0xff008000,	// Preprocessor
			0xff00ffff, // Identifier
			0xffffffff, // Known identifier
			0xffff00ff, // Preproc identifier
			0xff808080, // Comment (single line)
			0xff404040, // Comment (multi line)
			0xff800000, // Background
			0xff0080ff, // Cursor
			0x80ffff00, // Selection
			0xa00000ff, // ErrorMarker
			0x80ff8000, // Breakpoint
			0xff808000, // Line number
			0x40000000, // Current line fill
			0x40808080, // Current line fill (inactive)
			0x40000000, // Current line edge
		} };
	return p;
}

const TextEditor::Palette& TextEditor::GetGameMakerPalette()
{
	const static TextEditor::Palette p = { {
			0xFFB2B1FF, // None 
			0xFF71B8FF, // Keyword	 
			0xFF8B8BFF, // Number 
			0xFF00FFFF, // String 
			0xFFE7C547, // Char literal 
			0xFFC8C8C8, // Punctuation 
			0xFF8080FF, // Preprocessor 
			0xFFFFB1B2, // Identifier 
			0xFF71B8FF, // Known identifier 
			0xFF5AE558, // Preproc identifier 
			0xFF509050, // Comment (single line)
			0xFF509050, // Comment (multi line)
			0xFF202020, // Background
			0xFFFFFFFF, // Cursor
			0x80808000, // Selection
			0x800020ff, // ErrorMarker
			0x40F08000, // Breakpoint
			0xFFF4C340, // Line number
			0x20808080, // Current line fill
			0x10808080, // Current line fill (inactive)
			0x80000000, // Current line edge 
		} };
	return p;
}


std::string TextEditor::GetText() const
{
	return GetText(Coordinates(), Coordinates((int)mLines.size(), 0));
}

std::vector<std::string> TextEditor::GetTextLines() const
{
	std::vector<std::string> result;

	result.reserve(mLines.size());

	for (auto& line : mLines)
	{
		std::string text;

		text.resize(line.size());

		for (size_t i = 0; i < line.size(); ++i)
			text[i] = line[i].mChar;

		result.emplace_back(std::move(text));
	}

	return result;
}

std::string TextEditor::GetSelectedText() const
{
	return GetText(mState.mSelectionStart, mState.mSelectionEnd);
}

std::string TextEditor::GetCurrentLineText()const
{
	auto lineLength = GetLineMaxColumn(mState.mCursorPosition.mLine);
	return GetText(
		Coordinates(mState.mCursorPosition.mLine, 0),
		Coordinates(mState.mCursorPosition.mLine, lineLength));
}

void TextEditor::ProcessInputs()
{
}

void TextEditor::Colorize(int aFromLine, int aLines)
{
	int toLine = aLines == -1 ? (int)mLines.size() : std::min((int)mLines.size(), aFromLine + aLines);
	mColorRangeMin = std::min(mColorRangeMin, aFromLine);
	mColorRangeMax = std::max(mColorRangeMax, toLine);
	mColorRangeMin = std::max(0, mColorRangeMin);
	mColorRangeMax = std::max(mColorRangeMin, mColorRangeMax);
	mCheckComments = true;
}

void TextEditor::ColorizeRange(int aFromLine, int aToLine)
{
	if (mLines.empty() || aFromLine >= aToLine)
		return;

	std::string buffer;
	std::cmatch results;
	std::string id;

	int endLine = std::max(0, std::min((int)mLines.size(), aToLine));
	for (int i = aFromLine; i < endLine; ++i)
	{
		auto& line = mLines[i];

		if (line.empty())
			continue;

		buffer.resize(line.size());
		for (size_t j = 0; j < line.size(); ++j)
		{
			auto& col = line[j];
			buffer[j] = col.mChar;
			col.mColorIndex = PaletteIndex::Default;
		}

		const char* bufferBegin = &buffer.front();
		const char* bufferEnd = bufferBegin + buffer.size();

		auto last = bufferEnd;

		for (auto first = bufferBegin; first != last; )
		{
			const char* token_begin = nullptr;
			const char* token_end = nullptr;
			PaletteIndex token_color = PaletteIndex::Default;

			bool hasTokenizeResult = false;

			if (mLanguageDefinition.mTokenize != nullptr)
			{
				if (mLanguageDefinition.mTokenize(first, last, token_begin, token_end, token_color))
					hasTokenizeResult = true;
			}

			if (hasTokenizeResult == false)
			{
				// todo : remove
				//printf("using regex for %.*s\n", first + 10 < last ? 10 : int(last - first), first);

				for (auto& p : mRegexList)
				{
					if (std::regex_search(first, last, results, p.first, std::regex_constants::match_continuous))
					{
						hasTokenizeResult = true;

						auto& v = *results.begin();
						token_begin = v.first;
						token_end = v.second;
						token_color = p.second;
						break;
					}
				}
			}

			if (hasTokenizeResult == false)
			{
				first++;
			}
			else
			{
				const size_t token_length = token_end - token_begin;

				if (token_color == PaletteIndex::Identifier)
				{
					id.assign(token_begin, token_end);

					// todo : allmost all language definitions use lower case to specify keywords, so shouldn't this use ::tolower ?
					if (!mLanguageDefinition.mCaseSensitive)
						std::transform(id.begin(), id.end(), id.begin(), ::toupper);

					if (!line[first - bufferBegin].mPreprocessor)
					{
						if (mLanguageDefinition.mKeywords.count(id) != 0)
							token_color = PaletteIndex::Keyword;
						else if (mLanguageDefinition.mIdentifiers.count(id) != 0)
							token_color = PaletteIndex::KnownIdentifier;
						else if (mLanguageDefinition.mPreprocIdentifiers.count(id) != 0)
							token_color = PaletteIndex::PreprocIdentifier;
					}
					else
					{
						if (mLanguageDefinition.mPreprocIdentifiers.count(id) != 0)
							token_color = PaletteIndex::PreprocIdentifier;
					}
				}

				for (size_t j = 0; j < token_length; ++j)
					line[(token_begin - bufferBegin) + j].mColorIndex = token_color;

				first = token_end;
			}
		}
	}
}

void TextEditor::ColorizeInternal()
{
	if (mLines.empty() || !mColorizerEnabled)
		return;

	if (mCheckComments)
	{
		auto endLine = mLines.size();
		auto endIndex = 0;
		auto commentStartLine = endLine;
		auto commentStartIndex = endIndex;
		auto withinString = false;
		auto withinSingleLineComment = false;
		auto withinPreproc = false;
		auto firstChar = true;			// there is no other non-whitespace characters in the line before
		auto concatenate = false;		// '\' on the very end of the line
		auto currentLine = 0;
		auto currentIndex = 0;
		while (currentLine < endLine || currentIndex < endIndex)
		{
			auto& line = mLines[currentLine];

			if (currentIndex == 0 && !concatenate)
			{
				withinSingleLineComment = false;
				withinPreproc = false;
				firstChar = true;
			}

			concatenate = false;

			if (!line.empty())
			{
				auto& g = line[currentIndex];
				auto c = g.mChar;

				if (c != mLanguageDefinition.mPreprocChar && !isspace(c))
					firstChar = false;

				if (currentIndex == (int)line.size() - 1 && line[line.size() - 1].mChar == '\\')
					concatenate = true;

				bool inComment = (commentStartLine < currentLine || (commentStartLine == currentLine && commentStartIndex <= currentIndex));

				if (withinString)
				{
					line[currentIndex].mMultiLineComment = inComment;

					if (c == '\"')
					{
						if (currentIndex + 1 < (int)line.size() && line[currentIndex + 1].mChar == '\"')
						{
							currentIndex += 1;
							if (currentIndex < (int)line.size())
								line[currentIndex].mMultiLineComment = inComment;
						}
						else
							withinString = false;
					}
					else if (c == '\\')
					{
						currentIndex += 1;
						if (currentIndex < (int)line.size())
							line[currentIndex].mMultiLineComment = inComment;
					}
				}
				else
				{
					if (firstChar && c == mLanguageDefinition.mPreprocChar)
						withinPreproc = true;

					if (c == '\"')
					{
						withinString = true;
						line[currentIndex].mMultiLineComment = inComment;
					}
					else
					{
						auto pred = [](const char& a, const Glyph& b) { return a == b.mChar; };
						auto from = line.begin() + currentIndex;
						auto& startStr = mLanguageDefinition.mCommentStart;
						auto& singleStartStr = mLanguageDefinition.mSingleLineComment;

						if (singleStartStr.size() > 0 &&
							currentIndex + singleStartStr.size() <= line.size() &&
							equals(singleStartStr.begin(), singleStartStr.end(), from, from + singleStartStr.size(), pred))
						{
							withinSingleLineComment = true;
						}
						else if (!withinSingleLineComment && currentIndex + startStr.size() <= line.size() &&
							equals(startStr.begin(), startStr.end(), from, from + startStr.size(), pred))
						{
							commentStartLine = currentLine;
							commentStartIndex = currentIndex;
						}

						inComment = inComment = (commentStartLine < currentLine || (commentStartLine == currentLine && commentStartIndex <= currentIndex));

						line[currentIndex].mMultiLineComment = inComment;
						line[currentIndex].mComment = withinSingleLineComment;

						auto& endStr = mLanguageDefinition.mCommentEnd;
						if (currentIndex + 1 >= (int)endStr.size() &&
							equals(endStr.begin(), endStr.end(), from + 1 - endStr.size(), from + 1, pred))
						{
							commentStartIndex = endIndex;
							commentStartLine = endLine;
						}
					}
				}
				line[currentIndex].mPreprocessor = withinPreproc;
				currentIndex += UTF8CharLength(c);
				if (currentIndex >= (int)line.size())
				{
					currentIndex = 0;
					++currentLine;
				}
			}
			else
			{
				currentIndex = 0;
				++currentLine;
			}
		}
		mCheckComments = false;
	}

	if (mColorRangeMin < mColorRangeMax)
	{
		const int increment = (mLanguageDefinition.mTokenize == nullptr) ? 10 : 10000;
		const int to = std::min(mColorRangeMin + increment, mColorRangeMax);
		ColorizeRange(mColorRangeMin, to);
		mColorRangeMin = to;

		if (mColorRangeMax == mColorRangeMin)
		{
			mColorRangeMin = std::numeric_limits<int>::max();
			mColorRangeMax = 0;
		}
		return;
	}
}

float TextEditor::TextDistanceToLineStart(const Coordinates& aFrom) const
{
	auto& line = mLines[aFrom.mLine];
	float distance = 0.0f;
	float spaceSize = ImGui::GetFont()->CalcTextSizeA(ImGui::GetFontSize(), FLT_MAX, -1.0f, " ", nullptr, nullptr).x;
	int colIndex = GetCharacterIndex(aFrom);
	for (size_t it = 0u; it < line.size() && it < colIndex; )
	{
		if (line[it].mChar == '\t')
		{
			distance = (1.0f + std::floor((1.0f + distance) / (float(mTabSize) * spaceSize))) * (float(mTabSize) * spaceSize);
			++it;
		}
		else
		{
			auto d = UTF8CharLength(line[it].mChar);
			char tempCString[7];
			int i = 0;
			for (; i < 6 && d-- > 0 && it < (int)line.size(); i++, it++)
				tempCString[i] = line[it].mChar;

			tempCString[i] = '\0';
			distance += ImGui::GetFont()->CalcTextSizeA(ImGui::GetFontSize(), FLT_MAX, -1.0f, tempCString, nullptr, nullptr).x;
		}
	}

	return distance;
}

void TextEditor::EnsureCursorVisible()
{
	if (!mWithinRender)
	{
		mScrollToCursor = true;
		return;
	}

	float scrollX = ImGui::GetScrollX();
	float scrollY = ImGui::GetScrollY();

	auto height = ImGui::GetWindowHeight();
	auto width = ImGui::GetWindowWidth();

	auto top = 1 + (int)ceil(scrollY / mCharAdvance.y);
	auto bottom = (int)ceil((scrollY + height) / mCharAdvance.y);

	auto left = (int)ceil(scrollX / mCharAdvance.x);
	auto right = (int)ceil((scrollX + width) / mCharAdvance.x);

	auto pos = GetActualCursorCoordinates();
	auto len = TextDistanceToLineStart(pos);

	if (pos.mLine < top)
		ImGui::SetScrollY(std::max(0.0f, (pos.mLine - 1) * mCharAdvance.y));
	if (pos.mLine > bottom - 4)
		ImGui::SetScrollY(std::max(0.0f, (pos.mLine + 4) * mCharAdvance.y - height));
	if (len + mTextStart < left + 4)
		ImGui::SetScrollX(std::max(0.0f, len + mTextStart - 4));
	if (len + mTextStart > right - 4)
		ImGui::SetScrollX(std::max(0.0f, len + mTextStart + 4 - width));
}

int TextEditor::GetPageSize() const
{
	auto height = ImGui::GetWindowHeight() - 20.0f;
	return (int)floor(height / mCharAdvance.y);
}

TextEditor::UndoRecord::UndoRecord(
	const std::string& aAdded,
	const TextEditor::Coordinates aAddedStart,
	const TextEditor::Coordinates aAddedEnd,
	const std::string& aRemoved,
	const TextEditor::Coordinates aRemovedStart,
	const TextEditor::Coordinates aRemovedEnd,
	TextEditor::EditorState& aBefore,
	TextEditor::EditorState& aAfter)
	: mAdded(aAdded)
	, mAddedStart(aAddedStart)
	, mAddedEnd(aAddedEnd)
	, mRemoved(aRemoved)
	, mRemovedStart(aRemovedStart)
	, mRemovedEnd(aRemovedEnd)
	, mBefore(aBefore)
	, mAfter(aAfter)
{
	assert(mAddedStart <= mAddedEnd);
	assert(mRemovedStart <= mRemovedEnd);
}

void TextEditor::UndoRecord::Undo(TextEditor* aEditor)
{
	if (!mAdded.empty())
	{
		aEditor->DeleteRange(mAddedStart, mAddedEnd);
		aEditor->Colorize(mAddedStart.mLine - 1, mAddedEnd.mLine - mAddedStart.mLine + 2);
	}

	if (!mRemoved.empty())
	{
		auto start = mRemovedStart;
		aEditor->InsertTextAt(start, mRemoved.c_str());
		aEditor->Colorize(mRemovedStart.mLine - 1, mRemovedEnd.mLine - mRemovedStart.mLine + 2);
	}

	aEditor->mState = mBefore;
	aEditor->EnsureCursorVisible();

}

void TextEditor::UndoRecord::Redo(TextEditor* aEditor)
{
	if (!mRemoved.empty())
	{
		aEditor->DeleteRange(mRemovedStart, mRemovedEnd);
		aEditor->Colorize(mRemovedStart.mLine - 1, mRemovedEnd.mLine - mRemovedStart.mLine + 1);
	}

	if (!mAdded.empty())
	{
		auto start = mAddedStart;
		aEditor->InsertTextAt(start, mAdded.c_str());
		aEditor->Colorize(mAddedStart.mLine - 1, mAddedEnd.mLine - mAddedStart.mLine + 1);
	}

	aEditor->mState = mAfter;
	aEditor->EnsureCursorVisible();
}

static bool TokenizeCStyleString(const char* in_begin, const char* in_end, const char*& out_begin, const char*& out_end)
{
	const char* p = in_begin;

	if (*p == '"')
	{
		p++;

		while (p < in_end)
		{
			// handle end of string
			if (*p == '"')
			{
				out_begin = in_begin;
				out_end = p + 1;
				return true;
			}

			// handle escape character for "
			if (*p == '\\' && p + 1 < in_end && p[1] == '"')
				p++;

			p++;
		}
	}

	return false;
}

static bool TokenizeCStyleCharacterLiteral(const char* in_begin, const char* in_end, const char*& out_begin, const char*& out_end)
{
	const char* p = in_begin;

	if (*p == '\'')
	{
		p++;

		// handle escape characters
		if (p < in_end && *p == '\\')
			p++;

		if (p < in_end)
			p++;

		// handle end of character literal
		if (p < in_end && *p == '\'')
		{
			out_begin = in_begin;
			out_end = p + 1;
			return true;
		}
	}

	return false;
}

static bool TokenizeCStyleIdentifier(const char* in_begin, const char* in_end, const char*& out_begin, const char*& out_end)
{
	const char* p = in_begin;

	if ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || *p == '_')
	{
		p++;

		while ((p < in_end) && ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') || *p == '_'))
			p++;

		out_begin = in_begin;
		out_end = p;
		return true;
	}

	return false;
}

static bool TokenizeCStyleNumber(const char* in_begin, const char* in_end, const char*& out_begin, const char*& out_end)
{
	const char* p = in_begin;

	const bool startsWithNumber = *p >= '0' && *p <= '9';

	if (*p != '+' && *p != '-' && !startsWithNumber)
		return false;

	p++;

	bool hasNumber = startsWithNumber;

	while (p < in_end && (*p >= '0' && *p <= '9'))
	{
		hasNumber = true;

		p++;
	}

	if (hasNumber == false)
		return false;

	bool isFloat = false;
	bool isHex = false;
	bool isBinary = false;

	if (p < in_end)
	{
		if (*p == '.')
		{
			isFloat = true;

			p++;

			while (p < in_end && (*p >= '0' && *p <= '9'))
				p++;
		}
		else if (*p == 'x' || *p == 'X')
		{
			// hex formatted integer of the type 0xef80

			isHex = true;

			p++;

			while (p < in_end && ((*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'f') || (*p >= 'A' && *p <= 'F')))
				p++;
		}
		else if (*p == 'b' || *p == 'B')
		{
			// binary formatted integer of the type 0b01011101

			isBinary = true;

			p++;

			while (p < in_end && (*p >= '0' && *p <= '1'))
				p++;
		}
	}

	if (isHex == false && isBinary == false)
	{
		// floating point exponent
		if (p < in_end && (*p == 'e' || *p == 'E'))
		{
			isFloat = true;

			p++;

			if (p < in_end && (*p == '+' || *p == '-'))
				p++;

			bool hasDigits = false;

			while (p < in_end && (*p >= '0' && *p <= '9'))
			{
				hasDigits = true;

				p++;
			}

			if (hasDigits == false)
				return false;
		}

		// single precision floating point type
		if (p < in_end && *p == 'f')
			p++;
	}

	if (isFloat == false)
	{
		// integer size type
		while (p < in_end && (*p == 'u' || *p == 'U' || *p == 'l' || *p == 'L'))
			p++;
	}

	out_begin = in_begin;
	out_end = p;
	return true;
}

static bool TokenizeCStylePunctuation(const char* in_begin, const char* in_end, const char*& out_begin, const char*& out_end)
{
	(void)in_end;

	switch (*in_begin)
	{
	case '[':
	case ']':
	case '{':
	case '}':
	case '!':
	case '%':
	case '^':
	case '&':
	case '*':
	case '(':
	case ')':
	case '-':
	case '+':
	case '=':
	case '~':
	case '|':
	case '<':
	case '>':
	case '?':
	case ':':
	case '/':
	case ';':
	case ',':
	case '.':
		out_begin = in_begin;
		out_end = in_begin + 1;
		return true;
	}

	return false;
}

const TextEditor::LanguageDefinition& TextEditor::LanguageDefinition::CPlusPlus()
{
	static bool inited = false;
	static LanguageDefinition langDef;
	if (!inited)
	{
		static const char* const cppKeywords[] = {
			"alignas", "alignof", "and", "and_eq", "asm", "atomic_cancel", "atomic_commit", "atomic_noexcept", "auto", "bitand", "bitor", "bool", "break", "case", "catch", "char", "char16_t", "char32_t", "class",
			"compl", "concept", "const", "constexpr", "const_cast", "continue", "decltype", "default", "delete", "do", "double", "dynamic_cast", "else", "enum", "explicit", "export", "extern", "false", "float",
			"for", "friend", "goto", "if", "import", "inline", "int", "long", "module", "mutable", "namespace", "new", "noexcept", "not", "not_eq", "nullptr", "operator", "or", "or_eq", "private", "protected", "public",
			"register", "reinterpret_cast", "requires", "return", "short", "signed", "sizeof", "static", "static_assert", "static_cast", "struct", "switch", "synchronized", "template", "this", "thread_local",
			"throw", "true", "try", "typedef", "typeid", "typename", "union", "unsigned", "using", "virtual", "void", "volatile", "wchar_t", "while", "xor", "xor_eq"
		};
		for (auto& k : cppKeywords)
			langDef.mKeywords.insert(k);

		static const char* const identifiers[] = {
			"abort", "abs", "acos", "asin", "atan", "atexit", "atof", "atoi", "atol", "ceil", "clock", "cosh", "ctime", "div", "exit", "fabs", "floor", "fmod", "getchar", "getenv", "isalnum", "isalpha", "isdigit", "isgraph",
			"ispunct", "isspace", "isupper", "kbhit", "log10", "log2", "log", "memcmp", "modf", "pow", "printf", "sprintf", "snprintf", "putchar", "putenv", "puts", "rand", "remove", "rename", "sinh", "sqrt", "srand", "strcat", "strcmp", "strerror", "time", "tolower", "toupper",
			"std", "string", "vector", "map", "unordered_map", "set", "unordered_set", "min", "max"
		};
		for (auto& k : identifiers)
		{
			Identifier id;
			id.mDeclaration = "Built-in function";
			langDef.mIdentifiers.insert(std::make_pair(std::string(k), id));
		}

		langDef.mTokenize = [](const char* in_begin, const char* in_end, const char*& out_begin, const char*& out_end, PaletteIndex& paletteIndex) -> bool
			{
				paletteIndex = PaletteIndex::Max;

				while (in_begin < in_end && isascii(*in_begin) && isblank(*in_begin))
					in_begin++;

				if (in_begin == in_end)
				{
					out_begin = in_end;
					out_end = in_end;
					paletteIndex = PaletteIndex::Default;
				}
				else if (TokenizeCStyleString(in_begin, in_end, out_begin, out_end))
					paletteIndex = PaletteIndex::String;
				else if (TokenizeCStyleCharacterLiteral(in_begin, in_end, out_begin, out_end))
					paletteIndex = PaletteIndex::CharLiteral;
				else if (TokenizeCStyleIdentifier(in_begin, in_end, out_begin, out_end))
					paletteIndex = PaletteIndex::Identifier;
				else if (TokenizeCStyleNumber(in_begin, in_end, out_begin, out_end))
					paletteIndex = PaletteIndex::Number;
				else if (TokenizeCStylePunctuation(in_begin, in_end, out_begin, out_end))
					paletteIndex = PaletteIndex::Punctuation;

				return paletteIndex != PaletteIndex::Max;
			};

		langDef.mCommentStart = "/*";
		langDef.mCommentEnd = "*/";
		langDef.mSingleLineComment = "//";

		langDef.mCaseSensitive = true;
		langDef.mAutoIndentation = true;

		langDef.mName = "C++";

		inited = true;
	}
	return langDef;
}

const TextEditor::LanguageDefinition& TextEditor::LanguageDefinition::HLSL()
{
	static bool inited = false;
	static LanguageDefinition langDef;
	if (!inited)
	{
		static const char* const keywords[] = {
			"AppendStructuredBuffer", "asm", "asm_fragment", "BlendState", "bool", "break", "Buffer", "ByteAddressBuffer", "case", "cbuffer", "centroid", "class", "column_major", "compile", "compile_fragment",
			"CompileShader", "const", "continue", "ComputeShader", "ConsumeStructuredBuffer", "default", "DepthStencilState", "DepthStencilView", "discard", "do", "double", "DomainShader", "dword", "else",
			"export", "extern", "false", "float", "for", "fxgroup", "GeometryShader", "groupshared", "half", "Hullshader", "if", "in", "inline", "inout", "InputPatch", "int", "interface", "line", "lineadj",
			"linear", "LineStream", "matrix", "min16float", "min10float", "min16int", "min12int", "min16uint", "namespace", "nointerpolation", "noperspective", "NULL", "out", "OutputPatch", "packoffset",
			"pass", "pixelfragment", "PixelShader", "point", "PointStream", "precise", "RasterizerState", "RenderTargetView", "return", "register", "row_major", "RWBuffer", "RWByteAddressBuffer", "RWStructuredBuffer",
			"RWTexture1D", "RWTexture1DArray", "RWTexture2D", "RWTexture2DArray", "RWTexture3D", "sample", "sampler", "SamplerState", "SamplerComparisonState", "shared", "snorm", "stateblock", "stateblock_state",
			"static", "string", "struct", "switch", "StructuredBuffer", "tbuffer", "technique", "technique10", "technique11", "texture", "Texture1D", "Texture1DArray", "Texture2D", "Texture2DArray", "Texture2DMS",
			"Texture2DMSArray", "Texture3D", "TextureCube", "TextureCubeArray", "true", "typedef", "triangle", "triangleadj", "TriangleStream", "uint", "uniform", "unorm", "unsigned", "vector", "vertexfragment",
			"VertexShader", "void", "volatile", "while",
			"bool1","bool2","bool3","bool4","double1","double2","double3","double4", "float1", "float2", "float3", "float4", "int1", "int2", "int3", "int4", "in", "out", "inout",
			"uint1", "uint2", "uint3", "uint4", "dword1", "dword2", "dword3", "dword4", "half1", "half2", "half3", "half4",
			"float1x1","float2x1","float3x1","float4x1","float1x2","float2x2","float3x2","float4x2",
			"float1x3","float2x3","float3x3","float4x3","float1x4","float2x4","float3x4","float4x4",
			"half1x1","half2x1","half3x1","half4x1","half1x2","half2x2","half3x2","half4x2",
			"half1x3","half2x3","half3x3","half4x3","half1x4","half2x4","half3x4","half4x4",
		};
		for (auto& k : keywords)
			langDef.mKeywords.insert(k);

		static const char* const identifiers[] = {
			"abort", "abs", "acos", "all", "AllMemoryBarrier", "AllMemoryBarrierWithGroupSync", "any", "asdouble", "asfloat", "asin", "asint", "asint", "asuint",
			"asuint", "atan", "atan2", "ceil", "CheckAccessFullyMapped", "clamp", "clip", "cos", "cosh", "countbits", "cross", "D3DCOLORtoUBYTE4", "ddx",
			"ddx_coarse", "ddx_fine", "ddy", "ddy_coarse", "ddy_fine", "degrees", "determinant", "DeviceMemoryBarrier", "DeviceMemoryBarrierWithGroupSync",
			"distance", "dot", "dst", "errorf", "EvaluateAttributeAtCentroid", "EvaluateAttributeAtSample", "EvaluateAttributeSnapped", "exp", "exp2",
			"f16tof32", "f32tof16", "faceforward", "firstbithigh", "firstbitlow", "floor", "fma", "fmod", "frac", "frexp", "fwidth", "GetRenderTargetSampleCount",
			"GetRenderTargetSamplePosition", "GroupMemoryBarrier", "GroupMemoryBarrierWithGroupSync", "InterlockedAdd", "InterlockedAnd", "InterlockedCompareExchange",
			"InterlockedCompareStore", "InterlockedExchange", "InterlockedMax", "InterlockedMin", "InterlockedOr", "InterlockedXor", "isfinite", "isinf", "isnan",
			"ldexp", "length", "lerp", "lit", "log", "log10", "log2", "mad", "max", "min", "modf", "msad4", "mul", "noise", "normalize", "pow", "printf",
			"Process2DQuadTessFactorsAvg", "Process2DQuadTessFactorsMax", "Process2DQuadTessFactorsMin", "ProcessIsolineTessFactors", "ProcessQuadTessFactorsAvg",
			"ProcessQuadTessFactorsMax", "ProcessQuadTessFactorsMin", "ProcessTriTessFactorsAvg", "ProcessTriTessFactorsMax", "ProcessTriTessFactorsMin",
			"radians", "rcp", "reflect", "refract", "reversebits", "round", "rsqrt", "saturate", "sign", "sin", "sincos", "sinh", "smoothstep", "sqrt", "step",
			"tan", "tanh", "tex1D", "tex1D", "tex1Dbias", "tex1Dgrad", "tex1Dlod", "tex1Dproj", "tex2D", "tex2D", "tex2Dbias", "tex2Dgrad", "tex2Dlod", "tex2Dproj",
			"tex3D", "tex3D", "tex3Dbias", "tex3Dgrad", "tex3Dlod", "tex3Dproj", "texCUBE", "texCUBE", "texCUBEbias", "texCUBEgrad", "texCUBElod", "texCUBEproj", "transpose", "trunc"
		};
		for (auto& k : identifiers)
		{
			Identifier id;
			id.mDeclaration = "Built-in function";
			langDef.mIdentifiers.insert(std::make_pair(std::string(k), id));
		}

		langDef.mTokenRegexStrings.push_back(std::make_pair<std::string, PaletteIndex>("[ \\t]*#[ \\t]*[a-zA-Z_]+", PaletteIndex::Preprocessor));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::string, PaletteIndex>("L?\\\"(\\\\.|[^\\\"])*\\\"", PaletteIndex::String));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::string, PaletteIndex>("\\'\\\\?[^\\']\\'", PaletteIndex::CharLiteral));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::string, PaletteIndex>("[+-]?([0-9]+([.][0-9]*)?|[.][0-9]+)([eE][+-]?[0-9]+)?[fF]?", PaletteIndex::Number));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::string, PaletteIndex>("[+-]?[0-9]+[Uu]?[lL]?[lL]?", PaletteIndex::Number));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::string, PaletteIndex>("0[0-7]+[Uu]?[lL]?[lL]?", PaletteIndex::Number));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::string, PaletteIndex>("0[xX][0-9a-fA-F]+[uU]?[lL]?[lL]?", PaletteIndex::Number));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::string, PaletteIndex>("[a-zA-Z_][a-zA-Z0-9_]*", PaletteIndex::Identifier));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::string, PaletteIndex>("[\\[\\]\\{\\}\\!\\%\\^\\&\\*\\(\\)\\-\\+\\=\\~\\|\\<\\>\\?\\/\\;\\,\\.]", PaletteIndex::Punctuation));

		langDef.mCommentStart = "/*";
		langDef.mCommentEnd = "*/";
		langDef.mSingleLineComment = "//";

		langDef.mCaseSensitive = true;
		langDef.mAutoIndentation = true;

		langDef.mName = "HLSL";

		inited = true;
	}
	return langDef;
}

const TextEditor::LanguageDefinition& TextEditor::LanguageDefinition::GLSL()
{
	static bool inited = false;
	static LanguageDefinition langDef;
	if (!inited)
	{
		static const char* const keywords[] = {
			"auto", "break", "case", "char", "const", "continue", "default", "do", "double", "else", "enum", "extern", "float", "for", "goto", "if", "inline", "int", "long", "register", "restrict", "return", "short",
			"signed", "sizeof", "static", "struct", "switch", "typedef", "union", "unsigned", "void", "volatile", "while", "_Alignas", "_Alignof", "_Atomic", "_Bool", "_Complex", "_Generic", "_Imaginary",
			"_Noreturn", "_Static_assert", "_Thread_local"
		};
		for (auto& k : keywords)
			langDef.mKeywords.insert(k);

		static const char* const identifiers[] = {
			"abort", "abs", "acos", "asin", "atan", "atexit", "atof", "atoi", "atol", "ceil", "clock", "cosh", "ctime", "div", "exit", "fabs", "floor", "fmod", "getchar", "getenv", "isalnum", "isalpha", "isdigit", "isgraph",
			"ispunct", "isspace", "isupper", "kbhit", "log10", "log2", "log", "memcmp", "modf", "pow", "putchar", "putenv", "puts", "rand", "remove", "rename", "sinh", "sqrt", "srand", "strcat", "strcmp", "strerror", "time", "tolower", "toupper"
		};
		for (auto& k : identifiers)
		{
			Identifier id;
			id.mDeclaration = "Built-in function";
			langDef.mIdentifiers.insert(std::make_pair(std::string(k), id));
		}

		langDef.mTokenRegexStrings.push_back(std::make_pair<std::string, PaletteIndex>("[ \\t]*#[ \\t]*[a-zA-Z_]+", PaletteIndex::Preprocessor));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::string, PaletteIndex>("L?\\\"(\\\\.|[^\\\"])*\\\"", PaletteIndex::String));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::string, PaletteIndex>("\\'\\\\?[^\\']\\'", PaletteIndex::CharLiteral));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::string, PaletteIndex>("[+-]?([0-9]+([.][0-9]*)?|[.][0-9]+)([eE][+-]?[0-9]+)?[fF]?", PaletteIndex::Number));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::string, PaletteIndex>("[+-]?[0-9]+[Uu]?[lL]?[lL]?", PaletteIndex::Number));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::string, PaletteIndex>("0[0-7]+[Uu]?[lL]?[lL]?", PaletteIndex::Number));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::string, PaletteIndex>("0[xX][0-9a-fA-F]+[uU]?[lL]?[lL]?", PaletteIndex::Number));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::string, PaletteIndex>("[a-zA-Z_][a-zA-Z0-9_]*", PaletteIndex::Identifier));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::string, PaletteIndex>("[\\[\\]\\{\\}\\!\\%\\^\\&\\*\\(\\)\\-\\+\\=\\~\\|\\<\\>\\?\\/\\;\\,\\.]", PaletteIndex::Punctuation));

		langDef.mCommentStart = "/*";
		langDef.mCommentEnd = "*/";
		langDef.mSingleLineComment = "//";

		langDef.mCaseSensitive = true;
		langDef.mAutoIndentation = true;

		langDef.mName = "GLSL";

		inited = true;
	}
	return langDef;
}

const TextEditor::LanguageDefinition& TextEditor::LanguageDefinition::C()
{
	static bool inited = false;
	static LanguageDefinition langDef;
	if (!inited)
	{
		static const char* const keywords[] = {
			"auto", "break", "case", "char", "const", "continue", "default", "do", "double", "else", "enum", "extern", "float", "for", "goto", "if", "inline", "int", "long", "register", "restrict", "return", "short",
			"signed", "sizeof", "static", "struct", "switch", "typedef", "union", "unsigned", "void", "volatile", "while", "_Alignas", "_Alignof", "_Atomic", "_Bool", "_Complex", "_Generic", "_Imaginary",
			"_Noreturn", "_Static_assert", "_Thread_local"
		};
		for (auto& k : keywords)
			langDef.mKeywords.insert(k);

		static const char* const identifiers[] = {
			"abort", "abs", "acos", "asin", "atan", "atexit", "atof", "atoi", "atol", "ceil", "clock", "cosh", "ctime", "div", "exit", "fabs", "floor", "fmod", "getchar", "getenv", "isalnum", "isalpha", "isdigit", "isgraph",
			"ispunct", "isspace", "isupper", "kbhit", "log10", "log2", "log", "memcmp", "modf", "pow", "putchar", "putenv", "puts", "rand", "remove", "rename", "sinh", "sqrt", "srand", "strcat", "strcmp", "strerror", "time", "tolower", "toupper"
		};
		for (auto& k : identifiers)
		{
			Identifier id;
			id.mDeclaration = "Built-in function";
			langDef.mIdentifiers.insert(std::make_pair(std::string(k), id));
		}

		langDef.mTokenize = [](const char* in_begin, const char* in_end, const char*& out_begin, const char*& out_end, PaletteIndex& paletteIndex) -> bool
			{
				paletteIndex = PaletteIndex::Max;

				while (in_begin < in_end && isascii(*in_begin) && isblank(*in_begin))
					in_begin++;

				if (in_begin == in_end)
				{
					out_begin = in_end;
					out_end = in_end;
					paletteIndex = PaletteIndex::Default;
				}
				else if (TokenizeCStyleString(in_begin, in_end, out_begin, out_end))
					paletteIndex = PaletteIndex::String;
				else if (TokenizeCStyleCharacterLiteral(in_begin, in_end, out_begin, out_end))
					paletteIndex = PaletteIndex::CharLiteral;
				else if (TokenizeCStyleIdentifier(in_begin, in_end, out_begin, out_end))
					paletteIndex = PaletteIndex::Identifier;
				else if (TokenizeCStyleNumber(in_begin, in_end, out_begin, out_end))
					paletteIndex = PaletteIndex::Number;
				else if (TokenizeCStylePunctuation(in_begin, in_end, out_begin, out_end))
					paletteIndex = PaletteIndex::Punctuation;

				return paletteIndex != PaletteIndex::Max;
			};

		langDef.mCommentStart = "/*";
		langDef.mCommentEnd = "*/";
		langDef.mSingleLineComment = "//";

		langDef.mCaseSensitive = true;
		langDef.mAutoIndentation = true;

		langDef.mName = "C";

		inited = true;
	}
	return langDef;
}

const TextEditor::LanguageDefinition& TextEditor::LanguageDefinition::SQL()
{
	static bool inited = false;
	static LanguageDefinition langDef;
	if (!inited)
	{
		static const char* const keywords[] = {
			"ADD", "EXCEPT", "PERCENT", "ALL", "EXEC", "PLAN", "ALTER", "EXECUTE", "PRECISION", "AND", "EXISTS", "PRIMARY", "ANY", "EXIT", "PRINT", "AS", "FETCH", "PROC", "ASC", "FILE", "PROCEDURE",
			"AUTHORIZATION", "FILLFACTOR", "PUBLIC", "BACKUP", "FOR", "RAISERROR", "BEGIN", "FOREIGN", "READ", "BETWEEN", "FREETEXT", "READTEXT", "BREAK", "FREETEXTTABLE", "RECONFIGURE",
			"BROWSE", "FROM", "REFERENCES", "BULK", "FULL", "REPLICATION", "BY", "FUNCTION", "RESTORE", "CASCADE", "GOTO", "RESTRICT", "CASE", "GRANT", "RETURN", "CHECK", "GROUP", "REVOKE",
			"CHECKPOINT", "HAVING", "RIGHT", "CLOSE", "HOLDLOCK", "ROLLBACK", "CLUSTERED", "IDENTITY", "ROWCOUNT", "COALESCE", "IDENTITY_INSERT", "ROWGUIDCOL", "COLLATE", "IDENTITYCOL", "RULE",
			"COLUMN", "IF", "SAVE", "COMMIT", "IN", "SCHEMA", "COMPUTE", "INDEX", "SELECT", "CONSTRAINT", "INNER", "SESSION_USER", "CONTAINS", "INSERT", "SET", "CONTAINSTABLE", "INTERSECT", "SETUSER",
			"CONTINUE", "INTO", "SHUTDOWN", "CONVERT", "IS", "SOME", "CREATE", "JOIN", "STATISTICS", "CROSS", "KEY", "SYSTEM_USER", "CURRENT", "KILL", "TABLE", "CURRENT_DATE", "LEFT", "TEXTSIZE",
			"CURRENT_TIME", "LIKE", "THEN", "CURRENT_TIMESTAMP", "LINENO", "TO", "CURRENT_USER", "LOAD", "TOP", "CURSOR", "NATIONAL", "TRAN", "DATABASE", "NOCHECK", "TRANSACTION",
			"DBCC", "NONCLUSTERED", "TRIGGER", "DEALLOCATE", "NOT", "TRUNCATE", "DECLARE", "NULL", "TSEQUAL", "DEFAULT", "NULLIF", "UNION", "DELETE", "OF", "UNIQUE", "DENY", "OFF", "UPDATE",
			"DESC", "OFFSETS", "UPDATETEXT", "DISK", "ON", "USE", "DISTINCT", "OPEN", "USER", "DISTRIBUTED", "OPENDATASOURCE", "VALUES", "DOUBLE", "OPENQUERY", "VARYING","DROP", "OPENROWSET", "VIEW",
			"DUMMY", "OPENXML", "WAITFOR", "DUMP", "OPTION", "WHEN", "ELSE", "OR", "WHERE", "END", "ORDER", "WHILE", "ERRLVL", "OUTER", "WITH", "ESCAPE", "OVER", "WRITETEXT"
		};

		for (auto& k : keywords)
			langDef.mKeywords.insert(k);

		static const char* const identifiers[] = {
			"ABS",  "ACOS",  "ADD_MONTHS",  "ASCII",  "ASCIISTR",  "ASIN",  "ATAN",  "ATAN2",  "AVG",  "BFILENAME",  "BIN_TO_NUM",  "BITAND",  "CARDINALITY",  "CASE",  "CAST",  "CEIL",
			"CHARTOROWID",  "CHR",  "COALESCE",  "COMPOSE",  "CONCAT",  "CONVERT",  "CORR",  "COS",  "COSH",  "COUNT",  "COVAR_POP",  "COVAR_SAMP",  "CUME_DIST",  "CURRENT_DATE",
			"CURRENT_TIMESTAMP",  "DBTIMEZONE",  "DECODE",  "DECOMPOSE",  "DENSE_RANK",  "DUMP",  "EMPTY_BLOB",  "EMPTY_CLOB",  "EXP",  "EXTRACT",  "FIRST_VALUE",  "FLOOR",  "FROM_TZ",  "GREATEST",
			"GROUP_ID",  "HEXTORAW",  "INITCAP",  "INSTR",  "INSTR2",  "INSTR4",  "INSTRB",  "INSTRC",  "LAG",  "LAST_DAY",  "LAST_VALUE",  "LEAD",  "LEAST",  "LENGTH",  "LENGTH2",  "LENGTH4",
			"LENGTHB",  "LENGTHC",  "LISTAGG",  "LN",  "LNNVL",  "LOCALTIMESTAMP",  "LOG",  "LOWER",  "LPAD",  "LTRIM",  "MAX",  "MEDIAN",  "MIN",  "MOD",  "MONTHS_BETWEEN",  "NANVL",  "NCHR",
			"NEW_TIME",  "NEXT_DAY",  "NTH_VALUE",  "NULLIF",  "NUMTODSINTERVAL",  "NUMTOYMINTERVAL",  "NVL",  "NVL2",  "POWER",  "RANK",  "RAWTOHEX",  "REGEXP_COUNT",  "REGEXP_INSTR",
			"REGEXP_REPLACE",  "REGEXP_SUBSTR",  "REMAINDER",  "REPLACE",  "ROUND",  "ROWNUM",  "RPAD",  "RTRIM",  "SESSIONTIMEZONE",  "SIGN",  "SIN",  "SINH",
			"SOUNDEX",  "SQRT",  "STDDEV",  "SUBSTR",  "SUM",  "SYS_CONTEXT",  "SYSDATE",  "SYSTIMESTAMP",  "TAN",  "TANH",  "TO_CHAR",  "TO_CLOB",  "TO_DATE",  "TO_DSINTERVAL",  "TO_LOB",
			"TO_MULTI_BYTE",  "TO_NCLOB",  "TO_NUMBER",  "TO_SINGLE_BYTE",  "TO_TIMESTAMP",  "TO_TIMESTAMP_TZ",  "TO_YMINTERVAL",  "TRANSLATE",  "TRIM",  "TRUNC", "TZ_OFFSET",  "UID",  "UPPER",
			"USER",  "USERENV",  "VAR_POP",  "VAR_SAMP",  "VARIANCE",  "VSIZE "
		};
		for (auto& k : identifiers)
		{
			Identifier id;
			id.mDeclaration = "Built-in function";
			langDef.mIdentifiers.insert(std::make_pair(std::string(k), id));
		}

		langDef.mTokenRegexStrings.push_back(std::make_pair<std::string, PaletteIndex>("L?\\\"(\\\\.|[^\\\"])*\\\"", PaletteIndex::String));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::string, PaletteIndex>("\\\'[^\\\']*\\\'", PaletteIndex::String));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::string, PaletteIndex>("[+-]?([0-9]+([.][0-9]*)?|[.][0-9]+)([eE][+-]?[0-9]+)?[fF]?", PaletteIndex::Number));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::string, PaletteIndex>("[+-]?[0-9]+[Uu]?[lL]?[lL]?", PaletteIndex::Number));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::string, PaletteIndex>("0[0-7]+[Uu]?[lL]?[lL]?", PaletteIndex::Number));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::string, PaletteIndex>("0[xX][0-9a-fA-F]+[uU]?[lL]?[lL]?", PaletteIndex::Number));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::string, PaletteIndex>("[a-zA-Z_][a-zA-Z0-9_]*", PaletteIndex::Identifier));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::string, PaletteIndex>("[\\[\\]\\{\\}\\!\\%\\^\\&\\*\\(\\)\\-\\+\\=\\~\\|\\<\\>\\?\\/\\;\\,\\.]", PaletteIndex::Punctuation));

		langDef.mCommentStart = "/*";
		langDef.mCommentEnd = "*/";
		langDef.mSingleLineComment = "//";

		langDef.mCaseSensitive = false;
		langDef.mAutoIndentation = false;

		langDef.mName = "SQL";

		inited = true;
	}
	return langDef;
}

const TextEditor::LanguageDefinition& TextEditor::LanguageDefinition::AngelScript()
{
	static bool inited = false;
	static LanguageDefinition langDef;
	if (!inited)
	{
		static const char* const keywords[] = {
			"and", "abstract", "auto", "bool", "break", "case", "cast", "class", "const", "continue", "default", "do", "double", "else", "enum", "false", "final", "float", "for",
			"from", "funcdef", "function", "get", "if", "import", "in", "inout", "int", "interface", "int8", "int16", "int32", "int64", "is", "mixin", "namespace", "not",
			"null", "or", "out", "override", "private", "protected", "return", "set", "shared", "super", "switch", "this ", "true", "typedef", "uint", "uint8", "uint16", "uint32",
			"uint64", "void", "while", "xor"
		};

		for (auto& k : keywords)
			langDef.mKeywords.insert(k);

		static const char* const identifiers[] = {
			"cos", "sin", "tab", "acos", "asin", "atan", "atan2", "cosh", "sinh", "tanh", "log", "log10", "pow", "sqrt", "abs", "ceil", "floor", "fraction", "closeTo", "fpFromIEEE", "fpToIEEE",
			"complex", "opEquals", "opAddAssign", "opSubAssign", "opMulAssign", "opDivAssign", "opAdd", "opSub", "opMul", "opDiv"
		};
		for (auto& k : identifiers)
		{
			Identifier id;
			id.mDeclaration = "Built-in function";
			langDef.mIdentifiers.insert(std::make_pair(std::string(k), id));
		}

		langDef.mTokenRegexStrings.push_back(std::make_pair<std::string, PaletteIndex>("L?\\\"(\\\\.|[^\\\"])*\\\"", PaletteIndex::String));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::string, PaletteIndex>("\\'\\\\?[^\\']\\'", PaletteIndex::String));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::string, PaletteIndex>("[+-]?([0-9]+([.][0-9]*)?|[.][0-9]+)([eE][+-]?[0-9]+)?[fF]?", PaletteIndex::Number));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::string, PaletteIndex>("[+-]?[0-9]+[Uu]?[lL]?[lL]?", PaletteIndex::Number));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::string, PaletteIndex>("0[0-7]+[Uu]?[lL]?[lL]?", PaletteIndex::Number));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::string, PaletteIndex>("0[xX][0-9a-fA-F]+[uU]?[lL]?[lL]?", PaletteIndex::Number));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::string, PaletteIndex>("[a-zA-Z_][a-zA-Z0-9_]*", PaletteIndex::Identifier));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::string, PaletteIndex>("[\\[\\]\\{\\}\\!\\%\\^\\&\\*\\(\\)\\-\\+\\=\\~\\|\\<\\>\\?\\/\\;\\,\\.]", PaletteIndex::Punctuation));

		langDef.mCommentStart = "/*";
		langDef.mCommentEnd = "*/";
		langDef.mSingleLineComment = "//";

		langDef.mCaseSensitive = true;
		langDef.mAutoIndentation = true;

		langDef.mName = "AngelScript";

		inited = true;
	}
	return langDef;
}

const TextEditor::LanguageDefinition& TextEditor::LanguageDefinition::Lua()
{
	static bool inited = false;
	static LanguageDefinition langDef;
	if (!inited)
	{
		static const char* const keywords[] = {
			"and", "break", "do", "", "else", "elseif", "end", "false", "for", "function", "if", "in", "", "local", "nil", "not", "or", "repeat", "return", "then", "true", "until", "while"
		};

		for (auto& k : keywords)
			langDef.mKeywords.insert(k);

		static const char* const identifiers[] = {
			"assert", "collectgarbage", "dofile", "error", "getmetatable", "ipairs", "loadfile", "load", "loadstring",  "next",  "pairs",  "pcall",  "print",  "rawequal",  "rawlen",  "rawget",  "rawset",
			"select",  "setmetatable",  "tonumber",  "tostring",  "type",  "xpcall",  "_G",  "_VERSION","arshift", "band", "bnot", "bor", "bxor", "btest", "extract", "lrotate", "lshift", "replace",
			"rrotate", "rshift", "create", "resume", "running", "status", "wrap", "yield", "isyieldable", "debug","getuservalue", "gethook", "getinfo", "getlocal", "getregistry", "getmetatable",
			"getupvalue", "upvaluejoin", "upvalueid", "setuservalue", "sethook", "setlocal", "setmetatable", "setupvalue", "traceback", "close", "flush", "input", "lines", "open", "output", "popen",
			"read", "tmpfile", "type", "write", "close", "flush", "lines", "read", "seek", "setvbuf", "write", "__gc", "__tostring", "abs", "acos", "asin", "atan", "ceil", "cos", "deg", "exp", "tointeger",
			"floor", "fmod", "ult", "log", "max", "min", "modf", "rad", "random", "randomseed", "sin", "sqrt", "string", "tan", "type", "atan2", "cosh", "sinh", "tanh",
			"pow", "frexp", "ldexp", "log10", "pi", "huge", "maxinteger", "mininteger", "loadlib", "searchpath", "seeall", "preload", "cpath", "path", "searchers", "loaded", "module", "require", "clock",
			"date", "difftime", "execute", "exit", "getenv", "remove", "rename", "setlocale", "time", "tmpname", "byte", "char", "dump", "find", "format", "gmatch", "gsub", "len", "lower", "match", "rep",
			"reverse", "sub", "upper", "pack", "packsize", "unpack", "concat", "maxn", "insert", "pack", "unpack", "remove", "move", "sort", "offset", "codepoint", "char", "len", "codes", "charpattern",
			"coroutine", "table", "io", "os", "string", "utf8", "bit32", "math", "debug", "package"
		};
		for (auto& k : identifiers)
		{
			Identifier id;
			id.mDeclaration = "Built-in function";
			langDef.mIdentifiers.insert(std::make_pair(std::string(k), id));
		}

		langDef.mTokenRegexStrings.push_back(std::make_pair<std::string, PaletteIndex>("L?\\\"(\\\\.|[^\\\"])*\\\"", PaletteIndex::String));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::string, PaletteIndex>("\\\'[^\\\']*\\\'", PaletteIndex::String));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::string, PaletteIndex>("0[xX][0-9a-fA-F]+[uU]?[lL]?[lL]?", PaletteIndex::Number));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::string, PaletteIndex>("[+-]?([0-9]+([.][0-9]*)?|[.][0-9]+)([eE][+-]?[0-9]+)?[fF]?", PaletteIndex::Number));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::string, PaletteIndex>("[+-]?[0-9]+[Uu]?[lL]?[lL]?", PaletteIndex::Number));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::string, PaletteIndex>("[a-zA-Z_][a-zA-Z0-9_]*", PaletteIndex::Identifier));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::string, PaletteIndex>("[\\[\\]\\{\\}\\!\\%\\^\\&\\*\\(\\)\\-\\+\\=\\~\\|\\<\\>\\?\\/\\;\\,\\.]", PaletteIndex::Punctuation));

		langDef.mCommentStart = "--[[";
		langDef.mCommentEnd = "]]";
		langDef.mSingleLineComment = "--";

		langDef.mCaseSensitive = true;
		langDef.mAutoIndentation = false;

		langDef.mName = "Lua";

		inited = true;
	}
	return langDef;
}

const TextEditor::LanguageDefinition& TextEditor::LanguageDefinition::GML()
{
	static bool inited = false;
	static LanguageDefinition langDef;
	if (!inited)
	{
		// Keywords

		static const char* const keywords[] = {
			"if", "else", "switch", "case", "default", "break", "continue", "return",
			"for", "while", "do", "repeat", "with", "until", "exit",

			"function", "constructor", "static", "enum",
			"var", "globalvar", "new", "delete",

			"try", "catch", "throw", "finally",

			"begin", "end", "then", "and", "or", "xor", "not", "div", "mod",

			"macro", "region", "endregion"
		};
		for (auto& k : keywords)
			langDef.mKeywords.insert(k);

		// Built-in functions

		static const char* const identifiers[] = {
			// Arrays
			"array_length", "array_create", "array_copy", "array_equals", "array_sort",
			"array_push", "array_pop", "array_insert", "array_delete", "array_resize",
			"array_get", "array_set", "array_get_index", "array_first", "array_last",
			"array_contains", "array_find_index", "array_reverse", "array_shuffle",
			"array_concat", "array_union", "array_intersection", "array_unique",
			"array_map", "array_reduce", "array_filter", "array_foreach",

			// Strings
			"string", "string_format", "string_length", "string_byte_length", "string_pos",
			"string_pos_ext", "string_last_pos", "string_last_pos_ext",
			"string_copy", "string_char_at", "string_ord_at", "string_byte_at",
			"string_set_byte_at", "string_delete", "string_insert",
			"string_lower", "string_upper", "string_letters", "string_digits",
			"string_lettersdigits", "string_replace", "string_replace_all",
			"string_count", "string_hash_to_newline", "string_trim", "string_trim_start",
			"string_trim_end", "string_repeat", "string_split", "string_join",
			"string_starts_with", "string_ends_with", "string_ext",
			"string_width", "string_width_ext", "string_height", "string_height_ext",
			"ansi_char", "chr", "ord", "string_decimal",

			// Maths
			"abs", "sign", "round", "floor", "ceil", "frac", "sqrt", "sqr",
			"exp", "ln", "log2", "log10", "power", "logn",
			"min", "max", "mean", "median", "clamp", "lerp", "smoothstep",
			"sin", "cos", "tan", "arcsin", "arccos", "arctan", "arctan2",
			"dsin", "dcos", "dtan", "darcsin", "darccos", "darctan", "darctan2",
			"degtorad", "radtodeg",
			"point_distance", "point_distance_3d", "point_direction",
			"lengthdir_x", "lengthdir_y",
			"dot_product", "dot_product_3d", "dot_product_normalised", "dot_product_normalised_3d",
			"angle_difference", "math_set_epsilon", "math_get_epsilon",

			// Random
			"random", "random_range", "irandom", "irandom_range",
			"random_set_seed", "random_get_seed", "randomize", "randomise",
			"choose",

			// DS Lists
			"ds_list_create", "ds_list_destroy", "ds_list_clear", "ds_list_empty",
			"ds_list_size", "ds_list_add", "ds_list_insert", "ds_list_replace",
			"ds_list_delete", "ds_list_find_index", "ds_list_find_value",
			"ds_list_sort", "ds_list_shuffle", "ds_list_read", "ds_list_write",
			"ds_list_mark_as_list", "ds_list_mark_as_map", "ds_list_is_list", "ds_list_is_map",
			"ds_list_copy", "ds_list_set",

			// DS Maps
			"ds_map_create", "ds_map_destroy", "ds_map_clear", "ds_map_empty",
			"ds_map_size", "ds_map_add", "ds_map_replace", "ds_map_delete",
			"ds_map_exists", "ds_map_find_value", "ds_map_find_previous",
			"ds_map_find_next", "ds_map_find_first", "ds_map_find_last",
			"ds_map_read", "ds_map_write", "ds_map_copy", "ds_map_set",
			"ds_map_add_list", "ds_map_add_map", "ds_map_replace_list", "ds_map_replace_map",
			"ds_map_is_list", "ds_map_is_map", "ds_map_secure_save", "ds_map_secure_load",
			"ds_map_secure_save_buffer", "ds_map_secure_load_buffer",

			// DS Grids
			"ds_grid_create", "ds_grid_destroy", "ds_grid_clear", "ds_grid_copy",
			"ds_grid_resize", "ds_grid_width", "ds_grid_height",
			"ds_grid_get", "ds_grid_set", "ds_grid_add", "ds_grid_multiply",
			"ds_grid_set_region", "ds_grid_add_region", "ds_grid_multiply_region",
			"ds_grid_set_disk", "ds_grid_add_disk", "ds_grid_multiply_disk",
			"ds_grid_set_grid_region", "ds_grid_add_grid_region", "ds_grid_multiply_grid_region",
			"ds_grid_get_sum", "ds_grid_get_max", "ds_grid_get_min", "ds_grid_get_mean",
			"ds_grid_get_disk_sum", "ds_grid_get_disk_min", "ds_grid_get_disk_max", "ds_grid_get_disk_mean",
			"ds_grid_value_exists", "ds_grid_value_x", "ds_grid_value_y",
			"ds_grid_value_disk_exists", "ds_grid_value_disk_x", "ds_grid_value_disk_y",
			"ds_grid_shuffle", "ds_grid_write", "ds_grid_read", "ds_grid_sort",

			// DS Stacks
			"ds_stack_create", "ds_stack_destroy", "ds_stack_clear", "ds_stack_empty",
			"ds_stack_size", "ds_stack_push", "ds_stack_pop", "ds_stack_top",
			"ds_stack_write", "ds_stack_read", "ds_stack_copy",

			// DS Queues
			"ds_queue_create", "ds_queue_destroy", "ds_queue_clear", "ds_queue_empty",
			"ds_queue_size", "ds_queue_enqueue", "ds_queue_dequeue", "ds_queue_head", "ds_queue_tail",
			"ds_queue_write", "ds_queue_read", "ds_queue_copy",

			// DS Priority Queues
			"ds_priority_create", "ds_priority_destroy", "ds_priority_clear", "ds_priority_empty",
			"ds_priority_size", "ds_priority_add", "ds_priority_change_priority",
			"ds_priority_find_priority", "ds_priority_delete_value", "ds_priority_delete_min",
			"ds_priority_find_min", "ds_priority_delete_max", "ds_priority_find_max",
			"ds_priority_write", "ds_priority_read", "ds_priority_copy",

			// Instances
			"instance_create_layer", "instance_create_depth", "instance_destroy",
			"instance_exists", "instance_find", "instance_number", "instance_position",
			"instance_nearest", "instance_furthest", "instance_place",
			"instance_activate_all", "instance_deactivate_all", "instance_activate_object",
			"instance_deactivate_object", "instance_activate_region", "instance_deactivate_region",
			"instance_change", "instance_copy", "instance_id_get",

			// Objects
			"object_exists", "object_get_name", "object_get_depth", "object_get_parent",
			"object_get_mask", "object_get_persistent", "object_get_physics",
			"object_get_solid", "object_get_sprite", "object_get_visible",
			"object_is_ancestor", "object_set_depth", "object_set_mask",
			"object_set_persistent", "object_set_solid", "object_set_sprite", "object_set_visible",
			"object_add", "object_delete",

			// Sprites
			"sprite_exists", "sprite_get_name", "sprite_get_number", "sprite_get_width", "sprite_get_height",
			"sprite_get_xoffset", "sprite_get_yoffset", "sprite_get_bbox_left", "sprite_get_bbox_right",
			"sprite_get_bbox_top", "sprite_get_bbox_bottom", "sprite_get_bbox_mode",
			"sprite_get_texture", "sprite_get_uvs", "sprite_set_offset", "sprite_set_cache_size",
			"sprite_set_cache_size_ext", "sprite_get_tpe", "sprite_prefetch", "sprite_prefetch_multi",
			"sprite_flush", "sprite_flush_multi", "sprite_set_speed",
			"sprite_add", "sprite_replace", "sprite_duplicate", "sprite_assign", "sprite_merge",
			"sprite_create_from_surface", "sprite_add_from_surface", "sprite_delete",
			"sprite_set_alpha_from_sprite", "sprite_collision_mask",
			"sprite_nineslice_create", "sprite_get_nineslice",

			// Fonts
			"font_add", "font_add_sprite", "font_add_sprite_ext", "font_replace",
			"font_replace_sprite", "font_replace_sprite_ext", "font_delete",
			"font_exists", "font_get_name", "font_get_fontname", "font_get_bold",
			"font_get_italic", "font_get_first", "font_get_last", "font_get_size",
			"font_set_cache_size", "font_get_texture", "font_get_uvs",
			"font_get_info", "font_enable_sdf", "font_sdf_spread",

			// Drawing
			"draw_self", "draw_sprite", "draw_sprite_ext", "draw_sprite_stretched",
			"draw_sprite_stretched_ext", "draw_sprite_pos", "draw_sprite_general",
			"draw_sprite_part", "draw_sprite_part_ext", "draw_sprite_tiled", "draw_sprite_tiled_ext",
			"draw_text", "draw_text_ext", "draw_text_transformed", "draw_text_transformed_colour",
			"draw_text_colour", "draw_text_ext_colour", "draw_text_ext_transformed",
			"draw_text_ext_transformed_colour", "draw_text_color", "draw_text_ext_color",
			"draw_text_transformed_color", "draw_text_ext_transformed_color",
			"draw_point", "draw_line", "draw_line_width", "draw_rectangle", "draw_rectangle_colour",
			"draw_rectangle_color", "draw_roundrect", "draw_roundrect_colour", "draw_roundrect_colour_ext",
			"draw_roundrect_color", "draw_roundrect_color_ext",
			"draw_triangle", "draw_triangle_colour", "draw_triangle_color",
			"draw_circle", "draw_circle_colour", "draw_circle_color",
			"draw_ellipse", "draw_ellipse_colour", "draw_ellipse_color",
			"draw_arrow", "draw_button", "draw_path", "draw_healthbar",
			"draw_getpixel", "draw_getpixel_ext",
			"draw_set_colour", "draw_set_color", "draw_set_alpha", "draw_get_colour", "draw_get_color",
			"draw_get_alpha", "draw_set_font", "draw_get_font",
			"draw_set_halign", "draw_set_valign", "draw_get_halign", "draw_get_valign",
			"draw_primitive_begin", "draw_primitive_begin_texture", "draw_primitive_end",
			"draw_vertex", "draw_vertex_colour", "draw_vertex_color", "draw_vertex_texture",
			"draw_vertex_texture_colour", "draw_vertex_texture_color",
			"draw_clear", "draw_clear_alpha", "draw_point_colour", "draw_point_color",
			"draw_line_colour", "draw_line_color", "draw_line_width_colour", "draw_line_width_color",
			"draw_enable_alphablend", "draw_enable_drawevent", "draw_enable_swf_aa",
			"draw_flush", "draw_get_lighting", "draw_light_define_ambient",
			"draw_light_define_direction", "draw_light_define_point", "draw_light_enable",
			"draw_light_get", "draw_light_get_ambient", "draw_set_lighting",
			"draw_set_circle_precision", "draw_surface", "draw_surface_ext",
			"draw_surface_part", "draw_surface_part_ext", "draw_surface_stretched",
			"draw_surface_stretched_ext", "draw_surface_tiled", "draw_surface_tiled_ext",
			"draw_surface_general", "draw_tilemap", "draw_tile",

			// Surfaces
			"surface_exists", "surface_create", "surface_create_ext", "surface_resize",
			"surface_free", "surface_save", "surface_save_part",
			"surface_set_target", "surface_set_target_ext", "surface_reset_target",
			"surface_depth_disable", "surface_get_height", "surface_get_width",
			"surface_get_texture", "surface_get_depth_disable", "surface_getpixel",
			"surface_getpixel_ext", "surface_get_format", "surface_copy", "surface_copy_part",

			// Buffers
			"buffer_exists", "buffer_create", "buffer_write", "buffer_read",
			"buffer_seek", "buffer_get_surface", "buffer_set_surface", "buffer_delete",
			"buffer_save", "buffer_save_ext", "buffer_load", "buffer_load_ext",
			"buffer_copy", "buffer_fill", "buffer_get_size", "buffer_tell",
			"buffer_peek", "buffer_poke", "buffer_save_async", "buffer_load_async",
			"buffer_compress", "buffer_decompress", "buffer_async_group_begin",
			"buffer_async_group_option", "buffer_async_group_end",
			"buffer_copy_from_vertex_buffer", "buffer_get_type", "buffer_get_alignment",
			"buffer_sizeof", "buffer_md5", "buffer_sha1", "buffer_base64_encode",
			"buffer_base64_decode", "buffer_base64_decode_ext", "buffer_crc32",
			"buffer_set_used_size", "buffer_resize",

			// Audio
			"audio_exists", "audio_get_name", "audio_get_type", "audio_play_sound",
			"audio_play_sound_at", "audio_play_sound_on", "audio_pause_sound", "audio_pause_all",
			"audio_resume_sound", "audio_resume_all", "audio_stop_sound", "audio_stop_all",
			"audio_is_playing", "audio_is_paused", "audio_sound_length",
			"audio_get_listener_mask", "audio_get_listener_count", "audio_get_listener_info",
			"audio_sound_get_gain", "audio_sound_get_pitch", "audio_sound_get_track_position",
			"audio_sound_set_track_position", "audio_sound_gain", "audio_sound_pitch",
			"audio_falloff_set_model", "audio_emitter_create", "audio_emitter_exists",
			"audio_emitter_free", "audio_emitter_pitch", "audio_emitter_gain",
			"audio_emitter_falloff", "audio_emitter_position", "audio_emitter_velocity",
			"audio_group_load", "audio_group_unload", "audio_group_is_loaded",
			"audio_group_load_progress", "audio_group_name", "audio_group_stop_all",
			"audio_group_set_gain", "audio_create_stream", "audio_destroy_stream",
			"audio_create_sync_group", "audio_destroy_sync_group", "audio_play_in_sync_group",
			"audio_start_sync_group", "audio_stop_sync_group", "audio_pause_sync_group",
			"audio_resume_sync_group", "audio_sync_group_get_track_pos", "audio_sync_group_debug",
			"audio_sync_group_is_playing", "audio_debug", "audio_channel_num",
			"audio_master_gain", "audio_get_master_gain", "audio_set_master_gain",
			"audio_get_listener_position", "audio_get_listener_velocity",
			"audio_listener_position", "audio_listener_velocity", "audio_listener_orientation",
			"audio_listener_set_position", "audio_listener_set_velocity", "audio_listener_set_orientation",
			"audio_listener_get_data", "audio_set_listener_mask",

			// Inputs keyboard
			"keyboard_check", "keyboard_check_pressed", "keyboard_check_released",
			"keyboard_check_direct", "keyboard_get_numlock", "keyboard_set_numlock",
			"keyboard_key_press", "keyboard_key_release", "keyboard_clear",
			"keyboard_set_map", "keyboard_get_map", "keyboard_unset_map",
			"keyboard_lastchar", "keyboard_lastkey", "keyboard_string",
			"keyboard_virtual_show", "keyboard_virtual_hide", "keyboard_virtual_status",
			"keyboard_virtual_height",

			// Inputs mouse
			"io_clear", "mouse_check_button", "mouse_check_button_pressed",
			"mouse_check_button_released", "mouse_wheel_up", "mouse_wheel_down",
			"mouse_clear", "device_mouse_check_button", "device_mouse_check_button_pressed",
			"device_mouse_check_button_released", "device_mouse_x", "device_mouse_y",
			"device_mouse_dbclick_enable", "device_mouse_x_to_gui", "device_mouse_y_to_gui",
			"device_mouse_raw_x", "device_mouse_raw_y",
			"window_mouse_get_x", "window_mouse_get_y", "window_mouse_set",

			// Inputs Gamepad
			"gamepad_is_supported", "gamepad_get_device_count",
			"gamepad_is_connected", "gamepad_get_description", "gamepad_get_button_threshold",
			"gamepad_get_axis_deadzone", "gamepad_set_button_threshold", "gamepad_set_axis_deadzone",
			"gamepad_button_check", "gamepad_button_check_pressed", "gamepad_button_check_released",
			"gamepad_button_count", "gamepad_button_value", "gamepad_axis_count", "gamepad_axis_value",
			"gamepad_set_vibration", "gamepad_set_colour", "gamepad_set_color",
			"gamepad_get_option", "gamepad_set_option",

			// Inputs Gesture
			"gesture_drag_time", "gesture_drag_distance", "gesture_flick_speed",
			"gesture_double_tap_time", "gesture_double_tap_distance",
			"gesture_pinch_distance", "gesture_pinch_angle_towards", "gesture_pinch_angle_away",
			"gesture_rotate_time", "gesture_rotate_angle", "gesture_tap_count",
			"gesture_get_drag_time", "gesture_get_drag_distance", "gesture_get_flick_speed",
			"gesture_get_double_tap_time", "gesture_get_double_tap_distance",
			"gesture_get_pinch_distance", "gesture_get_pinch_angle_towards",
			"gesture_get_pinch_angle_away", "gesture_get_rotate_time", "gesture_get_rotate_angle",
			"gesture_get_tap_count",

			// Rooms
			"room_exists", "room_get_name", "room_goto", "room_goto_next", "room_goto_previous",
			"room_restart", "room_next", "room_previous", "room_get_camera",
			"room_set_camera", "room_set_width", "room_set_height", "room_set_persistent",
			"room_set_background_colour", "room_set_background_color",
			"room_get_width", "room_get_height", "room_get_viewport",
			"room_instance_add", "room_instance_clear",
			"room_set_view", "room_set_view_enabled", "room_get_view_enabled",
			"room_pack", "room_unpack",

			// Layers
			"layer_exists", "layer_create", "layer_destroy", "layer_get_id",
			"layer_get_id_at_depth", "layer_get_depth", "layer_get_element_layer",
			"layer_get_name", "layer_depth", "layer_get_visible", "layer_set_visible",
			"layer_get_all", "layer_get_all_elements", "layer_get_element_type",
			"layer_has_instance", "layer_add_instance", "layer_get_script_begin",
			"layer_get_script_end", "layer_script_begin", "layer_script_end",
			"layer_shader", "layer_get_shader", "layer_set_target_room", "layer_get_target_room",
			"layer_reset_target_room", "layer_get_forced_depth", "layer_force_draw_depth",
			"layer_is_draw_depth_forced", "layer_get_hspeed", "layer_get_vspeed",
			"layer_hspeed", "layer_vspeed", "layer_get_x", "layer_get_y", "layer_x", "layer_y",
			"layer_background_get_id", "layer_background_exists", "layer_background_create",
			"layer_background_destroy", "layer_background_visible", "layer_background_change",
			"layer_background_sprite", "layer_background_htiled", "layer_background_vtiled",
			"layer_background_stretch", "layer_background_yscale", "layer_background_xscale",
			"layer_background_blend", "layer_background_alpha", "layer_background_index",
			"layer_background_speed", "layer_background_get_visible", "layer_background_get_blend",
			"layer_background_get_alpha", "layer_background_get_index", "layer_background_get_speed",
			"layer_background_get_sprite", "layer_background_get_stretch", "layer_background_get_xscale",
			"layer_background_get_yscale", "layer_background_get_htiled", "layer_background_get_vtiled",
			"layer_sprite_get_id", "layer_sprite_exists", "layer_sprite_create", "layer_sprite_destroy",
			"layer_sprite_change", "layer_sprite_get_sprite", "layer_sprite_get_frame",
			"layer_sprite_get_speed", "layer_sprite_get_blend", "layer_sprite_get_alpha",
			"layer_sprite_get_x", "layer_sprite_get_y", "layer_sprite_get_angle",
			"layer_sprite_get_xscale", "layer_sprite_get_yscale", "layer_sprite_x",
			"layer_sprite_y", "layer_sprite_angle", "layer_sprite_xscale", "layer_sprite_yscale",
			"layer_sprite_blend", "layer_sprite_alpha", "layer_sprite_speed",
			"layer_tilemap_get_id", "layer_tilemap_exists", "layer_tilemap_create",
			"layer_tile_exists", "layer_tile_create", "layer_tile_destroy",
			"layer_tile_change", "layer_tile_xscale", "layer_tile_yscale",
			"layer_tile_blend", "layer_tile_alpha", "layer_tile_x", "layer_tile_y",
			"layer_tile_region", "layer_tile_visible", "layer_tile_get_visible",
			"layer_tile_get_xscale", "layer_tile_get_yscale", "layer_tile_get_blend",
			"layer_tile_get_alpha", "layer_tile_get_x", "layer_tile_get_y", "layer_tile_get_region",
			"layer_sequence_create", "layer_sequence_destroy", "layer_sequence_exists",
			"layer_sequence_x", "layer_sequence_y", "layer_sequence_angle",
			"layer_sequence_xscale", "layer_sequence_yscale", "layer_sequence_headpos",
			"layer_sequence_headdir", "layer_sequence_speedscale", "layer_sequence_get_x",
			"layer_sequence_get_y", "layer_sequence_get_angle", "layer_sequence_get_xscale",
			"layer_sequence_get_yscale", "layer_sequence_get_headpos", "layer_sequence_get_headdir",
			"layer_sequence_get_speedscale", "layer_sequence_get_sequence",
			"layer_sequence_play", "layer_sequence_pause", "layer_sequence_get_instance",
			"layer_sequence_get_head_position", "layer_sequence_get_length",
			"layer_effect_create", "layer_effect_destroy",

			// Camera/View
			"camera_create", "camera_create_view", "camera_destroy", "camera_apply",
			"camera_get_active", "camera_get_default", "camera_set_default",
			"camera_set_view_mat", "camera_set_proj_mat", "camera_set_update_script",
			"camera_set_begin_script", "camera_set_end_script", "camera_set_view_pos",
			"camera_set_view_size", "camera_set_view_speed", "camera_set_view_border",
			"camera_set_view_angle", "camera_set_view_target",
			"camera_get_view_mat", "camera_get_proj_mat", "camera_get_update_script",
			"camera_get_begin_script", "camera_get_end_script", "camera_get_view_x",
			"camera_get_view_y", "camera_get_view_width", "camera_get_view_height",
			"camera_get_view_speed_x", "camera_get_view_speed_y", "camera_get_view_border_x",
			"camera_get_view_border_y", "camera_get_view_angle", "camera_get_view_target",
			"view_get_camera", "view_get_visible", "view_get_xport", "view_get_yport",
			"view_get_wport", "view_get_hport", "view_get_surface_id",
			"view_set_camera", "view_set_visible", "view_set_xport", "view_set_yport",
			"view_set_wport", "view_set_hport", "view_set_surface_id",

			// Tilemaps
			"tilemap_tileset", "tilemap_x", "tilemap_y", "tilemap_set",
			"tilemap_set_at_pixel", "tilemap_set_global_mask", "tilemap_get_global_mask",
			"tilemap_get_tileset", "tilemap_get_tile_width", "tilemap_get_tile_height",
			"tilemap_get_width", "tilemap_get_height", "tilemap_get_x", "tilemap_get_y",
			"tilemap_get", "tilemap_get_at_pixel", "tilemap_get_cell_x_at_pixel",
			"tilemap_get_cell_y_at_pixel", "tilemap_clear", "tilemap_get_mask",
			"tilemap_set_mask", "tilemap_get_frame", "tile_set_empty", "tile_set_index",
			"tile_set_flip", "tile_set_mirror", "tile_set_rotate", "tile_get_empty",
			"tile_get_index", "tile_get_flip", "tile_get_mirror", "tile_get_rotate",
			"draw_tile", "draw_tilemap",

			// Tilesets
			"tileset_get_name", "tileset_get_texture", "tileset_get_uvs",
			"tileset_get_info", "tileset_get_tile_width", "tileset_get_tile_height",
			"tileset_get_tile_count",

			// Paths
			"path_start", "path_end", "path_exists", "path_get_name",
			"path_get_length", "path_get_time", "path_get_kind", "path_get_closed",
			"path_get_precision", "path_get_number", "path_get_point_x", "path_get_point_y",
			"path_get_point_speed", "path_get_x", "path_get_y", "path_get_speed",
			"path_set_kind", "path_set_closed", "path_set_precision",
			"path_add", "path_assign", "path_duplicate", "path_append",
			"path_delete", "path_add_point", "path_insert_point", "path_change_point",
			"path_delete_point", "path_clear_points", "path_reverse", "path_mirror",
			"path_flip", "path_rotate", "path_scale", "path_shift",

			// File i/o
			"file_exists", "file_delete", "file_rename", "file_copy",
			"directory_exists", "directory_create", "directory_destroy",
			"file_find_first", "file_find_next", "file_find_close",
			"file_attributes", "filename_name", "filename_path", "filename_dir",
			"filename_drive", "filename_ext", "filename_change_ext",
			"file_text_open_read", "file_text_open_write", "file_text_open_append",
			"file_text_close", "file_text_write_string", "file_text_write_real",
			"file_text_writeln", "file_text_read_string", "file_text_read_real",
			"file_text_readln", "file_text_eof", "file_text_eoln",
			"file_bin_open", "file_bin_rewrite", "file_bin_close", "file_bin_position",
			"file_bin_size", "file_bin_seek", "file_bin_write_byte", "file_bin_read_byte",
			"get_open_filename", "get_save_filename", "get_open_filename_ext",
			"get_save_filename_ext", "get_string", "get_string_async", "get_integer",
			"get_integer_async", "get_login_async",
			"zip_unzip", "load_csv",

			// Json
			"json_encode", "json_decode", "json_stringify", "json_parse",
			"json_string_encode", "json_string_decode",

			// === ENCODING FUNCTIONS ===
			"base64_encode", "base64_decode",
			"md5_string_unicode", "md5_string_utf8", "md5_file",
			"sha1_string_unicode", "sha1_string_utf8", "sha1_file",

			// Networking
			"http_get", "http_get_file", "http_post_string", "http_request",
			"http_get_request_crossorigin", "http_set_request_crossorigin",
			"network_create_socket", "network_create_socket_ext", "network_create_server",
			"network_create_server_raw", "network_connect", "network_connect_async",
			"network_connect_raw", "network_connect_raw_async", "network_resolve",
			"network_set_config", "network_set_timeout", "network_send_packet",
			"network_send_raw", "network_send_broadcast", "network_send_udp",
			"network_send_udp_raw", "network_destroy",

			// Utility/Types/Vars
			"show_debug_message", "show_debug_overlay", "debug_event", "debug_get_callstack",
			"show_message", "show_message_async", "show_question", "show_question_async",
			"show_error", "game_end", "game_restart", "game_load", "game_load_buffer", "game_save",
			"game_save_buffer", "game_save_id", "game_load_id",
			"real", "string", "int64", "ptr", "is_real", "is_string", "is_array",
			"is_undefined", "is_int32", "is_int64", "is_ptr", "is_vec3", "is_vec4",
			"is_matrix", "is_bool", "is_method", "is_struct", "is_numeric",
			"is_infinity", "is_nan", "is_handle",
			"typeof", "instanceof",
			"variable_global_exists", "variable_global_get", "variable_global_set",
			"variable_instance_exists", "variable_instance_get", "variable_instance_set",
			"variable_instance_get_names", "variable_struct_exists", "variable_struct_get",
			"variable_struct_get_names", "variable_struct_names_count", "variable_struct_remove",
			"variable_struct_set", "struct_get", "struct_get_names", "struct_names_count",
			"struct_remove", "struct_set", "struct_exists", "struct_foreach",
			"method", "method_call", "method_get_self", "method_get_index",
			"static_get", "static_set",
			"parameter_count", "parameter_string",

			// Collisions
			"place_free", "place_empty", "place_meeting", "place_snapped",
			"move_random", "move_snap", "move_towards_point", "move_contact_solid",
			"move_contact_all", "move_outside_solid", "move_outside_all",
			"move_bounce_solid", "move_bounce_all", "move_wrap",
			"distance_to_point", "distance_to_object", "position_empty", "position_meeting",
			"path_start", "path_end", "mp_linear_step", "mp_potential_step",
			"mp_linear_step_object", "mp_potential_step_object",
			"mp_linear_path", "mp_potential_path", "mp_linear_path_object",
			"mp_potential_path_object", "mp_grid_create", "mp_grid_destroy",
			"mp_grid_clear_all", "mp_grid_clear_cell", "mp_grid_clear_rectangle",
			"mp_grid_add_cell", "mp_grid_add_rectangle", "mp_grid_add_instances",
			"mp_grid_path", "mp_grid_draw", "mp_grid_to_ds_grid",
			"collision_point", "collision_point_list", "collision_rectangle",
			"collision_rectangle_list", "collision_circle", "collision_circle_list",
			"collision_ellipse", "collision_ellipse_list", "collision_line",
			"collision_line_list", "collision_line_first",

			// Time and Time Sources
			"date_current_datetime", "date_create_datetime", "date_valid_datetime",
			"date_inc_year", "date_inc_month", "date_inc_week", "date_inc_day",
			"date_inc_hour", "date_inc_minute", "date_inc_second",
			"date_get_year", "date_get_month", "date_get_week", "date_get_day",
			"date_get_hour", "date_get_minute", "date_get_second", "date_get_weekday",
			"date_get_day_of_year", "date_get_hour_of_year", "date_get_minute_of_year",
			"date_get_second_of_year", "date_year_span", "date_month_span",
			"date_week_span", "date_day_span", "date_hour_span", "date_minute_span",
			"date_second_span", "date_compare_datetime", "date_compare_date",
			"date_compare_time", "date_date_of", "date_time_of", "date_datetime_string",
			"date_date_string", "date_time_string", "date_days_in_month", "date_days_in_year",
			"date_leap_year", "date_is_today", "date_set_timezone", "date_get_timezone",
			"time_source_create", "time_source_destroy", "time_source_start",
			"time_source_stop", "time_source_pause", "time_source_resume",
			"time_source_reconfigure", "time_source_reset", "time_source_get_children",
			"time_source_get_period", "time_source_get_reps_completed",
			"time_source_get_reps_remaining", "time_source_get_state",
			"time_source_get_time_remaining", "time_source_exists",

			// Shaders
			"shader_set", "shader_reset", "shader_current", "shader_get_name",
			"shader_get_uniform", "shader_get_sampler_index", "shader_set_uniform_i",
			"shader_set_uniform_i_array", "shader_set_uniform_f", "shader_set_uniform_f_array",
			"shader_set_uniform_matrix", "shader_set_uniform_matrix_array",
			"shader_enable_corner_id", "shader_is_compiled", "shader_get_shader_info",

			// Particles
			"part_type_create", "part_type_destroy", "part_type_exists", "part_type_clear",
			"part_type_shape", "part_type_sprite", "part_type_size", "part_type_scale",
			"part_type_orientation", "part_type_life", "part_type_step", "part_type_death",
			"part_type_speed", "part_type_direction", "part_type_gravity", "part_type_colour1",
			"part_type_colour2", "part_type_colour3", "part_type_color1", "part_type_color2",
			"part_type_color3", "part_type_alpha1", "part_type_alpha2", "part_type_alpha3",
			"part_type_blend", "part_system_create", "part_system_destroy", "part_system_exists",
			"part_system_clear", "part_system_draw_order", "part_system_depth",
			"part_system_position", "part_system_automatic_update", "part_system_automatic_draw",
			"part_system_update", "part_system_drawit", "part_system_get_layer",
			"part_system_layer", "part_particles_create", "part_particles_create_colour",
			"part_particles_create_color", "part_particles_clear", "part_particles_count",
			"part_emitter_create", "part_emitter_destroy", "part_emitter_destroy_all",
			"part_emitter_exists", "part_emitter_clear", "part_emitter_region",
			"part_emitter_burst", "part_emitter_stream",

			// Sequences
			"sequence_create", "sequence_destroy", "sequence_exists", "sequence_get",
			"sequence_track_new", "sequence_keyframe_new", "sequence_keyframedata_new",
			"sequence_get_objects", "layer_sequence_create", "layer_sequence_destroy",
			"layer_sequence_exists", "layer_sequence_play", "layer_sequence_pause",
			"sequence_instance_override_object",

			// Animcurves
			"animcurve_get", "animcurve_get_channel", "animcurve_channel_evaluate",
			"animcurve_get_channel_index", "animcurve_channel_new",

			// Vertex
			"vertex_format_begin", "vertex_format_end", "vertex_format_delete",
			"vertex_format_add_position", "vertex_format_add_position_3d",
			"vertex_format_add_colour", "vertex_format_add_color", "vertex_format_add_normal",
			"vertex_format_add_texcoord", "vertex_format_add_textcoord",
			"vertex_format_add_custom", "vertex_create_buffer", "vertex_create_buffer_ext",
			"vertex_delete_buffer", "vertex_begin", "vertex_end", "vertex_position",
			"vertex_position_3d", "vertex_colour", "vertex_color", "vertex_argb",
			"vertex_texcoord", "vertex_normal", "vertex_float1", "vertex_float2",
			"vertex_float3", "vertex_float4", "vertex_ubyte4", "vertex_submit",
			"vertex_freeze", "vertex_get_number", "vertex_get_buffer_size",
			"vertex_create_buffer_from_buffer", "vertex_create_buffer_from_buffer_ext",

			// Physics
			"physics_world_create", "physics_world_gravity", "physics_world_update_speed",
			"physics_world_update_iterations", "physics_world_draw_debug",
			"physics_pause_enable", "physics_fixture_create", "physics_fixture_delete",
			"physics_fixture_set_kinematic", "physics_fixture_set_density",
			"physics_fixture_set_awake", "physics_fixture_set_restitution",
			"physics_fixture_set_friction", "physics_fixture_set_collision_group",
			"physics_fixture_set_sensor", "physics_fixture_set_linear_damping",
			"physics_fixture_set_angular_damping", "physics_fixture_set_circle_shape",
			"physics_fixture_set_box_shape", "physics_fixture_set_edge_shape",
			"physics_fixture_set_polygon_shape", "physics_fixture_set_chain_shape",
			"physics_fixture_add_point", "physics_fixture_bind", "physics_fixture_bind_ext",
			"physics_remove_fixture", "physics_set_friction", "physics_set_density",
			"physics_set_restitution", "physics_get_friction", "physics_get_density",
			"physics_get_restitution", "physics_mass_properties", "physics_draw_debug",
			"physics_test_overlap", "physics_remove_fixture", "physics_apply_force",
			"physics_apply_impulse", "physics_apply_angular_impulse", "physics_apply_local_force",
			"physics_apply_local_impulse", "physics_apply_torque", "physics_mass_properties",
			"physics_draw_debug", "physics_test_overlap", "physics_particle_create",
			"physics_particle_delete", "physics_particle_delete_region_circle",
			"physics_particle_delete_region_box", "physics_particle_delete_region_poly",
			"physics_particle_set_flags", "physics_particle_set_category_flags",
			"physics_particle_draw", "physics_particle_draw_ext", "physics_particle_count",
			"physics_particle_get_data", "physics_particle_get_data_particle",
			"physics_particle_group_begin", "physics_particle_group_circle",
			"physics_particle_group_box", "physics_particle_group_polygon",
			"physics_particle_group_add_point", "physics_particle_group_end",
			"physics_particle_group_join", "physics_particle_group_delete",
			"physics_particle_group_count", "physics_particle_group_get_data",
			"physics_particle_group_get_mass", "physics_particle_group_get_inertia",
			"physics_particle_group_get_centre_x", "physics_particle_group_get_centre_y",
			"physics_particle_group_get_vel_x", "physics_particle_group_get_vel_y",
			"physics_particle_group_get_ang_vel", "physics_particle_group_get_x",
			"physics_particle_group_get_y", "physics_particle_group_get_angle",
			"physics_particle_set_group_flags", "physics_particle_get_group_flags",
			"physics_particle_get_max_count", "physics_particle_get_radius",
			"physics_particle_get_density", "physics_particle_get_damping",
			"physics_particle_get_gravity_scale", "physics_particle_set_max_count",
			"physics_particle_set_radius", "physics_particle_set_density",
			"physics_particle_set_damping", "physics_particle_set_gravity_scale",
			"physics_joint_distance_create", "physics_joint_rope_create",
			"physics_joint_revolute_create", "physics_joint_prismatic_create",
			"physics_joint_pulley_create", "physics_joint_wheel_create",
			"physics_joint_weld_create", "physics_joint_friction_create",
			"physics_joint_gear_create", "physics_joint_delete", "physics_joint_enable_motor",
			"physics_joint_get_value", "physics_joint_set_value",
			"physics_overlap_region_create", "physics_overlap_region_destroy",
			"physics_overlap_region_report",

			// Extensions
			"external_call", "external_define", "external_free",
			"extension_get_option_value",

			// Spine
			"skeleton_animation_set", "skeleton_animation_get", "skeleton_animation_mix",
			"skeleton_animation_set_ext", "skeleton_animation_get_ext",
			"skeleton_animation_get_duration", "skeleton_animation_get_frames",
			"skeleton_animation_clear", "skeleton_skin_set", "skeleton_skin_get",
			"skeleton_skin_create", "skeleton_attachment_set", "skeleton_attachment_get",
			"skeleton_attachment_create", "skeleton_collision_draw_set",
			"skeleton_bone_data_get", "skeleton_bone_data_set", "skeleton_bone_state_get",
			"skeleton_bone_state_set", "skeleton_get_minmax", "skeleton_get_num_bounds",
			"skeleton_get_bounds", "skeleton_slot_data", "skeleton_slot_colour_set",
			"skeleton_slot_colour_get", "skeleton_slot_alpha_set", "skeleton_slot_alpha_get",
			"draw_skeleton", "draw_skeleton_time", "draw_skeleton_instance",
			"draw_skeleton_collision",

			// Rollback
			"rollback_create_game", "rollback_join_game", "rollback_leave_game",
			"rollback_get_info", "rollback_get_state", "rollback_connect_player",
			"rollback_disconnect_player", "rollback_sync_on_frame", "rollback_apply_state",
			"rollback_net_event", "rollback_get_current_frame", "rollback_get_player_input",
			"rollback_get_predicted_frame", "rollback_use_player_prefs",
			"rollback_event_param", "rollback_create_mock_input", "rollback_leave_game",
			"rollback_chat", "rollback_get_region", "rollback_debug_ping",

			// GPU/Window/Misc
			"clipboard_has_text", "clipboard_set_text", "clipboard_get_text",
			"get_timer", "get_integer", "get_string",
			"window_set_fullscreen", "window_get_fullscreen", "window_set_caption",
			"window_get_caption", "window_set_min_width", "window_set_min_height",
			"window_set_max_width", "window_set_max_height", "window_get_width",
			"window_get_height", "window_set_position", "window_get_x", "window_get_y",
			"window_set_size", "window_set_rectangle", "window_centre", "window_center",
			"window_get_colour", "window_get_color", "window_set_colour", "window_set_color",
			"window_get_visible_rects", "window_get_cursor", "window_set_cursor",
			"display_get_width", "display_get_height", "display_get_orientation",
			"display_get_gui_width", "display_get_gui_height", "display_get_gui_aspect_ratio",
			"display_set_gui_size", "display_set_gui_maximise", "display_set_gui_maximize",
			"display_get_timing_method", "display_set_timing_method",
			"display_set_sleep_margin", "display_get_sleep_margin",
			"display_set_frequency", "display_get_frequency", "display_reset",
			"display_mouse_get_x", "display_mouse_get_y", "display_mouse_set",
			"texture_set_stage", "texture_get_texel_width", "texture_get_texel_height",
			"shaders_are_supported", "texture_set_interpolation",
			"texture_set_blending", "texture_set_repeat", "texture_set_repeat_ext",
			"texture_get_width", "texture_get_height", "texture_get_uvs",
			"texture_debug_messages", "texture_is_ready", "texture_prefetch",
			"texture_flush", "texture_flush_multi", "gpu_set_blendenable",
			"gpu_set_ztestenable", "gpu_set_zfunc", "gpu_set_zwriteenable",
			"gpu_set_lightingenable", "gpu_set_fog", "gpu_set_cullmode",
			"gpu_set_blendmode", "gpu_set_blendmode_ext", "gpu_set_blendmode_ext_sepalpha",
			"gpu_set_colorwriteenable", "gpu_set_colourwriteenable", "gpu_set_alphatestenable",
			"gpu_set_alphatestref", "gpu_set_alphatestfunc", "gpu_set_texfilter",
			"gpu_set_texfilter_ext", "gpu_set_texrepeat", "gpu_set_texrepeat_ext",
			"gpu_set_tex_filter", "gpu_set_tex_repeat", "gpu_set_tex_mip_filter",
			"gpu_set_tex_mip_bias", "gpu_set_tex_min_mip", "gpu_set_tex_max_mip",
			"gpu_set_tex_max_aniso", "gpu_set_tex_mip_enable", "gpu_get_blendenable",
			"gpu_get_ztestenable", "gpu_get_zfunc", "gpu_get_zwriteenable",
			"gpu_get_lightingenable", "gpu_get_fog", "gpu_get_cullmode",
			"gpu_get_blendmode", "gpu_get_blendmode_ext", "gpu_get_blendmode_ext_sepalpha",
			"gpu_get_blendmode_src", "gpu_get_blendmode_dest", "gpu_get_blendmode_srcalpha",
			"gpu_get_blendmode_destalpha", "gpu_get_colorwriteenable",
			"gpu_get_colourwriteenable", "gpu_get_alphatestenable", "gpu_get_alphatestref",
			"gpu_get_alphatestfunc", "gpu_get_texfilter", "gpu_get_texfilter_ext",
			"gpu_get_texrepeat", "gpu_get_texrepeat_ext", "gpu_push_state", "gpu_pop_state",
			"gpu_get_state", "gpu_set_state", "matrix_get", "matrix_set",
			"matrix_build_identity", "matrix_build", "matrix_build_lookat",
			"matrix_build_projection_ortho", "matrix_build_projection_perspective",
			"matrix_build_projection_perspective_fov", "matrix_multiply",
			"matrix_transform_vertex", "matrix_stack_push", "matrix_stack_pop",
			"matrix_stack_multiply", "matrix_stack_set", "matrix_stack_clear",
			"matrix_stack_top", "matrix_stack_is_empty"
		};
		for (auto& k : identifiers)
		{
			Identifier id;
			langDef.mIdentifiers.insert(std::make_pair(std::string(k), id));
		}

		// Built-in Vars
		static const char* const preprocIdentifiers[] = {

			// Built-in instance
			"id", "object_index", "mask_index", "solid", "persistent", "depth",
			"layer", "alarm", "timeline_index", "timeline_position", "timeline_speed",
			"timeline_running", "timeline_loop", "visible", "sprite_index",
			"sprite_width", "sprite_height", "sprite_xoffset", "sprite_yoffset",
			"image_number", "image_index", "image_speed", "image_xscale", "image_yscale",
			"image_angle", "image_alpha", "image_blend",
			"bbox_left", "bbox_right", "bbox_top", "bbox_bottom",
			"x", "y", "xprevious", "yprevious", "xstart", "ystart",
			"hspeed", "vspeed", "direction", "speed", "friction", "gravity", "gravity_direction",
			"path_index", "path_position", "path_positionprevious", "path_speed",
			"path_scale", "path_orientation", "path_endaction",

			// Physics
			"phy_rotation", "phy_position_x", "phy_position_y", "phy_angular_velocity",
			"phy_linear_velocity_x", "phy_linear_velocity_y", "phy_speed_x", "phy_speed_y",
			"phy_angular_damping", "phy_linear_damping", "phy_bullet", "phy_fixed_rotation",
			"phy_active", "phy_mass", "phy_inertia", "phy_com_x", "phy_com_y",
			"phy_dynamic", "phy_kinematic", "phy_sleeping", "phy_collision_points",
			"phy_collision_x", "phy_collision_y", "phy_col_normal_x", "phy_col_normal_y",
			"phy_speed", "phy_position_xprevious", "phy_position_yprevious",

			// Room
			"room", "room_width", "room_height", "room_caption", "room_speed",
			"room_persistent", "room_first", "room_last",

			// Mouse
			"mouse_x", "mouse_y", "mouse_button", "mouse_lastbutton",

			// Keyboard
			"keyboard_key", "keyboard_lastkey", "keyboard_lastchar", "keyboard_string",

			// Game/Application
			"application_surface",
			"game_id", "game_display_name", "game_project_name", "game_save_id",
			"working_directory", "temp_directory", "program_directory",
			"browser_width", "browser_height",
			"os_type", "os_device", "os_browser", "os_version",
			"display_aa", "async_load", "event_data", "event_type", "event_number",
			"event_object", "event_action",
			"delta_time", "current_time", "current_year", "current_month",
			"current_day", "current_weekday", "current_hour", "current_minute", "current_second",
			"fps", "fps_real",
			"cursor_sprite", "error_occurred", "error_last",
			"gamemaker_pro", "gamemaker_registered", "gamemaker_version",

			// Arguments
			"argument0", "argument1", "argument2", "argument3", "argument4",
			"argument5", "argument6", "argument7", "argument8", "argument9",
			"argument10", "argument11", "argument12", "argument13", "argument14", "argument15",
			"argument_count", "argument_relative",

			// View
			"view_enabled", "view_current",
			"view_visible", "view_xview", "view_yview", "view_wview", "view_hview",
			"view_angle", "view_hborder", "view_vborder", "view_hspeed", "view_vspeed",
			"view_object", "view_surface_id", "view_camera",

		};
		for (auto& k : preprocIdentifiers)
		{
			Identifier id;
			id.mDeclaration = "Built-in variable";
			langDef.mPreprocIdentifiers.insert(std::make_pair(std::string(k), id));
		}

		// Constants
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::string, PaletteIndex>(
			"\\b(?:true|false|pi|infinity|NaN|self|other|all|noone|global|undefined|pointer_null|pointer_invalid"
			"c_[a-z]+|"
			"mb_[a-z]+|"
			"vk_[a-z0-9]+|"
			"fa_[a-z]+|"
			"pr_[a-z]+|"
			"bm_[a-z_]+|"
			"buffer_[a-z0-9]+|"
			"audio_[a-z_]+|"
			"gp_[a-z0-9]+|"
			"os_[a-z]+|"
			"browser_[a-z_]+|"
			"device_[a-z_]+|"
			"bboxkind_[a-z]+|bboxmode_[a-z]+|"
			"path_action_[a-z]+|"
			"layerelementtype_[a-z]+|"
			"surface_[a-z0-9]+|"
			"tf_[a-z]+|"
			"cr_[a-z]+|"
			"spritespeed_[a-z]+|"
			"gamespeed_[a-z]+|"
			"matrix_view|matrix_projection|matrix_world|"
			"ef_[a-z]+|fx_[a-z]+|"
			"ty_[a-z]+|"
			"ds_type_[a-z]+|"
			"ev_[a-z_0-9]+|"
			"network_socket_[a-z_]+|network_type_[a-z_]+|network_config_[a-z_]+|"
			"phy_debug_[a-z_]+|phy_joint_[a-z_]+|phy_particle_flag_[a-z]+|phy_particle_group_flag_[a-z]+|"
			"vertex_type_[a-z0-9]+|vertex_usage_[a-z]+|"
			"tile_rotate|tile_flip|tile_mirror|tile_index_mask|"
			"ev_gesture_[a-z]+|"
			"display_landscape|display_landscape_flipped|display_portrait|display_portrait_flipped|"
			"cmpfunc_[a-z]+|cull_[a-z]+|lighttype_[a-z]+|"
			"time_source_units_[a-z]+|time_source_state_[a-z]+|"
			"spr_[a-z]+|"
			"true|false"
			")\\b",
			PaletteIndex::Preprocessor
		));

		// Strings (double quotes)
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::string, PaletteIndex>(
			"\\\"(\\\\.|[^\\\"])*\\\"",
			PaletteIndex::String
		));

		// Strings (single quotes)
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::string, PaletteIndex>(
			"'(\\\\.|[^'])*'",
			PaletteIndex::String
		));

		// Multi-line strings (@"...")
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::string, PaletteIndex>(
			"@\\\"(\\\\.|[^\\\"])*\\\"",
			PaletteIndex::String
		));

		// Multi-line strings (@'...')
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::string, PaletteIndex>(
			"@'(\\\\.|[^'])*'",
			PaletteIndex::String
		));

		// Template strings ($"...")
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::string, PaletteIndex>(
			"\\$\\\"(\\\\.|[^\\\"])*\\\"",
			PaletteIndex::String
		));

		// Numbers (hex)
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::string, PaletteIndex>(
			"\\$[0-9a-fA-F]+",
			PaletteIndex::Number
		));

		langDef.mTokenRegexStrings.push_back(std::make_pair<std::string, PaletteIndex>(
			"0[xX][0-9a-fA-F]+",
			PaletteIndex::Number
		));

		// Numbers (binary)
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::string, PaletteIndex>(
			"0[bB][01]+",
			PaletteIndex::Number
		));

		// Numbers
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::string, PaletteIndex>(
			"[+-]?([0-9]+([.][0-9]*)?|[.][0-9]+)([eE][+-]?[0-9]+)?",
			PaletteIndex::Number
		));

		// Generic identifier
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::string, PaletteIndex>(
			"[a-zA-Z_][a-zA-Z0-9_]*",
			PaletteIndex::Identifier
		));

		// Punctuation
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::string, PaletteIndex>(
			"[\\[\\]\\{\\}\\!\\%\\^\\&\\*\\(\\)\\-\\+\\=\\~\\|\\<\\>\\?\\/\\;\\,\\.]",
			PaletteIndex::Punctuation
		));

		langDef.mSingleLineComment = "//";
		langDef.mCommentStart = "/*";
		langDef.mCommentEnd = "*/";

		langDef.mCaseSensitive = true;
		langDef.mAutoIndentation = true;
		langDef.mName = "GML";

		inited = true;
	}
	return langDef;
}