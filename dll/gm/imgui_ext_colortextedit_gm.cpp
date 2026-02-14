#include "../imgui_gm.h"
#include "../imgui/imgui_colortextedit.h"

#include <unordered_map>
#include <vector>
#include <string>

// Use a static map to store instances
static std::unordered_map<int32_t, TextEditor*> EDITORS;
static std::unordered_map<std::string, TextEditor::LanguageDefinition> LANG_DEF_CACHE;

// Stores IDs of destroyed editors
static std::vector<int32_t> FREE_IDS; 
static int32_t NEXT_ID = 0;

static inline TextEditor* __editor_get(int32_t _handle) {
	auto _entry = EDITORS.find(_handle);
	return (_entry == EDITORS.end()) ? nullptr : _entry->second;
}

#define CHECK_EDITOR_BOOL \
	auto* _editor = __editor_get(_handle); \
	if (!_editor) \
		__return_bool(Result, false);

#define CHECK_EDITOR_STRING \
	auto* _editor = __editor_get(_handle); \
	if (!_editor) \
		__return_string(Result, "");

enum TextEditorPalette : int32_t 
{
	PALETTE_DARK		= 0,
	PALETTE_LIGHT		= 1,
	PALETTE_RETROBLUE	= 2,
	PALETTE_GAMEMAKER	= 3
};

// @ COLOR HELPER
static inline int32_t IMU32_TO_GML_RGB(ImU32 _color)
{
	int32_t R = (_color & 0xFF);
	int32_t G = ((_color >> 8) & 0xFF);
	int32_t B = ((_color >> 16) & 0xFF);

	return (B << 16) | (G << 8) | R;
}

// @ RETURN HELPER
static inline void __return_i32(RValue& Result, int32_t _value)
{
	Result.kind = VALUE_INT32;
	Result.v32	= _value;
	return;
}

static inline void __return_i64(RValue& Result, int64_t _value)
{
	Result.kind = VALUE_INT64;
	Result.v64	= _value;
	return;
}

static inline void __return_bool(RValue& Result, bool _value)
{
	Result.kind = VALUE_BOOL;
	Result.val	= _value;
	return;
}

static inline void __return_undefined(RValue& Result)
{
	Result.kind = VALUE_UNDEFINED;
	return;
}

static inline void __return_string(RValue& Result, const std::string& _string)
{
	Result.kind = VALUE_STRING;
	YYCreateString(&Result, _string.c_str());
	return;
}

// @ PUBLIC API

GMFUNC(__imgui_text_editor_create)
{
	auto* _editor = new TextEditor();
	int32_t _handle;

	// Check if we can reuse an ID from the free list
	if (!FREE_IDS.empty())
	{
		// Get last recylced ID and remove it from list
		_handle = FREE_IDS.back();
		FREE_IDS.pop_back();
	}
	else
		_handle = NEXT_ID++;

	EDITORS[_handle] = _editor;
	__return_i32(Result, _handle);
}

GMFUNC(__imgui_text_editor_destroy)
{
	int32_t _handle = YYGetInt32(arg, 0);

	auto _entry = EDITORS.find(_handle);
	if (_entry != EDITORS.end())
	{
		// Free TextEditor memory and remove from active map
		delete _entry->second;
		EDITORS.erase(_entry);

		// Add ID to free list for recycling
		FREE_IDS.push_back(_handle);
	}
}

GMFUNC(__imgui_text_editor_cleanup)
{
	// Delete any remaining editors
	for (auto const& item : EDITORS)
	{
		// Free memory
		delete item.second;
	}

	// Clear containers
	LANG_DEF_CACHE.clear();
	EDITORS.clear();
	FREE_IDS.clear();
	NEXT_ID = 0;

	__return_bool(Result, true);
}

GMFUNC(__imgui_text_editor_set_text)
{
	int32_t _handle		= YYGetInt32(arg, 0);
	const char* _text	= YYGetString(arg, 1);

	CHECK_EDITOR_BOOL;

	_editor->SetText(_text ? _text : "");
}

