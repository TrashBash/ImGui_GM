#include "../imgui_gm.h"
#include "../imgui/imgui_guizmo.h"

GMFUNC(__imgui_guizmo_begin_frame)
{
	ImGuizmo::BeginFrame();
    Result.kind = VALUE_UNDEFINED;
}

GMFUNC(__imgui_guizmo_set_orthographic)
{
	bool _isOrtho = YYGetBool(arg, 0);

	ImGuizmo::SetOrthographic(_isOrtho);
	Result.kind = VALUE_BOOL;
	Result.val	= _isOrtho;
}

GMFUNC(__imgui_guizmo_is_using)
{
	Result.kind = VALUE_BOOL;
	Result.val	= ImGuizmo::IsUsing();
}

GMFUNC(__imgui_guizmo_is_over)
{
	Result.kind = VALUE_BOOL;
	Result.val	= ImGuizmo::IsOver();
}

GMFUNC(__imgui_guizmo_manipulate)
{   
    const int MATRIX_SIZE = 16;
    
    float* _viewMat = YYGetArray<float>(arg, 0, MATRIX_SIZE);
    float* _projMat = YYGetArray<float>(arg, 1, MATRIX_SIZE);
    int _operation  = YYGetInt32(arg, 2);
    int _mode       = YYGetInt32(arg, 3);
    float* _mdlMat  = YYGetArray<float>(arg, 4, MATRIX_SIZE);

    // Honestly absolutely no idea how else to get optional array args to work
    // with the wrapper.js
    RValue* _deltaMatrix = &arg[5]; GMHINT(Array<Real>);    GMDEFAULT(undefined);
    float* _deltaMat = nullptr;

    if (_deltaMatrix->kind == VALUE_ARRAY) {
        float* _deltaMat = YYGetArray<float>(arg, 5, MATRIX_SIZE);
    }

    RValue* _snap = &arg[6];     GMHINT(Array<Real>);    GMDEFAULT(undefined);
    float* _snapVals = nullptr;

    if (_snap->kind == VALUE_ARRAY) {
        float* _snapVals = YYGetArray<float>(arg, 6, 3);
    }

    RValue* _localBounds = &arg[7]; GMHINT(Array<Real>);    GMDEFAULT(undefined);
    float* _localBoundsVals = nullptr;

    if (_localBounds->kind == VALUE_ARRAY) {
        float* _localBoundsVals = YYGetArray<float>(arg, 7, 6);
    }

    ImGuizmo::Manipulate
    (
        _viewMat,
        _projMat,
        (ImGuizmo::OPERATION)_operation,
        (ImGuizmo::MODE)_mode,
        _mdlMat,
        _deltaMat,
        _snapVals,
        _localBoundsVals,
        nullptr
    );

    bool _wasUsed = ImGuizmo::IsUsing();

    if (_wasUsed)
    {
        YYSetArray(&arg[4], _mdlMat, MATRIX_SIZE);
       
        if (_deltaMat != nullptr)
            YYSetArray(&arg[5], _deltaMat, MATRIX_SIZE);
    }

    delete[] _viewMat;
    delete[] _projMat;
    delete[] _mdlMat;
    delete[] _deltaMat;
    delete[] _snapVals;
    delete[] _localBoundsVals;

    Result.kind = VALUE_BOOL;
    Result.val  = _wasUsed;
}

GMFUNC(__imgui_guizmo_set_drawlist)
{
    RValue* _drawlist = &arg[0];
    GMDEFAULT(undefined);
    GMHINT(Pointer);

    if (_drawlist->kind == VALUE_UNDEFINED)
    {
        ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());
        Result.kind = VALUE_BOOL;
        Result.val  = true;
        return;
    }

    ImGuizmo::SetDrawlist((ImDrawList*)YYGetPtr(arg, 0));
    Result.kind = VALUE_UNDEFINED;
}

GMFUNC(__imgui_guizmo_set_rect)
{
    float _x        = YYGetReal(arg, 0);
    float _y        = YYGetReal(arg, 1);
    float _width    = YYGetReal(arg, 2);
    float _height   = YYGetReal(arg, 3);

    ImGuizmo::SetRect(_x, _y, _width, _height);

    Result.kind = VALUE_UNDEFINED;
}

GMFUNC(__imgui_guizmo_draw_grid)
{
    const int MATRIX_SIZE = 16;

    float* view = YYGetArray<float>(arg, 0, MATRIX_SIZE);
    float* proj = YYGetArray<float>(arg, 1, MATRIX_SIZE);
    float* grid = YYGetArray<float>(arg, 2, MATRIX_SIZE);
    float  size = YYGetReal(arg, 3);

    ImGuizmo::DrawGrid(view, proj, grid, size);

    delete[] view; delete[] proj; delete[] grid;

    Result.kind = VALUE_UNDEFINED;
}

GMFUNC(__imgui_guizmo_enable)
{
    bool _enable = YYGetBool(arg, 0);

    ImGuizmo::Enable(_enable);

    Result.kind = VALUE_BOOL;
    Result.val  = _enable;
}

GMFUNC(__imgui_guizmo_set_id)
{
    int _id = YYGetInt32(arg, 0);

    // This is passed to the ImGui ID stack.
    ImGuizmo::SetID(_id);

    Result.kind = VALUE_UNDEFINED;
}

GMFUNC(__imgui_guizmo_allow_axis_flip)
{
    bool _allow = YYGetBool(arg, 0);

    ImGuizmo::AllowAxisFlip(_allow);

    Result.kind = VALUE_BOOL;
    Result.val  = _allow;
}

GMFUNC(__imgui_guizmo_decompose_matrix_to_components)
{
    float* _matrix          = YYGetArray<float>(arg, 0, 16);
    float* _outTranslation  = YYGetArray<float>(arg, 1, 3);
    float* _outRotation     = YYGetArray<float>(arg, 2, 3);
    float* _outScale        = YYGetArray<float>(arg, 3, 3);

    ImGuizmo::DecomposeMatrixToComponents(_matrix, _outTranslation, _outRotation, _outScale);

    YYSetArray(&arg[1], _outTranslation, 3);
    YYSetArray(&arg[2], _outRotation, 3);
    YYSetArray(&arg[3], _outScale, 3);

    Result.kind = VALUE_UNDEFINED;
}

GMFUNC(__imgui_guizmo_recompose_matrix_from_components)
{
    float* _translation = YYGetArray<float>(arg, 0, 3);
    float* _rotation    = YYGetArray<float>(arg, 1, 3);
    float* _scale       = YYGetArray<float>(arg, 2, 3);
    float* _outMatrix   = YYGetArray<float>(arg, 3, 16);

    ImGuizmo::RecomposeMatrixFromComponents(_translation, _rotation, _scale, _outMatrix);

    YYSetArray(&arg[3], _outMatrix, 16);

    Result.kind = VALUE_UNDEFINED;
}