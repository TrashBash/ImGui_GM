///
/// Game End Event
///

ImGui.TextEditorCleanup();

/// Save settings
if ini_filename != "" {
    ImGui.SaveIniSettingsToDisk(ini_filename);
}
ImGui.__Shutdown();