GMFUNC(__imgui_text_editor_get_text)
{
	int32_t _handle	= YYGetInt32(arg, 0);

	CHECK_EDITOR_STRING;

	__return_string(Result, _editor->GetText());
}

GMFUNC(__imgui_text_editor_set_language)
{
	int32_t _handle		= YYGetInt32(arg, 0);
	int32_t _languageID	= YYGetInt32(arg, 1);	GMDEFAULT(0);

	CHECK_EDITOR_BOOL;

	switch (_languageID)
	{
		case 0: _editor->SetLanguageDefinition(TextEditor::LanguageDefinition::CPlusPlus());	break;
		case 1: _editor->SetLanguageDefinition(TextEditor::LanguageDefinition::HLSL());			break;
		case 2: _editor->SetLanguageDefinition(TextEditor::LanguageDefinition::GLSL());			break;
		case 3: _editor->SetLanguageDefinition(TextEditor::LanguageDefinition::Lua());			break;
		case 4: _editor->SetLanguageDefinition(TextEditor::LanguageDefinition::GML());			break;
		default:
			break;
	}

	__return_bool(Result, true);
}

GMFUNC(__imgui_text_editor_render)
{
	int32_t _handle			= YYGetInt32(arg, 0);
	const char* _title		= YYGetString(arg, 1);
	double _width			= YYGetReal(arg, 2);	GMDEFAULT(0);
	double _height			= YYGetReal(arg, 3);	GMDEFAULT(0);
	ImGuiChildFlags _flags	= YYGetInt64(arg, 4);	GMDEFAULT(0);

	CHECK_EDITOR_BOOL;

	_editor->Render(_title, ImVec2(_width, _height), _flags);
	__return_bool(Result, true);
}

GMFUNC(__imgui_text_editor_is_read_only)
{
	int32_t _handle = YYGetInt32(arg, 0);

	CHECK_EDITOR_BOOL;

	__return_bool(Result, _editor->IsReadOnly());
}

GMFUNC(__imgui_text_editor_set_read_only)
{
	int32_t _handle	= YYGetInt32(arg, 0);
	bool _readOnly	= YYGetBool(arg, 1);

	CHECK_EDITOR_BOOL;

	_editor->SetReadOnly(_readOnly);
	__return_bool(Result, true);
}

GMFUNC(__imgui_text_editor_is_text_modified)
{
	int32_t _handle = YYGetInt32(arg, 0);

	CHECK_EDITOR_BOOL;

	__return_bool(Result, _editor->IsTextChanged());
}

// @ LINES / POSITIONS / SELECTION

GMFUNC(__imgui_text_editor_has_selection)
{
	int32_t _handle = YYGetInt32(arg, 0);

	CHECK_EDITOR_BOOL;

	__return_bool(Result, _editor->HasSelection());
}

GMFUNC(__imgui_text_editor_select_all)
{
	int32_t _handle = YYGetInt32(arg, 0);

	CHECK_EDITOR_BOOL;

	_editor->SelectAll();
	__return_bool(Result, true);
}

GMFUNC(__imgui_text_editor_select_word_under_cursor)
{
	int32_t _handle = YYGetInt32(arg, 0);

	CHECK_EDITOR_BOOL;

	_editor->SelectWordUnderCursor();
	__return_bool(Result, true);
}

GMFUNC(__imgui_text_editor_select_line)
{
	int32_t _handle = YYGetInt32(arg, 0);
	int64_t _line	= YYGetInt64(arg, 1);

	CHECK_EDITOR_BOOL;

	_line = max(0, _line - 1);
	TextEditor::Coordinates _start, _end;

	_start.mLine	= _line;
	_start.mColumn	= 0;

	_end.mLine		= _line;
	_end.mColumn	= 0;
	
	_editor->SetSelection(_start, _end, TextEditor::SelectionMode::Line);
	__return_bool(Result, true);
}

GMFUNC(__imgui_text_editor_get_selected_text)
{
	int32_t _handle = YYGetInt32(arg, 0);

	CHECK_EDITOR_BOOL;

	__return_string(Result, _editor->GetSelectedText());
}

GMFUNC(__imgui_text_editor_set_cursor_pos_line_column)
{
	int32_t _handle = YYGetInt32(arg, 0);
	int64_t _line	= YYGetInt64(arg, 1);	GMDEFAULT(1);
	int64_t _column = YYGetInt64(arg, 2);	GMDEFAULT(1);

	CHECK_EDITOR_BOOL;

	// 1-based index
	_editor->SetCursorPosition(TextEditor::Coordinates(_line - 1, _column - 1));
	__return_bool(Result, true);
}

GMFUNC(__imgui_text_editor_get_cursor_pos_line)
{
	int32_t _handle = YYGetInt32(arg, 0);

	CHECK_EDITOR_BOOL;

	auto _coordinates = _editor->GetCursorPosition();
	
	// Convert to 1-based index for GML
	__return_i64(Result, _coordinates.mLine + 1);
}

GMFUNC(__imgui_text_editor_get_cursor_pos_column)
{
	int32_t _handle = YYGetInt32(arg, 0);

	CHECK_EDITOR_BOOL;

	auto _coordinates = _editor->GetCursorPosition();
	
	// Convert to 1-based index for GML
	__return_i64(Result, _coordinates.mColumn + 1);
}

GMFUNC(__imgui_text_editor_get_total_lines)
{
	int32_t _handle = YYGetInt32(arg, 0);

	CHECK_EDITOR_BOOL;

	__return_i64(Result, _editor->GetTotalLines());
}

// @ UNDO / REDO

GMFUNC(__imgui_text_editor_can_undo)
{
	int32_t _handle = YYGetInt32(arg, 0);

	CHECK_EDITOR_BOOL;

	__return_bool(Result, _editor->CanUndo());
}

GMFUNC(__imgui_text_editor_can_redo)
{
	int32_t _handle = YYGetInt32(arg, 0);

	CHECK_EDITOR_BOOL;

	__return_bool(Result, _editor->CanRedo());
}

GMFUNC(__imgui_text_editor_undo)
{
	int32_t _handle = YYGetInt32(arg, 0);

	CHECK_EDITOR_BOOL;

	_editor->Undo();
	__return_bool(Result, true);
}

GMFUNC(__imgui_text_editor_redo)
{
	int32_t _handle = YYGetInt32(arg, 0);

	CHECK_EDITOR_BOOL;

	_editor->Redo();
	__return_bool(Result, true);
}

// @ CLIPBOARD / EDIT OPERATIONS

GMFUNC(__imgui_text_editor_is_overwrite)
{
	int32_t _handle = YYGetInt32(arg, 0);

	CHECK_EDITOR_BOOL;

	__return_bool(Result, _editor->IsOverwrite());
}

GMFUNC(__imgui_text_editor_copy)
{
	int32_t _handle = YYGetInt32(arg, 0);

	CHECK_EDITOR_BOOL;

	_editor->Copy();
	__return_bool(Result, true);
}

GMFUNC(__imgui_text_editor_paste)
{
	int32_t _handle = YYGetInt32(arg, 0);

	CHECK_EDITOR_BOOL;

	_editor->Paste();
	__return_bool(Result, true);
}

GMFUNC(__imgui_text_editor_cut)
{
	int32_t _handle = YYGetInt32(arg, 0);

	CHECK_EDITOR_BOOL;

	_editor->Cut();
	__return_bool(Result, true);
}

GMFUNC(__imgui_text_editor_delete)
{
	int32_t _handle = YYGetInt32(arg, 0);

	CHECK_EDITOR_BOOL;

	_editor->Delete();
	__return_bool(Result, true);
}

GMFUNC(__imgui_text_editor_insert_text)
{
	int32_t _handle		= YYGetInt32(arg, 0);
	const char* _text	= YYGetString(arg, 1);

	CHECK_EDITOR_BOOL;

	_editor->InsertText(_text ? _text : "");
	__return_bool(Result, true);
}

// @ VISUAL OPTIONS

GMFUNC(__imgui_text_editor_get_tab_size)
{
	int32_t _handle = YYGetInt32(arg, 0);

	CHECK_EDITOR_BOOL;

	__return_i64(Result, _editor->GetTabSize());
}

GMFUNC(__imgui_text_editor_set_tab_size)
{
	int32_t _handle = YYGetInt32(arg, 0);
	int64_t _size	= YYGetInt64(arg, 1);

	CHECK_EDITOR_BOOL;

	_editor->SetTabSize(_size);
	__return_bool(Result, true);
}

GMFUNC(__imgui_text_editor_is_showing_whitespaces)
{
	int32_t _handle = YYGetInt32(arg, 0);

	CHECK_EDITOR_BOOL;

	__return_bool(Result, _editor->IsShowingWhitespaces());
}

GMFUNC(__imgui_text_editor_set_show_whitespaces)
{
	int32_t _handle = YYGetInt32(arg, 0);
	int64_t _enable	= YYGetBool(arg, 1);

	CHECK_EDITOR_BOOL;

	_editor->SetShowWhitespaces(_enable);
	__return_bool(Result, true);
}

GMFUNC(__imgui_text_editor_is_colorizer_enabled)
{
	int32_t _handle = YYGetInt32(arg, 0);

	CHECK_EDITOR_BOOL;

	__return_bool(Result, _editor->IsColorizerEnabled());
}

GMFUNC(__imgui_text_editor_set_colorizer_enable)
{
	int32_t _handle = YYGetInt32(arg, 0);
	int64_t _enable	= YYGetBool(arg, 1);

	CHECK_EDITOR_BOOL;

	_editor->SetColorizerEnable(_enable);
	__return_bool(Result, true);
}

GMFUNC(__imgui_text_editor_set_palette)
{
	int32_t _handle		= YYGetInt32(arg, 0);
	int64_t _paletteID	= YYGetInt64(arg, 1);

	CHECK_EDITOR_BOOL;

	switch (_paletteID)
	{
		case PALETTE_DARK:		_editor->SetPalette(TextEditor::GetDarkPalette());		break;
		case PALETTE_LIGHT:		_editor->SetPalette(TextEditor::GetLightPalette());		break;
		case PALETTE_RETROBLUE:	_editor->SetPalette(TextEditor::GetRetroBluePalette());	break;
		case PALETTE_GAMEMAKER:	_editor->SetPalette(TextEditor::GetGameMakerPalette());	break;
		default:
			_editor->SetPalette(TextEditor::GetDarkPalette());	break;
	}

	__return_bool(Result, true);
}

GMFUNC(__imgui_text_editor_get_palette_color)
{
	int32_t _handle = YYGetInt32(arg, 0);
	int64_t _index	= YYGetInt64(arg, 1);

	CHECK_EDITOR_BOOL;

	// Check if the index is valid
	if (_index < 0 || _index >= (int32_t)TextEditor::PaletteIndex::Max)
		__return_bool(Result, false);

	const auto& _pal = _editor->GetPalette();

	ImU32 c = _pal[_index];

	Result.kind = VALUE_REAL;
	Result.val	= IMU32_TO_GML_RGB(c);
}

GMFUNC(__imgui_text_editor_get_palette_alpha)
{
	int32_t _handle = YYGetInt32(arg, 0);
	int64_t _index	= YYGetInt64(arg, 1);

	CHECK_EDITOR_BOOL;

	// Check if the index is valid
	if (_index < 0 || _index >= (int32_t)TextEditor::PaletteIndex::Max)
		__return_bool(Result, false);

	const auto& _pal = _editor->GetPalette();
	ImU32 c = _pal[_index];

	// Extract alpha from ABGR format
	int A = (c >> 24) & 0xFF;

	Result.kind = VALUE_REAL;
	Result.val	= (double)A;
}

GMFUNC(__imgui_text_editor_set_palette_color)
{
	int32_t _handle = YYGetInt32(arg, 0);
	int32_t _index	= YYGetInt32(arg, 1);
	int32_t _color	= YYGetInt32(arg, 2);
	int _alpha		= YYGetReal(arg, 3);	GMDEFAULT(255);

	CHECK_EDITOR_BOOL;

	// Check if the index is valid
	if (_index < 0 || _index >= (int32_t)TextEditor::PaletteIndex::Max)
		__return_bool(Result, false);

	TextEditor::Palette _pal = _editor->GetPalette();

	int R = _color & 0xFF;
	int G = (_color >> 8) & 0xFF;
	int B = (_color >> 16) & 0xFF;

	_pal[_index] = IM_COL32(R, G, B, _alpha);
	_editor->SetPalette(_pal);

	__return_bool(Result, true);
}

// @ ERROR MARKERS

GMFUNC(__imgui_text_editor_set_error_marker)
{
	int32_t _handle			= YYGetInt32(arg, 0);
	int64_t _line			= YYGetInt64(arg, 1);
	const char* _message	= YYGetString(arg, 2);

	CHECK_EDITOR_BOOL;

	auto _markers		= _editor->GetErrorMarkers();
	_markers[_line]		= _message ? _message : "";

	_editor->SetErrorMarkers(_markers);

	__return_bool(Result, true);
}

GMFUNC(__imgui_text_editor_remove_error_marker)
{
	int32_t _handle = YYGetInt32(arg, 0);
	int64_t _line	= YYGetInt64(arg, 1);

	CHECK_EDITOR_BOOL;

	auto _markers = _editor->GetErrorMarkers();
	_markers.erase(_line);
	_editor->SetErrorMarkers(_markers);

	__return_bool(Result, true);
}

GMFUNC(__imgui_text_editor_clear_error_markers)
{
	int32_t _handle = YYGetInt32(arg, 0);

	CHECK_EDITOR_BOOL;

	TextEditor::ErrorMarkers _empty;
	_editor->SetErrorMarkers(_empty);

	__return_bool(Result, true);
}

GMFUNC(__imgui_text_editor_get_error_marker)
{
	int32_t _handle = YYGetInt32(arg, 0);
	int64_t _line	= YYGetInt64(arg, 1);

	CHECK_EDITOR_STRING;

	auto _markers	= _editor->GetErrorMarkers();
	auto _marker	= _markers.find(_line);

	if (_marker != _markers.end())
		__return_string(Result, _marker->second);

	__return_string(Result, "");
}

// @ BREAKPOINTS

GMFUNC(__imgui_text_editor_set_breakpoint)
{
	int32_t _handle = YYGetInt32(arg, 0);
	int64_t _line	= YYGetInt64(arg, 1);

	CHECK_EDITOR_BOOL;

	auto _breakpoints = _editor->GetBreakpoints();
	_breakpoints.insert(_line);
	_editor->SetBreakpoints(_breakpoints);

	__return_bool(Result, true);
}

GMFUNC(__imgui_text_editor_remove_breakpoint)
{
	int32_t _handle = YYGetInt32(arg, 0);
	int64_t _line	= YYGetInt64(arg, 1);

	CHECK_EDITOR_BOOL;

	auto _breakpoints = _editor->GetBreakpoints();
	_breakpoints.erase(_line);
	_editor->SetBreakpoints(_breakpoints);

	__return_bool(Result, true);
}

GMFUNC(__imgui_text_editor_clear_breakpoints)
{
	int32_t _handle = YYGetInt32(arg, 0);

	CHECK_EDITOR_BOOL;

	TextEditor::Breakpoints _empty;
	_editor->SetBreakpoints(_empty);

	__return_bool(Result, true);
}

GMFUNC(__imgui_text_editor_has_breakpoint)
{
	int32_t _handle = YYGetInt32(arg, 0);
	int64_t _line	= YYGetInt64(arg, 1);

	CHECK_EDITOR_BOOL;

	auto _breakpoints = _editor->GetBreakpoints();
	__return_bool(Result, _breakpoints.count(_line) > 0);
}

// @ LANGUAGE DEFINITION - KEYWORDS

GMFUNC(__imgui_text_editor_add_keyword)
{
	int32_t _handle			= YYGetInt32(arg, 0);
	const char* _keyword	= YYGetString(arg, 1);

	CHECK_EDITOR_BOOL;

	if (!_keyword)
		__return_bool(Result, false);

	auto _langDef = _editor->GetLanguageDefinition();
	_langDef.mKeywords.insert(_keyword);
	_editor->SetLanguageDefinition(_langDef);

	__return_bool(Result, true);
}

GMFUNC(__imgui_text_editor_remove_keyword)
{
	int32_t _handle			= YYGetInt32(arg, 0);
	const char* _keyword	= YYGetString(arg, 1);

	CHECK_EDITOR_BOOL;

	if (!_keyword)
		__return_bool(Result, false);

	auto _langDef = _editor->GetLanguageDefinition();
	_langDef.mKeywords.erase(_keyword);
	_editor->SetLanguageDefinition(_langDef);

	__return_bool(Result, true);
}

GMFUNC(__imgui_text_editor_clear_keywords)
{
	int32_t _handle = YYGetInt32(arg, 0);

	CHECK_EDITOR_BOOL;

	auto _langDef = _editor->GetLanguageDefinition();
	_langDef.mKeywords.clear();
	_editor->SetLanguageDefinition(_langDef);

	__return_bool(Result, true);
}

// @ LANGUAGE DEFINITION - IDENTIFIERS (+ TOOLTIPS)

GMFUNC(__imgui_text_editor_add_identifier)
{
	int32_t _handle				= YYGetInt32(arg, 0);
	const char* _identifier		= YYGetString(arg, 1);
	const char* _declaration	= YYGetString(arg, 2);	GMDEFAULT("");

	CHECK_EDITOR_BOOL;

	if (!_identifier)
		__return_bool(Result, false);

	auto _langDef = _editor->GetLanguageDefinition();

	TextEditor::Identifier _id;
	_id.mDeclaration = _declaration ? _declaration : "";

	_langDef.mIdentifiers[_identifier] = _id;
	_editor->SetLanguageDefinition(_langDef);

	__return_bool(Result, true);
}

GMFUNC(__imgui_text_editor_remove_identifier)
{
	int32_t _handle			= YYGetInt32(arg, 0);
	const char* _identifier = YYGetString(arg, 1);

	CHECK_EDITOR_BOOL;

	if (!_identifier)
		__return_bool(Result, false);

	auto _langDef = _editor->GetLanguageDefinition();
	_langDef.mIdentifiers.erase(_identifier);
	_editor->SetLanguageDefinition(_langDef);

	__return_bool(Result, true);
}

GMFUNC(__imgui_text_editor_clear_identifiers)
{
	int32_t _handle = YYGetInt32(arg, 0);

	CHECK_EDITOR_BOOL;

	auto _langDef = _editor->GetLanguageDefinition();
	_langDef.mIdentifiers.clear();
	_editor->SetLanguageDefinition(_langDef);

	__return_bool(Result, true);
}

// @ LANGUAGE DEFINITION - PREPROCESSOR IDENTIFIERS

GMFUNC(__imgui_text_editor_add_preproc_identifier)
{
	int32_t _handle				= YYGetInt32(arg, 0);
	const char* _identifier		= YYGetString(arg, 1);
	const char* _declaration	= YYGetString(arg, 2);	GMDEFAULT("");

	CHECK_EDITOR_BOOL;

	if (!_identifier)
		__return_bool(Result, false);

	auto _langDef = _editor->GetLanguageDefinition();

	TextEditor::Identifier _id;
	_id.mDeclaration = _declaration ? _declaration : "";

	_langDef.mPreprocIdentifiers[_identifier] = _id;
	_editor->SetLanguageDefinition(_langDef);

	__return_bool(Result, true);
}

GMFUNC(__imgui_text_editor_remove_preproc_identifier)
{
	int32_t _handle			= YYGetInt32(arg, 0);
	const char* _identifier = YYGetString(arg, 1);

	CHECK_EDITOR_BOOL;

	if (!_identifier)
		__return_bool(Result, false);

	auto _langDef = _editor->GetLanguageDefinition();
	_langDef.mPreprocIdentifiers.erase(_identifier);
	_editor->SetLanguageDefinition(_langDef);

	__return_bool(Result, true);
}

GMFUNC(__imgui_text_editor_clear_preproc_identifiers)
{
	int32_t _handle = YYGetInt32(arg, 0);

	CHECK_EDITOR_BOOL;

	auto _langDef = _editor->GetLanguageDefinition();
	_langDef.mPreprocIdentifiers.clear();
	_editor->SetLanguageDefinition(_langDef);

	__return_bool(Result, true);
}

// @ LANGUAGE DEFINITION - CONSTANTS

GMFUNC(__imgui_text_editor_add_constant)
{
	int32_t _handle			= YYGetInt32(arg, 0);
	const char* _constant	= YYGetString(arg, 1);

	CHECK_EDITOR_BOOL;

	if (!_constant || strlen(_constant) == 0)
	{
		Result.kind = VALUE_BOOL;
		Result.val	= false;
		return;
	}

	auto _langDef = _editor->GetLanguageDefinition();

	const std::string _toAdd(_constant);
	const std::string _insertion = "|" + _toAdd;

	for (auto& pattern_pair : _langDef.mTokenRegexStrings)
	{
		if (pattern_pair.second != TextEditor::PaletteIndex::Preprocessor)
			continue;

		std::string& pattern = pattern_pair.first;

		// Already exists?
		if (pattern.find(_toAdd) != std::string::npos)
		{
			Result.kind = VALUE_BOOL;
			Result.val = true;
			return;
		}

		// insert before ")\\b"
		size_t _pos = pattern.rfind(")\\b");
		if (_pos != std::string::npos)
		{
			pattern.insert(_pos, _insertion);
			_editor->SetLanguageDefinition(_langDef);
			Result.kind = VALUE_BOOL;
			Result.val	= true;
			return;
		}

		// insert before final "\\b"
		_pos = pattern.rfind("\\b");
		if (_pos > 2)
		{
			pattern.insert(_pos, _insertion);
			_editor->SetLanguageDefinition(_langDef);
			Result.kind = VALUE_BOOL;
			Result.val	= true;
			return;
		}

		// First pattern has unexpected structure - bail out
		Result.kind = VALUE_BOOL;
		Result.val	= false;
		return;
	}

	// No pattern exists - create new
	_langDef.mTokenRegexStrings.emplace_back
	(
		"\\b" + _toAdd + "\\b",
		TextEditor::PaletteIndex::Preprocessor
	);

	_editor->SetLanguageDefinition(_langDef);
	Result.kind = VALUE_BOOL;
	Result.val	= true;
}

GMFUNC(__imgui_text_editor_remove_constant)
{
	int32_t _handle			= YYGetInt32(arg, 0);
	const char* _constant	= YYGetString(arg, 1);

	CHECK_EDITOR_BOOL;

	if (!_constant || strlen(_constant) == 0)
	{
		Result.kind = VALUE_BOOL;
		Result.val	= false;
		return;
	}

	auto _langDef = _editor->GetLanguageDefinition();

	// Build all search strings
	const std::string _toRemove(_constant);
	const std::string _fullPattern		= "\\b" + _toRemove + "\\b";
	const std::string _endGML			= "|"	+ _toRemove + ")\\b";
	const std::string _endStandalone	= "|"	+ _toRemove + "\\b";
	const std::string _middle			= "|"	+ _toRemove + "|";
	const std::string _start			= "\\b" + _toRemove + "|";

	bool _anyFound = false;

	for (int i = (int)_langDef.mTokenRegexStrings.size() - 1; i >= 0; i--)
	{
		if (_langDef.mTokenRegexStrings[i].second != TextEditor::PaletteIndex::Preprocessor)
			continue;

		std::string& _pattern = _langDef.mTokenRegexStrings[i].first;

		// At end before )\b (GML pattern with custom constants)
		size_t _pos = _pattern.find(_endGML);
		if (_pos != std::string::npos)
		{
			_pattern.erase(_pos, _toRemove.length() + 1);
			_anyFound = true;
			continue;
		}

		// Entire pattern match
		if (_pattern == _fullPattern)
		{
			_langDef.mTokenRegexStrings.erase(_langDef.mTokenRegexStrings.begin() + i);
			_anyFound = true;
			continue;
		}

		// At end of standalone pattern
		_pos = _pattern.find(_endStandalone);
		if (_pos != std::string::npos)
		{
			_pattern.erase(_pos, _toRemove.length() + 1);
			_anyFound = true;
			continue;
		}

		// In middle
		_pos = _pattern.find(_middle);
		if (_pos != std::string::npos)
		{
			_pattern.erase(_pos, _toRemove.length() + 1);
			_anyFound = true;
			continue;
		}

		// At start
		if (_pattern.find(_start) == 0)
		{
			_pattern.erase(2, _toRemove.length() + 1);
			_anyFound = true;
			continue;
		}
	}

	if (_anyFound)
		_editor->SetLanguageDefinition(_langDef);

	Result.kind = VALUE_BOOL;
	Result.val	= _anyFound;
}


GMFUNC(__imgui_text_editor_clear_constants)
{
	int32_t _handle = YYGetInt32(arg, 0);

	CHECK_EDITOR_BOOL;

	auto _langDef = _editor->GetLanguageDefinition();

	// This clears both built-in and custom constants
	_langDef.mTokenRegexStrings.erase
	(
		std::remove_if
		(
			_langDef.mTokenRegexStrings.begin(),
			_langDef.mTokenRegexStrings.end(),
			[](const std::pair<std::string, TextEditor::PaletteIndex>& pair) {
				return pair.second == TextEditor::PaletteIndex::Preprocessor;
			}
		),
		_langDef.mTokenRegexStrings.end()
	);

	_editor->SetLanguageDefinition(_langDef);

	__return_bool(Result, true);
}

// @ LANGUAGE DEFINITION - CACHING

GMFUNC(__imgui_text_editor_save_language_def)
{
	int32_t _handle		= YYGetInt32(arg, 0);
	const char* _name	= YYGetString(arg, 1);

	CHECK_EDITOR_BOOL;

	if (!_name || strlen(_name) == 0)
	{
		__return_bool(Result, false);
		return;
	}

	// Copy the full language definition into the cache
	LANG_DEF_CACHE[_name] = _editor->GetLanguageDefinition();

	__return_bool(Result, true);
}

GMFUNC(__imgui_text_editor_load_language_def)
{
	int32_t _handle		= YYGetInt32(arg, 0);
	const char* _name	= YYGetString(arg, 1);

	CHECK_EDITOR_BOOL;

	if (!_name || strlen(_name) == 0)
	{
		__return_bool(Result, false);
		return;
	}

	auto _entry = LANG_DEF_CACHE.find(_name);
	if (_entry == LANG_DEF_CACHE.end())
	{
		__return_bool(Result, false);
		return;
	}

	_editor->SetLanguageDefinition(_entry->second);

	__return_bool(Result, true);
}